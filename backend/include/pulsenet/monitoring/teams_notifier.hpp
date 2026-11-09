#pragma once

#include "pulsenet/models/incident.hpp"
#include "pulsenet/models/monitor_result.hpp"
#include "pulsenet/models/monitor_target.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace pulsenet {

[[nodiscard]] std::string teams_down_message(const MonitorTarget& target,
                                             const Incident& incident,
                                             const MonitorResult& result);
[[nodiscard]] std::string teams_recovered_message(std::string_view target_name, const Incident& incident);

// Posts {"text": "..."} to a Teams Workflows webhook. An empty URL does nothing.
// A failed post is logged and does not throw.
void post_teams_messages(std::string_view webhook, const std::vector<std::string>& messages);

}  // namespace pulsenet
