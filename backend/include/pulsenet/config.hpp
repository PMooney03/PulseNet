#pragma once

#include "pulsenet/utilities/logger.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace pulsenet {

class ConfigError : public std::runtime_error {
public:
    explicit ConfigError(const std::string& message) : std::runtime_error(message) {}
};

struct Config {
    std::filesystem::path database_path{"data/pulsenet.db"};
    LogLevel log_level{LogLevel::Info};
    bool demo{false};
    bool check{false};
    bool watch{false};
    bool serve{false};
    std::string listen{"127.0.0.1:8080"};
    std::filesystem::path web_root;
    std::string teams_webhook;
    bool show_help{false};
    bool show_version{false};
};

struct ListenAddress {
    std::string host{"127.0.0.1"};
    unsigned short port{8080};
};

[[nodiscard]] ListenAddress parse_listen_address(std::string_view text);

// Precedence, lowest to highest: built-in defaults, JSON file, environment,
// command-line flags. An empty environment variable is ignored.
[[nodiscard]] Config load_config(const std::vector<std::string>& args);
[[nodiscard]] std::string_view usage_text();

}  // namespace pulsenet
