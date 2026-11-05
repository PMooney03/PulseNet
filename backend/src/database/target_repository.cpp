#include "pulsenet/database/target_repository.hpp"

#include "database/statement.hpp"
#include "pulsenet/database/database.hpp"
#include "pulsenet/models/validation.hpp"
#include "pulsenet/utilities/time.hpp"

#include <chrono>
#include <utility>

namespace pulsenet {
namespace {

constexpr const char* kInsert =
    "INSERT INTO targets("
    "name, host, port, monitor_type, interval_seconds, timeout_ms, enabled, "
    "latency_threshold_ms, failure_threshold, created_at_ms) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

constexpr const char* kSelect =
    "SELECT id, name, host, port, monitor_type, interval_seconds, timeout_ms, enabled, "
    "latency_threshold_ms, failure_threshold, created_at_ms FROM targets";

void bind_fields(Statement& statement, const MonitorTarget& target) {
    statement.bind_text(1, target.name);
    statement.bind_text(2, target.host);
    if (target.port) {
        statement.bind_int64(3, *target.port);
    } else {
        statement.bind_null(3);
    }
    statement.bind_text(4, to_string(target.type));
    statement.bind_int64(5, target.interval.count());
    statement.bind_int64(6, target.timeout.count());
    statement.bind_int64(7, target.enabled ? 1 : 0);
    statement.bind_int64(8, target.latency_threshold.count());
    statement.bind_int64(9, target.failure_threshold);
}

MonitorTarget read_target(Statement& statement) {
    MonitorTarget target;
    target.id = statement.column_int64(0);
    target.name = statement.column_text(1);
    target.host = statement.column_text(2);
    if (const auto port = statement.column_optional_int64(3)) {
        if (*port < 1 || *port > 65535) {
            throw DatabaseError(SQLITE_CORRUPT, "stored port is invalid");
        }
        target.port = static_cast<int>(*port);
    }
    const auto type = monitor_type_from_string(statement.column_text(4));
    if (!type) {
        throw DatabaseError(SQLITE_CORRUPT, "stored monitor type is invalid");
    }
    target.type = *type;
    target.interval = std::chrono::seconds{statement.column_int64(5)};
    target.timeout = std::chrono::milliseconds{statement.column_int64(6)};
    target.enabled = statement.column_int64(7) != 0;
    target.latency_threshold = std::chrono::milliseconds{statement.column_int64(8)};
    target.failure_threshold = static_cast<int>(statement.column_int64(9));
    target.created_at = from_unix_ms(statement.column_int64(10));
    return target;
}

MonitorTarget prepare_target(const MonitorTarget& target, bool assign_timestamp) {
    MonitorTarget stored = normalize(target);
    if (assign_timestamp && stored.created_at == std::chrono::system_clock::time_point{}) {
        stored.created_at = std::chrono::system_clock::now();
    }
    return stored;
}

}  // namespace

TargetRepository::TargetRepository(Database& database) : database_(database) {}

Result<std::int64_t> TargetRepository::insert(const MonitorTarget& target) {
    const MonitorTarget stored = prepare_target(target, true);
    if (const auto problem = validate(stored)) {
        return Result<std::int64_t>::failure(*problem);
    }

    try {
        std::int64_t id = 0;
        database_.with_connection([&](sqlite3* connection) {
            Statement statement(connection, kInsert);
            bind_fields(statement, stored);
            statement.bind_int64(10, to_unix_ms(stored.created_at));
            statement.execute();
            id = sqlite3_last_insert_rowid(connection);
        });
        return Result<std::int64_t>::success(id);
    } catch (const DatabaseError& ex) {
        return Result<std::int64_t>::failure(map_database_error(ex));
    }
}

Result<MonitorTarget> TargetRepository::find_by_id(std::int64_t id) {
    if (id <= 0) {
        return Result<MonitorTarget>::failure(Error{ErrorCode::Validation, "Target id is required"});
    }
    try {
        std::optional<MonitorTarget> found;
        database_.with_connection([&](sqlite3* connection) {
            Statement statement(connection, std::string(kSelect) + " WHERE id = ?");
            statement.bind_int64(1, id);
            if (statement.step()) {
                found = read_target(statement);
            }
        });
        if (!found) {
            return Result<MonitorTarget>::failure(Error{ErrorCode::NotFound, "Target not found"});
        }
        return Result<MonitorTarget>::success(std::move(*found));
    } catch (const DatabaseError& ex) {
        return Result<MonitorTarget>::failure(map_database_error(ex));
    }
}

Result<std::vector<MonitorTarget>> TargetRepository::list_all() {
    try {
        std::vector<MonitorTarget> rows;
        database_.with_connection([&](sqlite3* connection) {
            Statement statement(connection, std::string(kSelect) + " ORDER BY id ASC");
            while (statement.step()) {
                rows.push_back(read_target(statement));
            }
        });
        return Result<std::vector<MonitorTarget>>::success(std::move(rows));
    } catch (const DatabaseError& ex) {
        return Result<std::vector<MonitorTarget>>::failure(map_database_error(ex));
    }
}

Status TargetRepository::update(const MonitorTarget& target) {
    MonitorTarget stored = prepare_target(target, false);
    if (stored.id <= 0) {
        return Status::failure(Error{ErrorCode::Validation, "Target id is required"});
    }
    if (const auto problem = validate(stored)) {
        return Status::failure(*problem);
    }

    try {
        int changed = 0;
        database_.with_connection([&](sqlite3* connection) {
            Statement statement(connection,
                                "UPDATE targets SET name = ?, host = ?, port = ?, monitor_type = ?, "
                                "interval_seconds = ?, timeout_ms = ?, enabled = ?, latency_threshold_ms = ?, "
                                "failure_threshold = ? WHERE id = ?");
            bind_fields(statement, stored);
            statement.bind_int64(10, stored.id);
            statement.execute();
            changed = sqlite3_changes(connection);
        });
        if (changed == 0) {
            return Status::failure(Error{ErrorCode::NotFound, "Target not found"});
        }
        return Status::success();
    } catch (const DatabaseError& ex) {
        return Status::failure(map_database_error(ex));
    }
}

Status TargetRepository::remove(std::int64_t id) {
    if (id <= 0) {
        return Status::failure(Error{ErrorCode::Validation, "Target id is required"});
    }
    try {
        int changed = 0;
        database_.with_connection([&](sqlite3* connection) {
            Statement statement(connection, "DELETE FROM targets WHERE id = ?");
            statement.bind_int64(1, id);
            statement.execute();
            changed = sqlite3_changes(connection);
        });
        if (changed == 0) {
            return Status::failure(Error{ErrorCode::NotFound, "Target not found"});
        }
        return Status::success();
    } catch (const DatabaseError& ex) {
        return Status::failure(map_database_error(ex));
    }
}

}  // namespace pulsenet
