#include "database/statement.hpp"

namespace pulsenet {

Statement::Statement(sqlite3* database, std::string_view sql) {
    const int rc = sqlite3_prepare_v2(database, sql.data(), static_cast<int>(sql.size()), &statement_, nullptr);
    if (rc != SQLITE_OK) {
        throw DatabaseError(sqlite3_extended_errcode(database), sqlite3_errmsg(database));
    }
}

Statement::~Statement() { sqlite3_finalize(statement_); }

void Statement::check(int rc) const {
    if (rc != SQLITE_OK) {
        sqlite3* database = sqlite3_db_handle(statement_);
        throw DatabaseError(sqlite3_extended_errcode(database), sqlite3_errmsg(database));
    }
}

void Statement::bind_int64(int index, std::int64_t value) { check(sqlite3_bind_int64(statement_, index, value)); }

void Statement::bind_text(int index, std::string_view value) {
    check(sqlite3_bind_text(statement_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT));
}

void Statement::bind_null(int index) { check(sqlite3_bind_null(statement_, index)); }

bool Statement::step() {
    const int rc = sqlite3_step(statement_);
    if (rc == SQLITE_ROW) {
        return true;
    }
    if (rc == SQLITE_DONE) {
        return false;
    }
    sqlite3* database = sqlite3_db_handle(statement_);
    throw DatabaseError(sqlite3_extended_errcode(database), sqlite3_errmsg(database));
}

void Statement::execute() {
    if (step()) {
        throw DatabaseError(SQLITE_MISUSE, "statement returned a row that was not consumed");
    }
}

std::int64_t Statement::column_int64(int index) { return sqlite3_column_int64(statement_, index); }

std::string Statement::column_text(int index) {
    if (column_is_null(index)) {
        return {};
    }
    const auto* text = sqlite3_column_text(statement_, index);
    const int size = sqlite3_column_bytes(statement_, index);
    if (text == nullptr || size <= 0) {
        return {};
    }
    return std::string(reinterpret_cast<const char*>(text), static_cast<std::size_t>(size));
}

bool Statement::column_is_null(int index) { return sqlite3_column_type(statement_, index) == SQLITE_NULL; }

std::optional<std::int64_t> Statement::column_optional_int64(int index) {
    if (column_is_null(index)) {
        return std::nullopt;
    }
    return column_int64(index);
}

std::optional<std::string> Statement::column_optional_text(int index) {
    if (column_is_null(index)) {
        return std::nullopt;
    }
    return column_text(index);
}

}  // namespace pulsenet
