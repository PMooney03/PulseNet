#include "support.hpp"

#include "pulsenet/api/http_server.hpp"
#include "pulsenet/database/database.hpp"
#include "pulsenet/database/incident_repository.hpp"
#include "pulsenet/database/migrator.hpp"
#include "pulsenet/database/result_repository.hpp"
#include "pulsenet/database/target_repository.hpp"
#include "pulsenet/monitoring/check_service.hpp"
#include "pulsenet/monitoring/probe_engine.hpp"
#include "pulsenet/monitoring/scheduler.hpp"
#include "pulsenet/utilities/logger.hpp"

#include <gtest/gtest.h>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <fstream>

namespace {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = net::ip::tcp;

using pulsenet::CheckService;
using pulsenet::Database;
using pulsenet::HttpServer;
using pulsenet::HttpServerOptions;
using pulsenet::IncidentRepository;
using pulsenet::LogLevel;
using pulsenet::Logger;
using pulsenet::Migrator;
using pulsenet::ProbeEngine;
using pulsenet::ResultRepository;
using pulsenet::Scheduler;
using pulsenet::TargetRepository;
using pulsenet::test_support::make_temp_directory;

std::string request(const unsigned short port, const std::string& method, const std::string& path, const std::string& body = {}) {
    net::io_context io;
    tcp::socket socket(io);
    socket.connect({net::ip::make_address("127.0.0.1"), port});
    http::request<http::string_body> req{http::string_to_verb(method), path, 11};
    req.set(http::field::host, "localhost");
    req.set(http::field::connection, "close");
    if (!body.empty()) {
        req.set(http::field::content_type, "application/json");
        req.body() = body;
        req.prepare_payload();
    }
    http::write(socket, req);
    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(socket, buffer, response);
    return std::string(response.reason()) + "\n" + std::to_string(response.result_int()) + "\n" + response.body();
}

}  // namespace

TEST(ApiTest, HealthTargetsCheckAndDashboardFile) {
    Logger::instance().set_level(LogLevel::Error);
    const auto directory = make_temp_directory();
    Database database(directory / "pulsenet.db");
    Migrator{database}.apply();
    TargetRepository targets(database);
    ResultRepository results(database);
    IncidentRepository incidents(database);
    ProbeEngine engine;
    CheckService checks(results, engine);
    Scheduler scheduler(targets, results, incidents, checks);

    const auto web = directory / "web";
    std::filesystem::create_directory(web);
    std::ofstream(web / "index.html") << "PulseNet dashboard";

    HttpServer server(HttpServerOptions{"127.0.0.1", 0, web}, targets, results, incidents, scheduler);
    server.start();

    const auto health = request(server.port(), "GET", "/api/health");
    EXPECT_NE(health.find("\"status\":\"ok\""), std::string::npos);

    const auto created = request(server.port(), "POST", "/api/targets",
                                 R"({"name":"Local DNS","host":"localhost","type":"dns","failure_threshold":3,"timeout_ms":2000})");
    EXPECT_NE(created.find("\n200\n"), std::string::npos) << created;
    EXPECT_NE(created.find("Local DNS"), std::string::npos) << created;

    const auto listed = request(server.port(), "GET", "/api/targets");
    EXPECT_NE(listed.find("Local DNS"), std::string::npos);

    const auto id_at = created.find("\"id\":");
    ASSERT_NE(id_at, std::string::npos);
    const auto id = created.substr(id_at + 5, created.find_first_of(",}", id_at) - (id_at + 5));
    const auto checked = request(server.port(), "POST", "/api/targets/" + id + "/diagnose");
    EXPECT_TRUE(checked.find("DNS lookup") != std::string::npos || checked.find("No answer from localhost") != std::string::npos)
        << checked;

    const auto page = request(server.port(), "GET", "/");
    EXPECT_NE(page.find("PulseNet dashboard"), std::string::npos);

    const auto outside = request(server.port(), "GET", "/../support.hpp");
    EXPECT_NE(outside.find("\n400\n"), std::string::npos) << outside;

    server.stop();
    std::filesystem::remove_all(directory);
}
