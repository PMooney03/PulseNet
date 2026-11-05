#pragma once

#include "pulsenet/database/database_error.hpp"

#include <sqlite3.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace pulsenet {

// RAII wrapper around sqlite3_stmt*. Bind indexes are 1-based, matching the
// SQLite C API. Column indexes are 0-based, also matching SQLite.
// Text is copied at bind time (SQLITE_TRANSIENT), so the caller's buffer
// does not need to outlive step().
class Statement {
public:
    Statement(sqlite3* database, std::string_view sql);
    ~Statement();

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&&) = delete;
    Statement& operator=(Statement&&) = delete;

    void bind_int64(int index, std::int64_t value);
    void bind_text(int index, std::string_view value);
    void bind_null(int index);

    // True when a row is available. False when the statement is finished.
    bool step();
    void execute();

    [[nodiscard]] std::int64_t column_int64(int index);
    [[nodiscard]] std::string column_text(int index);
    [[nodiscard]] bool column_is_null(int index);
    [[nodiscard]] std::optional<std::int64_t> column_optional_int64(int index);
    [[nodiscard]] std::optional<std::string> column_optional_text(int index);

private:
    void check(int rc) const;

    sqlite3_stmt* statement_{nullptr};
};

}  // namespace pulsenet
