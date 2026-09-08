#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>

#include "mcp/McpBridgeConfig.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::mcp
{

TEST_CASE("McpBridgeConfig")
{
  SECTION("defaults to a disabled loopback bridge")
  {
    const auto config = defaultBridgeConfig();
    CHECK(config.mode == McpMode::Off);
    CHECK(config.httpEnabled);
    CHECK(config.httpHost == "127.0.0.1");
    CHECK(config.httpPort == 37666);
    CHECK(config.configVersion == 2);
  }

  SECTION("drops retired profile fields when a legacy config is rewritten")
  {
    auto directory = QTemporaryDir{};
    REQUIRE(directory.isValid());
    const auto path = QDir{directory.path()}.filePath("config.json");
    auto file = QFile{path};
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument{
      QJsonObject{
        {"pipeName", "test-pipe"},
        {"mode", "Edit"},
        {"toolProfile", "Full"},
      }}.toJson());
    file.close();

    auto error = QString{};
    const auto config = readOrCreateBridgeConfig(path, &error);
    REQUIRE(config);
    CHECK(error.isEmpty());
    CHECK(config->mode == McpMode::Off);
    REQUIRE(file.open(QIODevice::ReadOnly));
    const auto rewritten = QJsonDocument::fromJson(file.readAll()).object();
    CHECK_FALSE(rewritten.contains("toolProfile"));
    CHECK(rewritten.value("mode").toString() == "Off");
  }

  SECTION("retires elevated configurations to Off")
  {
    auto directory = QTemporaryDir{};
    REQUIRE(directory.isValid());
    const auto path = QDir{directory.path()}.filePath("config.json");
    auto file = QFile{path};
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument{
      QJsonObject{
        {"pipeName", "test-pipe"},
        {"mode", "Danger"},
        {"configVersion", 2},
      }}.toJson());
    file.close();

    auto error = QString{};
    const auto config = readOrCreateBridgeConfig(path, &error);
    REQUIRE(config);
    CHECK(error.isEmpty());
    CHECK(config->mode == McpMode::Off);
    REQUIRE(file.open(QIODevice::ReadOnly));
    const auto rewritten = QJsonDocument::fromJson(file.readAll()).object();
    CHECK(rewritten.value("mode").toString() == "Off");
  }
}

} // namespace tb::mcp
