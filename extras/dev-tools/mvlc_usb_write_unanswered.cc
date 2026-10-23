// Writes command transactions (super ref + hardware_id register read) to the
// MVLC via USB without reading any responses. Used to leave stale responses in
// the command pipe to test the post connect cleanup.

#include <cstdio>
#include <iostream>

#include <ftd3xx.h>
#include <lyra/lyra.hpp>
#include <unistd.h>

int main(int argc, char *argv[])
{
    unsigned count = 100;
    bool showHelp = false;

    auto cli
        = lyra::help(showHelp)
        | lyra::arg(count, "count")("number of transactions to write (default=100)");

    auto parseResult = cli.parse({ argc, argv });

    if (!parseResult)
    {
        std::cout << "Error: " << parseResult.message() << "\n";
        return 1;
    }

    if (showHelp)
    {
        std::cout << cli << "\n";
        return 0;
    }

    FT_HANDLE handle = nullptr;

    if (auto st = FT_Create(reinterpret_cast<void *>(0), FT_OPEN_BY_INDEX, &handle))
    {
        printf("FT_Create: %d\n", st);
        return 1;
    }

    unsigned written = 0;

    for (; written<count; written++)
    {
        unsigned cmd[4] = { 0xF1000000u, 0x01010000u | ((0x1000 + written) & 0xffff), 0x01026008u, 0xF2000000u };
        ULONG transferred = 0;

        if (auto st = FT_WritePipeEx(handle, 0, reinterpret_cast<PUCHAR>(cmd), sizeof(cmd), &transferred, 1000))
        {
            printf("write #%u: st=%d\n", written, st);
            break;
        }
    }

    printf("wrote %u transactions\n", written);

    FT_Close(handle);
    alarm(0);
    return 0;
}
