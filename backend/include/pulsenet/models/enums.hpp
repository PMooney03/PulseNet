#pragma once

#include <optional>
#include <string_view>

namespace pulsenet {

enum class MonitorType { Tcp, Http, Https, Dns };

enum class TargetStatus { Online, Degraded, Offline, Unknown };

enum class Severity { Info, Warning, Critical };

enum class IncidentStatus { Open, Resolved };

[[nodiscard]] std::string_view to_string(MonitorType type);
[[nodiscard]] std::optional<MonitorType> monitor_type_from_string(std::string_view text);

[[nodiscard]] std::string_view to_string(TargetStatus status);
[[nodiscard]] std::optional<TargetStatus> target_status_from_string(std::string_view text);

[[nodiscard]] std::string_view to_string(Severity severity);
[[nodiscard]] std::optional<Severity> severity_from_string(std::string_view text);

[[nodiscard]] std::string_view to_string(IncidentStatus status);
[[nodiscard]] std::optional<IncidentStatus> incident_status_from_string(std::string_view text);

}  // namespace pulsenet
