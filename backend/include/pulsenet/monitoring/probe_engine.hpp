#pragma once

#include "pulsenet/models/monitor_result.hpp"
#include "pulsenet/models/monitor_target.hpp"

#include <cstddef>
#include <vector>

namespace pulsenet {

struct ProbeOptions {
    // Threads calling io_context::run(). This is not one thread per target.
    // Asio multiplexes every socket on this pool.
    std::size_t worker_threads{2};
    // Production checks verify the certificate chain and hostname.
    // Tests set this false for a local self-signed certificate.
    bool verify_tls{true};
};

// Runs one round of checks. The object itself owns no sockets; each call
// builds an io_context, runs it, and joins the workers before returning.
class ProbeEngine {
public:
    explicit ProbeEngine(ProbeOptions options = {});

    [[nodiscard]] std::vector<MonitorResult> check_all(const std::vector<MonitorTarget>& targets) const;

private:
    ProbeOptions options_;
};

}  // namespace pulsenet
