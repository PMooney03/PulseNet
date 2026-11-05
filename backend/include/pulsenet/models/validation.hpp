#pragma once

#include "pulsenet/error.hpp"
#include "pulsenet/models/incident.hpp"
#include "pulsenet/models/monitor_result.hpp"
#include "pulsenet/models/monitor_target.hpp"

#include <optional>

namespace pulsenet {

[[nodiscard]] MonitorTarget normalize(MonitorTarget target);
[[nodiscard]] MonitorResult normalize(MonitorResult result);
[[nodiscard]] Incident normalize(Incident incident);

[[nodiscard]] std::optional<Error> validate(const MonitorTarget& target);
[[nodiscard]] std::optional<Error> validate(const MonitorResult& result);
[[nodiscard]] std::optional<Error> validate(const Incident& incident);

}  // namespace pulsenet
