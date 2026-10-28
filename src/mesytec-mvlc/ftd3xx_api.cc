#include "ftd3xx_api.h"

#include <memory>
#include <string>

#include "util/logging.h"

#ifdef MESYTEC_MVLC_PLATFORM_WINDOWS
#include <windows.h>
#endif

namespace mesytec::mvlc::usb
{

#ifndef MESYTEC_MVLC_PLATFORM_WINDOWS

const std::vector<const Ftd3xxApi *> &ftd3xx_apis()
{
#define MVLC_FTD3XX_STATIC_ENTRY(fn) &::FT_##fn,
    static const Ftd3xxApi api = { "libftd3xx", MVLC_FTD3XX_FUNCTIONS(MVLC_FTD3XX_STATIC_ENTRY) };
#undef MVLC_FTD3XX_STATIC_ENTRY

    static const std::vector<const Ftd3xxApi *> apis = { &api };
    return apis;
}

#else // MESYTEC_MVLC_PLATFORM_WINDOWS

namespace
{

// Directory containing mesytec-mvlc.dll, including the trailing separator.
std::string own_module_dir()
{
    HMODULE self = nullptr;

    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                            | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(&own_module_dir), &self))
        return {};

    char buffer[MAX_PATH] = {};
    auto len = GetModuleFileNameA(self, buffer, sizeof(buffer));

    if (len == 0 || len == sizeof(buffer))
        return {};

    std::string result(buffer, len);
    return result.substr(0, result.find_last_of("\\/") + 1);
}

// Prefers the copy shipped next to mesytec-mvlc.dll over the default DLL
// search order.
HMODULE load_ftdi_dll(const char *dllName)
{
    auto path = own_module_dir() + dllName;

    if (auto lib = LoadLibraryExA(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH))
        return lib;

    return LoadLibraryA(dllName);
}

// FTD3XXWU.dll exports some functions only with an FT3_ prefix.
template<typename Fn>
bool resolve(HMODULE lib, const char *fn, Fn &dest)
{
    auto proc = GetProcAddress(lib, (std::string("FT3_") + fn).c_str());

    if (!proc)
        proc = GetProcAddress(lib, (std::string("FT_") + fn).c_str());

    dest = reinterpret_cast<Fn>(proc);
    return dest != nullptr;
}

// The DLL stays loaded for the lifetime of the process.
std::unique_ptr<Ftd3xxApi> load_api(const char *dllName)
{
    auto logger = get_logger("mvlc_usb");
    auto lib = load_ftdi_dll(dllName);

    if (!lib)
    {
        logger->info("ftd3xx: could not load {} (error {})", dllName, GetLastError());
        return {};
    }

    auto api = std::make_unique<Ftd3xxApi>();
    api->name = dllName;

#define MVLC_FTD3XX_RESOLVE(fn) \
    if (!resolve(lib, #fn, api->fn)) \
    { \
        logger->warn("ftd3xx: {} does not export FT_{}", dllName, #fn); \
        FreeLibrary(lib); \
        return {}; \
    }
    MVLC_FTD3XX_FUNCTIONS(MVLC_FTD3XX_RESOLVE)
#undef MVLC_FTD3XX_RESOLVE

    DWORD version = 0;
    api->GetLibraryVersion(&version);
    logger->info("ftd3xx: loaded {}, library version 0x{:08x}", dllName, version);

    return api;
}

}

const std::vector<const Ftd3xxApi *> &ftd3xx_apis()
{
    static const std::vector<const Ftd3xxApi *> apis = []
    {
        std::vector<const Ftd3xxApi *> result;

        for (auto dllName: { "FTD3XXWU.dll", "FTD3XX.dll" })
        {
            if (auto api = load_api(dllName))
                result.push_back(api.release());
        }

        return result;
    }();

    return apis;
}

#endif // MESYTEC_MVLC_PLATFORM_WINDOWS

}
