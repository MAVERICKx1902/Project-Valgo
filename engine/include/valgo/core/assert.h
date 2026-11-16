// =============================================================================
// Project Valgo — engine core: assertions.
//
// VALGO_ASSERT is compiled in for debug builds and stripped in release builds.
// VALGO_VERIFY always evaluates its expression and never compiles out, which is
// the right choice for checks whose side effects matter.
// =============================================================================
#pragma once

#include "valgo/core/types.h"

namespace valgo {

/// Invoked by the assertion macros. Logs the failure and aborts the process.
[[noreturn]] void HandleAssertionFailure(const char* expression, const char* message, const char* file, int line);

/// Logs a fatal error and aborts. Used for unrecoverable engine states.
[[noreturn]] void Panic(const char* message, const char* file, int line);

/// Returns true while a debugger is attached to the process (best effort).
bool IsDebuggerAttached();

}  // namespace valgo

#if defined(VALGO_RELEASE_BUILD)
#define VALGO_ASSERT(expression, message) ((void)0)
#else
#define VALGO_ASSERT(expression, message)                            \
    do {                                                             \
        if (!(expression)) {                                         \
            ::valgo::HandleAssertionFailure(#expression, (message),  \
                                            __FILE__, __LINE__);     \
        }                                                            \
    } while (false)
#endif

#define VALGO_VERIFY(expression, message)                            \
    do {                                                             \
        if (!(expression)) {                                         \
            ::valgo::HandleAssertionFailure(#expression, (message),  \
                                            __FILE__, __LINE__);     \
        }                                                            \
    } while (false)

#define VALGO_PANIC(message) ::valgo::Panic((message), __FILE__, __LINE__)

#define VALGO_NOT_IMPLEMENTED() VALGO_PANIC("not implemented: " __FILE__)
