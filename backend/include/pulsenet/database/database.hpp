#pragma once

#include "pulsenet/database/database_error.hpp"

#include <sqlite3.h>

#include <filesystem>
#include <memory>
#include <mutex>
#include <string_view>
#include <utility>

namespace pulsenet {

// One SQLite connection for the process.
//
// Ownership: Database owns the sqlite3* through std::unique_ptr. Repositories
// hold a Database& and do not own it. shared_ptr is not used because there is
// no shared lifetime: the application creates the database, then the
// repositories, and destroys the repositories first.
//
// Thread safety: every public method takes mutex_ before touching the
// connection. The lock covers multi-step work such as INSERT plus
// sqlite3_last_insert_rowid, which SQLite's own mutex does not treat as one
// critical section. Do not call Database methods from inside with_connection
// or transaction. mutex_ is not recursive, so that deadlocks on purpose
// instead of hiding a lock-order bug.
//
// SQLite is also opened in full-mutex mode as a backstop. WAL mode is enabled
// so a crash mid-write is less likely to corrupt the file. Phase 1 still
// serializes callers on mutex_; WAL is not a second writer pool.
class Database {
public:
    struct SqliteDeleter {
        void operator()(sqlite3* db) const noexcept {
            if (db != nullptr) {
                sqlite3_close_v2(db);
            }
        }
    };

    explicit Database(std::filesystem::path path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&&) = delete;
    Database& operator=(Database&&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

    void execute(std::string_view sql);
    [[nodiscard]] int schema_version();

    template <typename Fn>
    auto with_connection(Fn&& fn) -> decltype(fn(static_cast<sqlite3*>(nullptr))) {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::forward<Fn>(fn)(db_.get());
    }

    template <typename Fn>
    void transaction(Fn&& fn) {
        std::lock_guard<std::mutex> lock(mutex_);
        execute_unlocked("BEGIN IMMEDIATE");
        try {
            std::forward<Fn>(fn)(db_.get());
            execute_unlocked("COMMIT");
        } catch (...) {
            rollback_unlocked();
            throw;
        }
    }

private:
    void execute_unlocked(std::string_view sql);
    void rollback_unlocked() noexcept;

    std::filesystem::path path_;
    std::unique_ptr<sqlite3, SqliteDeleter> db_;
    mutable std::mutex mutex_;
};

}  // namespace pulsenet
