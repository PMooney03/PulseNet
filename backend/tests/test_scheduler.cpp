#include "support.hpp"

#include "pulsenet/database/database.hpp"
#include "pulsenet/database/incident_repository.hpp"
#include "pulsenet/database/migrator.hpp"
#include "pulsenet/database/result_repository.hpp"
#include "pulsenet/database/target_repository.hpp"
#include "pulsenet/monitoring/check_service.hpp"
#include "pulsenet/monitoring/probe_engine.hpp"
#include "pulsenet/monitoring/scheduler.hpp"
#include "pulsenet/utilities/logger.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace {

using pulsenet::CheckService;
using pulsenet::Database;
using pulsenet::IncidentRepository;
using pulsenet::IncidentStatus;
using pulsenet::LogLevel;
using pulsenet::Logger;
using pulsenet::Migrator;
using pulsenet::MonitorTarget;
using pulsenet::MonitorType;
using pulsenet::ProbeEngine;
using pulsenet::ResultRepository;
using pulsenet::Scheduler;
using pulsenet::Severity;
using pulsenet::TargetRepository;
using pulsenet::TargetStatus;
using pulsenet::due_target_ids;
using pulsenet::test_support::make_temp_directory;

MonitorTarget make_target(const std::string& name, const MonitorType type, const int port) {
    MonitorTarget target;
    target.name = name;
    target.host = "127.0.0.1";
    if (port > 0) {
        target.port = port;
    }
    target.type = type;
    target.interval = std::chrono::seconds{30};
    target.timeout = std::chrono::milliseconds{400};
    target.enabled = true;
    target.latency_threshold = std::chrono::milliseconds{1000};
    target.failure_threshold = 2;
    return target;
}

}  // namespace

TEST(DueSelection, SkipsDisabledInFlightAndFutureTargets) {
    MonitorTarget due = make_target("due", MonitorType::Tcp, 80);
    due.id = 1;
    MonitorTarget later = make_target("later", MonitorType::Tcp, 80);
    later.id = 2;
    MonitorTarget busy = make_target("busy", MonitorType::Tcp, 80);
    busy.id = 3;
    MonitorTarget disabled = make_target("off", MonitorType::Tcp, 80);
    disabled.id = 4;
    disabled.enabled = false;

    const auto now = std::chrono::system_clock::time_point{std::chrono::seconds{100}};
    std::unordered_map<std::int64_t, std::chrono::system_clock::time_point> next_due{
        {2, now + std::chrono::seconds{10}},
    };
    std::unordered_set<std::int64_t> in_flight{3};
    const auto selected = due_target_ids({due, later, busy, disabled}, next_due, in_flight, now);
    ASSERT_EQ(selected.size(), 1u);
    EXPECT_EQ(selected.front(), 1);
}

TEST(SchedulerTest, OpensACriticalIncidentAfterRepeatedFailures) {
    Logger::instance().set_level(LogLevel::Error);
    const auto directory = make_temp_directory();
    Database database(directory / "pulsenet.db");
    Migrator{database}.apply();
    TargetRepository targets(database);
    auto draft = make_target("closed", MonitorType::Tcp, 1);
    const auto id = targets.insert(draft);
    ASSERT_TRUE(id);
    const auto stored = targets.find_by_id(id.value());
    ASSERT_TRUE(stored);

    ResultRepository results(database);
    IncidentRepository incidents(database);
    ProbeEngine engine;
    CheckService checks(results, engine);
    Scheduler scheduler(targets, results, incidents, checks);
    const auto now = std::chrono::system_clock::now();
    const auto first = scheduler.check_now(stored.value(), now);
    ASSERT_TRUE(first) << first.error().message;
    ASSERT_EQ(first.value().results.size(), 1u);
    EXPECT_EQ(first.value().results.front().status, TargetStatus::Offline);
    EXPECT_TRUE(first.value().opened.empty());

    const auto second = scheduler.check_now(stored.value(), now + std::chrono::seconds{1});
    ASSERT_TRUE(second);
    ASSERT_EQ(second.value().opened.size(), 1u);
    EXPECT_EQ(second.value().opened.front().severity, Severity::Critical);

    const auto open = incidents.list_open();
    ASSERT_TRUE(open);
    ASSERT_EQ(open.value().size(), 1u);
    EXPECT_EQ(open.value().front().status, IncidentStatus::Open);
    std::filesystem::remove_all(directory);
}

TEST(SchedulerTest, OneInFlightCheckWhenSeveralThreadsAskAtOnce) {
    Logger::instance().set_level(LogLevel::Error);
    const auto directory = make_temp_directory();
    Database database(directory / "pulsenet.db");
    Migrator{database}.apply();
    TargetRepository targets(database);
    auto draft = make_target("dns", MonitorType::Dns, 0);
    draft.port.reset();
    draft.host = "localhost";
    draft.timeout = std::chrono::milliseconds{2000};
    const auto id = targets.insert(draft);
    ASSERT_TRUE(id);
    const auto stored = targets.find_by_id(id.value());
    ASSERT_TRUE(stored);

    ResultRepository results(database);
    IncidentRepository incidents(database);
    ProbeEngine engine;
    CheckService checks(results, engine);
    Scheduler scheduler(targets, results, incidents, checks);

    std::atomic<bool> release{false};
    std::atomic<int> entered{0};
    scheduler.set_before_probe([&] {
        entered.fetch_add(1);
        while (!release.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
    });

    std::atomic<int> wins{0};
    const auto now = std::chrono::system_clock::now();
    std::thread first([&] {
        const auto report = scheduler.check_now(stored.value(), now);
        if (report && !report.value().results.empty()) {
            wins.fetch_add(1);
        }
    });
    while (entered.load() == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    const auto skipped = scheduler.check_now(stored.value(), now);
    ASSERT_TRUE(skipped);
    EXPECT_TRUE(skipped.value().results.empty());
    release.store(true);
    first.join();
    EXPECT_EQ(wins.load(), 1);
    const auto history = results.list_for_target(id.value());
    ASSERT_TRUE(history);
    EXPECT_EQ(history.value().size(), 1u);
    std::filesystem::remove_all(directory);
}

TEST(SchedulerTest, StopFlagEndsTheLoop) {
    Logger::instance().set_level(LogLevel::Error);
    const auto directory = make_temp_directory();
    Database database(directory / "pulsenet.db");
    Migrator{database}.apply();
    TargetRepository targets(database);
    ResultRepository results(database);
    IncidentRepository incidents(database);
    ProbeEngine engine;
    CheckService checks(results, engine);
    Scheduler scheduler(targets, results, incidents, checks);
    std::atomic<bool> stop{true};
    const auto started = std::chrono::steady_clock::now();
    scheduler.run(stop);
    EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds{2});
    std::filesystem::remove_all(directory);
}
