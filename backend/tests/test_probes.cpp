#include "support.hpp"

#include "pulsenet/database/database.hpp"
#include "pulsenet/database/migrator.hpp"
#include "pulsenet/database/result_repository.hpp"
#include "pulsenet/database/target_repository.hpp"
#include "pulsenet/monitoring/check_service.hpp"
#include "pulsenet/monitoring/probe_engine.hpp"
#include "pulsenet/utilities/logger.hpp"

#include <gtest/gtest.h>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

using pulsenet::CheckService;
using pulsenet::Database;
using pulsenet::LogLevel;
using pulsenet::Logger;
using pulsenet::Migrator;
using pulsenet::MonitorTarget;
using pulsenet::MonitorType;
using pulsenet::ProbeEngine;
using pulsenet::ProbeOptions;
using pulsenet::ResultRepository;
using pulsenet::TargetRepository;
using pulsenet::TargetStatus;
using pulsenet::test_support::make_temp_directory;

MonitorTarget base_target(MonitorType type, int port) {
    MonitorTarget target;
    target.name = "local";
    target.host = "127.0.0.1";
    target.port = port;
    target.type = type;
    target.interval = std::chrono::seconds{30};
    target.timeout = std::chrono::milliseconds{2000};
    target.enabled = true;
    target.latency_threshold = std::chrono::milliseconds{1000};
    target.failure_threshold = 3;
    return target;
}

class LocalServer {
public:
    explicit LocalServer(std::string response,
                         std::chrono::milliseconds delay = {},
                         bool stall = false,
                         std::filesystem::path certificate = {},
                         std::filesystem::path key = {})
        : response_(std::move(response)), delay_(delay), stall_(stall), acceptor_(io_) {
        if (!certificate.empty()) {
            tls_ = std::make_unique<ssl::context>(ssl::context::tls_server);
            tls_->use_certificate_chain_file(certificate.string());
            tls_->use_private_key_file(key.string(), ssl::context::pem);
        }
        acceptor_.open(tcp::v4());
        acceptor_.set_option(tcp::acceptor::reuse_address(true));
        acceptor_.bind(tcp::endpoint(net::ip::make_address("127.0.0.1"), 0));
        acceptor_.listen();
        port_ = acceptor_.local_endpoint().port();
        thread_ = std::thread([this] { loop(); });
    }

    LocalServer(const LocalServer&) = delete;
    LocalServer& operator=(const LocalServer&) = delete;

    ~LocalServer() { stop(); }

    [[nodiscard]] int port() const { return port_; }

    void stop() {
        if (stopped_.exchange(true)) {
            return;
        }
        stop_flag_.store(true);
        cv_.notify_all();
        // Closing the acceptor from this thread deadlocks with a blocking
        // accept() on the server thread: that call holds the io_context lock
        // inside the syscall, and close() waits for the same lock. A connection
        // on a separate io_context wakes accept() so the thread can exit.
        if (port_ != 0) {
            net::io_context wake_io;
            tcp::socket wake(wake_io);
            boost::system::error_code ignored;
            wake.connect(tcp::endpoint(net::ip::make_address("127.0.0.1"), static_cast<unsigned short>(port_)),
                         ignored);
        }
        if (thread_.joinable()) {
            thread_.join();
        }
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        boost::system::error_code ignored;
        acceptor_.close(ignored);
    }

private:
    void loop() {
        while (!stop_flag_.load()) {
            tcp::socket socket(io_);
            boost::system::error_code ec;
            acceptor_.accept(socket, ec);
            if (stop_flag_.load() || ec) {
                break;
            }
            workers_.emplace_back([this, sock = std::move(socket)]() mutable { handle(std::move(sock)); });
        }
    }

    void handle(tcp::socket socket) {
        boost::system::error_code ec;
        if (tls_) {
            ssl::stream<tcp::socket> stream(std::move(socket), *tls_);
            stream.handshake(ssl::stream_base::server, ec);
            if (ec) {
                return;
            }
            std::array<char, 2048> buffer{};
            stream.read_some(net::buffer(buffer), ec);
            if (delay_.count() > 0) {
                std::this_thread::sleep_for(delay_);
            }
            net::write(stream, net::buffer(response_), ec);
            return;
        }
        if (!stall_) {
            read_request(socket);
            if (stop_flag_.load()) {
                return;
            }
            if (delay_.count() > 0) {
                std::this_thread::sleep_for(delay_);
            }
            if (!response_.empty()) {
                net::write(socket, net::buffer(response_), ec);
            }
            return;
        }
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return stop_flag_.load(); });
    }

    // Non-blocking so stop() can join the worker without waiting for the client.
    void read_request(tcp::socket& socket) {
        socket.non_blocking(true);
        std::array<char, 2048> buffer{};
        boost::system::error_code ec;
        while (!stop_flag_.load()) {
            socket.read_some(net::buffer(buffer), ec);
            if (!ec || ec != net::error::would_block) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    std::string response_;
    std::chrono::milliseconds delay_;
    bool stall_;
    net::io_context io_;
    tcp::acceptor acceptor_;
    int port_{0};
    std::atomic<bool> stop_flag_{false};
    std::atomic<bool> stopped_{false};
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread thread_;
    std::vector<std::thread> workers_;
    std::unique_ptr<ssl::context> tls_;
};

const char* kHttp204 = "HTTP/1.1 204 No Content\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
const char* kHttp503 = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";

bool generate_certificate(const std::filesystem::path& directory,
                          std::filesystem::path& certificate,
                          std::filesystem::path& key) {
    certificate = directory / "cert.pem";
    key = directory / "key.pem";
    const std::string command = "openssl req -x509 -newkey rsa:2048 -keyout '" + key.string() + "' -out '" +
                                certificate.string() + "' -days 1 -nodes -subj /CN=localhost >/dev/null 2>&1";
    return std::system(command.c_str()) == 0;
}

class ProbeTest : public ::testing::Test {
protected:
    void SetUp() override {
        previous_ = Logger::instance().level();
        Logger::instance().set_level(LogLevel::Error);
        options_.worker_threads = 2;
        options_.verify_tls = false;
    }

    void TearDown() override { Logger::instance().set_level(previous_); }

    ProbeOptions options_;
    LogLevel previous_{LogLevel::Info};
};

}  // namespace

TEST_F(ProbeTest, TcpAndHttpLocalServers) {
    LocalServer tcp_server("");
    LocalServer http_server(kHttp204);
    LocalServer failing_server(kHttp503);
    ProbeEngine engine(options_);

    auto tcp = base_target(MonitorType::Tcp, tcp_server.port());
    tcp.name = "tcp";
    auto http = base_target(MonitorType::Http, http_server.port());
    http.name = "http";
    auto failing = base_target(MonitorType::Http, failing_server.port());
    failing.name = "failing";
    failing.latency_threshold = std::chrono::milliseconds{50};

    const auto results = engine.check_all({tcp, http, failing});
    ASSERT_EQ(results.size(), 3u);

    EXPECT_EQ(results[0].status, TargetStatus::Online);
    EXPECT_TRUE(results[0].success);
    EXPECT_FALSE(results[0].http_status.has_value());
    ASSERT_TRUE(results[0].response_time);

    EXPECT_EQ(results[1].status, TargetStatus::Online);
    ASSERT_TRUE(results[1].http_status);
    EXPECT_EQ(*results[1].http_status, 204);

    EXPECT_EQ(results[2].status, TargetStatus::Degraded);
    EXPECT_TRUE(results[2].success);
    ASSERT_TRUE(results[2].http_status);
    EXPECT_EQ(*results[2].http_status, 503);
}

TEST_F(ProbeTest, ClosedPortDnsAndDisabledTargets) {
    ProbeEngine engine(options_);
    auto closed = base_target(MonitorType::Tcp, 1);
    closed.name = "closed";
    closed.timeout = std::chrono::milliseconds{500};

    auto dns = base_target(MonitorType::Dns, 0);
    dns.port.reset();
    dns.name = "dns";
    dns.host = "localhost";

    auto missing = base_target(MonitorType::Dns, 0);
    missing.port.reset();
    missing.name = "missing";
    missing.host = "pulsenet.invalid";
    missing.timeout = std::chrono::milliseconds{1500};

    auto disabled = base_target(MonitorType::Tcp, 1);
    disabled.name = "disabled";
    disabled.enabled = false;

    auto invalid = base_target(MonitorType::Tcp, 1);
    invalid.name = "invalid";
    invalid.port.reset();

    const auto started = std::chrono::steady_clock::now();
    const auto results = engine.check_all({closed, dns, missing, disabled, invalid});
    const auto elapsed = std::chrono::steady_clock::now() - started;
    ASSERT_EQ(results.size(), 5u);
    EXPECT_LT(elapsed, std::chrono::seconds{5});

    EXPECT_EQ(results[0].status, TargetStatus::Offline);
    EXPECT_FALSE(results[0].success);

    EXPECT_TRUE(results[1].success);
    EXPECT_NE(results[1].status, TargetStatus::Offline);

    EXPECT_EQ(results[2].status, TargetStatus::Offline);
    EXPECT_FALSE(results[2].success);

    EXPECT_EQ(results[3].status, TargetStatus::Unknown);
    ASSERT_TRUE(results[3].error_message);
    EXPECT_NE(results[3].error_message->find("disabled"), std::string::npos);

    EXPECT_EQ(results[4].status, TargetStatus::Unknown);
    ASSERT_TRUE(results[4].error_message);
    EXPECT_NE(results[4].error_message->find("port"), std::string::npos);
}

TEST_F(ProbeTest, HttpTimeoutAndParallelChecks) {
    LocalServer stall(kHttp204, {}, true);
    auto stalled = base_target(MonitorType::Http, stall.port());
    stalled.timeout = std::chrono::milliseconds{250};
    ProbeEngine engine(options_);
    const auto timed = engine.check_all({stalled});
    ASSERT_EQ(timed.size(), 1u);
    EXPECT_EQ(timed[0].status, TargetStatus::Offline);
    ASSERT_TRUE(timed[0].error_message);
    EXPECT_NE(timed[0].error_message->find("timed out"), std::string::npos);
    stall.stop();

    constexpr int kTargets = 4;
    LocalServer slow(kHttp204, std::chrono::milliseconds{300});
    std::vector<MonitorTarget> targets;
    targets.reserve(kTargets);
    for (int index = 0; index < kTargets; ++index) {
        auto target = base_target(MonitorType::Http, slow.port());
        target.name = "slow-" + std::to_string(index);
        target.latency_threshold = std::chrono::milliseconds{5000};
        target.timeout = std::chrono::milliseconds{3000};
        targets.push_back(std::move(target));
    }

    const auto started = std::chrono::steady_clock::now();
    const auto results = engine.check_all(targets);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    ASSERT_EQ(results.size(), static_cast<std::size_t>(kTargets));
    for (const auto& result : results) {
        EXPECT_EQ(result.status, TargetStatus::Online) << (result.error_message ? *result.error_message : "");
    }
    // Four serial 300 ms server delays would take at least 1200 ms.
    EXPECT_LT(elapsed.count(), 1000);
}

TEST_F(ProbeTest, HttpsRejectsAndAllowsALocalCertificate) {
    const auto directory = make_temp_directory();
    std::filesystem::path certificate;
    std::filesystem::path key;
    if (!generate_certificate(directory, certificate, key)) {
        std::filesystem::remove_all(directory);
        GTEST_SKIP() << "openssl is not available to mint a test certificate";
    }

    LocalServer server(kHttp204, {}, false, certificate, key);
    auto target = base_target(MonitorType::Https, server.port());
    target.timeout = std::chrono::milliseconds{3000};

    ProbeOptions verifying = options_;
    verifying.verify_tls = true;
    const auto rejected = ProbeEngine(verifying).check_all({target});
    ASSERT_EQ(rejected.size(), 1u);
    EXPECT_EQ(rejected[0].status, TargetStatus::Offline);
    ASSERT_TRUE(rejected[0].error_message);
    EXPECT_NE(rejected[0].error_message->find("tls"), std::string::npos);

    const auto accepted = ProbeEngine(options_).check_all({target});
    ASSERT_EQ(accepted.size(), 1u);
    EXPECT_EQ(accepted[0].status, TargetStatus::Online) << (accepted[0].error_message ? *accepted[0].error_message : "");
    ASSERT_TRUE(accepted[0].http_status);
    EXPECT_EQ(*accepted[0].http_status, 204);

    std::filesystem::remove_all(directory);
}

TEST_F(ProbeTest, CheckServiceStoresTheMeasurement) {
    LocalServer server(kHttp204);
    const auto directory = make_temp_directory();
    Database database(directory / "pulsenet.db");
    Migrator{database}.apply();

    auto draft = base_target(MonitorType::Http, server.port());
    draft.name = "stored";
    const auto id = TargetRepository(database).insert(draft);
    ASSERT_TRUE(id) << id.error().message;
    const auto stored = TargetRepository(database).find_by_id(id.value());
    ASSERT_TRUE(stored);

    ProbeEngine engine(options_);
    ResultRepository results(database);
    CheckService service(results, engine);
    const auto round = service.run_round({stored.value()});
    ASSERT_TRUE(round) << round.error().message;
    ASSERT_EQ(round.value().size(), 1u);
    EXPECT_EQ(round.value().front().status, TargetStatus::Online);
    EXPECT_GT(round.value().front().id, 0);

    const auto history = ResultRepository(database).list_for_target(id.value());
    ASSERT_TRUE(history);
    ASSERT_EQ(history.value().size(), 1u);
    EXPECT_EQ(history.value().front().status, TargetStatus::Online);
    ASSERT_TRUE(history.value().front().http_status);
    EXPECT_EQ(*history.value().front().http_status, 204);

    std::filesystem::remove_all(directory);
}
