#pragma once

#if !defined(_WIN32)
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#endif

#include <stdlib.h>

#include <filesystem>
#include <optional>
#include <random>
#include <string>

namespace pulsenet::test_support {

class ScopedEnv {
public:
    ScopedEnv(const char* key, const char* value) : key_(key) {
        if (const char* current = std::getenv(key)) {
            previous_ = current;
        }
        assign(value);
    }

    ~ScopedEnv() { assign(previous_ ? previous_->c_str() : nullptr); }

    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
    void assign(const char* value) {
#ifdef _WIN32
        _putenv_s(key_.c_str(), value == nullptr ? "" : value);
#else
        if (value == nullptr) {
            ::unsetenv(key_.c_str());
        } else {
            ::setenv(key_.c_str(), value, 1);
        }
#endif
    }

    std::string key_;
    std::optional<std::string> previous_;
};

inline std::filesystem::path make_temp_directory() {
    std::random_device device;
    std::mt19937 generator(device());
    std::uniform_int_distribution<int> distribution(0, 15);
    std::string name = "pulsenet-";
    constexpr char hex[] = "0123456789abcdef";
    for (int index = 0; index < 16; ++index) {
        name.push_back(hex[distribution(generator)]);
    }
    const auto directory = std::filesystem::temp_directory_path() / name;
    std::filesystem::create_directories(directory);
    return directory;
}

}  // namespace pulsenet::test_support
