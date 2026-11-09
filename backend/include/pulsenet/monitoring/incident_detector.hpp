#pragma once

#include "pulsenet/models/incident.hpp"
#include "pulsenet/models/monitor_result.hpp"
#include "pulsenet/models/monitor_target.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace pulsenet {

// Why an incident is open. Rank increases from a status note, to sustained
// degradation, to an unavailable target. A higher rank replaces a lower one.
enum class IncidentCause { None, StatusChange, Degraded, Unavailable };

struct TrackedTarget {
    TargetStatus previous{TargetStatus::Unknown};
    bool has_previous{false};
    bool loaded{false};
    int offline_streak{0};
    int degraded_streak{0};
    std::int64_t open_incident_id{0};
    IncidentCause open_cause{IncidentCause::None};
};

struct DetectorOutput {
    std::optional<std::int64_t> resolve_id;
    std::optional<Incident> open;
};

// Updates streak memory and says whether to resolve the open incident and
// whether to insert a new one. UNKNOWN results are ignored.
[[nodiscard]] DetectorOutput evaluate_incident(const MonitorTarget& target,
                                               const MonitorResult& result,
                                               TrackedTarget& state);

}  // namespace pulsenet
