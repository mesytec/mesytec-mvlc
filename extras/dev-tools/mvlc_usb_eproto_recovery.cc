// Linux/libftd3xx-1.x: provoke the FT60x command pipe IN endpoint error state
// (EPROTO, every read fails with FT_OPERATION_ABORTED) by doing a read smaller
// than the max packet size. Then apply a recovery method and check if command
// transactions work again.

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <ftd3xx.h>
#include <linux/usbdevice_fs.h>
#include <lyra/lyra.hpp>
#include <sys/ioctl.h>
#include <unistd.h>

namespace
{

const unsigned char CommandPipeIn = 0x82;
const unsigned char CommandFifo = 0;
const unsigned StreamPipeReadSize = 1024 * 1024;

FT_HANDLE g_handle = nullptr;
std::vector<unsigned> g_buffer(StreamPipeReadSize / sizeof(unsigned));
unsigned g_ref = 0x100;

double now_ms()
{
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

void sleep_ms(unsigned ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

std::string read_sysfs(const std::string &path)
{
    std::ifstream f(path);
    std::string s;
    std::getline(f, s);
    return s;
}

// USBDEVFS_RESET on the first FT601 found in sysfs. The FT handle must be
// closed, otherwise the kernel refuses the reset.
int usbfs_reset()
{
    DIR *dir = opendir("/sys/bus/usb/devices");

    if (!dir)
        return -1;

    int ret = -1;

    while (auto entry = readdir(dir))
    {
        std::string base = std::string("/sys/bus/usb/devices/") + entry->d_name;

        if (read_sysfs(base + "/idVendor") != "0403" || read_sysfs(base + "/idProduct") != "601f")
            continue;

        char node[64];
        snprintf(node, sizeof(node), "/dev/bus/usb/%03d/%03d",
                 std::stoi(read_sysfs(base + "/busnum")), std::stoi(read_sysfs(base + "/devnum")));

        int fd = open(node, O_WRONLY);

        if (fd < 0)
        {
            perror(node);
            break;
        }

        if ((ret = ioctl(fd, USBDEVFS_RESET, 0)))
            perror("USBDEVFS_RESET");

        close(fd);
        printf("  usbfs reset %s serial=%s ret=%d\n", node, read_sysfs(base + "/serial").c_str(), ret);
        break;
    }

    closedir(dir);
    return ret;
}

bool open_device()
{
    if (auto st = FT_Create(reinterpret_cast<void *>(0), FT_OPEN_BY_INDEX, &g_handle))
    {
        printf("FT_Create: %d\n", st);
        return false;
    }

    if (auto st = FT_SetStreamPipe(g_handle, false, true, 0, StreamPipeReadSize))
        printf("FT_SetStreamPipe: %d\n", st);

    return true;
}

void close_device()
{
    if (g_handle)
    {
        FT_Close(g_handle);
        g_handle = nullptr;
        // libftd3xx arms alarm(5) in streaming reads and does not cancel it.
        alarm(0);
    }
}

// Polls until the device can be opened again, e.g. after re-enumeration.
bool reopen_poll()
{
    double t0 = now_ms();

    for (int i=0; i<200; i++)
    {
        if (FT_Create(reinterpret_cast<void *>(0), FT_OPEN_BY_INDEX, &g_handle) == FT_OK)
        {
            FT_SetStreamPipe(g_handle, false, true, 0, StreamPipeReadSize);
            printf("  reopened after %.1fms (%d attempts)\n", now_ms() - t0, i + 1);
            return true;
        }

        sleep_ms(25);
    }

    printf("  reopen failed\n");
    return false;
}

FT_STATUS read_pipe(unsigned size, unsigned timeout_ms, ULONG &transferred)
{
    transferred = 0;
    return FT_ReadPipe(g_handle, CommandPipeIn, reinterpret_cast<PUCHAR>(g_buffer.data()),
                       size, &transferred, timeout_ms);
}

void drain()
{
    ULONG transferred = 0;
    FT_STATUS st = FT_OK;
    size_t total = 0;
    int reads = 0;

    do
    {
        st = read_pipe(StreamPipeReadSize, 100, transferred);
        total += transferred;
    } while (transferred > 0 && ++reads < 100);

    printf("  drain: total=%zu bytes, last st=%d\n", total, st);
}

// Sends a reference word super command and reads the response using the given
// read size.
FT_STATUS transact(unsigned readSize)
{
    unsigned cmd[3] = { 0xF1000000u, 0x01010000u | (++g_ref & 0xffff), 0xF2000000u };
    ULONG transferred = 0;

    if (auto st = FT_WritePipeEx(g_handle, CommandFifo, reinterpret_cast<PUCHAR>(cmd),
                                 sizeof(cmd), &transferred, 1000))
    {
        printf("  write: st=%d transferred=%lu\n", st, transferred);
        return st;
    }

    sleep_ms(2);
    double t0 = now_ms();
    auto st = read_pipe(readSize, 500, transferred);

    printf("  read(size=%u): st=%d transferred=%lu dt=%.1fms", readSize, st, transferred, now_ms() - t0);

    for (unsigned i=0; i<transferred/sizeof(unsigned) && i<6; i++)
        printf(" 0x%08x", g_buffer[i]);

    printf("  (want ref 0x%04x)\n", g_ref & 0xffff);

    return st;
}

}

int main(int argc, char *argv[])
{
    std::string method = "none";
    bool noProvoke = false;
    bool showHelp = false;

    auto cli
        = lyra::help(showHelp)
        | lyra::opt(noProvoke)["--no-provoke"]("do not provoke the error state, e.g. if it is already present")
        | lyra::arg(method, "method")
            ("recovery method: none, abort, flush, stream, reopen, reset, cycle, ftreset_reopen, usbfs_reopen");

    auto parseResult = cli.parse({ argc, argv });

    if (!parseResult)
    {
        printf("Error: %s\n", parseResult.message().c_str());
        return 1;
    }

    if (showHelp)
    {
        std::cout << cli << "\n";
        return 0;
    }

    if (!open_device())
        return 1;

    printf("initial state:\n");
    drain();
    transact(1024);

    if (!noProvoke)
    {
        printf("provoke with 4 byte read:\n");
        transact(4);
        printf("after provoke:\n");
        transact(1024);
    }

    printf("recovery method '%s':\n", method.c_str());
    FT_STATUS st = FT_OK;
    double t0 = now_ms();

    if (method == "none")
    {
    }
    else if (method == "abort")
        st = FT_AbortPipe(g_handle, CommandPipeIn);
    else if (method == "flush")
        st = FT_FlushPipe(g_handle, CommandPipeIn);
    else if (method == "stream")
    {
        FT_ClearStreamPipe(g_handle, false, true, 0);
        st = FT_SetStreamPipe(g_handle, false, true, 0, StreamPipeReadSize);
    }
    else if (method == "reopen")
    {
        close_device();

        if (!open_device())
            return 1;
    }
    else if (method == "reset" || method == "cycle")
    {
        st = method == "reset" ? FT_ResetDevicePort(g_handle) : FT_CycleDevicePort(g_handle);
        ULONG transferred = 0;
        auto st2 = read_pipe(1024, 100, transferred);
        printf("  read on old handle: st=%d transferred=%lu\n", st2, transferred);
    }
    else if (method == "ftreset_reopen")
    {
        st = FT_ResetDevicePort(g_handle);
        close_device();
        printf("  reset took %.1fms\n", now_ms() - t0);

        if (!reopen_poll())
            return 1;
    }
    else if (method == "usbfs_reopen")
    {
        close_device();
        usbfs_reset();
        printf("  reset took %.1fms\n", now_ms() - t0);

        if (!reopen_poll())
            return 1;
    }
    else
    {
        printf("unknown recovery method '%s'\n", method.c_str());
        close_device();
        return 1;
    }

    printf("  -> st=%d\n", st);

    printf("after recovery:\n");
    drain();

    for (int i=0; i<3; i++)
        transact(1024);

    close_device();
    return 0;
}
