#pragma once

#include "pulsenet/models/monitor_result.hpp"
#include "pulsenet/models/monitor_target.hpp"

#include <chrono>
#include <optional>
#include <string>

namespace pulsenet {

// What a probe observed. Classification is a pure function of this record so
// tests can cover every status without opening a socket.
struct Observation {
    // Disabled target, or the target failed validation before any I/O.
    bool skipped{false};
    // TCP connected, DNS returned an address, or HTTP delivered a status line.
    bool reached_service{false};
    std::optional<std::chrono::milliseconds> response_time;
    std::optional<int> http_status;
    std::optional<std::string> error_message;
};

struct Classification {
    TargetStatus status{TargetStatus::Unknown};
    bool success{false};
    std::optional<std::string> error_message;
};

// Status rules:
//   UNKNOWN   the check was not attempted
//   OFFLINE   nothing answered (timeout, DNS failure, refused, TLS, or no HTTP status)
//   DEGRADED  the service answered, but it was slow or the HTTP status was outside 200-399
//   ONLINE    the service answered inside the latency threshold
// HTTP 200-399 includes redirects. 4xx and 5xx are degraded, not offline,
// because the server did respond. success is true for ONLINE and DEGRADED.
[[nodiscard]] Classification classify(MonitorType type,
                                      std::chrono::milliseconds latency_threshold,
                                      const Observation& observation);

[[nodiscard]] MonitorResult make_result(const MonitorTarget& target, const Observation& observation);

}  // namespace pulsenet
