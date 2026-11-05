#include "pulsenet/database/incident_repository.hpp"

#include "database/statement.hpp"
#include "pulsenet/database/database.hpp"
#include "pulsenet/models/validation.hpp"
#include "pulsenet/utilities/time.hpp"

#include <chrono>
#include <string>
#include <utility>

namespace pulsenet {
namespace {

constexpr const char* kSelect =
    "SELECT id, target_id, severity, reason, started_at_ms, resolved_at_ms, status FROM incidents";

constexpr int kMaxLimit = 10000;

void bind_fields(Statement& statement, const Incident& incident) {
    statement.bind_int64(1, incident.target_id);
    statement.bind_text(2, to_string(incident.severity));
    statement.bind_text(3, incident.reason);
    statement.bind_int64(4, to_unix_ms(incident.started_at));
    if (incident.resolved_at) {
        statement.bind_int64(5, to_unix_ms(*incident.resolved_at));
    } else {
        statement.bind_null(5);
    }
    statement.bind_text(6, to_string(incident.status));
}

Incident read_incident(Statement& statement) {
    Incident incident;
    incident.id = statement.column_int64(0);
    incident.target_id = statement.column_int64(1);
    const auto severity = severity_from_string(statement.column_text(2));
    if (!severity) {
        throw DatabaseError(SQLITE_CORRUPT, "stored severity is invalid");
    }
    incident.severity = *severity;
    incident.reason = statement.column_text(3);
    incident.started_at = from_unix_ms(statement.column_int64(4));
    if (const auto resolved = statement.column_optional_int64(5)) {
        incident.resolved_at = from_unix_ms(*resolved);
    }
    const auto status = incident_status_from_string(statement.column_text(6));
    if (!status) {
        throw DatabaseError(SQLITE_CORRUPT, "stored incident status is invalid");
    }
    incident.status = *status;
    return incident;
}

Incident prepare_incident(const Incident& incident) {
    Incident stored = normalize(incident);
    if (stored.started_at == std::chrono::system_clock::time_point{}) {
        stored.started_at = std::chrono::system_clock::now();
    }
    return stored;
}

Result<std::vector<Incident>> list_with(Database& database, const std::string& sql, std::int64_t bind_a,
                                        std::optional<std::int64_t> bind_b) {
    try {
        std::vector<Incident> rows;
        database.with_connection([&](sqlite3* connection) {
            Statement statement(connection, sql);
            statement.bind_int64(1, bind_a);
            if (bind_b) {
                statement.bind_int64(2, *bind_b);
            }
            while (statement.step()) {
                rows.push_back(read_incident(statement));
            }
        });
        return Result<std::vector<Incident>>::success(std::move(rows));
    } catch (const DatabaseError& ex) {
        return Result<std::vector<Incident>>::failure(map_database_error(ex));
    }
}

}  // namespace

IncidentRepository::IncidentRepository(Database& database) : database_(database) {}

Result<std::int64_t> IncidentRepository::insert(const Incident& incident) {
    const Incident stored = prepare_incident(incident);
    if (const auto problem = validate(stored)) {
        return Result<std::int64_t>::failure(*problem);
    }

    try {
        std::int64_t id = 0;
        database_.with_connection([&](sqlite3* connection) {
            Statement statement(connection,
                                "INSERT INTO incidents("
                                "target_id, severity, reason, started_at_ms, resolved_at_ms, status) "
                                "VALUES (?, ?, ?, ?, ?, ?)");
            bind_fields(statement, stored);
            statement.execute();
            id = sqlite3_last_insert_rowid(connection);
        });
        return Result<std::int64_t>::success(id);
    } catch (const DatabaseError& ex) {
        return Result<std::int64_t>::failure(map_database_error(ex));
    }
}

Result<Incident> IncidentRepository::find_by_id(std::int64_t id) {
    if (id <= 0) {
        return Result<Incident>::failure(Error{ErrorCode::Validation, "Incident id is required"});
    }
    try {
        std::optional<Incident> found;
        database_.with_connection([&](sqlite3* connection) {
            Statement statement(connection, std::string(kSelect) + " WHERE id = ?");
            statement.bind_int64(1, id);
            if (statement.step()) {
                found = read_incident(statement);
            }
        });
        if (!found) {
            return Result<Incident>::failure(Error{ErrorCode::NotFound, "Incident not found"});
        }
        return Result<Incident>::success(std::move(*found));
    } catch (const DatabaseError& ex) {
        return Result<Incident>::failure(map_database_error(ex));
    }
}

Result<std::vector<Incident>> IncidentRepository::list_open(int limit) {
    if (limit < 1 || limit > kMaxLimit) {
        return Result<std::vector<Incident>>::failure(
            Error{ErrorCode::Validation, "Limit must be between 1 and 10000"});
    }
    return list_with(database_, std::string(kSelect) + " WHERE status = 'open' ORDER BY started_at_ms DESC, id DESC LIMIT ?",
                     limit, std::nullopt);
}

Result<std::vector<Incident>> IncidentRepository::list_for_target(std::int64_t target_id, int limit) {
    if (target_id <= 0) {
        return Result<std::vector<Incident>>::failure(Error{ErrorCode::Validation, "Target id is required"});
    }
    if (limit < 1 || limit > kMaxLimit) {
        return Result<std::vector<Incident>>::failure(
            Error{ErrorCode::Validation, "Limit must be between 1 and 10000"});
    }
    return list_with(database_,
                     std::string(kSelect) + " WHERE target_id = ? ORDER BY started_at_ms DESC, id DESC LIMIT ?",
                     target_id, limit);
}

Status IncidentRepository::resolve(std::int64_t id, std::chrono::system_clock::time_point resolved_at) {
    if (id <= 0) {
        return Status::failure(Error{ErrorCode::Validation, "Incident id is required"});
    }

    try {
        bool found = false;
        std::optional<Error> problem;
        database_.with_connection([&](sqlite3* connection) {
            Statement read(connection, "SELECT status, started_at_ms FROM incidents WHERE id = ?");
            read.bind_int64(1, id);
            if (!read.step()) {
                return;
            }
            found = true;
            const auto status = incident_status_from_string(read.column_text(0));
            if (!status) {
                throw DatabaseError(SQLITE_CORRUPT, "stored incident status is invalid");
            }
            if (*status == IncidentStatus::Resolved) {
                return;
            }
            const auto started_at = from_unix_ms(read.column_int64(1));
            if (resolved_at < started_at) {
                problem = Error{ErrorCode::Validation, "Resolved time is earlier than the start time"};
                return;
            }
            Statement update(connection, "UPDATE incidents SET status = 'resolved', resolved_at_ms = ? WHERE id = ?");
            update.bind_int64(1, to_unix_ms(resolved_at));
            update.bind_int64(2, id);
            update.execute();
        });
        if (!found) {
            return Status::failure(Error{ErrorCode::NotFound, "Incident not found"});
        }
        if (problem) {
            return Status::failure(*problem);
        }
        return Status::success();
    } catch (const DatabaseError& ex) {
        return Status::failure(map_database_error(ex));
    }
}

}  // namespace pulsenet
