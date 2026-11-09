#pragma once

#include "pulsenet/monitoring/scheduler.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace pulsenet {

class TargetRepository;
class ResultRepository;
class IncidentRepository;

struct HttpServerOptions {
    std::string host{"127.0.0.1"};
    unsigned short port{8080};
    std::filesystem::path web_root;
};

// Dashboard API and WebSocket fan-out. Connections use shared_ptr because a
// browser socket outlives the function that accepted it. Probe sessions do not.
class HttpServer {
public:
    HttpServer(HttpServerOptions options,
               TargetRepository& targets,
               ResultRepository& results,
               IncidentRepository& incidents,
               Scheduler& scheduler);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    void start();
    void stop();
    [[nodiscard]] unsigned short port() const;
    void publish(const SchedulerReport& report);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace pulsenet
