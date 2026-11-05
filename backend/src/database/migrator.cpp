#include "pulsenet/database/migrator.hpp"

#include "database/statement.hpp"
#include "pulsenet/database/database.hpp"
#include "pulsenet/utilities/logger.hpp"
#include "pulsenet/utilities/time.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <string_view>

namespace pulsenet {
namespace {

constexpr std::string_view kBookkeeping = R"SQL(
CREATE TABLE IF NOT EXISTS schema_migrations (
    version INTEGER PRIMARY KEY,
    applied_at_ms INTEGER NOT NULL
);
)SQL";

constexpr std::string_view kVersion1 = R"SQL(
CREATE TABLE targets (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    host TEXT NOT NULL,
    port INTEGER,
    monitor_type TEXT NOT NULL,
    interval_seconds INTEGER NOT NULL,
    timeout_ms INTEGER NOT NULL,
    enabled INTEGER NOT NULL CHECK (enabled IN (0, 1)),
    latency_threshold_ms INTEGER NOT NULL,
    failure_threshold INTEGER NOT NULL,
    created_at_ms INTEGER NOT NULL
);

CREATE TABLE results (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    target_id INTEGER NOT NULL REFERENCES targets(id) ON DELETE CASCADE,
    checked_at_ms INTEGER NOT NULL,
    success INTEGER NOT NULL CHECK (success IN (0, 1)),
    response_time_ms INTEGER,
    http_status INTEGER,
    error_message TEXT,
    status TEXT NOT NULL
);

CREATE INDEX idx_results_target_checked ON results(target_id, checked_at_ms DESC, id DESC);

CREATE TABLE incidents (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    target_id INTEGER NOT NULL REFERENCES targets(id) ON DELETE CASCADE,
    severity TEXT NOT NULL,
    reason TEXT NOT NULL,
    started_at_ms INTEGER NOT NULL,
    resolved_at_ms INTEGER,
    status TEXT NOT NULL
);

CREATE INDEX idx_incidents_target ON incidents(target_id, started_at_ms DESC);
CREATE INDEX idx_incidents_open ON incidents(status, started_at_ms DESC);
)SQL";

void exec_raw(sqlite3* database, std::string_view sql) {
    char* raw_error = nullptr;
    const std::string text{sql};
    const int rc = sqlite3_exec(database, text.c_str(), nullptr, nullptr, &raw_error);
    std::unique_ptr<char, decltype(&sqlite3_free)> error{raw_error, &sqlite3_free};
    if (rc != SQLITE_OK) {
        const char* message = error ? error.get() : sqlite3_errmsg(database);
        throw DatabaseError(sqlite3_extended_errcode(database), message);
    }
}

}  // namespace

Migrator::Migrator(Database& database) : database_(database) {}

void Migrator::apply() {
    database_.execute(kBookkeeping);
    const int current = database_.schema_version();
    if (current >= 1) {
        log_info("schema ready version=" + std::to_string(current));
        return;
    }

    // DDL is transactional in SQLite. If version 1 fails, the new tables and
    // the version row roll back together and the next start tries again.
    database_.transaction([&](sqlite3* connection) {
        exec_raw(connection, kVersion1);
        Statement statement(connection,
                            "INSERT INTO schema_migrations(version, applied_at_ms) VALUES (1, ?)");
        statement.bind_int64(1, to_unix_ms(std::chrono::system_clock::now()));
        statement.execute();
    });
    log_info("applied schema migration version=1");
}

}  // namespace pulsenet
