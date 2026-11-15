// 2.2 development (Kid Bunka): the Mutable Instruments engines Mutable Dreams calls per audio block run
// from RAM on the FM-1 (plat.h PLAT_RAM_CODE: section .app_ram), out of the way of the shared 32 KB flash
// cache's misses. Only in the device build (fm1.toml o2_cflags -DMD_DSP_RAM); the host, tests and the
// browser build are unchanged. The device build moves the functions of the units fm1.toml ram_units names
// to .app_ram after compiling them (base/tools/build.py; this toolchain has no section pragma). The build
// checks that RAM code calls nothing in the flash (tools/ramcode.py).
#pragma once
#if defined(MD_DSP_RAM) && !defined(__APPLE__) && !defined(__linux__) && !defined(__EMSCRIPTEN__) && !defined(_WIN32)
#define MD_DSP_IN_RAM 1
#endif
