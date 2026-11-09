#pragma once

#include "pulsenet/error.hpp"
#include "pulsenet/models/incident.hpp"
#include "pulsenet/models/monitor_result.hpp"
#include "pulsenet/models/monitor_target.hpp"
#include "pulsenet/monitoring/check_service.hpp"
#include "pulsenet/monitoring/incident_detector.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pulsenet {

class TargetRepository;
class ResultRepository;
class IncidentRepository;

struct SchedulerReport {
    std::vector<MonitorResult> results;
    std::vector<Incident> opened;
    std::vector<std::int64_t> resolved_ids;
    std::vector<std::string> teams_messages;
};

// Targets with no recorded due time are due. In-flight ids and disabled rows are skipped.
[[nodiscard]] std::vector<std::int64_t> due_target_ids(
    const std::vector<MonitorTarget>& targets,
    const std::unordered_map<std::int64_t, std::chrono::system_clock::time_point>& next_due,
    const std::unordered_set<std::int64_t>& in_flight,
    std::chrono::system_clock::time_point now);

// One thread runs the loop. check_now and run_due may be called from other threads;
// a mutex keeps a target from being probed twice at once, and it is not held during I/O.
class Scheduler {
public:
    Scheduler(TargetRepository& targets,
              ResultRepository& results,
              IncidentRepository& incidents,
              CheckService& checks);

    void set_listener(std::function<void(const SchedulerReport&)> listener);
    void set_teams_webhook(std::string webhook);
    // Runs after a target is marked in flight and before the probe starts.
    // Tests use it to prove a second caller observes the in-flight set.
    void set_before_probe(std::function<void()> hook);

    [[nodiscard]] Result<SchedulerReport> run_due(std::chrono::system_clock::time_point now);
    [[nodiscard]] Result<SchedulerReport> check_now(const MonitorTarget& target,
                                                    std::chrono::system_clock::time_point now);

    // Returns when stop becomes true. The current probe round is allowed to finish.
    void run(std::atomic<bool>& stop);

private:
    [[nodiscard]] Result<SchedulerReport> check_targets(const std::vector<MonitorTarget>& targets,
                                                        std::chrono::system_clock::time_point now,
                                                        bool respect_due);
    [[nodiscard]] TrackedTarget& state_for(std::int64_t target_id);
    [[nodiscard]] TrackedTarget load_state(std::int64_t target_id);
    void publish(const SchedulerReport& report);

    TargetRepository& targets_;
    ResultRepository& results_;
    IncidentRepository& incidents_;
    CheckService& checks_;
    std::mutex mutex_;
    std::mutex wait_mutex_;
    std::condition_variable stop_cv_;
    std::unordered_map<std::int64_t, std::chrono::system_clock::time_point> next_due_;
    std::unordered_map<std::int64_t, TrackedTarget> tracked_;
    std::unordered_set<std::int64_t> in_flight_;
    std::function<void(const SchedulerReport&)> listener_;
    std::function<void()> before_probe_;
    std::string teams_webhook_;
};

}  // namespace pulsenet
