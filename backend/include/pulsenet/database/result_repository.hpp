#pragma once

#include "pulsenet/error.hpp"
#include "pulsenet/models/monitor_result.hpp"

#include <cstdint>
#include <vector>

namespace pulsenet {

class Database;

class ResultRepository {
public:
    explicit ResultRepository(Database& database);

    [[nodiscard]] Result<std::int64_t> insert(const MonitorResult& result);
    [[nodiscard]] Result<MonitorResult> find_by_id(std::int64_t id);
    [[nodiscard]] Result<std::vector<MonitorResult>> list_for_target(std::int64_t target_id,
                                                                      int limit = 100);

private:
    Database& database_;
};

}  // namespace pulsenet
