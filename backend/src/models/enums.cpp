#include "pulsenet/models/enums.hpp"

#include "pulsenet/utilities/text.hpp"

#include <stdexcept>
#include <string>

namespace pulsenet {
namespace {

[[noreturn]] void unknown_enum(const char* name) {
    throw std::logic_error(std::string("unknown ") + name);
}

}  // namespace

std::string_view to_string(MonitorType type) {
    switch (type) {
        case MonitorType::Tcp:
            return "tcp";
        case MonitorType::Http:
            return "http";
        case MonitorType::Https:
            return "https";
        case MonitorType::Dns:
            return "dns";
    }
    unknown_enum("monitor type");
}

std::optional<MonitorType> monitor_type_from_string(std::string_view text) {
    const auto value = to_lower_ascii(text);
    if (value == "tcp") {
        return MonitorType::Tcp;
    }
    if (value == "http") {
        return MonitorType::Http;
    }
    if (value == "https") {
        return MonitorType::Https;
    }
    if (value == "dns") {
        return MonitorType::Dns;
    }
    return std::nullopt;
}

std::string_view to_string(TargetStatus status) {
    switch (status) {
        case TargetStatus::Online:
            return "online";
        case TargetStatus::Degraded:
            return "degraded";
        case TargetStatus::Offline:
            return "offline";
        case TargetStatus::Unknown:
            return "unknown";
    }
    unknown_enum("target status");
}

std::optional<TargetStatus> target_status_from_string(std::string_view text) {
    const auto value = to_lower_ascii(text);
    if (value == "online") {
        return TargetStatus::Online;
    }
    if (value == "degraded") {
        return TargetStatus::Degraded;
    }
    if (value == "offline") {
        return TargetStatus::Offline;
    }
    if (value == "unknown") {
        return TargetStatus::Unknown;
    }
    return std::nullopt;
}

std::string_view to_string(Severity severity) {
    switch (severity) {
        case Severity::Info:
            return "info";
        case Severity::Warning:
            return "warning";
        case Severity::Critical:
            return "critical";
    }
    unknown_enum("severity");
}

std::optional<Severity> severity_from_string(std::string_view text) {
    const auto value = to_lower_ascii(text);
    if (value == "info") {
        return Severity::Info;
    }
    if (value == "warning") {
        return Severity::Warning;
    }
    if (value == "critical") {
        return Severity::Critical;
    }
    return std::nullopt;
}

std::string_view to_string(IncidentStatus status) {
    switch (status) {
        case IncidentStatus::Open:
            return "open";
        case IncidentStatus::Resolved:
            return "resolved";
    }
    unknown_enum("incident status");
}

std::optional<IncidentStatus> incident_status_from_string(std::string_view text) {
    const auto value = to_lower_ascii(text);
    if (value == "open") {
        return IncidentStatus::Open;
    }
    if (value == "resolved") {
        return IncidentStatus::Resolved;
    }
    return std::nullopt;
}

}  // namespace pulsenet
