#include "valgo/core/log.h"

#include <cstdio>
#include <mutex>

#include "valgo/core/time.h"

namespace valgo {
namespace {

/// Fixed-width relative timestamp: log columns stay aligned in a terminal.
std::string FormatTimestamp(f64 seconds) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%8.3f", seconds);
    return buffer;
}

class ConsoleSink final : public ILogSink {
public:
    void Write(const LogMessage& message) override {
        const std::string line = Format("[{}s][{}][{}] {}", FormatTimestamp(message.timestamp_seconds),
                                        LogLevelName(message.level), message.category, message.text);
        std::FILE* stream = message.level >= LogLevel::Warn ? stderr : stdout;
        std::fwrite(line.data(), 1, line.size(), stream);
        std::fputc('\n', stream);
        std::fflush(stream);
    }
};

class FileSink final : public ILogSink {
public:
    explicit FileSink(std::string path) : path_(std::move(path)) {
        stream_ = std::fopen(path_.c_str(), "w");
        if (stream_ == nullptr) {
            std::fprintf(stderr, "[valgo][log] failed to open log file '%s'\n", path_.c_str());
        }
    }

    ~FileSink() override {
        if (stream_ != nullptr) {
            std::fclose(stream_);
        }
    }

    void Write(const LogMessage& message) override {
        if (stream_ == nullptr) {
            return;
        }
        const std::string line = Format("[{}s][{}][{}] {}", FormatTimestamp(message.timestamp_seconds),
                                        LogLevelName(message.level), message.category, message.text);
        std::fwrite(line.data(), 1, line.size(), stream_);
        std::fputc('\n', stream_);
    }

    void Flush() override {
        if (stream_ != nullptr) {
            std::fflush(stream_);
        }
    }

private:
    std::string path_;
    std::FILE* stream_ = nullptr;
};

}  // namespace

std::string_view LogLevelName(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "trace";
        case LogLevel::Debug: return "debug";
        case LogLevel::Info: return "info";
        case LogLevel::Warn: return "warn";
        case LogLevel::Error: return "error";
        case LogLevel::Fatal: return "fatal";
        case LogLevel::Off: return "off";
    }
    return "unknown";
}

struct Logger::Impl {
    std::mutex mutex;
    ConsoleSink console;
};

Logger& Logger::Get() {
    static Logger instance;
    return instance;
}

Logger::Logger() : impl_(std::make_unique<Impl>()) {
    history_.reserve(kHistoryCapacity);
    sinks_.push_back(&impl_->console);
}

Logger::~Logger() = default;

void Logger::SetLevel(LogLevel level) { level_ = level; }
LogLevel Logger::level() const { return level_; }

void Logger::AddSink(ILogSink* sink) {
    if (sink == nullptr) {
        return;
    }
    sinks_.push_back(sink);
}

void Logger::RemoveSink(ILogSink* sink) {
    for (auto it = sinks_.begin(); it != sinks_.end(); ++it) {
        if (*it == sink) {
            sinks_.erase(it);
            return;
        }
    }
}

bool Logger::AddFileSink(const std::string& path) {
    auto sink = std::make_unique<FileSink>(path);
    bool ok = true;  // The sink logs its own failure; keep the console usable either way.
    sinks_.push_back(sink.get());
    owned_sinks_.push_back(std::move(sink));
    return ok;
}

void Logger::Write(LogLevel level, std::string_view category, std::string_view text) {
    if (static_cast<u8>(level) < static_cast<u8>(level_)) {
        return;
    }

    LogMessage message;
    message.level = level;
    // Timestamps are relative to the first log call so log files read as
    // "seconds since the process started", not as machine uptime.
    static const f64 start_seconds = MonotonicNowSeconds();
    message.timestamp_seconds = MonotonicNowSeconds() - start_seconds;
    message.category.assign(category);
    message.text.assign(text);

    {
        std::lock_guard<std::mutex> lock(impl_->mutex);

        if (history_.size() < kHistoryCapacity) {
            history_.push_back(message);
        } else {
            history_[history_head_] = message;
            history_head_ = (history_head_ + 1) % kHistoryCapacity;
        }

        for (ILogSink* sink : sinks_) {
            sink->Write(message);
        }
        if (level >= LogLevel::Fatal) {
            for (ILogSink* sink : sinks_) {
                sink->Flush();
            }
        }
    }
}

std::vector<LogMessage> Logger::RecentMessages(usize max_count) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::vector<LogMessage> out;
    if (history_.empty()) {
        return out;
    }
    const usize count = max_count < history_.size() ? max_count : history_.size();
    // Oldest first: when the ring wrapped, start at history_head_.
    const usize start = history_.size() < kHistoryCapacity ? 0 : history_head_;
    for (usize i = 0; i < count; ++i) {
        const usize index = (start + history_.size() - count + i) % history_.size();
        out.push_back(history_[index]);
    }
    return out;
}

}  // namespace valgo
