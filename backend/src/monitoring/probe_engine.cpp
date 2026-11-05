#include "pulsenet/monitoring/probe_engine.hpp"

#include "networking/sessions.hpp"
#include "pulsenet/utilities/logger.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl/context.hpp>

#include <atomic>
#include <thread>
#include <utility>

namespace pulsenet {
namespace {

class ThreadJoiner {
public:
    explicit ThreadJoiner(std::vector<std::thread>& threads) : threads_(threads) {}
    ~ThreadJoiner() {
        for (auto& thread : threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    ThreadJoiner(const ThreadJoiner&) = delete;
    ThreadJoiner& operator=(const ThreadJoiner&) = delete;

private:
    std::vector<std::thread>& threads_;
};

std::size_t worker_count(std::size_t requested) {
    if (requested == 0) {
        return 1;
    }
    return requested > 32 ? 32 : requested;
}

}  // namespace

ProbeEngine::ProbeEngine(ProbeOptions options) : options_(std::move(options)) {}

std::vector<MonitorResult> ProbeEngine::check_all(const std::vector<MonitorTarget>& targets) const {
    if (targets.empty()) {
        return {};
    }

    // The context is configured before any worker starts, then only used for
    // concurrent handshakes. OpenSSL allows that. Sessions hold a reference;
    // they are destroyed before this context because they are declared later.
    boost::asio::io_context io;
    boost::asio::ssl::context tls(boost::asio::ssl::context::tls_client);
    tls.set_options(boost::asio::ssl::context::default_workarounds);
    if (options_.verify_tls) {
        boost::system::error_code ec;
        tls.set_default_verify_paths(ec);
        if (ec) {
            log_warn(std::string("tls trust store: ") + ec.message());
        }
    }

    auto work = boost::asio::make_work_guard(io);
    std::vector<MonitorResult> results(targets.size());
    std::atomic<std::size_t> remaining{targets.size()};
    std::vector<std::unique_ptr<Session>> sessions;
    sessions.reserve(targets.size());

    for (std::size_t index = 0; index < targets.size(); ++index) {
        sessions.push_back(make_session(
            io, tls, options_, targets[index],
            [&results, &remaining, &work, index](MonitorResult result) {
                results[index] = std::move(result);
                if (remaining.fetch_sub(1) == 1) {
                    work.reset();
                }
            }));
    }

    // start() only queues handlers. Join before touching results: the worker
    // threads are the ones that write each result slot.
    for (const auto& session : sessions) {
        session->start();
    }

    const std::size_t count = worker_count(options_.worker_threads);
    std::vector<std::thread> workers;
    workers.reserve(count);
    ThreadJoiner joiner(workers);
    for (std::size_t index = 0; index < count; ++index) {
        workers.emplace_back([&io] { io.run(); });
    }
    for (auto& thread : workers) {
        thread.join();
    }
    return results;
}

}  // namespace pulsenet
