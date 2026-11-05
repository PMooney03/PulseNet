#pragma once

#include "pulsenet/models/enums.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace pulsenet {

// One completed or failed check. The monitoring engine (Phase 2) decides
// success and status. The repository stores that decision; it does not
// recalculate health.
struct MonitorResult {
    std::int64_t id{0};
    std::int64_t target_id{0};
    std::chrono::system_clock::time_point checked_at{};
    bool success{false};
    std::optional<std::chrono::milliseconds> response_time;
    std::optional<int> http_status;
    std::optional<std::string> error_message;
    TargetStatus status{TargetStatus::Unknown};

    bool operator==(const MonitorResult&) const = default;
};

}  // namespace pulsenet
