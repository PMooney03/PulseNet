#include "pulsenet/error.hpp"
#include "pulsenet/models/validation.hpp"
#include "pulsenet/utilities/logger.hpp"
#include "pulsenet/utilities/time.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

namespace {

using pulsenet::ErrorCode;
using pulsenet::Incident;
using pulsenet::IncidentStatus;
using pulsenet::LogLevel;
using pulsenet::Logger;
using pulsenet::MonitorResult;
using pulsenet::MonitorTarget;
using pulsenet::MonitorType;
using pulsenet::Severity;
using pulsenet::TargetStatus;
using pulsenet::format_utc;
using pulsenet::from_unix_ms;
using pulsenet::incident_status_from_string;
using pulsenet::monitor_type_from_string;
using pulsenet::severity_from_string;
using pulsenet::target_status_from_string;
using pulsenet::to_string;
using pulsenet::to_unix_ms;
using pulsenet::validate;

MonitorTarget valid_tcp_target() {
    MonitorTarget target;
    target.name = "Google DNS";
    target.host = "8.8.8.8";
    target.port = 53;
    target.type = MonitorType::Tcp;
    target.interval = std::chrono::seconds{30};
    target.timeout = std::chrono::milliseconds{2000};
    target.latency_threshold = std::chrono::milliseconds{100};
    target.failure_threshold = 3;
    return target;
}

class LoggerGuard {
public:
    LoggerGuard() : previous_(Logger::instance().level()) {}
    ~LoggerGuard() {
        Logger::instance().set_sink(nullptr);
        Logger::instance().set_level(previous_);
    }

private:
    LogLevel previous_;
};

}  // namespace

TEST(ModelEnums, RoundTripCanonicalValues) {
    for (const auto type : {MonitorType::Tcp, MonitorType::Http, MonitorType::Https, MonitorType::Dns}) {
        const auto parsed = monitor_type_from_string(to_string(type));
        ASSERT_TRUE(parsed);
        EXPECT_EQ(*parsed, type);
    }
    for (const auto status :
         {TargetStatus::Online, TargetStatus::Degraded, TargetStatus::Offline, TargetStatus::Unknown}) {
        const auto parsed = target_status_from_string(to_string(status));
        ASSERT_TRUE(parsed);
        EXPECT_EQ(*parsed, status);
    }
    for (const auto severity : {Severity::Info, Severity::Warning, Severity::Critical}) {
        const auto parsed = severity_from_string(to_string(severity));
        ASSERT_TRUE(parsed);
        EXPECT_EQ(*parsed, severity);
    }
    for (const auto status : {IncidentStatus::Open, IncidentStatus::Resolved}) {
        const auto parsed = incident_status_from_string(to_string(status));
        ASSERT_TRUE(parsed);
        EXPECT_EQ(*parsed, status);
    }
}

TEST(ModelEnums, ParseIsCaseInsensitiveAndRejectsUnknownValues) {
    const auto https = monitor_type_from_string("HTTPS");
    ASSERT_TRUE(https);
    EXPECT_EQ(*https, MonitorType::Https);
    EXPECT_FALSE(monitor_type_from_string("icmp"));
    EXPECT_FALSE(target_status_from_string("up"));
    EXPECT_EQ(to_string(ErrorCode::NotFound), "not_found");
}

TEST(TimeTest, UnixMillisecondsRoundTripAndUtcFormat) {
    EXPECT_EQ(to_unix_ms(from_unix_ms(1704067200123)), 1704067200123);
    EXPECT_EQ(format_utc(from_unix_ms(0)), "1970-01-01T00:00:00.000Z");
    EXPECT_EQ(format_utc(from_unix_ms(1704067200000)), "2024-01-01T00:00:00.000Z");
    EXPECT_EQ(format_utc(from_unix_ms(1704067200123)), "2024-01-01T00:00:00.123Z");
}

TEST(ValidationTest, AcceptsTcpDnsAndTrimsStoredText) {
    EXPECT_FALSE(validate(valid_tcp_target()));

    MonitorTarget dns;
    dns.name = "  Resolver  ";
    dns.host = "  one.one.one.one  ";
    dns.type = MonitorType::Dns;
    dns.interval = std::chrono::seconds{60};
    dns.timeout = std::chrono::milliseconds{1500};
    dns.latency_threshold = std::chrono::milliseconds{50};
    dns.failure_threshold = 2;
    const auto normalized = pulsenet::normalize(dns);
    EXPECT_EQ(normalized.name, "Resolver");
    EXPECT_EQ(normalized.host, "one.one.one.one");
    EXPECT_FALSE(validate(normalized));
}

TEST(ValidationTest, RejectsIncompleteOrAmbiguousTargets) {
    auto missing_name = valid_tcp_target();
    missing_name.name = "   ";
    auto problem = validate(pulsenet::normalize(missing_name));
    ASSERT_TRUE(problem);
    EXPECT_EQ(problem->code, ErrorCode::Validation);

    auto missing_port = valid_tcp_target();
    missing_port.port.reset();
    problem = validate(missing_port);
    ASSERT_TRUE(problem);
    EXPECT_NE(problem->message.find("port"), std::string::npos);

    auto embedded = valid_tcp_target();
    embedded.host = "8.8.8.8:53";
    problem = validate(embedded);
    ASSERT_TRUE(problem);

    auto url = valid_tcp_target();
    url.host = "https://example.com";
    problem = validate(url);
    ASSERT_TRUE(problem);

    auto bad_port = valid_tcp_target();
    bad_port.port = 70000;
    problem = validate(bad_port);
    ASSERT_TRUE(problem);

    auto bad_interval = valid_tcp_target();
    bad_interval.interval = std::chrono::seconds{0};
    problem = validate(bad_interval);
    ASSERT_TRUE(problem);
}

TEST(ValidationTest, ChecksResultAndIncidentInvariants) {
    MonitorResult result;
    auto problem = validate(result);
    ASSERT_TRUE(problem);

    result.target_id = 1;
    result.http_status = 99;
    problem = validate(result);
    ASSERT_TRUE(problem);

    result.http_status = 204;
    result.response_time = std::chrono::milliseconds{14};
    EXPECT_FALSE(validate(result));

    Incident open;
    open.target_id = 1;
    open.reason = "connection refused";
    open.started_at = from_unix_ms(1704067200000);
    open.resolved_at = open.started_at;
    open.status = IncidentStatus::Open;
    problem = validate(open);
    ASSERT_TRUE(problem);

    open.resolved_at.reset();
    EXPECT_FALSE(validate(open));

    Incident resolved = open;
    resolved.status = IncidentStatus::Resolved;
    problem = validate(resolved);
    ASSERT_TRUE(problem);
    resolved.resolved_at = from_unix_ms(1704067205000);
    EXPECT_FALSE(validate(resolved));
}

TEST(LoggerTest, FiltersMessagesBelowTheConfiguredLevel) {
    LoggerGuard guard;
    std::vector<std::string> lines;
    Logger::instance().set_level(LogLevel::Info);
    Logger::instance().set_sink([&](LogLevel, std::string_view line) { lines.emplace_back(line); });

    pulsenet::log_debug("hidden");
    pulsenet::log_info("visible");
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_NE(lines[0].find("visible"), std::string::npos);
    EXPECT_NE(lines[0].find("INFO"), std::string::npos);

    Logger::instance().set_level(LogLevel::Debug);
    pulsenet::log_debug("shown");
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_NE(lines[1].find("DEBUG"), std::string::npos);
}
