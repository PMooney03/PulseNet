#include "pulsenet/monitoring/scheduler.hpp"

#include "pulsenet/database/incident_repository.hpp"
#include "pulsenet/database/result_repository.hpp"
#include "pulsenet/database/target_repository.hpp"
#include "pulsenet/monitoring/teams_notifier.hpp"
#include "pulsenet/utilities/logger.hpp"

#include <utility>

namespace pulsenet {
namespace {

class InFlightGuard {
public:
    InFlightGuard(std::mutex& mutex, std::unordered_set<std::int64_t>& in_flight, std::vector<std::int64_t> ids)
        : mutex_(mutex), in_flight_(in_flight), ids_(std::move(ids)) {}

    ~InFlightGuard() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto id : ids_) {
            in_flight_.erase(id);
        }
    }

    InFlightGuard(const InFlightGuard&) = delete;
    InFlightGuard& operator=(const InFlightGuard&) = delete;

private:
    std::mutex& mutex_;
    std::unordered_set<std::int64_t>& in_flight_;
    std::vector<std::int64_t> ids_;
};

IncidentCause cause_from(const Incident& incident) {
    if (incident.severity == Severity::Critical) {
        return IncidentCause::Unavailable;
    }
    if (incident.severity == Severity::Warning) {
        return IncidentCause::Degraded;
    }
    return IncidentCause::StatusChange;
}

}  // namespace

std::vector<std::int64_t> due_target_ids(
    const std::vector<MonitorTarget>& targets,
    const std::unordered_map<std::int64_t, std::chrono::system_clock::time_point>& next_due,
    const std::unordered_set<std::int64_t>& in_flight,
    const std::chrono::system_clock::time_point now) {
    std::vector<std::int64_t> due;
    for (const auto& target : targets) {
        if (!target.enabled || target.id <= 0 || in_flight.contains(target.id)) {
            continue;
        }
        const auto scheduled = next_due.find(target.id);
        if (scheduled != next_due.end() && scheduled->second > now) {
            continue;
        }
        due.push_back(target.id);
    }
    return due;
}

Scheduler::Scheduler(TargetRepository& targets,
                     ResultRepository& results,
                     IncidentRepository& incidents,
                     CheckService& checks)
    : targets_(targets), results_(results), incidents_(incidents), checks_(checks) {}

void Scheduler::set_listener(std::function<void(const SchedulerReport&)> listener) {
    std::lock_guard<std::mutex> lock(mutex_);
    listener_ = std::move(listener);
}

void Scheduler::set_teams_webhook(std::string webhook) {
    std::lock_guard<std::mutex> lock(mutex_);
    teams_webhook_ = std::move(webhook);
}

void Scheduler::set_before_probe(std::function<void()> hook) {
    std::lock_guard<std::mutex> lock(mutex_);
    before_probe_ = std::move(hook);
}

Result<SchedulerReport> Scheduler::run_due(const std::chrono::system_clock::time_point now) {
    const auto listed = targets_.list_all();
    if (!listed) {
        return Result<SchedulerReport>::failure(listed.error());
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::unordered_set<std::int64_t> live;
        for (const auto& target : listed.value()) {
            live.insert(target.id);
        }
        for (auto it = tracked_.begin(); it != tracked_.end();) {
            if (!live.contains(it->first)) {
                next_due_.erase(it->first);
                it = tracked_.erase(it);
            } else {
                ++it;
            }
        }
    }
    return check_targets(listed.value(), now, true);
}

Result<SchedulerReport> Scheduler::check_now(const MonitorTarget& target,
                                             const std::chrono::system_clock::time_point now) {
    return check_targets({target}, now, false);
}

void Scheduler::run(std::atomic<bool>& stop) {
    while (!stop.load()) {
        const auto now = std::chrono::system_clock::now();
        auto report = run_due(now);
        if (!report) {
            log_error(report.error().message);
        }
        std::chrono::milliseconds wait{250};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& [id, due] : next_due_) {
                (void)id;
                if (due > now) {
                    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(due - now);
                    if (remaining.count() > 0 && remaining < wait) {
                        wait = remaining;
                    }
                }
            }
        }
        std::unique_lock<std::mutex> wait_lock(wait_mutex_);
        stop_cv_.wait_for(wait_lock, wait, [&stop] { return stop.load(); });
    }
}

Result<SchedulerReport> Scheduler::check_targets(const std::vector<MonitorTarget>& targets,
                                                 const std::chrono::system_clock::time_point now,
                                                 const bool respect_due) {
    std::vector<MonitorTarget> selected;
    std::vector<std::int64_t> ids;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::unordered_set<std::int64_t> empty;
        const auto& flights = in_flight_;
        const auto due = respect_due ? due_target_ids(targets, next_due_, flights, now)
                                     : [&] {
                                           std::vector<std::int64_t> forced;
                                           for (const auto& target : targets) {
                                               if (target.enabled && target.id > 0 && !flights.contains(target.id)) {
                                                   forced.push_back(target.id);
                                               }
                                           }
                                           return forced;
                                       }();
        for (const auto id : due) {
            for (const auto& target : targets) {
                if (target.id == id) {
                    in_flight_.insert(id);
                    ids.push_back(id);
                    selected.push_back(target);
                    // Load history before the new row is inserted, or the streak
                    // would count that row twice.
                    static_cast<void>(state_for(id));
                    break;
                }
            }
        }
    }

    InFlightGuard guard(mutex_, in_flight_, ids);
    if (selected.empty()) {
        return Result<SchedulerReport>::success(SchedulerReport{});
    }

    std::function<void()> before;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        before = before_probe_;
    }
    if (before) {
        before();
    }

    auto checked = checks_.run_round(selected);
    if (!checked) {
        return Result<SchedulerReport>::failure(checked.error());
    }

    SchedulerReport report;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (std::size_t index = 0; index < selected.size() && index < checked.value().size(); ++index) {
            const auto& target = selected[index];
            const auto& result = checked.value()[index];
            report.results.push_back(result);
            const auto interval = target.interval.count() > 0 ? target.interval : std::chrono::seconds{1};
            next_due_[target.id] = now + interval;
            if (result.status == TargetStatus::Unknown) {
                continue;
            }

            auto& state = state_for(target.id);
            const auto decision = evaluate_incident(target, result, state);
            if (decision.resolve_id) {
                const auto previous = incidents_.find_by_id(*decision.resolve_id);
                const auto resolved = incidents_.resolve(*decision.resolve_id, result.checked_at);
                if (!resolved) {
                    return Result<SchedulerReport>::failure(resolved.error());
                }
                report.resolved_ids.push_back(*decision.resolve_id);
                log_info("incident resolved id=" + std::to_string(*decision.resolve_id));
                if (previous && result.status == TargetStatus::Online &&
                    previous.value().severity != Severity::Info) {
                    report.teams_messages.push_back(
                        teams_recovered_message(target.name, previous.value()));
                }
            }
            if (decision.open) {
                auto inserted = incidents_.insert(*decision.open);
                if (!inserted) {
                    state.open_cause = IncidentCause::None;
                    return Result<SchedulerReport>::failure(inserted.error());
                }
                state.open_incident_id = inserted.value();
                Incident opened = *decision.open;
                opened.id = inserted.value();
                log_info("incident opened id=" + std::to_string(opened.id) + " severity=" +
                         std::string(to_string(opened.severity)) + " reason=" + opened.reason);
                if (opened.severity != Severity::Info) {
                    report.teams_messages.push_back(teams_down_message(target, opened, result));
                }
                report.opened.push_back(std::move(opened));
            }
        }
    }

    publish(report);
    return Result<SchedulerReport>::success(std::move(report));
}

TrackedTarget& Scheduler::state_for(const std::int64_t target_id) {
    auto [it, inserted] = tracked_.emplace(target_id, TrackedTarget{});
    if (inserted || !it->second.loaded) {
        it->second = load_state(target_id);
    }
    return it->second;
}

TrackedTarget Scheduler::load_state(const std::int64_t target_id) {
    TrackedTarget state;
    state.loaded = true;
    const auto history = results_.list_for_target(target_id, 50);
    if (history && !history.value().empty() && history.value().front().status != TargetStatus::Unknown) {
        const auto latest = history.value().front().status;
        state.previous = latest;
        state.has_previous = true;
        int streak = 0;
        for (const auto& row : history.value()) {
            if (row.status != latest) {
                break;
            }
            ++streak;
        }
        if (latest == TargetStatus::Offline) {
            state.offline_streak = streak;
        } else if (latest == TargetStatus::Degraded) {
            state.degraded_streak = streak;
        }
    }

    const auto existing = incidents_.list_for_target(target_id, 20);
    if (existing) {
        for (const auto& incident : existing.value()) {
            if (incident.status == IncidentStatus::Open) {
                state.open_incident_id = incident.id;
                state.open_cause = cause_from(incident);
                break;
            }
        }
    }
    return state;
}

void Scheduler::publish(const SchedulerReport& report) {
    std::function<void(const SchedulerReport&)> listener;
    std::string webhook;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        listener = listener_;
        webhook = teams_webhook_;
    }
    if (listener) {
        try {
            listener(report);
        } catch (const std::exception& ex) {
            log_error(std::string("scheduler listener failed: ") + ex.what());
        }
    }
    post_teams_messages(webhook, report.teams_messages);
}

}  // namespace pulsenet
