#include "pulsenet/monitoring/diagnostics.hpp"

#include <string>

namespace pulsenet {
namespace {

std::string endpoint(const MonitorTarget& target) {
    if (!target.port) {
        return target.host;
    }
    return target.host + ":" + std::to_string(*target.port);
}

std::string latency(const MonitorResult& result) {
    if (!result.response_time) {
        return "an unknown time";
    }
    return std::to_string(result.response_time->count()) + " ms";
}

}  // namespace

std::string explain_result(const MonitorTarget& target, const std::optional<MonitorResult>& result) {
    if (!result) {
        return "No checks have been stored for this target.";
    }
    const std::string where = endpoint(target);
    if (result->status == TargetStatus::Unknown) {
        return result->error_message.value_or("The check was not attempted.");
    }
    if (result->status == TargetStatus::Offline) {
        std::string line = "No answer from " + where + ".";
        if (result->error_message) {
            line += " " + *result->error_message;
        }
        return line;
    }

    std::string line;
    if (target.type == MonitorType::Dns) {
        line = "DNS lookup for " + target.host + " succeeded in " + latency(*result) + ".";
    } else if (target.type == MonitorType::Http || target.type == MonitorType::Https) {
        const std::string code = result->http_status ? std::to_string(*result->http_status) : "no status";
        line = "HTTP check of " + where + " returned " + code + " in " + latency(*result) + ".";
    } else {
        line = "TCP connect to " + where + " succeeded in " + latency(*result) + ".";
    }
    if (result->status == TargetStatus::Degraded && result->error_message) {
        line += " " + *result->error_message;
    }
    return line;
}

}  // namespace pulsenet
