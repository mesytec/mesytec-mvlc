#ifndef MESYTEC_MVLC_FTD3XX_API_H
#define MESYTEC_MVLC_FTD3XX_API_H

#include <vector>
#include "mesytec-mvlc/mesytec-mvlc_export.h"

#ifdef _WIN32
#include <FTD3XX.h>
#else
#include <ftd3xx.h>
#endif

// Table of the libftd3xx functions used by mesytec-mvlc. All FTDI calls go
// through such a table.
//
// Linux/macOS: a single table pointing to the linked libftd3xx.
// Windows: one table per FTDI DLL found at runtime: FTD3XXWU.dll (1.4.x,
// WinUSB driver) and FTD3XX.dll (1.3.x, FTDIBUS3 driver). Each device is
// accessed through the table of the library that enumerated it.

#define MVLC_FTD3XX_FUNCTIONS(X) \
    X(Create) \
    X(Close) \
    X(CreateDeviceInfoList) \
    X(GetDeviceInfoList) \
    X(GetChipConfiguration) \
    X(GetDriverVersion) \
    X(GetLibraryVersion) \
    X(SetPipeTimeout) \
    X(SetStreamPipe) \
    X(ClearStreamPipe) \
    X(AbortPipe) \
    X(FlushPipe) \
    X(ReadPipe) \
    X(ReadPipeEx) \
    X(WritePipe) \
    X(WritePipeEx) \
    X(GetOverlappedResult) \
    X(ResetDevicePort) \
    X(CycleDevicePort)

namespace mesytec::mvlc::usb
{

struct Ftd3xxApi
{
    const char *name;

#define MVLC_FTD3XX_DECLARE_MEMBER(fn) decltype(&::FT_##fn) fn;
    MVLC_FTD3XX_FUNCTIONS(MVLC_FTD3XX_DECLARE_MEMBER)
#undef MVLC_FTD3XX_DECLARE_MEMBER
};

// The available FTDI libraries in device enumeration order. Loaded on first
// use. Empty on Windows if no FTDI DLL could be loaded.
MESYTEC_MVLC_EXPORT const std::vector<const Ftd3xxApi *> &ftd3xx_apis();

}

#endif /* MESYTEC_MVLC_FTD3XX_API_H */
