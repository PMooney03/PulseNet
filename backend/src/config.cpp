#include "pulsenet/config.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <optional>
#include <utility>

namespace pulsenet {
namespace {

std::string require_value(const std::vector<std::string>& args, std::size_t& index, std::string_view flag) {
    if (index + 1 >= args.size()) {
        throw ConfigError(std::string(flag) + " requires a value");
    }
    ++index;
    return args[index];
}

void apply_file(Config& config, const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        throw ConfigError("cannot read config file: " + path.string());
    }

    try {
        nlohmann::json document;
        input >> document;
        if (!document.is_object()) {
            throw ConfigError("config file must be a JSON object");
        }
        if (document.contains("database_path")) {
            if (!document["database_path"].is_string()) {
                throw ConfigError("database_path must be a string");
            }
            config.database_path = document["database_path"].get<std::string>();
        }
        if (document.contains("log_level")) {
            if (!document["log_level"].is_string()) {
                throw ConfigError("log_level must be a string");
            }
            const auto level = log_level_from_string(document["log_level"].get<std::string>());
            if (!level) {
                throw ConfigError("invalid log level in config file");
            }
            config.log_level = *level;
        }
        if (document.contains("teams_webhook")) {
            if (!document["teams_webhook"].is_string()) {
                throw ConfigError("teams_webhook must be a string");
            }
            config.teams_webhook = document["teams_webhook"].get<std::string>();
        }
    } catch (const ConfigError&) {
        throw;
    } catch (const std::exception& ex) {
        throw ConfigError(std::string("invalid config file: ") + ex.what());
    }
}

void apply_environment(Config& config) {
    if (const char* path = std::getenv("PULSENET_DATABASE_PATH")) {
        if (path[0] != '\0') {
            config.database_path = path;
        }
    }
    if (const char* level = std::getenv("PULSENET_LOG_LEVEL")) {
        if (level[0] == '\0') {
            return;
        }
        const auto parsed = log_level_from_string(level);
        if (!parsed) {
            throw ConfigError(std::string("invalid PULSENET_LOG_LEVEL: ") + level);
        }
        config.log_level = *parsed;
    }
    if (const char* webhook = std::getenv("PULSENET_TEAMS_WEBHOOK")) {
        if (webhook[0] != '\0') {
            config.teams_webhook = webhook;
        }
    }
}

void apply_flags(Config& config, const std::vector<std::string>& args) {
    for (std::size_t index = 1; index < args.size(); ++index) {
        const std::string& arg = args[index];
        if (arg == "--help" || arg == "-h") {
            config.show_help = true;
        } else if (arg == "--version") {
            config.show_version = true;
        } else if (arg == "--demo") {
            config.demo = true;
        } else if (arg == "--check") {
            config.check = true;
        } else if (arg == "--watch") {
            config.watch = true;
        } else if (arg == "--serve") {
            config.serve = true;
            config.watch = true;
        } else if (arg == "--listen") {
            config.listen = require_value(args, index, arg);
        } else if (arg == "--web") {
            config.web_root = require_value(args, index, arg);
        } else if (arg == "--teams-webhook") {
            config.teams_webhook = require_value(args, index, arg);
        } else if (arg == "--db") {
            config.database_path = require_value(args, index, arg);
        } else if (arg == "--log-level") {
            const auto text = require_value(args, index, arg);
            const auto level = log_level_from_string(text);
            if (!level) {
                throw ConfigError("invalid log level: " + text);
            }
            config.log_level = *level;
        } else if (arg == "--config") {
            require_value(args, index, arg);
        } else {
            throw ConfigError("unknown argument: " + arg);
        }
    }
}

std::optional<std::filesystem::path> find_config_path(const std::vector<std::string>& args) {
    for (std::size_t index = 1; index < args.size(); ++index) {
        if (args[index] == "--config") {
            return require_value(args, index, "--config");
        }
    }
    return std::nullopt;
}

}  // namespace

std::string_view usage_text() {
    return "PulseNet " PULSENET_VERSION
           "\n"
           "Local network monitoring backend\n"
           "\n"
           "Usage:\n"
           "  pulsenet [--db PATH] [--log-level LEVEL] [--config FILE] [--demo] [--check]\n"
           "  pulsenet --serve [--listen HOST:PORT] [--web DIR] [--demo]\n"
           "\n"
           "Options:\n"
           "  --db PATH          SQLite database file (default: data/pulsenet.db)\n"
           "  --log-level LEVEL  trace, debug, info, warn, or error (default: info)\n"
           "  --config FILE      JSON config file\n"
           "  --demo             Create the sample Google DNS target if it is missing\n"
           "  --check            Run one TCP, HTTP, HTTPS, or DNS check per target, then exit\n"
           "  --watch            Keep checking targets on their intervals until Ctrl+C\n"
           "  --serve            Run the scheduler, HTTP API, and dashboard until Ctrl+C\n"
           "  --listen HOST:PORT Address for --serve (default: 127.0.0.1:8080)\n"
           "  --web DIR          Dashboard files to serve (the frontend build)\n"
           "  --teams-webhook URL  Teams Workflows webhook for critical incidents\n"
           "  --version          Print the version and exit\n"
           "  --help             Show this help and exit\n"
           "\n"
           "Environment variables are overridden by flags:\n"
           "  PULSENET_DATABASE_PATH\n"
           "  PULSENET_LOG_LEVEL\n"
           "  PULSENET_TEAMS_WEBHOOK\n"
           "\n"
           "Phase 1 stores monitors in SQLite.\n"
           "--check measures every stored target once.\n"
           "--serve keeps checking and opens the dashboard.\n";
}

ListenAddress parse_listen_address(const std::string_view text) {
    const auto colon = text.rfind(':');
    if (colon == std::string_view::npos || colon == 0 || colon + 1 >= text.size()) {
        throw ConfigError("listen address must be host:port");
    }
    const auto host = std::string(text.substr(0, colon));
    const auto port_text = std::string(text.substr(colon + 1));
    int port = 0;
    try {
        std::size_t consumed = 0;
        port = std::stoi(port_text, &consumed, 10);
        if (consumed != port_text.size() || port < 1 || port > 65535) {
            throw ConfigError("invalid listen port");
        }
    } catch (const ConfigError&) {
        throw;
    } catch (const std::exception&) {
        throw ConfigError("invalid listen port");
    }
    return ListenAddress{host, static_cast<unsigned short>(port)};
}

Config load_config(const std::vector<std::string>& args) {
    Config config;
    if (const auto file = find_config_path(args)) {
        apply_file(config, *file);
    }
    apply_environment(config);
    apply_flags(config, args);
    if (config.database_path.empty()) {
        throw ConfigError("database path is empty");
    }
    if (!config.teams_webhook.empty() && !config.teams_webhook.starts_with("https://")) {
        throw ConfigError("teams webhook must be an https URL");
    }
    return config;
}

}  // namespace pulsenet
