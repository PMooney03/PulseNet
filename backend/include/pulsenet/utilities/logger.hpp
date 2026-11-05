#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace pulsenet {

enum class LogLevel { Trace, Debug, Info, Warn, Error };

std::string_view to_string(LogLevel level);
std::optional<LogLevel> log_level_from_string(std::string_view text);

// Process-wide logger. One mutex keeps a log line intact when several threads
// write at once. The sink must not call back into the logger or the database:
// the mutex is not recursive, so that would deadlock.
class Logger {
public:
    using Sink = std::function<void(LogLevel level, std::string_view line)>;

    static Logger& instance();

    void set_level(LogLevel level);
    LogLevel level() const;

    // Passing an empty sink restores logging to stderr.
    void set_sink(Sink sink);

    void log(LogLevel level, std::string_view message);

private:
    Logger() = default;

    mutable std::mutex mutex_;
    LogLevel level_{LogLevel::Info};
    Sink sink_;
};

void log_trace(std::string_view message);
void log_debug(std::string_view message);
void log_info(std::string_view message);
void log_warn(std::string_view message);
void log_error(std::string_view message);

}  // namespace pulsenet
