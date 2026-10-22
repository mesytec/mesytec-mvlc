#include <ftd3xx.h>
#include <spdlog/spdlog.h>
#include <mesytec-mvlc/mesytec-mvlc.h>

using namespace mesytec::mvlc;

enum class EndpointDirection
{
    In,
    Out
};

static const int ReadTimeout = 1000;
static const int WriteTimeout = 1000;

constexpr u8 get_endpoint(unsigned pipe, EndpointDirection dir)
{
    u8 result = 0;

    switch (pipe)
    {
    case CommandPipe:
        result = 0x2;
        break;

    case DataPipe:
        result = 0x3;
        break;
    }

    if (dir == EndpointDirection::In)
        result |= 0x80;

    return result;
}

int main(int argc, char **argv)
{
    // open
    FT_HANDLE handle;
    auto st = FT_Create(reinterpret_cast<void *>(0), FT_OPEN_BY_INDEX, &handle);

    if (st != FT_OK)
    {
        spdlog::error("Failed to create FTDI handle: {}", st);
        return 1;
    }

    // set timeouts
    if ((st = FT_SetPipeTimeout(handle, get_endpoint(CommandPipe, EndpointDirection::In),
                                ReadTimeout)))
    {
        spdlog::error("Failed to set pipe timeout for CommandPipe In: {}", st);
        return 1;
    }
    if ((st = FT_SetPipeTimeout(handle, get_endpoint(CommandPipe, EndpointDirection::Out),
                                WriteTimeout)))
    {
        spdlog::error("Failed to set pipe timeout for CommandPipe Out: {}", st);
        return 1;
    }
    if ((st =
             FT_SetPipeTimeout(handle, get_endpoint(DataPipe, EndpointDirection::In), ReadTimeout)))
    {
        spdlog::error("Failed to set pipe timeout for DataPipe In: {}", st);
        return 1;
    }
    if ((st = FT_SetPipeTimeout(handle, get_endpoint(DataPipe, EndpointDirection::Out),
                                WriteTimeout)))
    {
        spdlog::error("Failed to set pipe timeout for DataPipe Out: {}", st);
        return 1;
    }

    // Mimic the drivers usb cleanup step: read from the command pipe until timeout.
    // This is just here so we can test the timeout behavior and error codes.
    // In the actual driver code this would read from both pipes while writing
    // 'daq stop' commands.
    std::vector<u32> cleanupBuffer;
    cleanupBuffer.resize((1024 * 1024) / sizeof(u32));
    u32 bytesTransferred = 0;

    st = FT_ReadPipeEx(handle, CommandPipe, reinterpret_cast<unsigned char *>(cleanupBuffer.data()),
                       cleanupBuffer.size() * sizeof(u32), &bytesTransferred, ReadTimeout);

    if (st != FT_OK)
    {
        spdlog::error("Failed to read from CommandPipe: {} (FT_ReadPipeEx)", st);
    }

    st = FT_ReadPipe(handle, get_endpoint(CommandPipe, EndpointDirection::In),
                     reinterpret_cast<unsigned char *>(cleanupBuffer.data()),
                     cleanupBuffer.size() * sizeof(u32), &bytesTransferred, ReadTimeout);

    if (st != FT_OK)
    {
        spdlog::error("Failed to read from CommandPipe: {} (FT_ReadPipe)", st);
    }

    // now send a refword super command and read the response
    SuperCommandBuilder scb;
    scb.addReferenceWord(0x1234);

    auto writeBuffer = make_command_buffer(scb);

    st = FT_WritePipe(handle, get_endpoint(CommandPipe, EndpointDirection::Out),
                      reinterpret_cast<unsigned char *>(writeBuffer.data()),
                      writeBuffer.size() * sizeof(u32), &bytesTransferred, WriteTimeout);

    if (st != FT_OK)
    {
        spdlog::error("Failed to write to CommandPipe: {}", st);
        return 1;
    }
    else
    {
        spdlog::info("Successfully wrote to CommandPipe: {} bytes transferred", bytesTransferred);
    }

    //st = FT_ReadPipeEx(handle, CommandPipe, reinterpret_cast<unsigned char *>(cleanupBuffer.data()),
    //                   cleanupBuffer.size() * sizeof(u32), &bytesTransferred, ReadTimeout);

    st = FT_ReadPipe(handle, get_endpoint(CommandPipe, EndpointDirection::In),
                     reinterpret_cast<unsigned char *>(cleanupBuffer.data()),
                     cleanupBuffer.size() * sizeof(u32), &bytesTransferred, ReadTimeout);

    if (st != FT_OK)
    {
        spdlog::error("Failed to read from CommandPipe: {} (FT_ReadPipeEx)", st);
        return 1;
    }

    // close the FTDI handle
    if ((st = FT_Close(handle)))
    {
        spdlog::error("Failed to close FTDI handle: {}", st);
        return 1;
    }

    return 0;
}
