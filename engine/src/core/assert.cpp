#include "valgo/core/assert.h"

#include <cstdio>
#include <cstdlib>

#include "valgo/core/log.h"

#if defined(VALGO_PLATFORM_WINDOWS)
#include <windows.h>
#elif defined(VALGO_PLATFORM_LINUX)
#include <csignal>
#endif

namespace valgo {
namespace {

[[noreturn]] void ReportAndAbort(const char* kind, const char* expression, const char* message, const char* file, int line) {
    (void)expression;
    (void)message;
    (void)file;
    (void)line;
    // Print through the logger first so file sinks capture the failure, then
    // also write to stderr so the message survives a logger that was never
    // configured (very early startup failures).
    Log(LogLevel::Fatal, "assert", "{} failed: {} ({}:{})", kind,
        expression != nullptr ? expression : "", file != nullptr ? file : "", line);
    if (message != nullptr && message[0] != '\0') {
        std::fprintf(stderr, "[valgo][fatal] %s\n", message);
    }
    for (const LogMessage& recent : Logger::Get().RecentMessages(16)) {
        std::fprintf(stderr, "  [recent][%s][%s] %s\n", LogLevelName(recent.level).data(),
                     recent.category.c_str(), recent.text.c_str());
    }
    std::fflush(stderr);

    if (IsDebuggerAttached()) {
        VALGO_UNUSED(0);  // debugging hook
    }

#if defined(VALGO_PLATFORM_WINDOWS)
    __debugbreak();
#endif
    std::abort();
}

}  // namespace

void HandleAssertionFailure(const char* expression, const char* message, const char* file, int line) {
    ReportAndAbort("assertion", expression, message, file, line);
}

void Panic(const char* message, const char* file, int line) {
    ReportAndAbort("panic", message, nullptr, file, line);
}

bool IsDebuggerAttached() {
#if defined(VALGO_PLATFORM_WINDOWS)
    return ::IsDebuggerPresent() != FALSE;
#else
    return false;
#endif
}

}  // namespace valgo
