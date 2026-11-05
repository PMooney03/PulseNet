#include "networking/sessions.hpp"

#include "pulsenet/models/validation.hpp"
#include "pulsenet/monitoring/health.hpp"
#include "pulsenet/utilities/logger.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <openssl/ssl.h>

#include <chrono>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace pulsenet {
namespace {

namespace net = boost::asio;
namespace ssl = net::ssl;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = net::ip::tcp;

bool is_ip_address(const std::string& host) {
    boost::system::error_code ec;
    net::ip::make_address(host, ec);
    return !ec;
}

std::string host_header(const MonitorTarget& target) {
    const int port = target.port.value_or(0);
    const bool ipv6 = target.host.find(':') != std::string::npos;
    const bool default_port = (target.type == MonitorType::Https && port == 443) ||
                              (target.type == MonitorType::Http && port == 80);
    const std::string rendered = ipv6 ? "[" + target.host + "]" : target.host;
    if (default_port) {
        return rendered;
    }
    return rendered + ":" + std::to_string(port);
}

MonitorResult skipped_result(const MonitorTarget& target, std::string reason) {
    Observation observation;
    observation.skipped = true;
    observation.error_message = std::move(reason);
    return make_result(target, observation);
}

// Shared timer, resolver, and completion path. The socket stays in the
// derived session because TCP, DNS, and TLS do not share one stream type.
class ProbeSupport {
public:
    ProbeSupport(net::io_context& io, MonitorTarget target, std::function<void(MonitorResult)> done)
        : strand_(net::make_strand(io)),
          resolver_(strand_),
          timer_(strand_),
          target_(std::move(target)),
          done_(std::move(done)),
          started_(std::chrono::steady_clock::now()) {}

    net::strand<net::io_context::executor_type>& executor() { return strand_; }
    tcp::resolver& resolver() { return resolver_; }
    const MonitorTarget& target() const { return target_; }
    bool finished() const { return finished_; }
    bool timed_out() const { return timed_out_; }

    void set_canceller(std::function<void()> cancel_io) { cancel_io_ = std::move(cancel_io); }

    void arm_timer() {
        timer_.expires_after(target_.timeout);
        timer_.async_wait([this](const boost::system::error_code& ec) {
            if (ec || finished_) {
                return;
            }
            timed_out_ = true;
            resolver_.cancel();
            if (cancel_io_) {
                cancel_io_();
            }
            // Cancel does not always deliver a completion. Finish the probe here
            // so a missing handler cannot leave io_context::run() blocked.
            fail("connection timed out");
        });
    }

    [[nodiscard]] std::chrono::milliseconds elapsed() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_);
    }

    // True when the handler should stop. A timeout is turned into one result.
    bool stop_if_done() {
        if (finished_) {
            return true;
        }
        if (timed_out_) {
            fail("connection timed out");
            return true;
        }
        return false;
    }

    void fail(std::string message) {
        if (finished_) {
            return;
        }
        log_debug("probe host=" + target_.host + " detail=" + message);
        Observation observation;
        observation.response_time = elapsed();
        observation.error_message = std::move(message);
        complete(std::move(observation));
    }

    void succeed(Observation observation) {
        if (finished_) {
            return;
        }
        observation.reached_service = true;
        if (!observation.response_time) {
            observation.response_time = elapsed();
        }
        complete(std::move(observation));
    }

private:
    void complete(Observation observation) {
        finished_ = true;
        boost::system::error_code ignored;
        timer_.cancel(ignored);
        if (cancel_io_) {
            cancel_io_();
        }
        auto callback = std::move(done_);
        if (!callback) {
            return;
        }
        try {
            callback(make_result(target_, observation));
        } catch (const std::exception& ex) {
            log_error(std::string("check callback failed: ") + ex.what());
        }
    }

    net::strand<net::io_context::executor_type> strand_;
    tcp::resolver resolver_;
    net::steady_timer timer_;
    MonitorTarget target_;
    std::function<void(MonitorResult)> done_;
    std::function<void()> cancel_io_;
    std::chrono::steady_clock::time_point started_;
    bool finished_{false};
    bool timed_out_{false};
};

class ImmediateSession final : public Session {
public:
    ImmediateSession(net::io_context& io, MonitorResult result, std::function<void(MonitorResult)> done)
        : io_(io), result_(std::move(result)), done_(std::move(done)) {}

    void start() override {
        net::post(io_, [this] {
            if (done_) {
                done_(std::move(result_));
            }
        });
    }

private:
    net::io_context& io_;
    MonitorResult result_;
    std::function<void(MonitorResult)> done_;
};

class DnsSession final : public Session {
public:
    DnsSession(net::io_context& io, MonitorTarget target, std::function<void(MonitorResult)> done)
        : support_(io, std::move(target), std::move(done)) {}

    void start() override {
        support_.arm_timer();
        net::dispatch(support_.executor(), [this] { resolve(); });
    }

private:
    void resolve() {
        if (support_.stop_if_done()) {
            return;
        }
        support_.resolver().async_resolve(
            support_.target().host, "0", [this](const boost::system::error_code& ec, tcp::resolver::results_type results) {
                if (support_.stop_if_done()) {
                    return;
                }
                if (ec) {
                    support_.fail("dns lookup failed: " + ec.message());
                    return;
                }
                if (results.empty()) {
                    support_.fail("dns lookup failed: no addresses");
                    return;
                }
                log_debug("dns host=" + support_.target().host + " addresses=" + std::to_string(results.size()));
                Observation observation;
                observation.response_time = support_.elapsed();
                support_.succeed(std::move(observation));
            });
    }

    ProbeSupport support_;
};

class TcpSession final : public Session {
public:
    TcpSession(net::io_context& io, MonitorTarget target, std::function<void(MonitorResult)> done)
        : support_(io, std::move(target), std::move(done)), socket_(support_.executor()) {}

    void start() override {
        service_ = std::to_string(*support_.target().port);
        support_.set_canceller([this] {
            boost::system::error_code ignored;
            socket_.cancel(ignored);
            socket_.close(ignored);
        });
        support_.arm_timer();
        net::dispatch(support_.executor(), [this] { resolve(); });
    }

private:
    void resolve() {
        if (support_.stop_if_done()) {
            return;
        }
        support_.resolver().async_resolve(
            support_.target().host, service_, [this](const boost::system::error_code& ec, tcp::resolver::results_type results) {
                on_resolve(ec, std::move(results));
            });
    }

    void on_resolve(const boost::system::error_code& ec, tcp::resolver::results_type results) {
        if (support_.stop_if_done()) {
            return;
        }
        if (ec) {
            support_.fail("dns lookup failed: " + ec.message());
            return;
        }
        if (results.empty()) {
            support_.fail("dns lookup failed: no addresses");
            return;
        }
        net::async_connect(socket_, results,
                           [this](const boost::system::error_code& connect_ec, const tcp::endpoint&) { on_connect(connect_ec); });
    }

    void on_connect(const boost::system::error_code& ec) {
        if (support_.stop_if_done()) {
            return;
        }
        if (ec) {
            support_.fail("connect failed: " + ec.message());
            return;
        }
        Observation observation;
        observation.response_time = support_.elapsed();
        support_.succeed(std::move(observation));
    }

    ProbeSupport support_;
    tcp::socket socket_;
    std::string service_;
};

template <bool UseTls>
class HttpSession final : public Session {
public:
    HttpSession(net::io_context& io,
                ssl::context& tls,
                bool verify_tls,
                MonitorTarget target,
                std::function<void(MonitorResult)> done)
        : support_(io, std::move(target), std::move(done)),
          stream_(make_stream(support_.executor(), tls)),
          verify_tls_(verify_tls) {
        parser_.body_limit(64 * 1024);
    }

    void start() override {
        service_ = std::to_string(*support_.target().port);
        request_.method(http::verb::get);
        request_.target("/");
        request_.version(11);
        request_.set(http::field::host, host_header(support_.target()));
        request_.set(http::field::user_agent, std::string("PulseNet/") + PULSENET_VERSION);
        request_.set(http::field::accept, "*/*");
        request_.set(http::field::connection, "close");
        support_.set_canceller([this] {
            boost::system::error_code ignored;
            lowest().cancel(ignored);
            lowest().close(ignored);
        });
        support_.arm_timer();
        net::dispatch(support_.executor(), [this] { resolve(); });
    }

private:
    using Stream = std::conditional_t<UseTls, ssl::stream<tcp::socket>, tcp::socket>;

    static Stream make_stream(net::strand<net::io_context::executor_type>& executor, ssl::context& tls) {
        if constexpr (UseTls) {
            return Stream(executor, tls);
        } else {
            (void)tls;
            return Stream(executor);
        }
    }

    tcp::socket& lowest() {
        if constexpr (UseTls) {
            return stream_.next_layer();
        } else {
            return stream_;
        }
    }

    void resolve() {
        if (support_.stop_if_done()) {
            return;
        }
        support_.resolver().async_resolve(
            support_.target().host, service_, [this](const boost::system::error_code& ec, tcp::resolver::results_type results) {
                on_resolve(ec, std::move(results));
            });
    }

    void on_resolve(const boost::system::error_code& ec, tcp::resolver::results_type results) {
        if (support_.stop_if_done()) {
            return;
        }
        if (ec) {
            support_.fail("dns lookup failed: " + ec.message());
            return;
        }
        if (results.empty()) {
            support_.fail("dns lookup failed: no addresses");
            return;
        }
        net::async_connect(lowest(), results,
                           [this](const boost::system::error_code& connect_ec, const tcp::endpoint&) {
                               on_connect(connect_ec);
                           });
    }

    void on_connect(const boost::system::error_code& ec) {
        if (support_.stop_if_done()) {
            return;
        }
        if (ec) {
            support_.fail("connect failed: " + ec.message());
            return;
        }
        if constexpr (UseTls) {
            begin_tls();
        } else {
            write_request();
        }
    }

    void begin_tls() {
        if constexpr (UseTls) {
            const std::string& host = support_.target().host;
            if (!is_ip_address(host)) {
                if (SSL_set_tlsext_host_name(stream_.native_handle(), host.c_str()) == 0) {
                    support_.fail("tls setup failed");
                    return;
                }
            }
            if (verify_tls_) {
                stream_.set_verify_mode(ssl::verify_peer);
                if (!is_ip_address(host)) {
                    stream_.set_verify_callback(ssl::host_name_verification(host));
                }
            } else {
                stream_.set_verify_mode(ssl::verify_none);
            }
            stream_.async_handshake(ssl::stream_base::client, [this](const boost::system::error_code& ec) {
                if (support_.stop_if_done()) {
                    return;
                }
                if (ec) {
                    support_.fail("tls handshake failed: " + ec.message());
                    return;
                }
                write_request();
            });
        }
    }

    void write_request() {
        http::async_write(stream_, request_, [this](const boost::system::error_code& ec, std::size_t) {
            if (support_.stop_if_done()) {
                return;
            }
            if (ec) {
                support_.fail("http write failed: " + ec.message());
                return;
            }
            http::async_read(stream_, buffer_, parser_, [this](const boost::system::error_code& read_ec, std::size_t) {
                on_read(read_ec);
            });
        });
    }

    void on_read(const boost::system::error_code& ec) {
        if (support_.stop_if_done()) {
            return;
        }
        if (ec) {
            support_.fail("http read failed: " + ec.message());
            return;
        }
        Observation observation;
        observation.http_status = static_cast<int>(parser_.get().result_int());
        observation.response_time = support_.elapsed();
        support_.succeed(std::move(observation));
    }

    ProbeSupport support_;
    Stream stream_;
    bool verify_tls_;
    std::string service_;
    beast::flat_buffer buffer_;
    http::request<http::empty_body> request_;
    http::response_parser<http::string_body> parser_;
};

}  // namespace

std::unique_ptr<Session> make_session(boost::asio::io_context& io,
                                      boost::asio::ssl::context& tls,
                                      const ProbeOptions& options,
                                      MonitorTarget target,
                                      std::function<void(MonitorResult)> done) {
    if (!target.enabled) {
        return std::make_unique<ImmediateSession>(io, skipped_result(target, "target is disabled"), std::move(done));
    }

    MonitorTarget prepared = normalize(std::move(target));
    if (!prepared.port) {
        if (prepared.type == MonitorType::Http) {
            prepared.port = 80;
        } else if (prepared.type == MonitorType::Https) {
            prepared.port = 443;
        }
    }
    if (const auto problem = validate(prepared)) {
        return std::make_unique<ImmediateSession>(io, skipped_result(prepared, problem->message), std::move(done));
    }

    switch (prepared.type) {
        case MonitorType::Tcp:
            return std::make_unique<TcpSession>(io, std::move(prepared), std::move(done));
        case MonitorType::Dns:
            return std::make_unique<DnsSession>(io, std::move(prepared), std::move(done));
        case MonitorType::Http:
            return std::make_unique<HttpSession<false>>(io, tls, options.verify_tls, std::move(prepared), std::move(done));
        case MonitorType::Https:
            return std::make_unique<HttpSession<true>>(io, tls, options.verify_tls, std::move(prepared), std::move(done));
    }
    return std::make_unique<ImmediateSession>(io, skipped_result(prepared, "unsupported monitor type"), std::move(done));
}

}  // namespace pulsenet
