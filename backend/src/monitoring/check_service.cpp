#include "pulsenet/monitoring/check_service.hpp"

#include "pulsenet/database/result_repository.hpp"
#include "pulsenet/utilities/logger.hpp"

#include <string>
#include <utility>

namespace pulsenet {
namespace {

std::string describe(const MonitorTarget& target, const MonitorResult& result) {
    std::string line = "check name=" + target.name + " status=" + std::string(to_string(result.status));
    if (result.response_time) {
        line += " latency_ms=" + std::to_string(result.response_time->count());
    }
    if (result.http_status) {
        line += " http=" + std::to_string(*result.http_status);
    }
    if (result.error_message) {
        line += " error=" + *result.error_message;
    }
    return line;
}

}  // namespace

CheckService::CheckService(ResultRepository& results, ProbeEngine& engine)
    : results_(results), engine_(engine) {}

Result<std::vector<MonitorResult>> CheckService::run_round(const std::vector<MonitorTarget>& targets) {
    auto checked = engine_.check_all(targets);
    if (checked.size() != targets.size()) {
        return Result<std::vector<MonitorResult>>::failure(
            Error{ErrorCode::Database, "check round returned the wrong number of results"});
    }

    for (std::size_t index = 0; index < checked.size(); ++index) {
        auto& result = checked[index];
        const std::string line = describe(targets[index], result);
        if (result.status == TargetStatus::Offline) {
            log_warn(line);
        } else {
            log_info(line);
        }
        // UNKNOWN means the probe never ran (disabled or invalid). Keeping
        // those out of the history table leaves room for real measurements.
        if (result.status == TargetStatus::Unknown || result.target_id <= 0) {
            continue;
        }
        const auto inserted = results_.insert(result);
        if (!inserted) {
            return Result<std::vector<MonitorResult>>::failure(inserted.error());
        }
        result.id = inserted.value();
    }
    return Result<std::vector<MonitorResult>>::success(std::move(checked));
}

}  // namespace pulsenet
