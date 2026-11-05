#include "pulsenet/utilities/logger.hpp"

#include "pulsenet/utilities/text.hpp"
#include "pulsenet/utilities/time.hpp"

#include <chrono>
#include <iostream>
#include <sstream>
#include <utility>

namespace pulsenet {
namespace {

std::string format_line(LogLevel level, std::string_view message) {
    std::ostringstream out;
    out << format_utc(std::chrono::system_clock::now()) << ' ' << to_string(level) << ' ' << message;
    return out.str();
}

}  // namespace

std::string_view to_string(LogLevel level) {
    switch (level) {
        case LogLevel::Trace:
            return "TRACE";
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
    }
    return "INFO";
}

std::optional<LogLevel> log_level_from_string(std::string_view text) {
    const auto value = to_lower_ascii(text);
    if (value == "trace") {
        return LogLevel::Trace;
    }
    if (value == "debug") {
        return LogLevel::Debug;
    }
    if (value == "info") {
        return LogLevel::Info;
    }
    if (value == "warn" || value == "warning") {
        return LogLevel::Warn;
    }
    if (value == "error") {
        return LogLevel::Error;
    }
    return std::nullopt;
}

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::set_level(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
}

LogLevel Logger::level() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return level_;
}

void Logger::set_sink(Sink sink) {
    std::lock_guard<std::mutex> lock(mutex_);
    sink_ = std::move(sink);
}

void Logger::log(LogLevel level, std::string_view message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (static_cast<int>(level) < static_cast<int>(level_)) {
        return;
    }
    const std::string line = format_line(level, message);
    if (sink_) {
        sink_(level, line);
    } else {
        std::cerr << line << '\n';
    }
}

void log_trace(std::string_view message) { Logger::instance().log(LogLevel::Trace, message); }
void log_debug(std::string_view message) { Logger::instance().log(LogLevel::Debug, message); }
void log_info(std::string_view message) { Logger::instance().log(LogLevel::Info, message); }
void log_warn(std::string_view message) { Logger::instance().log(LogLevel::Warn, message); }
void log_error(std::string_view message) { Logger::instance().log(LogLevel::Error, message); }

}  // namespace pulsenet
