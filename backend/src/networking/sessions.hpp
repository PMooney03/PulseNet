#pragma once

#include "pulsenet/models/monitor_result.hpp"
#include "pulsenet/models/monitor_target.hpp"
#include "pulsenet/monitoring/probe_engine.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>

#include <functional>
#include <memory>

namespace pulsenet {

// One in-flight check. The engine owns these with unique_ptr until
// io_context::run() returns, so handlers may use this freely and do not
// take a shared_ptr.
class Session {
public:
    virtual ~Session() = default;
    virtual void start() = 0;
};

[[nodiscard]] std::unique_ptr<Session> make_session(boost::asio::io_context& io,
                                                    boost::asio::ssl::context& tls,
                                                    const ProbeOptions& options,
                                                    MonitorTarget target,
                                                    std::function<void(MonitorResult)> done);

}  // namespace pulsenet
