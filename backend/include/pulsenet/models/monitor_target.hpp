#pragma once

#include "pulsenet/models/enums.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace pulsenet {

// A target is configuration. Phase 1 stores it. Later phases schedule checks
// against it. id 0 means the row has not been inserted yet.
struct MonitorTarget {
    std::int64_t id{0};
    std::string name;
    std::string host;
    std::optional<int> port;
    MonitorType type{MonitorType::Tcp};
    std::chrono::seconds interval{30};
    std::chrono::milliseconds timeout{5000};
    bool enabled{true};
    std::chrono::milliseconds latency_threshold{200};
    int failure_threshold{3};
    std::chrono::system_clock::time_point created_at{};

    bool operator==(const MonitorTarget&) const = default;
};

}  // namespace pulsenet
