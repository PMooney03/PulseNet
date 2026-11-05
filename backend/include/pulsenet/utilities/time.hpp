#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace pulsenet {

// Stored timestamps are UTC milliseconds since the Unix epoch.
// Integer milliseconds sort correctly in SQLite and round-trip through
// std::chrono without a timezone library.
std::int64_t to_unix_ms(std::chrono::system_clock::time_point time);
std::chrono::system_clock::time_point from_unix_ms(std::int64_t milliseconds);
std::string format_utc(std::chrono::system_clock::time_point time);

}  // namespace pulsenet
