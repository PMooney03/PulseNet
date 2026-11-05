#pragma once

#include "pulsenet/models/enums.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace pulsenet {

// Incident detection itself is Phase 3. Phase 1 only defines the record and
// stores it so the later detector has a stable table to write.
struct Incident {
    std::int64_t id{0};
    std::int64_t target_id{0};
    Severity severity{Severity::Warning};
    std::string reason;
    std::chrono::system_clock::time_point started_at{};
    std::optional<std::chrono::system_clock::time_point> resolved_at;
    IncidentStatus status{IncidentStatus::Open};

    bool operator==(const Incident&) const = default;
};

}  // namespace pulsenet
