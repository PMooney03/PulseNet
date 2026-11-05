#pragma once

namespace pulsenet {

class Database;

// Applies ordered schema versions. Running it twice is safe: applied versions
// are recorded in schema_migrations and skipped.
class Migrator {
public:
    explicit Migrator(Database& database);

    void apply();

private:
    Database& database_;
};

}  // namespace pulsenet
