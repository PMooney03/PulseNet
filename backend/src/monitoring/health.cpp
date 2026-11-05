#include "pulsenet/monitoring/health.hpp"

#include <utility>

namespace pulsenet {
namespace {

constexpr std::size_t kMaxStoredError = 500;

std::optional<std::string> limit_message(std::optional<std::string> message) {
    if (!message) {
        return std::nullopt;
    }
    if (message->size() > kMaxStoredError) {
        message->resize(kMaxStoredError);
    }
    return message;
}

bool slow(const Observation& observation, std::chrono::milliseconds threshold) {
    return observation.response_time.has_value() && *observation.response_time > threshold;
}

}  // namespace

Classification classify(MonitorType type,
                        std::chrono::milliseconds latency_threshold,
                        const Observation& observation) {
    Classification result;
    if (observation.skipped) {
        result.status = TargetStatus::Unknown;
        result.success = false;
        result.error_message = observation.error_message.value_or("check skipped");
        return result;
    }

    const bool http = type == MonitorType::Http || type == MonitorType::Https;
    if (!observation.reached_service || (http && !observation.http_status)) {
        result.status = TargetStatus::Offline;
        result.success = false;
        result.error_message = observation.error_message.value_or("check failed");
        return result;
    }

    const bool over_threshold = slow(observation, latency_threshold);
    if (http) {
        const int status = *observation.http_status;
        const bool healthy = status >= 200 && status < 400;
        if (!healthy || over_threshold) {
            result.status = TargetStatus::Degraded;
            result.success = true;
            std::string message = healthy ? "response time exceeded latency threshold"
                                          : "HTTP " + std::to_string(status);
            if (!healthy && over_threshold) {
                message += "; response time exceeded latency threshold";
            }
            result.error_message = std::move(message);
            return result;
        }
    } else if (over_threshold) {
        result.status = TargetStatus::Degraded;
        result.success = true;
        result.error_message = "response time exceeded latency threshold";
        return result;
    }

    result.status = TargetStatus::Online;
    result.success = true;
    return result;
}

MonitorResult make_result(const MonitorTarget& target, const Observation& observation) {
    const Classification classification = classify(target.type, target.latency_threshold, observation);
    MonitorResult result;
    result.target_id = target.id;
    result.checked_at = std::chrono::system_clock::now();
    result.success = classification.success;
    result.status = classification.status;
    result.error_message = limit_message(classification.error_message);
    result.http_status = observation.http_status;
    if (!observation.skipped) {
        result.response_time = observation.response_time;
    }
    return result;
}

}  // namespace pulsenet
