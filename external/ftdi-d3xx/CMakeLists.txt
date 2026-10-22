# --- ftd3xx vendor driver integration --------------------------------------
#
# This file lives at external/ftd3xx/CMakeLists.txt and is pulled in via
# add_subdirectory(ftd3xx) from external/CMakeLists.txt. All vendor blobs
# are direct children of this same directory (normalized layout; FTDI's own
# zip layout changes per release, so this tree is what we control — see
# external/ftd3xx/README.md for re-vendoring steps when a new FTDI package
# drops):
#
# external/ftd3xx/                             (this directory)
#   windows/legacy-wdf/include/FTD3XX.h        (v1.3.x, FTDIBUS3.sys)
#   windows/legacy-wdf/x64/FTD3XX.dll
#   windows/winusb/include/FTD3XX.h            (v1.4.x, WinUsb.sys)
#   windows/winusb/x64/FTD3XXWU.dll
#   windows/winusb/arm-v8/FTD3XXWU.dll
#   linux/x64/include/  libftd3xx-static.a
#   linux/arm-v8/include/  libftd3xx-static.a
#   macos/include/  libftd3xx.dylib             (universal: x86_64 + arm64 in one Mach-O)
#
# "arm-v8" rather than "arm64" — FTDI ships more than one ARM variant per
# platform (e.g. armv7/armhf alongside aarch64 on Linux), so the dir name
# says which ISA generation, leaving room for an "arm-v7" slot later
# without the two being confusable.
#
# No x86 (32-bit) slot — neither mesytec-mvlc nor mvme build 32-bit.
#
# Windows ships BOTH driver architectures side by side and picks between
# them at process startup (see ftd3xx_dual_loader.hpp) — mesytec-mvlc never
# links against FTD3XX.lib / FTD3XXWU.lib on Windows. It only needs the
# header (either package's FTD3XX.h — identical per FTDI's compat notice)
# and both DLLs present next to the executable, loaded via
# LoadLibrary/GetProcAddress at runtime. This also sidesteps the
# __imp_FT_CreateDeviceInfoList static-link problem from the old setup —
# there is no static Windows link anymore.
#
# Every arch slot below is optional (checked with EXISTS) so a platform can
# be vendored incrementally — e.g. adding a Windows-on-ARM build later is
# just dropping files into windows/winusb/arm-v8/, no CMake changes required.

set(FTD3XX_VENDOR_DIR ${CMAKE_CURRENT_SOURCE_DIR})

if (WIN32)
    add_library(ftd3xx INTERFACE)

    target_include_directories(ftd3xx INTERFACE
        ${FTD3XX_VENDOR_DIR}/windows/winusb/include)

    if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "ftd3xx: 32-bit Windows build requested, but only x64/arm-v8 are vendored")
    endif()

    if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|aarch64)$")
        set(_ftd3xx_win_arch arm-v8)
    else()
        set(_ftd3xx_win_arch x64)
    endif()

    set(_ftd3xx_legacy_dll ${FTD3XX_VENDOR_DIR}/windows/legacy-wdf/${_ftd3xx_win_arch}/FTD3XX.dll)
    set(_ftd3xx_winusb_dll ${FTD3XX_VENDOR_DIR}/windows/winusb/${_ftd3xx_win_arch}/FTD3XXWU.dll)

    # FTDI has never shipped a legacy-wdf ARM64 build (WDF driver predates
    # relevant Windows-on-ARM deployments) — skip it there rather than fail.
    if (EXISTS ${_ftd3xx_legacy_dll})
        file(COPY ${_ftd3xx_legacy_dll} DESTINATION ${CMAKE_BINARY_DIR})
        install(FILES ${_ftd3xx_legacy_dll} DESTINATION bin)
    elseif (NOT _ftd3xx_win_arch STREQUAL "arm-v8")
        message(WARNING "ftd3xx: no legacy-wdf FTD3XX.dll for ${_ftd3xx_win_arch} — "
                         "MVLCs still bound to the 1.3.x/FTDIBUS3 driver won't be detected")
    endif()

    if (EXISTS ${_ftd3xx_winusb_dll})
        file(COPY ${_ftd3xx_winusb_dll} DESTINATION ${CMAKE_BINARY_DIR})
        install(FILES ${_ftd3xx_winusb_dll} DESTINATION bin)
    else()
        message(WARNING "ftd3xx: no winusb FTD3XXWU.dll for ${_ftd3xx_win_arch}")
    endif()

    # NOTE: file(COPY ... DESTINATION ${CMAKE_BINARY_DIR}) only lands next to
    # the binary for single-config generators (Ninja/Makefiles). With the
    # Visual Studio multi-config generator, targets build into
    # ${CMAKE_BINARY_DIR}/$<CONFIG>/ instead — if you build with VS, this
    # needs a per-target add_custom_command(POST_BUILD) copy using
    # $<TARGET_FILE_DIR:your-target> rather than the plain file(COPY) above.

elseif (UNIX AND NOT APPLE)
    add_library(ftd3xx-static STATIC IMPORTED GLOBAL)

    if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
        set(_ftd3xx_linux_arch x64)
    elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
        set(_ftd3xx_linux_arch arm-v8)
    else()
        message(FATAL_ERROR "ftd3xx: unsupported Linux CMAKE_SYSTEM_PROCESSOR '${CMAKE_SYSTEM_PROCESSOR}'")
    endif()

    set(_ftd3xx_linux_dir ${FTD3XX_VENDOR_DIR}/linux/${_ftd3xx_linux_arch})

    set_target_properties(ftd3xx-static PROPERTIES
        IMPORTED_LOCATION ${_ftd3xx_linux_dir}/libftd3xx-static.a)

    target_include_directories(ftd3xx-static INTERFACE
        ${_ftd3xx_linux_dir}/include)

    find_package(Threads REQUIRED)
    target_link_libraries(ftd3xx-static INTERFACE Threads::Threads)

elseif (APPLE)
    # Universal dylib (x86_64 + arm64 slices in one Mach-O) — no per-arch dir.
    add_library(ftd3xx SHARED IMPORTED GLOBAL)

    set(_ftd3xx_macos_dir ${FTD3XX_VENDOR_DIR}/macos)

    set_target_properties(ftd3xx PROPERTIES
        IMPORTED_LOCATION ${_ftd3xx_macos_dir}/libftd3xx.dylib
        IMPORTED_NO_SONAME TRUE)

    target_include_directories(ftd3xx INTERFACE
        ${_ftd3xx_macos_dir}/include)

    install(FILES ${_ftd3xx_macos_dir}/libftd3xx.dylib DESTINATION lib)
endif()
