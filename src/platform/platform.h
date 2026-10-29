#pragma once

// Platform macros (fury3d style). Always defined as 0/1 so `#if PLATFORM_WINDOWS`
// works without `#if defined(...)`; typo'd names fail loudly via -Wundef.
#if defined(_WIN32)
    #define PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
    #define PLATFORM_MACOS 1
#elif defined(__linux__)
    #define PLATFORM_LINUX 1
#else
    #error "Unsupported platform - add a PLATFORM_* macro for this target"
#endif

#ifndef PLATFORM_WINDOWS
    #define PLATFORM_WINDOWS 0
#endif
#ifndef PLATFORM_MACOS
    #define PLATFORM_MACOS 0
#endif
#ifndef PLATFORM_LINUX
    #define PLATFORM_LINUX 0
#endif
