#include "pulsenet/models/validation.hpp"

#include "pulsenet/utilities/text.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace pulsenet {
namespace {

constexpr int kMaxNameLength = 120;
constexpr int kMaxHostLength = 255;
constexpr int kMaxReasonLength = 2000;
constexpr int kMaxErrorLength = 2000;
constexpr std::int64_t kMaxIntervalSeconds = 24 * 60 * 60;
constexpr std::int64_t kMaxTimeoutMs = 120000;
constexpr std::int64_t kMaxLatencyMs = 60000;
constexpr std::int64_t kMaxResponseMs = 600000;
constexpr int kMaxFailureThreshold = 100;

Error invalid(std::string message) { return Error{ErrorCode::Validation, std::move(message)}; }

bool contains_whitespace(std::string_view value) {
    return value.find_first_of(" \t\r\n") != std::string_view::npos;
}

// "example.com:443" and "8.8.8.8:53" belong in separate host and port fields.
// IPv6 addresses contain more than one colon, so they are left alone.
bool host_embeds_port(std::string_view host) {
    const auto colon = host.find(':');
    if (colon == std::string_view::npos || host.find(':', colon + 1) != std::string_view::npos) {
        return false;
    }
    const auto suffix = host.substr(colon + 1);
    if (suffix.empty()) {
        return false;
    }
    return std::all_of(suffix.begin(), suffix.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; });
}

}  // namespace

MonitorTarget normalize(MonitorTarget target) {
    target.name = trim_ascii(target.name);
    target.host = trim_ascii(target.host);
    return target;
}

MonitorResult normalize(MonitorResult result) {
    if (!result.error_message) {
        return result;
    }
    auto text = trim_ascii(*result.error_message);
    if (text.empty()) {
        result.error_message.reset();
    } else {
        result.error_message = std::move(text);
    }
    return result;
}

Incident normalize(Incident incident) {
    incident.reason = trim_ascii(incident.reason);
    return incident;
}

std::optional<Error> validate(const MonitorTarget& target) {
    if (target.name.empty() || target.name.size() > kMaxNameLength) {
        return invalid("Name is required and must be at most 120 characters");
    }
    if (target.host.empty() || target.host.size() > kMaxHostLength) {
        return invalid("Host is required and must be at most 255 characters");
    }
    if (contains_whitespace(target.host) || target.host.find("://") != std::string::npos ||
        target.host.find('/') != std::string::npos) {
        return invalid("Host must be a hostname or IP address, not a URL");
    }
    if (host_embeds_port(target.host)) {
        return invalid("Host must not include a port. Set the port separately");
    }
    if (target.port && (*target.port < 1 || *target.port > 65535)) {
        return invalid("Port must be between 1 and 65535");
    }
    if (target.type == MonitorType::Tcp && !target.port) {
        return invalid("TCP monitors require a port");
    }
    if (target.interval.count() < 1 || target.interval.count() > kMaxIntervalSeconds) {
        return invalid("Interval must be between 1 second and 24 hours");
    }
    if (target.timeout.count() < 1 || target.timeout.count() > kMaxTimeoutMs) {
        return invalid("Timeout must be between 1 ms and 120000 ms");
    }
    if (target.latency_threshold.count() < 1 || target.latency_threshold.count() > kMaxLatencyMs) {
        return invalid("Latency threshold must be between 1 ms and 60000 ms");
    }
    if (target.failure_threshold < 1 || target.failure_threshold > kMaxFailureThreshold) {
        return invalid("Failure threshold must be between 1 and 100");
    }
    return std::nullopt;
}

std::optional<Error> validate(const MonitorResult& result) {
    if (result.target_id <= 0) {
        return invalid("Target id is required");
    }
    if (result.response_time &&
        (result.response_time->count() < 0 || result.response_time->count() > kMaxResponseMs)) {
        return invalid("Response time is outside the supported range");
    }
    if (result.http_status && (*result.http_status < 100 || *result.http_status > 599)) {
        return invalid("HTTP status must be between 100 and 599");
    }
    if (result.error_message && result.error_message->size() > kMaxErrorLength) {
        return invalid("Error message must be at most 2000 characters");
    }
    return std::nullopt;
}

std::optional<Error> validate(const Incident& incident) {
    if (incident.target_id <= 0) {
        return invalid("Target id is required");
    }
    if (incident.reason.empty() || incident.reason.size() > kMaxReasonLength) {
        return invalid("Incident reason is required and must be at most 2000 characters");
    }
    if (incident.status == IncidentStatus::Open && incident.resolved_at) {
        return invalid("An open incident cannot have a resolved time");
    }
    if (incident.status == IncidentStatus::Resolved && !incident.resolved_at) {
        return invalid("A resolved incident requires a resolved time");
    }
    if (incident.resolved_at && *incident.resolved_at < incident.started_at) {
        return invalid("Resolved time is earlier than the start time");
    }
    return std::nullopt;
}

}  // namespace pulsenet