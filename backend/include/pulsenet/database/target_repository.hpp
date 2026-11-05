#pragma once

#include "pulsenet/error.hpp"
#include "pulsenet/models/monitor_target.hpp"

#include <cstdint>
#include <vector>

namespace pulsenet {

class Database;

// Non-owning view over Database. Copying a repository copies the reference,
// not the connection.
class TargetRepository {
public:
    explicit TargetRepository(Database& database);

    [[nodiscard]] Result<std::int64_t> insert(const MonitorTarget& target);
    [[nodiscard]] Result<MonitorTarget> find_by_id(std::int64_t id);
    [[nodiscard]] Result<std::vector<MonitorTarget>> list_all();
    [[nodiscard]] Status update(const MonitorTarget& target);
    [[nodiscard]] Status remove(std::int64_t id);

private:
    Database& database_;
};

}  // namespace pulsenet
