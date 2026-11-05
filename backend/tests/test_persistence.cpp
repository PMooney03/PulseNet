#include "support.hpp"

#include "pulsenet/database/database.hpp"
#include "pulsenet/database/incident_repository.hpp"
#include "pulsenet/database/migrator.hpp"
#include "pulsenet/database/result_repository.hpp"
#include "pulsenet/database/target_repository.hpp"
#include "pulsenet/utilities/logger.hpp"
#include "pulsenet/utilities/time.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using pulsenet::Database;
using pulsenet::DatabaseError;
using pulsenet::ErrorCode;
using pulsenet::Incident;
using pulsenet::IncidentRepository;
using pulsenet::IncidentStatus;
using pulsenet::LogLevel;
using pulsenet::Logger;
using pulsenet::Migrator;
using pulsenet::MonitorResult;
using pulsenet::MonitorTarget;
using pulsenet::MonitorType;
using pulsenet::ResultRepository;
using pulsenet::Severity;
using pulsenet::TargetRepository;
using pulsenet::TargetStatus;
using pulsenet::from_unix_ms;
using pulsenet::test_support::make_temp_directory;
using pulsenet::to_unix_ms;

class TempDirTest : public ::testing::Test {
protected:
    void SetUp() override {
        previous_level_ = Logger::instance().level();
        Logger::instance().set_level(LogLevel::Error);
        directory_ = make_temp_directory();
    }

    void TearDown() override {
        std::filesystem::remove_all(directory_);
        Logger::instance().set_level(previous_level_);
    }

    std::filesystem::path directory_;
    LogLevel previous_level_{LogLevel::Info};
};

class PersistenceTest : public TempDirTest {
protected:
    void SetUp() override {
        TempDirTest::SetUp();
        database_ = std::make_unique<Database>(directory_ / "pulsenet.db");
        Migrator{*database_}.apply();
    }

    void TearDown() override {
        database_.reset();
        TempDirTest::TearDown();
    }

    MonitorTarget sample_target() const {
        MonitorTarget target;
        target.name = "Google DNS";
        target.host = "8.8.8.8";
        target.port = 53;
        target.type = MonitorType::Tcp;
        target.interval = std::chrono::seconds{30};
        target.timeout = std::chrono::milliseconds{2000};
        target.enabled = false;
        target.latency_threshold = std::chrono::milliseconds{100};
        target.failure_threshold = 3;
        target.created_at = from_unix_ms(1700000000000);
        return target;
    }

    std::unique_ptr<Database> database_;
};

}  // namespace

TEST_F(TempDirTest, MigrationIsIdempotent) {
    Database database(directory_ / "fresh.db");
    EXPECT_THROW(static_cast<void>(database.schema_version()), DatabaseError);
    Migrator{database}.apply();
    EXPECT_EQ(database.schema_version(), 1);
    Migrator{database}.apply();
    EXPECT_EQ(database.schema_version(), 1);
}

TEST_F(PersistenceTest, StoresAndUpdatesTargets) {
    TargetRepository targets(*database_);
    auto draft = sample_target();
    draft.name = "  Google DNS  ";
    draft.id = 42;
    const auto inserted = targets.insert(draft);
    ASSERT_TRUE(inserted) << inserted.error().message;
    EXPECT_EQ(inserted.value(), 1);

    const auto loaded = targets.find_by_id(inserted.value());
    ASSERT_TRUE(loaded) << loaded.error().message;
    EXPECT_EQ(loaded.value().name, "Google DNS");
    EXPECT_EQ(loaded.value().host, "8.8.8.8");
    ASSERT_TRUE(loaded.value().port);
    EXPECT_EQ(*loaded.value().port, 53);
    EXPECT_EQ(loaded.value().type, MonitorType::Tcp);
    EXPECT_EQ(loaded.value().interval, std::chrono::seconds{30});
    EXPECT_EQ(loaded.value().timeout, std::chrono::milliseconds{2000});
    EXPECT_FALSE(loaded.value().enabled);
    EXPECT_EQ(loaded.value().latency_threshold, std::chrono::milliseconds{100});
    EXPECT_EQ(loaded.value().failure_threshold, 3);
    EXPECT_EQ(to_unix_ms(loaded.value().created_at), 1700000000000);

    auto updated = loaded.value();
    updated.name = "Home Resolver";
    updated.host = "1.1.1.1";
    updated.port.reset();
    updated.type = MonitorType::Dns;
    updated.enabled = true;
    updated.created_at = from_unix_ms(1);
    const auto saved = targets.update(updated);
    ASSERT_TRUE(saved) << saved.error().message;

    const auto reread = targets.find_by_id(inserted.value());
    ASSERT_TRUE(reread);
    EXPECT_EQ(reread.value().name, "Home Resolver");
    EXPECT_EQ(reread.value().type, MonitorType::Dns);
    EXPECT_FALSE(reread.value().port.has_value());
    EXPECT_TRUE(reread.value().enabled);
    EXPECT_EQ(to_unix_ms(reread.value().created_at), 1700000000000);

    const auto listed = targets.list_all();
    ASSERT_TRUE(listed);
    ASSERT_EQ(listed.value().size(), 1u);
}

TEST_F(PersistenceTest, RejectsInvalidTargetsAndMissingRows) {
    TargetRepository targets(*database_);
    MonitorTarget broken;
    broken.name = "Router";
    broken.host = "192.168.1.1";
    broken.type = MonitorType::Tcp;
    const auto rejected = targets.insert(broken);
    EXPECT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, ErrorCode::Validation);

    EXPECT_EQ(targets.find_by_id(5).error().code, ErrorCode::NotFound);
    EXPECT_EQ(targets.remove(5).error().code, ErrorCode::NotFound);
    auto missing = sample_target();
    missing.id = 9;
    EXPECT_EQ(targets.update(missing).error().code, ErrorCode::NotFound);

    const auto listed = targets.list_all();
    ASSERT_TRUE(listed);
    EXPECT_TRUE(listed.value().empty());
}

TEST_F(PersistenceTest, ResultOrderForeignKeysAndIncidentResolution) {
    TargetRepository targets(*database_);
    const auto target_id = targets.insert(sample_target());
    ASSERT_TRUE(target_id);

    ResultRepository results(*database_);
    MonitorResult older;
    older.target_id = target_id.value();
    older.checked_at = from_unix_ms(1700000001000);
    older.success = false;
    older.status = TargetStatus::Offline;
    older.error_message = "  connection refused  ";
    const auto older_id = results.insert(older);
    ASSERT_TRUE(older_id) << older_id.error().message;
    const auto loaded_older = results.find_by_id(older_id.value());
    ASSERT_TRUE(loaded_older);
    ASSERT_TRUE(loaded_older.value().error_message);
    EXPECT_EQ(*loaded_older.value().error_message, "connection refused");
    EXPECT_FALSE(loaded_older.value().http_status.has_value());
    EXPECT_FALSE(loaded_older.value().response_time.has_value());

    MonitorResult newer;
    newer.target_id = target_id.value();
    newer.checked_at = from_unix_ms(1700000005000);
    newer.success = true;
    newer.status = TargetStatus::Online;
    newer.response_time = std::chrono::milliseconds{14};
    newer.http_status = 204;
    const auto newer_id = results.insert(newer);
    ASSERT_TRUE(newer_id) << newer_id.error().message;
    const auto loaded_newer = results.find_by_id(newer_id.value());
    ASSERT_TRUE(loaded_newer);
    ASSERT_TRUE(loaded_newer.value().http_status);
    EXPECT_EQ(*loaded_newer.value().http_status, 204);
    ASSERT_TRUE(loaded_newer.value().response_time);
    EXPECT_EQ(*loaded_newer.value().response_time, std::chrono::milliseconds{14});
    EXPECT_FALSE(loaded_newer.value().error_message.has_value());

    const auto history = results.list_for_target(target_id.value(), 1);
    ASSERT_TRUE(history);
    ASSERT_EQ(history.value().size(), 1u);
    EXPECT_EQ(history.value().front().id, newer_id.value());

    MonitorResult orphan;
    orphan.target_id = 99999;
    orphan.success = false;
    orphan.status = TargetStatus::Offline;
    const auto rejected = results.insert(orphan);
    EXPECT_FALSE(rejected);
    EXPECT_EQ(rejected.error().code, ErrorCode::Constraint);

    IncidentRepository incidents(*database_);
    Incident incident;
    incident.target_id = target_id.value();
    incident.severity = Severity::Critical;
    incident.reason = "  PostgreSQL became unavailable  ";
    incident.started_at = from_unix_ms(1700000001000);
    incident.status = IncidentStatus::Open;
    const auto incident_id = incidents.insert(incident);
    ASSERT_TRUE(incident_id) << incident_id.error().message;

    const auto open = incidents.list_open();
    ASSERT_TRUE(open);
    ASSERT_EQ(open.value().size(), 1u);
    EXPECT_EQ(open.value().front().reason, "PostgreSQL became unavailable");
    EXPECT_EQ(open.value().front().severity, Severity::Critical);
    EXPECT_FALSE(open.value().front().resolved_at.has_value());

    EXPECT_EQ(incidents.resolve(incident_id.value(), from_unix_ms(1700000000000)).error().code,
              ErrorCode::Validation);
    ASSERT_TRUE(incidents.resolve(incident_id.value(), from_unix_ms(1700000009000)));
    ASSERT_TRUE(incidents.resolve(incident_id.value(), from_unix_ms(1700000010000)));

    const auto resolved = incidents.find_by_id(incident_id.value());
    ASSERT_TRUE(resolved);
    EXPECT_EQ(resolved.value().status, IncidentStatus::Resolved);
    ASSERT_TRUE(resolved.value().resolved_at);
    EXPECT_EQ(to_unix_ms(*resolved.value().resolved_at), 1700000009000);
    const auto still_open = incidents.list_open();
    ASSERT_TRUE(still_open);
    EXPECT_TRUE(still_open.value().empty());

    ASSERT_TRUE(targets.remove(target_id.value()));
    EXPECT_EQ(results.find_by_id(newer_id.value()).error().code, ErrorCode::NotFound);
    EXPECT_EQ(incidents.find_by_id(incident_id.value()).error().code, ErrorCode::NotFound);
    EXPECT_EQ(targets.find_by_id(target_id.value()).error().code, ErrorCode::NotFound);
}

TEST_F(PersistenceTest, RowsSurviveReopen) {
    const auto target_id = TargetRepository(*database_).insert(sample_target());
    ASSERT_TRUE(target_id);
    const auto path = database_->path();
    database_.reset();

    database_ = std::make_unique<Database>(path);
    Migrator{*database_}.apply();
    const auto loaded = TargetRepository(*database_).find_by_id(target_id.value());
    ASSERT_TRUE(loaded) << loaded.error().message;
    EXPECT_EQ(loaded.value().host, "8.8.8.8");
    EXPECT_EQ(database_->schema_version(), 1);
}

TEST_F(PersistenceTest, ConcurrentInsertsKeepEveryRow) {
    constexpr int kThreads = 4;
    constexpr int kEach = 25;
    std::mutex error_mutex;
    std::vector<std::string> errors;
    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int thread_index = 0; thread_index < kThreads; ++thread_index) {
        threads.emplace_back([&, thread_index] {
            TargetRepository targets(*database_);
            for (int row = 0; row < kEach; ++row) {
                auto target = sample_target();
                target.name = "t" + std::to_string(thread_index) + "-" + std::to_string(row);
                target.host = "10.0.0." + std::to_string(thread_index + 1);
                const auto inserted = targets.insert(target);
                if (!inserted) {
                    std::lock_guard<std::mutex> lock(error_mutex);
                    errors.push_back(inserted.error().message);
                }
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_TRUE(errors.empty());
    const auto listed = TargetRepository(*database_).list_all();
    ASSERT_TRUE(listed) << listed.error().message;
    EXPECT_EQ(listed.value().size(), static_cast<std::size_t>(kThreads * kEach));
}
