/* mesytec-mvlc - driver library for the Mesytec MVLC VME controller
 *
 * Copyright (C) 2020-2023 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include "mvlc_impl_usb.h"
#include "mvlc_impl_usb_internal.h"

#include <atomic>
#include <cassert>
#include <cstdio>
#include <future>
#include <iomanip>
#include <numeric>
#include <regex>
#include <unistd.h>

#include "mvlc_dialog.h"
#include "mvlc_dialog_util.h"
#include "mvlc_error.h"
#include "mvlc_util.h"
#include "util/logging.h"
#include "util/pretty_function.h"

namespace
{
using namespace mesytec::mvlc;

static const unsigned WriteTimeout_ms = 2000;
// XXX: Do not raise the read timeout above 1000ms, otherwise mvme rate monitoring will break!
static const unsigned ReadTimeout_ms  = 1000;
// Short timeout so that the cmd_pipe_reader can quickly react to quit requests.
static const unsigned CommandReadTimeout_ms = 100;

}

namespace mesytec::mvlc::usb
{

Impl::Impl()
    : m_connectMode{ConnectMode::First, {}, {}}
{
    m_readBuffers.resize(PipeCount);
}

Impl::Impl(unsigned index)
    : m_connectMode{ConnectMode::ByIndex, index, {}}
{
    m_readBuffers.resize(PipeCount);
}

Impl::Impl(const std::string &serial)
    : m_connectMode{ConnectMode::BySerial, 0, serial}
{
    m_readBuffers.resize(PipeCount);
}

Impl::~Impl()
{
    disconnect();
}

std::error_code Impl::closeHandle()
{
    FT_STATUS st = FT_OK;

    if (m_handle)
    {
        st = FT_Close(m_handle);
        m_handle = nullptr;
        // libftd3xx arms alarm(5) in streaming FT_ReadPipe() and leaves it
        // armed if the read fails. The SIGALRM handler uses the closed handle.
        alarm(0);
    }

    return make_error_code(st);
}

std::error_code Impl::connect()
{
    auto logger = get_logger("mvlc_usb");
    logger->trace("begin {}", PRETTY_FUNCTION);

    if (isConnected())
        return make_error_code(MVLCErrorCode::IsConnected);

    // Open the USB device. Try multiple times because with USB2 FT_Create()
    // sometimes fails the first time.
    const int MaxOpenAttempts = 3;

    auto infoList = get_device_info_list();
    std::error_code ec;

    for (int attempt=0; attempt<MaxOpenAttempts; ++attempt)
    {
        switch (m_connectMode.mode)
        {
            case ConnectMode::First:
                {
                    if (!infoList.empty())
                    {
                        m_deviceInfo = infoList[0];
                        auto st = FT_Create(reinterpret_cast<void *>(m_deviceInfo.index),
                                            FT_OPEN_BY_INDEX, &m_handle);
                        ec = make_error_code(st);
                    }
                    else
                    {
                        ec = MVLCErrorCode::USBNoDeviceFound;
                    }
                }
                break;

            case ConnectMode::ByIndex:
                {
                    ec = MVLCErrorCode::USBNoDeviceFound;

                    for (auto &info: infoList)
                    {
                        if (info.index == static_cast<int>(m_connectMode.index))
                        {
                            m_deviceInfo = info;
                            auto st = FT_Create(reinterpret_cast<void *>(m_deviceInfo.index),
                                                FT_OPEN_BY_INDEX, &m_handle);
                            ec = make_error_code(st);
                            break;
                        }
                    }
                }
                break;

            case ConnectMode::BySerial:
                {
                    ec = MVLCErrorCode::USBNoDeviceFound;

                    if ((m_deviceInfo = get_device_info_by_serial(infoList, m_connectMode.serial)))
                    {
                        auto st = FT_Create(reinterpret_cast<void *>(m_deviceInfo.index),
                                            FT_OPEN_BY_INDEX, &m_handle);
                        ec = make_error_code(st);
                    }
                }
                break;
        }

        if (!ec)
            break;
    }

    logger->trace("FT_Create done: {}", ec.message());

    for (auto &readBuffer: m_readBuffers)
        readBuffer.clear();

    if (ec)
        return ec;

    if (auto ec = check_chip_configuration(m_handle))
    {
        closeHandle();
        return ec;
    }

    logger->trace("check_chip_configuration done");

    // Note: libftd3xx-1.x uses the timeout passed to FT_ReadPipe(), a timeout
    // of 0 means wait forever.
    for (auto pipe: { Pipe::Command, Pipe::Data})
    {
        if (auto ec = set_endpoint_timeout(
                m_handle, get_endpoint(pipe, EndpointDirection::In),
                ReadTimeout_ms))
        {
            closeHandle();
            return ec;
        }

        if (auto ec = set_endpoint_timeout(
                m_handle, get_endpoint(pipe, EndpointDirection::Out),
                WriteTimeout_ms))
        {
            closeHandle();
            return ec;
        }
    }

    logger->trace("set pipe timeouts done");

    // Without streaming mode FT_ReadPipe() only returns once the full
    // requested size has been read or the timeout expires.
    if (auto ec = make_error_code(FT_SetStreamPipe(m_handle, false, true, 0, USBStreamPipeReadSize)))
    {
        logger->error("FT_SetStreamPipe failed: {}", ec.message());
        closeHandle();
        return ec;
    }

    logger->info("opened USB device");

    if (disableTriggersOnConnect())
    {
        for (int try_=0; try_<2; ++try_)
        {
            if (auto ec = post_connect_cleanup(*this))
            {
                logger->warn("error from USB post connect cleanup: {}", ec.message());
                return ec;
            }
            else
                break;
        }
    }

    logger->trace("end {}", PRETTY_FUNCTION);

    return {};
}

std::error_code Impl::disconnect()
{
    if (!isConnected())
        return make_error_code(MVLCErrorCode::IsDisconnected);

    auto ec = closeHandle();

    return ec;
}

bool Impl::isConnected() const
{
    return m_handle != nullptr;
}

std::error_code Impl::write(Pipe pipe, const u8 *buffer, size_t size,
                            size_t &bytesTransferred)
{
    auto logger = get_logger("mvlc_usb");

    assert(buffer);
    assert(size <= USBSingleTransferMaxBytes);
    assert(static_cast<unsigned>(pipe) < PipeCount);

    if (static_cast<unsigned>(pipe) >= PipeCount)
        return make_error_code(MVLCErrorCode::InvalidPipe);

    ULONG transferred = 0; // FT API needs a ULONG*

    FT_STATUS st = FT_WritePipeEx(m_handle, get_fifo_id(pipe),
                                  const_cast<u8 *>(buffer), size,
                                  &transferred,
                                  WriteTimeout_ms);

    bytesTransferred = transferred;

    auto ec = make_error_code(st);

    if (ec)
    {
        logger->warn("pipe={}, wrote {} of {} bytes, result={}",
                 static_cast<unsigned>(pipe),
                 bytesTransferred, size,
                 ec.message().c_str());
    }

    return ec;
}

std::error_code Impl::read(Pipe pipe, u8 *buffer, size_t size,
                           size_t &bytesTransferred)
{
    auto logger = get_logger("mvlc_usb");

    assert(buffer);
    assert(size <= USBSingleTransferMaxBytes);
    assert(static_cast<unsigned>(pipe) < PipeCount);

    if (static_cast<unsigned>(pipe) >= PipeCount)
        return make_error_code(MVLCErrorCode::InvalidPipe);

    if (size == 0)
        return {};

    const size_t requestedSize = size;
    bytesTransferred = 0u;

    auto &readBuffer = m_readBuffers[static_cast<unsigned>(pipe)];

    auto copy_and_update = [&buffer, &size, &bytesTransferred, &readBuffer] ()
    {
        if (size_t toCopy = std::min(readBuffer.size(), size))
        {
            memcpy(buffer, readBuffer.first, toCopy);
            buffer += toCopy;
            size -= toCopy;
            readBuffer.first += toCopy;
            bytesTransferred += toCopy;
        }
    };

    copy_and_update();

    if (size == 0)
        return {};

    assert(readBuffer.size() == 0);

    // libftd3xx-1.x submits a single URB of the requested size. Requests
    // smaller than the endpoints max packet size overflow and leave the FT60x
    // IN endpoint in a broken state (EPROTO) until the device port is reset.
    // Always read the full stream pipe size into the buffer instead.
    //
    // A read only completes early on a short packet. Responses that are an
    // exact multiple of the packet size would block until the timeout. The
    // command pipe thus reads a single packet at a time for low latency.
    const size_t maxPacketSize = (m_deviceInfo.flags & DeviceInfo::Flags::USB2) ? 512 : 1024;
    const size_t readSize = pipe == Pipe::Command ? maxPacketSize : readBuffer.capacity();
    ULONG transferred = 0; // FT API wants a ULONG* parameter

    FT_STATUS st = FT_ReadPipe(
        m_handle,
        get_endpoint(pipe, EndpointDirection::In),
        readBuffer.data.data(),
        readSize,
        &transferred,
        pipe == Pipe::Command ? CommandReadTimeout_ms : ReadTimeout_ms);

    auto ec = make_error_code(st);

    readBuffer.first = readBuffer.data.data();
    readBuffer.last  = readBuffer.first + transferred;

    copy_and_update();

    if (ec && ec != ErrorType::Timeout)
    {
        logger->warn("pipe={}, read {} of {} bytes, result={}",
                     static_cast<unsigned>(pipe),
                     bytesTransferred, requestedSize,
                     ec.message().c_str());
    }

    return ec;
}

std::error_code Impl::read_unbuffered(Pipe pipe, u8 *buffer, size_t size,
                                      size_t &bytesTransferred)
{
    auto logger = get_logger("mvlc_usb");

    assert(buffer);
    assert(static_cast<unsigned>(pipe) < PipeCount);

    if (static_cast<unsigned>(pipe) >= PipeCount)
        return make_error_code(MVLCErrorCode::InvalidPipe);

    //logger->trace("begin unbuffered read: pipe={}, size={} bytes",
    //          static_cast<unsigned>(pipe), size);

    ULONG transferred = 0; // FT API wants a ULONG* parameter
    std::error_code ec = {};

    FT_STATUS st = FT_ReadPipe(
        m_handle, get_endpoint(pipe, EndpointDirection::In),
        buffer, size,
        &transferred,
        ReadTimeout_ms);

    bytesTransferred = transferred;
    ec = make_error_code(st);

    logger->trace("end unbuffered read: pipe={}, size={} bytes, transferred={} bytes, ec={}",
              static_cast<unsigned>(pipe), size, bytesTransferred, ec.message().c_str());

    return ec;
}

std::string Impl::connectionInfo() const
{
    std::string result = "mvlc_usb: speed=";

    auto devInfo = getDeviceInfo();

    if (devInfo.flags & DeviceInfo::Flags::USB2)
        result += "USB2";
    else if (devInfo.flags & DeviceInfo::Flags::USB3)
        result += "USB3";
    else
        result += "<unknown>";

    const auto serialString = devInfo.serial.empty() ? std::string("<unknown>") : devInfo.serial;

    result += ", serial='" + serialString + "'";

    return result;
}

std::error_code set_endpoint_timeout(void *handle, u8 ep, unsigned ms)
{
    FT_STATUS st = FT_SetPipeTimeout(handle, ep, ms);
    return mesytec::mvlc::usb::make_error_code(st);
}


}
