// =============================================================================
// Project Valgo — engine core: logging.
//
// Any subsystem can log without initialising anything: the default logger
// writes to stdout and keeps the last N messages in memory so that crash
// reports and the in-game debug overlay can show recent history.
// =============================================================================
#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "valgo/core/format.h"
#include "valgo/core/types.h"

namespace valgo {

enum class LogLevel : u8 {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warn = 3,
    Error = 4,
    Fatal = 5,
    Off = 6,
};

std::string_view LogLevelName(LogLevel level);

struct LogMessage {
    LogLevel level = LogLevel::Info;
    f64 timestamp_seconds = 0.0;
    std::string category;
    std::string text;
};

/// A destination for log messages (console, file, in-game console, test sink).
class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void Write(const LogMessage& message) = 0;
    virtual void Flush() {}
};

class Logger {
public:
    static Logger& Get();

    /// Messages below this level are discarded.
    void SetLevel(LogLevel level);
    LogLevel level() const;

    void AddSink(ILogSink* sink);
    void RemoveSink(ILogSink* sink);

    /// Convenience sink that appends to a file (created/truncated on open).
    bool AddFileSink(const std::string& path);

    void Write(LogLevel level, std::string_view category, std::string_view text);

    /// Most recent messages, oldest first (bounded ring buffer).
    std::vector<LogMessage> RecentMessages(usize max_count = 64) const;

private:
    Logger();
    ~Logger();

    static constexpr usize kHistoryCapacity = 256;

    LogLevel level_ = LogLevel::Info;
    std::vector<ILogSink*> sinks_;
    std::vector<LogMessage> history_;
    usize history_head_ = 0;
    std::vector<std::unique_ptr<ILogSink>> owned_sinks_;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

template <typename... Args>
void Log(LogLevel level, std::string_view category, std::string_view format, const Args&... args) {
    Logger& logger = Logger::Get();
    if (static_cast<u8>(level) < static_cast<u8>(logger.level())) {
        return;
    }
    logger.Write(level, category, Format(format, args...));
}

template <typename... Args>
void LogTrace(std::string_view category, std::string_view format, const Args&... args) {
    Log(LogLevel::Trace, category, format, args...);
}
template <typename... Args>
void LogDebug(std::string_view category, std::string_view format, const Args&... args) {
    Log(LogLevel::Debug, category, format, args...);
}
template <typename... Args>
void LogInfo(std::string_view category, std::string_view format, const Args&... args) {
    Log(LogLevel::Info, category, format, args...);
}
template <typename... Args>
void LogWarn(std::string_view category, std::string_view format, const Args&... args) {
    Log(LogLevel::Warn, category, format, args...);
}
template <typename... Args>
void LogError(std::string_view category, std::string_view format, const Args&... args) {
    Log(LogLevel::Error, category, format, args...);
}

}  // namespace valgo
