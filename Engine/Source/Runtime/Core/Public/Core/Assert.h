// Assert.h - assertion and fatal-error macros. No exceptions; never throws.
//
//   ENGINE_CHECK(cond)    - debug-only; compiled out in Release.
//   ENGINE_VERIFY(cond)   - runs in Release too; evaluates cond and breaks on failure.
//   ENGINE_FATAL(msg)     - unconditional, prints to stderr, breaks, then aborts.
//
// ENGINE_BUILD_DEBUG / ENGINE_BUILD_RELEASE are injected by add_engine_module() per build config.

#pragma once

#include <cstdio>
#include <cstdlib>

#if defined(_MSC_VER)
    #define ENGINE_DEBUG_BREAK() __debugbreak()
#elif defined(__has_builtin)
    #if __has_builtin(__builtin_debugtrap)
        #define ENGINE_DEBUG_BREAK() __builtin_debugtrap()
    #else
        #define ENGINE_DEBUG_BREAK() __builtin_trap()
    #endif
#else
    #define ENGINE_DEBUG_BREAK() std::abort()
#endif

#define ENGINE_VERIFY(cond)                                                                     \
    do {                                                                                        \
        if (!(cond)) {                                                                          \
            std::fprintf(stderr, "[ENGINE_VERIFY FAILED] %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ENGINE_DEBUG_BREAK();                                                               \
            std::abort();                                                                       \
        }                                                                                       \
    } while (0)

#if defined(ENGINE_BUILD_DEBUG)
    #define ENGINE_CHECK(cond) ENGINE_VERIFY(cond)
#else
    #define ENGINE_CHECK(cond) ((void)0)
#endif

#define ENGINE_FATAL(msg)                                                  \
    do {                                                                   \
        std::fprintf(stderr, "[ENGINE_FATAL] %s:%d: %s\n", __FILE__, __LINE__, msg); \
        ENGINE_DEBUG_BREAK();                                              \
        std::abort();                                                      \
    } while (0)
