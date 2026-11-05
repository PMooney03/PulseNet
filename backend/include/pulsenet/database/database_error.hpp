#pragma once

#include "pulsenet/error.hpp"

#include <sqlite3.h>

#include <stdexcept>
#include <string>

namespace pulsenet {

class DatabaseError : public std::runtime_error {
public:
    DatabaseError(int sqlite_code, const std::string& message)
        : std::runtime_error(message), sqlite_code_(sqlite_code) {}

    [[nodiscard]] int sqlite_code() const noexcept { return sqlite_code_; }

private:
    int sqlite_code_;
};

inline Error map_database_error(const DatabaseError& error) {
    const int primary = error.sqlite_code() & 0xff;
    if (primary == SQLITE_CONSTRAINT) {
        return Error{ErrorCode::Constraint, error.what()};
    }
    return Error{ErrorCode::Database, error.what()};
}

}  // namespace pulsenet
