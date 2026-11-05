#include "pulsenet/config.hpp"
#include "pulsenet/database/database.hpp"
#include "pulsenet/database/migrator.hpp"
#include "pulsenet/database/result_repository.hpp"
#include "pulsenet/database/target_repository.hpp"
#include "pulsenet/models/monitor_target.hpp"
#include "pulsenet/monitoring/check_service.hpp"
#include "pulsenet/monitoring/probe_engine.hpp"
#include "pulsenet/utilities/logger.hpp"

#include <chrono>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pulsenet {
namespace {

constexpr std::string_view kDemoName = "Google DNS";
constexpr std::string_view kDemoHost = "8.8.8.8";

void print_demo(const MonitorTarget& target) {
    std::cout << "demo_id: " << target.id << '\n';
    std::cout << "demo_name: " << target.name << '\n';
    std::cout << "demo_host: " << target.host << '\n';
    std::cout << "demo_port: " << (target.port ? std::to_string(*target.port) : std::string{"-"}) << '\n';
    std::cout << "demo_type: " << to_string(target.type) << '\n';
    std::cout << "demo_enabled: " << (target.enabled ? "true" : "false") << '\n';
}

void print_check(const MonitorTarget& target, const MonitorResult& result) {
    std::cout << "check: " << target.name << " status=" << to_string(result.status) << " latency_ms="
              << (result.response_time ? std::to_string(result.response_time->count()) : std::string{"-"})
              << " http=" << (result.http_status ? std::to_string(*result.http_status) : std::string{"-"}) << '\n';
}

std::optional<MonitorTarget> ensure_demo_target(TargetRepository& targets) {
    const auto listed = targets.list_all();
    if (!listed) {
        log_error(listed.error().message);
        return std::nullopt;
    }

    for (const auto& target : listed.value()) {
        if (target.name == kDemoName && target.host == kDemoHost) {
            log_info("demo target already present id=" + std::to_string(target.id));
            return target;
        }
    }

    MonitorTarget target;
    target.name = std::string{kDemoName};
    target.host = std::string{kDemoHost};
    target.port = 53;
    target.type = MonitorType::Tcp;
    target.interval = std::chrono::seconds{30};
    target.timeout = std::chrono::milliseconds{2000};
    target.enabled = true;
    target.latency_threshold = std::chrono::milliseconds{100};
    target.failure_threshold = 3;

    const auto inserted = targets.insert(target);
    if (!inserted) {
        log_error(inserted.error().message);
        return std::nullopt;
    }
    const auto stored = targets.find_by_id(inserted.value());
    if (!stored) {
        log_error(stored.error().message);
        return std::nullopt;
    }
    log_info("demo target created id=" + std::to_string(stored.value().id));
    return stored.value();
}

int run(const std::vector<std::string>& args) {
    const Config config = load_config(args);
    if (config.show_help) {
        std::cout << usage_text();
        return 0;
    }
    if (config.show_version) {
        std::cout << "PulseNet " << PULSENET_VERSION << '\n';
        return 0;
    }

    Logger::instance().set_level(config.log_level);
    log_info(std::string("PulseNet ") + PULSENET_VERSION + " starting");

    Database database(config.database_path);
    Migrator{database}.apply();

    TargetRepository targets(database);
    std::optional<MonitorTarget> demo;
    if (config.demo) {
        demo = ensure_demo_target(targets);
        if (!demo) {
            return 1;
        }
    }

    const auto listed = targets.list_all();
    if (!listed) {
        log_error(listed.error().message);
        return 1;
    }

    std::optional<std::vector<MonitorResult>> checks;
    if (config.check) {
        ResultRepository results(database);
        ProbeEngine engine;
        CheckService service(results, engine);
        auto round = service.run_round(listed.value());
        if (!round) {
            log_error(round.error().message);
            return 1;
        }
        checks = std::move(round.value());
    }

    std::cout << "PulseNet " << PULSENET_VERSION << '\n';
    std::cout << "status: ready\n";
    std::cout << "database: " << config.database_path.string() << '\n';
    std::cout << "schema_version: " << database.schema_version() << '\n';
    std::cout << "targets: " << listed.value().size() << '\n';
    std::cout << "monitoring: " << (config.check ? "round complete" : "not started") << '\n';
    if (demo) {
        print_demo(*demo);
    }
    if (checks) {
        for (std::size_t index = 0; index < checks->size() && index < listed.value().size(); ++index) {
            print_check(listed.value()[index], (*checks)[index]);
        }
    }
    log_info("startup complete");
    return 0;
}

}  // namespace
}  // namespace pulsenet

int main(int argc, char** argv) {
    try {
        return pulsenet::run(std::vector<std::string>(argv, argv + argc));
    } catch (const std::exception& ex) {
        std::cerr << "pulsenet: " << ex.what() << '\n';
        return 1;
    }
}
