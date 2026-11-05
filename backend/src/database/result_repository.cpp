#include "pulsenet/database/result_repository.hpp"

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
    "SELECT id, target_id, checked_at_ms, success, response_time_ms, http_status, error_message, status "
    "FROM results";

constexpr int kMaxLimit = 10000;

void bind_fields(Statement& statement, const MonitorResult& result) {
    statement.bind_int64(1, result.target_id);
    statement.bind_int64(2, to_unix_ms(result.checked_at));
    statement.bind_int64(3, result.success ? 1 : 0);
    if (result.response_time) {
        statement.bind_int64(4, result.response_time->count());
    } else {
        statement.bind_null(4);
    }
    if (result.http_status) {
        statement.bind_int64(5, *result.http_status);
    } else {
        statement.bind_null(5);
    }
    if (result.error_message) {
        statement.bind_text(6, *result.error_message);
    } else {
        statement.bind_null(6);
    }
    statement.bind_text(7, to_string(result.status));
}

MonitorResult read_result(Statement& statement) {
    MonitorResult result;
    result.id = statement.column_int64(0);
    result.target_id = statement.column_int64(1);
    result.checked_at = from_unix_ms(statement.column_int64(2));
    result.success = statement.column_int64(3) != 0;
    if (const auto response = statement.column_optional_int64(4)) {
        result.response_time = std::chrono::milliseconds{*response};
    }
    if (const auto http = statement.column_optional_int64(5)) {
        result.http_status = static_cast<int>(*http);
    }
    result.error_message = statement.column_optional_text(6);
    const auto status = target_status_from_string(statement.column_text(7));
    if (!status) {
        throw DatabaseError(SQLITE_CORRUPT, "stored target status is invalid");
    }
    result.status = *status;
    return result;
}

MonitorResult prepare_result(const MonitorResult& result) {
    MonitorResult stored = normalize(result);
    if (stored.checked_at == std::chrono::system_clock::time_point{}) {
        stored.checked_at = std::chrono::system_clock::now();
    }
    return stored;
}

}  // namespace

ResultRepository::ResultRepository(Database& database) : database_(database) {}

Result<std::int64_t> ResultRepository::insert(const MonitorResult& result) {
    const MonitorResult stored = prepare_result(result);
    if (const auto problem = validate(stored)) {
        return Result<std::int64_t>::failure(*problem);
    }

    try {
        std::int64_t id = 0;
        database_.with_connection([&](sqlite3* connection) {
            Statement statement(connection,
                                "INSERT INTO results("
                                "target_id, checked_at_ms, success, response_time_ms, http_status, "
                                "error_message, status) VALUES (?, ?, ?, ?, ?, ?, ?)");
            bind_fields(statement, stored);
            statement.execute();
            id = sqlite3_last_insert_rowid(connection);
        });
        return Result<std::int64_t>::success(id);
    } catch (const DatabaseError& ex) {
        return Result<std::int64_t>::failure(map_database_error(ex));
    }
}

Result<MonitorResult> ResultRepository::find_by_id(std::int64_t id) {
    if (id <= 0) {
        return Result<MonitorResult>::failure(Error{ErrorCode::Validation, "Result id is required"});
    }
    try {
        std::optional<MonitorResult> found;
        database_.with_connection([&](sqlite3* connection) {
            Statement statement(connection, std::string(kSelect) + " WHERE id = ?");
            statement.bind_int64(1, id);
            if (statement.step()) {
                found = read_result(statement);
            }
        });
        if (!found) {
            return Result<MonitorResult>::failure(Error{ErrorCode::NotFound, "Result not found"});
        }
        return Result<MonitorResult>::success(std::move(*found));
    } catch (const DatabaseError& ex) {
        return Result<MonitorResult>::failure(map_database_error(ex));
    }
}

Result<std::vector<MonitorResult>> ResultRepository::list_for_target(std::int64_t target_id, int limit) {
    if (target_id <= 0) {
        return Result<std::vector<MonitorResult>>::failure(
            Error{ErrorCode::Validation, "Target id is required"});
    }
    if (limit < 1 || limit > kMaxLimit) {
        return Result<std::vector<MonitorResult>>::failure(
            Error{ErrorCode::Validation, "Limit must be between 1 and 10000"});
    }

    try {
        std::vector<MonitorResult> rows;
        database_.with_connection([&](sqlite3* connection) {
            Statement statement(connection, std::string(kSelect) +
                                                " WHERE target_id = ? ORDER BY checked_at_ms DESC, id DESC LIMIT ?");
            statement.bind_int64(1, target_id);
            statement.bind_int64(2, limit);
            while (statement.step()) {
                rows.push_back(read_result(statement));
            }
        });
        return Result<std::vector<MonitorResult>>::success(std::move(rows));
    } catch (const DatabaseError& ex) {
        return Result<std::vector<MonitorResult>>::failure(map_database_error(ex));
    }
}

}  // namespace pulsenet
