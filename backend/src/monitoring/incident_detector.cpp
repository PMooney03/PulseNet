#include "pulsenet/monitoring/incident_detector.hpp"

#include <string>

namespace pulsenet {
namespace {

int rank_of(IncidentCause cause) {
    switch (cause) {
        case IncidentCause::StatusChange:
            return 1;
        case IncidentCause::Degraded:
            return 2;
        case IncidentCause::Unavailable:
            return 3;
        case IncidentCause::None:
            return 0;
    }
    return 0;
}

}  // namespace

DetectorOutput evaluate_incident(const MonitorTarget& target, const MonitorResult& result, TrackedTarget& state) {
    DetectorOutput output;
    if (result.status == TargetStatus::Unknown) {
        return output;
    }

    const bool changed = state.has_previous && state.previous != result.status;
    const TargetStatus previous = state.previous;

    if (result.status == TargetStatus::Offline) {
        ++state.offline_streak;
        state.degraded_streak = 0;
    } else if (result.status == TargetStatus::Degraded) {
        ++state.degraded_streak;
        state.offline_streak = 0;
    } else {
        state.offline_streak = 0;
        state.degraded_streak = 0;
        if (state.open_incident_id != 0) {
            output.resolve_id = state.open_incident_id;
            state.open_incident_id = 0;
            state.open_cause = IncidentCause::None;
        }
        state.previous = result.status;
        state.has_previous = true;
        return output;
    }

    const int threshold = target.failure_threshold < 1 ? 1 : target.failure_threshold;
    IncidentCause cause = IncidentCause::None;
    Severity severity = Severity::Info;
    std::string reason;
    if (state.offline_streak >= threshold) {
        cause = IncidentCause::Unavailable;
        severity = Severity::Critical;
        reason = "unavailable for " + std::to_string(state.offline_streak) + " consecutive checks";
    } else if (state.degraded_streak >= threshold) {
        cause = IncidentCause::Degraded;
        severity = Severity::Warning;
        reason = "degraded for " + std::to_string(state.degraded_streak) + " consecutive checks";
    } else if (changed) {
        cause = IncidentCause::StatusChange;
        severity = Severity::Info;
        reason = "status changed from " + std::string(to_string(previous)) + " to " +
                 std::string(to_string(result.status));
    }

    if (cause != IncidentCause::None && rank_of(cause) > rank_of(state.open_cause)) {
        if (state.open_incident_id != 0) {
            output.resolve_id = state.open_incident_id;
            state.open_incident_id = 0;
        }
        Incident incident;
        incident.target_id = target.id;
        incident.severity = severity;
        incident.reason = std::move(reason);
        incident.started_at = result.checked_at;
        incident.status = IncidentStatus::Open;
        output.open = std::move(incident);
        state.open_cause = cause;
    }

    state.previous = result.status;
    state.has_previous = true;
    return output;
}

}  // namespace pulsenet
