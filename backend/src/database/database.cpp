#include "pulsenet/database/database.hpp"

#include "database/statement.hpp"
#include "pulsenet/utilities/logger.hpp"

#include <string>
#include <utility>

namespace pulsenet {
namespace {

std::string path_to_utf8(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    std::string bytes;
    bytes.resize(utf8.size());
    for (std::size_t index = 0; index < utf8.size(); ++index) {
        bytes[index] = static_cast<char>(utf8[index]);
    }
    return bytes;
}

struct SqliteFree {
    void operator()(char* pointer) const noexcept { sqlite3_free(pointer); }
};

}  // namespace

Database::Database(std::filesystem::path path) : path_(std::move(path)) {
    const auto parent = path_.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    sqlite3* raw = nullptr;
    const std::string utf8 = path_to_utf8(path_);
    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    const int rc = sqlite3_open_v2(utf8.c_str(), &raw, flags, nullptr);
    db_.reset(raw);
    if (rc != SQLITE_OK) {
        const char* message = db_ ? sqlite3_errmsg(db_.get()) : "unable to allocate sqlite";
        throw DatabaseError(rc, std::string("failed to open database: ") + message);
    }

    sqlite3_extended_result_codes(db_.get(), 1);
    sqlite3_busy_timeout(db_.get(), 5000);
    // foreign_keys is off by default and must be enabled on every connection.
    // WAL plus NORMAL sync is the usual durability setting for a local app:
    // commits are durable across power loss without waiting on every fsync.
    execute_unlocked("PRAGMA foreign_keys = ON");
    execute_unlocked("PRAGMA journal_mode = WAL");
    execute_unlocked("PRAGMA synchronous = NORMAL");
    log_info("database opened path=" + utf8);
}

Database::~Database() = default;

void Database::execute(std::string_view sql) {
    std::lock_guard<std::mutex> lock(mutex_);
    execute_unlocked(sql);
}

int Database::schema_version() {
    int version = 0;
    with_connection([&](sqlite3* connection) {
        Statement statement(connection, "SELECT COALESCE(MAX(version), 0) FROM schema_migrations");
        if (statement.step()) {
            version = static_cast<int>(statement.column_int64(0));
        }
    });
    return version;
}

void Database::execute_unlocked(std::string_view sql) {
    char* raw_error = nullptr;
    const std::string text{sql};
    const int rc = sqlite3_exec(db_.get(), text.c_str(), nullptr, nullptr, &raw_error);
    std::unique_ptr<char, SqliteFree> error{raw_error};
    if (rc != SQLITE_OK) {
        const std::string message = error ? error.get() : sqlite3_errmsg(db_.get());
        throw DatabaseError(sqlite3_extended_errcode(db_.get()), message);
    }
}

void Database::rollback_unlocked() noexcept {
    try {
        execute_unlocked("ROLLBACK");
    } catch (const std::exception& ex) {
        log_error(std::string("database rollback failed: ") + ex.what());
    }
}

}  // namespace pulsenet
