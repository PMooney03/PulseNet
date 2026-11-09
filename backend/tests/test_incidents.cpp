#include "pulsenet/monitoring/diagnostics.hpp"
#include "pulsenet/monitoring/incident_detector.hpp"
#include "pulsenet/monitoring/teams_notifier.hpp"

#include <gtest/gtest.h>

#include <chrono>

namespace {

using pulsenet::IncidentCause;
using pulsenet::MonitorResult;
using pulsenet::MonitorTarget;
using pulsenet::MonitorType;
using pulsenet::TargetStatus;
using pulsenet::TrackedTarget;
using pulsenet::evaluate_incident;
using pulsenet::explain_result;

MonitorTarget target_with_threshold(const int threshold) {
    MonitorTarget target;
    target.id = 7;
    target.name = "edge";
    target.host = "10.0.0.8";
    target.port = 443;
    target.type = MonitorType::Https;
    target.failure_threshold = threshold;
    return target;
}

MonitorResult result_with(const TargetStatus status, const std::string& error = {}) {
    MonitorResult result;
    result.target_id = 7;
    result.status = status;
    result.success = status == TargetStatus::Online || status == TargetStatus::Degraded;
    result.checked_at = std::chrono::system_clock::time_point{std::chrono::milliseconds{1'700'000'000'000}};
    if (!error.empty()) {
        result.error_message = error;
    }
    if (status == TargetStatus::Degraded || status == TargetStatus::Online) {
        result.response_time = std::chrono::milliseconds{40};
        result.http_status = status == TargetStatus::Degraded ? 503 : 204;
    }
    return result;
}

}  // namespace

TEST(IncidentDetector, UnavailableAfterTheFailureThreshold) {
    auto target = target_with_threshold(3);
    TrackedTarget state;
    EXPECT_FALSE(evaluate_incident(target, result_with(TargetStatus::Offline), state).open);
    EXPECT_FALSE(evaluate_incident(target, result_with(TargetStatus::Offline), state).open);
    const auto third = evaluate_incident(target, result_with(TargetStatus::Offline), state);
    ASSERT_TRUE(third.open);
    EXPECT_EQ(third.open->severity, pulsenet::Severity::Critical);
    EXPECT_NE(third.open->reason.find("unavailable for 3"), std::string::npos);
    EXPECT_EQ(state.open_cause, IncidentCause::Unavailable);

    const auto again = evaluate_incident(target, result_with(TargetStatus::Offline), state);
    EXPECT_FALSE(again.open);
    EXPECT_FALSE(again.resolve_id);
}

TEST(IncidentDetector, DegradedOpensAWarningAndRecoveryResolvesIt) {
    auto target = target_with_threshold(2);
    TrackedTarget state;
    EXPECT_FALSE(evaluate_incident(target, result_with(TargetStatus::Degraded), state).open);
    const auto opened = evaluate_incident(target, result_with(TargetStatus::Degraded), state);
    ASSERT_TRUE(opened.open);
    EXPECT_EQ(opened.open->severity, pulsenet::Severity::Warning);
    state.open_incident_id = 11;

    const auto recovered = evaluate_incident(target, result_with(TargetStatus::Online), state);
    EXPECT_EQ(recovered.resolve_id, 11);
    EXPECT_FALSE(recovered.open);
    EXPECT_EQ(state.open_incident_id, 0);
    EXPECT_EQ(state.offline_streak, 0);
}

TEST(IncidentDetector, StatusChangeIsInfoUntilTheThresholdEscalates) {
    auto target = target_with_threshold(3);
    TrackedTarget state;
    static_cast<void>(evaluate_incident(target, result_with(TargetStatus::Online), state));
    const auto changed = evaluate_incident(target, result_with(TargetStatus::Offline), state);
    ASSERT_TRUE(changed.open);
    EXPECT_EQ(changed.open->severity, pulsenet::Severity::Info);
    EXPECT_NE(changed.open->reason.find("online"), std::string::npos);
    state.open_incident_id = 4;

    EXPECT_FALSE(evaluate_incident(target, result_with(TargetStatus::Offline), state).open);
    const auto critical = evaluate_incident(target, result_with(TargetStatus::Offline), state);
    EXPECT_EQ(critical.resolve_id, 4);
    ASSERT_TRUE(critical.open);
    EXPECT_EQ(critical.open->severity, pulsenet::Severity::Critical);
}

TEST(IncidentDetector, UnknownDoesNotMoveTheStreak) {
    auto target = target_with_threshold(1);
    TrackedTarget state;
    static_cast<void>(evaluate_incident(target, result_with(TargetStatus::Unknown, "target is disabled"), state));
    EXPECT_FALSE(state.has_previous);
    EXPECT_EQ(state.offline_streak, 0);
}

TEST(Diagnostics, ExplainsSuccessFailureAndAMissingCheck) {
    auto target = target_with_threshold(3);
    target.type = MonitorType::Tcp;
    auto online = result_with(TargetStatus::Online);
    online.http_status.reset();
    online.response_time = std::chrono::milliseconds{6};
    EXPECT_NE(explain_result(target, online).find("TCP connect"), std::string::npos);
    EXPECT_NE(explain_result(target, online).find("6 ms"), std::string::npos);

    auto offline = result_with(TargetStatus::Offline, "connection timed out");
    EXPECT_NE(explain_result(target, offline).find("connection timed out"), std::string::npos);
    EXPECT_NE(explain_result(target, std::nullopt).find("No checks"), std::string::npos);

    target.type = MonitorType::Dns;
    target.port.reset();
    auto looked_up = result_with(TargetStatus::Online);
    looked_up.http_status.reset();
    EXPECT_NE(explain_result(target, looked_up).find("DNS lookup"), std::string::npos);
}

TEST(TeamsMessage, NamesTheTargetAndDoesNotPostWithoutAWebhook) {
    MonitorTarget target;
    target.name = "Google DNS";
    target.host = "8.8.8.8";
    target.port = 53;
    target.type = MonitorType::Tcp;
    pulsenet::Incident incident;
    incident.id = 6;
    incident.reason = "unavailable for 3 consecutive checks";
    incident.severity = pulsenet::Severity::Critical;
    auto offline = result_with(TargetStatus::Offline, "connection timed out");
    const auto down = pulsenet::teams_down_message(target, incident, offline);
    EXPECT_NE(down.find("PN-6"), std::string::npos);
    EXPECT_NE(down.find("Google DNS is down"), std::string::npos);
    EXPECT_NE(down.find("unavailable for 3 consecutive checks"), std::string::npos);
    EXPECT_NE(down.find("connection timed out"), std::string::npos);
    const auto recovered = pulsenet::teams_recovered_message(target.name, incident);
    EXPECT_NE(recovered.find("PN-6 is resolved"), std::string::npos);
    EXPECT_NE(recovered.find("is back"), std::string::npos);
    pulsenet::post_teams_messages({}, {down});
}
