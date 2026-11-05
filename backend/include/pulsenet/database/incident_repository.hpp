#pragma once

#include "pulsenet/error.hpp"
#include "pulsenet/models/incident.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

namespace pulsenet {

class Database;

class IncidentRepository {
public:
    explicit IncidentRepository(Database& database);

    [[nodiscard]] Result<std::int64_t> insert(const Incident& incident);
    [[nodiscard]] Result<Incident> find_by_id(std::int64_t id);
    [[nodiscard]] Result<std::vector<Incident>> list_open(int limit = 100);
    [[nodiscard]] Result<std::vector<Incident>> list_for_target(std::int64_t target_id,
                                                                int limit = 100);
    // Resolving an already-resolved incident succeeds and keeps the original time.
    [[nodiscard]] Status resolve(std::int64_t id, std::chrono::system_clock::time_point resolved_at);

private:
    Database& database_;
};

}  // namespace pulsenet
