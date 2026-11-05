#include "pulsenet/monitoring/health.hpp"

#include <gtest/gtest.h>

#include <chrono>

namespace {

using pulsenet::Classification;
using pulsenet::MonitorType;
using pulsenet::Observation;
using pulsenet::TargetStatus;
using pulsenet::classify;

Observation answered(std::chrono::milliseconds latency) {
    Observation observation;
    observation.reached_service = true;
    observation.response_time = latency;
    return observation;
}

Classification tcp(std::chrono::milliseconds latency, std::chrono::milliseconds threshold) {
    return classify(MonitorType::Tcp, threshold, answered(latency));
}

}  // namespace

TEST(HealthTest, SkippedChecksStayUnknown) {
    Observation observation;
    observation.skipped = true;
    observation.error_message = "target is disabled";
    const auto result = classify(MonitorType::Tcp, std::chrono::milliseconds{100}, observation);
    EXPECT_EQ(result.status, TargetStatus::Unknown);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error_message, "target is disabled");
}

TEST(HealthTest, TransportFailureIsOffline) {
    Observation observation;
    observation.error_message = "connection timed out";
    observation.response_time = std::chrono::milliseconds{200};
    const auto result = classify(MonitorType::Tcp, std::chrono::milliseconds{100}, observation);
    EXPECT_EQ(result.status, TargetStatus::Offline);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error_message, "connection timed out");
}

TEST(HealthTest, LatencyThresholdIsInclusive) {
    const auto threshold = std::chrono::milliseconds{200};
    const auto at_limit = tcp(threshold, threshold);
    EXPECT_EQ(at_limit.status, TargetStatus::Online);
    EXPECT_TRUE(at_limit.success);
    EXPECT_FALSE(at_limit.error_message.has_value());

    const auto over = tcp(std::chrono::milliseconds{201}, threshold);
    EXPECT_EQ(over.status, TargetStatus::Degraded);
    EXPECT_TRUE(over.success);
    EXPECT_EQ(over.error_message, "response time exceeded latency threshold");
}

TEST(HealthTest, DnsUsesTheSameLatencyRule) {
    const auto fast = classify(MonitorType::Dns, std::chrono::milliseconds{50}, answered(std::chrono::milliseconds{18}));
    EXPECT_EQ(fast.status, TargetStatus::Online);
    const auto slow = classify(MonitorType::Dns, std::chrono::milliseconds{50}, answered(std::chrono::milliseconds{80}));
    EXPECT_EQ(slow.status, TargetStatus::Degraded);
}

TEST(HealthTest, HttpStatusAndLatency) {
    const auto threshold = std::chrono::milliseconds{200};

    Observation ok = answered(std::chrono::milliseconds{14});
    ok.http_status = 204;
    const auto online = classify(MonitorType::Https, threshold, ok);
    EXPECT_EQ(online.status, TargetStatus::Online);
    EXPECT_TRUE(online.success);

    Observation redirect = answered(std::chrono::milliseconds{20});
    redirect.http_status = 301;
    EXPECT_EQ(classify(MonitorType::Http, threshold, redirect).status, TargetStatus::Online);

    Observation missing = answered(std::chrono::milliseconds{20});
    missing.http_status = 404;
    const auto degraded = classify(MonitorType::Http, threshold, missing);
    EXPECT_EQ(degraded.status, TargetStatus::Degraded);
    EXPECT_TRUE(degraded.success);
    EXPECT_EQ(degraded.error_message, "HTTP 404");

    Observation down = answered(std::chrono::milliseconds{400});
    down.http_status = 503;
    const auto both = classify(MonitorType::Http, threshold, down);
    EXPECT_EQ(both.status, TargetStatus::Degraded);
    EXPECT_EQ(both.error_message, "HTTP 503; response time exceeded latency threshold");

    Observation slow_ok = answered(std::chrono::milliseconds{400});
    slow_ok.http_status = 200;
    EXPECT_EQ(classify(MonitorType::Http, threshold, slow_ok).error_message, "response time exceeded latency threshold");

    Observation no_status = answered(std::chrono::milliseconds{10});
    const auto offline = classify(MonitorType::Http, threshold, no_status);
    EXPECT_EQ(offline.status, TargetStatus::Offline);
    EXPECT_FALSE(offline.success);
}
