#pragma once

#include "pulsenet/error.hpp"
#include "pulsenet/models/monitor_result.hpp"
#include "pulsenet/models/monitor_target.hpp"
#include "pulsenet/monitoring/probe_engine.hpp"

#include <vector>

namespace pulsenet {

class ResultRepository;

// Owns neither the database nor the engine. It runs a probe round and stores
// every real measurement. Disabled and invalid targets come back as UNKNOWN
// and are not inserted.
class CheckService {
public:
    CheckService(ResultRepository& results, ProbeEngine& engine);

    [[nodiscard]] Result<std::vector<MonitorResult>> run_round(const std::vector<MonitorTarget>& targets);

private:
    ResultRepository& results_;
    ProbeEngine& engine_;
};

}  // namespace pulsenet
