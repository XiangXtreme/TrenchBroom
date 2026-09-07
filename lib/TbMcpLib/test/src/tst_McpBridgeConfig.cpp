/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "mcp/McpBridgeConfig.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::mcp
{

TEST_CASE("McpBridgeConfig")
{
  SECTION("default config is disabled and loopback only")
  {
    const auto config = defaultBridgeConfig();

    CHECK(config.pipeName.startsWith("trenchbroom-mcp-"));
    CHECK(config.mode == McpMode::Off);
    CHECK(config.httpEnabled);
    CHECK(config.httpHost == "127.0.0.1");
    CHECK(config.httpPort == 37666);
    CHECK(config.toolProfile == McpToolProfile::Modeling);
  }

  SECTION("json roundtrip")
  {
    const auto config = McpBridgeConfig{
      "test-pipe", McpMode::ReadOnly, true, "localhost", 37667, McpToolProfile::Full};

    const auto parsed = bridgeConfigFromJson(toJson(config));

    REQUIRE(parsed);
    CHECK(parsed->pipeName == config.pipeName);
    CHECK(parsed->mode == config.mode);
    CHECK(parsed->httpEnabled == config.httpEnabled);
    CHECK(parsed->httpHost == config.httpHost);
    CHECK(parsed->httpPort == config.httpPort);
    CHECK(parsed->toolProfile == config.toolProfile);
    CHECK(parsed->configVersion == 2);
  }

  SECTION("legacy token is ignored and omitted when rewritten")
  {
    const auto parsed = bridgeConfigFromJson(QJsonObject{
      {"pipeName", "test-pipe"},
      {"token", "secret-token"},
      {"mode", "ReadOnly"},
    });

    REQUIRE(parsed);
    CHECK_FALSE(toJson(*parsed).contains("token"));
    CHECK(parsed->httpEnabled);
    CHECK(parsed->httpHost == "127.0.0.1");
    CHECK(parsed->httpPort == 37666);
    CHECK(parsed->toolProfile == McpToolProfile::Modeling);
  }

  SECTION("reads tool profile")
  {
    const auto parsed = bridgeConfigFromJson(QJsonObject{
      {"pipeName", "test-pipe"},
      {"mode", "ReadOnly"},
      {"toolProfile", "Core"},
    });

    REQUIRE(parsed);
    CHECK(parsed->toolProfile == McpToolProfile::Core);

    const auto modeling = bridgeConfigFromJson(QJsonObject{
      {"pipeName", "test-pipe"},
      {"mode", "ReadOnly"},
      {"toolProfile", "Modeling"},
    });
    REQUIRE(modeling);
    CHECK(modeling->toolProfile == McpToolProfile::Modeling);

    const auto balanced = bridgeConfigFromJson(QJsonObject{
      {"pipeName", "test-pipe"},
      {"mode", "ReadOnly"},
      {"toolProfile", "Balanced"},
    });
    REQUIRE(balanced);
    CHECK(balanced->toolProfile == McpToolProfile::Modeling);
    CHECK(toJson(*balanced).value("toolProfile").toString() == "Modeling");
  }

  SECTION("rejects invalid json")
  {
    auto error = QString{};

    CHECK(!bridgeConfigFromJson(QJsonObject{}, &error));
    CHECK(error.contains("pipeName"));

    CHECK(!bridgeConfigFromJson(
      QJsonObject{
        {"pipeName", "test-pipe"},
        {"mode", "ReadOnly"},
        {"toolProfile", "Tiny"},
      },
      &error));
    CHECK(error.contains("toolProfile"));
  }

  SECTION("read or create config")
  {
    auto tempDir = QTemporaryDir{};
    REQUIRE(tempDir.isValid());

    const auto path = QDir{tempDir.path()}.filePath("MCP/config.json");
    auto error = QString{};
    const auto created = readOrCreateBridgeConfig(path, &error);

    REQUIRE(created);
    CHECK(error.isEmpty());
    CHECK(created->mode == McpMode::Off);

    auto loaded = readBridgeConfig(path, &error);
    REQUIRE(loaded);
    CHECK(loaded->pipeName == created->pipeName);
    CHECK(loaded->mode == created->mode);
    CHECK_FALSE(toJson(*loaded).contains("token"));
  }

  SECTION("read or create removes a legacy token from disk")
  {
    auto tempDir = QTemporaryDir{};
    REQUIRE(tempDir.isValid());

    const auto path = QDir{tempDir.path()}.filePath("config.json");
    auto file = QFile{path};
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument{
      QJsonObject{
        {"pipeName", "test-pipe"},
        {"token", "legacy-secret"},
        {"mode", "ReadOnly"},
      }}.toJson());
    file.close();

    auto error = QString{};
    const auto config = readOrCreateBridgeConfig(path, &error);
    REQUIRE(config);
    CHECK(error.isEmpty());

    REQUIRE(file.open(QIODevice::ReadOnly));
    const auto migrated = QJsonDocument::fromJson(file.readAll()).object();
    CHECK_FALSE(migrated.contains("token"));
    CHECK(migrated.value("mode").toString() == "Off");
    CHECK(migrated.value("configVersion").toInt() == 2);
  }

  SECTION("a failed configuration migration remains disabled for this process")
  {
    auto tempDir = QTemporaryDir{};
    REQUIRE(tempDir.isValid());

    const auto path = QDir{tempDir.path()}.filePath("config.json");
    auto file = QFile{path};
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument{
      QJsonObject{
        {"pipeName", "test-pipe"},
        {"token", "legacy-secret"},
        {"mode", "ReadOnly"},
      }}.toJson());
    file.close();
    REQUIRE(QFile::setPermissions(path, QFileDevice::ReadOwner));

    auto error = QString{};
    const auto config = readOrCreateBridgeConfig(path, &error);
    REQUIRE(config);
    CHECK(error.isEmpty());
    CHECK(config->pipeName == "test-pipe");
    CHECK(config->mode == McpMode::Off);

    REQUIRE(
      QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
  }

  SECTION("upgrades an unversioned trusted-Python-era configuration to Off")
  {
    auto tempDir = QTemporaryDir{};
    REQUIRE(tempDir.isValid());

    const auto path = QDir{tempDir.path()}.filePath("config.json");
    auto file = QFile{path};
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument{
      QJsonObject{
        {"pipeName", "test-pipe"},
        {"mode", "Edit"},
        {"httpEnabled", false},
        {"httpHost", "127.0.0.1"},
        {"httpPort", 37700},
      }}.toJson());
    file.close();

    auto error = QString{};
    const auto upgraded = readOrCreateBridgeConfig(path, &error);
    REQUIRE(upgraded);
    CHECK(error.isEmpty());
    CHECK(upgraded->mode == McpMode::Off);
    CHECK_FALSE(upgraded->httpEnabled);
    CHECK(upgraded->httpPort == 37700);
    CHECK(upgraded->configVersion == 2);
  }
}

} // namespace tb::mcp
