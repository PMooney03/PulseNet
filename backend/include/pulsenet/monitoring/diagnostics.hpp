#pragma once

#include "pulsenet/models/monitor_result.hpp"
#include "pulsenet/models/monitor_target.hpp"

#include <optional>
#include <string>

namespace pulsenet {

// One paragraph a person can read. Built from the stored check, not a second probe.
[[nodiscard]] std::string explain_result(const MonitorTarget& target, const std::optional<MonitorResult>& result);

}  // namespace pulsenet
