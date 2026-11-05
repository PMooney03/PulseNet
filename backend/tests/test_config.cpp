#include "support.hpp"

#include "pulsenet/config.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

using pulsenet::ConfigError;
using pulsenet::LogLevel;
using pulsenet::load_config;
using pulsenet::test_support::ScopedEnv;
using pulsenet::test_support::make_temp_directory;
using pulsenet::usage_text;

class ConfigTest : public ::testing::Test {
protected:
    void SetUp() override { directory_ = make_temp_directory(); }
    void TearDown() override { std::filesystem::remove_all(directory_); }

    std::filesystem::path write_config(std::string_view json) const {
        const auto path = directory_ / "pulsenet.json";
        std::ofstream output(path);
        output << json;
        return path;
    }

    std::filesystem::path directory_;
};

}  // namespace

TEST_F(ConfigTest, DefaultsApplyWhenEnvironmentIsUnset) {
    const ScopedEnv database("PULSENET_DATABASE_PATH", nullptr);
    const ScopedEnv level("PULSENET_LOG_LEVEL", nullptr);
    const auto config = load_config({"pulsenet"});
    EXPECT_EQ(config.database_path, std::filesystem::path{"data/pulsenet.db"});
    EXPECT_EQ(config.log_level, LogLevel::Info);
    EXPECT_FALSE(config.demo);
}

TEST_F(ConfigTest, EnvironmentOverridesFileAndFlagsOverrideEnvironment) {
    const ScopedEnv database("PULSENET_DATABASE_PATH", "from-env.db");
    const ScopedEnv level("PULSENET_LOG_LEVEL", "error");
    const auto path = write_config(R"({"database_path":"from-file.db","log_level":"warning"})");

    const auto from_env = load_config({"pulsenet", "--config", path.string()});
    EXPECT_EQ(from_env.database_path, std::filesystem::path{"from-env.db"});
    EXPECT_EQ(from_env.log_level, LogLevel::Error);

    const auto from_flags =
        load_config({"pulsenet", "--config", path.string(), "--db", "from-argv.db", "--log-level", "debug", "--demo"});
    EXPECT_EQ(from_flags.database_path, std::filesystem::path{"from-argv.db"});
    EXPECT_EQ(from_flags.log_level, LogLevel::Debug);
    EXPECT_TRUE(from_flags.demo);
}

TEST_F(ConfigTest, FileIsUsedWhenEnvironmentIsUnset) {
    const ScopedEnv database("PULSENET_DATABASE_PATH", nullptr);
    const ScopedEnv level("PULSENET_LOG_LEVEL", nullptr);
    const auto path = write_config(R"({"database_path":"data/from-file.db","log_level":"warning"})");
    const auto config = load_config({"pulsenet", "--config", path.string(), "--help"});
    EXPECT_EQ(config.database_path, std::filesystem::path{"data/from-file.db"});
    EXPECT_EQ(config.log_level, LogLevel::Warn);
    EXPECT_TRUE(config.show_help);
}

TEST_F(ConfigTest, RejectsBadInput) {
    const ScopedEnv database("PULSENET_DATABASE_PATH", nullptr);
    const ScopedEnv level("PULSENET_LOG_LEVEL", nullptr);
    EXPECT_THROW(load_config({"pulsenet", "--nope"}), ConfigError);
    EXPECT_THROW(load_config({"pulsenet", "--db"}), ConfigError);
    EXPECT_THROW(load_config({"pulsenet", "--log-level", "loud"}), ConfigError);
    EXPECT_THROW(load_config({"pulsenet", "--config", (directory_ / "missing.json").string()}), ConfigError);
    const auto broken = write_config("{");
    EXPECT_THROW(load_config({"pulsenet", "--config", broken.string()}), ConfigError);
    const auto typed = write_config(R"({"database_path":12})");
    EXPECT_THROW(load_config({"pulsenet", "--config", typed.string()}), ConfigError);
}

TEST(UsageTest, HelpMentionsDemoAndDatabase) {
    const auto text = std::string{usage_text()};
    EXPECT_NE(text.find("--demo"), std::string::npos);
    EXPECT_NE(text.find("PULSENET_DATABASE_PATH"), std::string::npos);
    EXPECT_NE(text.find("Phase 1"), std::string::npos);
    EXPECT_NE(text.find("--check"), std::string::npos);
}
