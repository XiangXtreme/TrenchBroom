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

#include "mcp/McpBridgeConfig.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

namespace tb::mcp
{
namespace
{

QString currentUserName()
{
  auto userName = qEnvironmentVariable("USERNAME");
  if (userName.isEmpty())
  {
    userName = qEnvironmentVariable("USER");
  }
  if (userName.isEmpty())
  {
    userName = QDir::home().dirName();
  }
  if (userName.isEmpty())
  {
    userName = "user";
  }

  userName.replace(QRegularExpression{"[^A-Za-z0-9_.-]"}, "_");
  return userName;
}

bool ensureParentDirectory(const QString& filePath, QString* error)
{
  const auto parentDir = QFileInfo{filePath}.absoluteDir();
  if (parentDir.exists())
  {
    return true;
  }

  if (!QDir{}.mkpath(parentDir.absolutePath()))
  {
    if (error)
    {
      *error = QString{"Could not create MCP config directory: %1"}.arg(
        parentDir.absolutePath());
    }
    return false;
  }

  return true;
}

std::optional<QJsonObject> readConfigJson(const QString& filePath, QString* error)
{
  auto file = QFile{filePath};
  if (!file.open(QIODevice::ReadOnly))
  {
    if (error)
    {
      *error = QString{"Could not open MCP config: %1"}.arg(file.errorString());
    }
    return std::nullopt;
  }

  auto parseError = QJsonParseError{};
  const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject())
  {
    if (error)
    {
      *error = QString{"Could not parse MCP config: %1"}.arg(parseError.errorString());
    }
    return std::nullopt;
  }

  return document.object();
}

} // namespace

QString defaultConfigDirectory()
{
  const auto appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  const auto root = appData.isEmpty() ? QDir::homePath() + "/.trenchbroom" : appData;
  return QDir{root}.filePath("MCP");
}

QString defaultConfigPath()
{
  return QDir{defaultConfigDirectory()}.filePath("config.json");
}

McpBridgeConfig defaultBridgeConfig()
{
  return McpBridgeConfig{
    QString{"trenchbroom-mcp-%1"}.arg(currentUserName()),
    McpMode::Off,
    true,
    "127.0.0.1",
    37666,
    McpToolProfile::Modeling,
    2,
  };
}

QJsonObject toJson(const McpBridgeConfig& config)
{
  return QJsonObject{
    {"pipeName", config.pipeName},
    {"mode", modeName(config.mode)},
    {"httpEnabled", config.httpEnabled},
    {"httpHost", config.httpHost},
    {"httpPort", int(config.httpPort)},
    {"toolProfile", toolProfileName(config.toolProfile)},
    {"configVersion", config.configVersion},
  };
}

std::optional<McpBridgeConfig> bridgeConfigFromJson(
  const QJsonObject& json, QString* error)
{
  const auto pipeName = json.value("pipeName");
  if (!pipeName.isString() || pipeName.toString().trimmed().isEmpty())
  {
    if (error)
    {
      *error = "MCP pipeName is missing or empty";
    }
    return std::nullopt;
  }

  const auto modeValue = json.value("mode");
  if (!modeValue.isString())
  {
    if (error)
    {
      *error = "MCP mode is missing or not a string";
    }
    return std::nullopt;
  }

  const auto mode = parseMode(modeValue.toString());
  if (!mode)
  {
    if (error)
    {
      *error = "MCP mode is unknown";
    }
    return std::nullopt;
  }

  auto httpEnabled = true;
  const auto httpEnabledValue = json.value("httpEnabled");
  if (!httpEnabledValue.isUndefined())
  {
    if (!httpEnabledValue.isBool())
    {
      if (error)
      {
        *error = "MCP httpEnabled must be a bool";
      }
      return std::nullopt;
    }
    httpEnabled = httpEnabledValue.toBool();
  }

  auto httpHost = QString{"127.0.0.1"};
  const auto httpHostValue = json.value("httpHost");
  if (!httpHostValue.isUndefined())
  {
    if (!httpHostValue.isString() || httpHostValue.toString().trimmed().isEmpty())
    {
      if (error)
      {
        *error = "MCP httpHost must not be empty";
      }
      return std::nullopt;
    }
    httpHost = httpHostValue.toString().trimmed();
  }

  auto httpPort = quint16{37666};
  const auto httpPortValue = json.value("httpPort");
  if (!httpPortValue.isUndefined())
  {
    if (!httpPortValue.isDouble())
    {
      if (error)
      {
        *error = "MCP httpPort must be a number";
      }
      return std::nullopt;
    }
    const auto port = httpPortValue.toInt();
    if (port <= 0 || port > 65535)
    {
      if (error)
      {
        *error = "MCP httpPort must be between 1 and 65535";
      }
      return std::nullopt;
    }
    httpPort = quint16(port);
  }

  auto toolProfile = McpToolProfile::Modeling;
  const auto toolProfileValue = json.value("toolProfile");
  if (!toolProfileValue.isUndefined())
  {
    if (!toolProfileValue.isString())
    {
      if (error)
      {
        *error = "MCP toolProfile must be a string";
      }
      return std::nullopt;
    }
    const auto parsedProfile = parseToolProfile(toolProfileValue.toString());
    if (!parsedProfile)
    {
      if (error)
      {
        *error = "MCP toolProfile is unknown";
      }
      return std::nullopt;
    }
    toolProfile = *parsedProfile;
  }

  auto configVersion = 1;
  const auto configVersionValue = json.value("configVersion");
  if (!configVersionValue.isUndefined())
  {
    if (!configVersionValue.isDouble() || configVersionValue.toInt() != 2)
    {
      if (error)
      {
        *error = "MCP configVersion is unsupported";
      }
      return std::nullopt;
    }
    configVersion = 2;
  }

  return McpBridgeConfig{
    pipeName.toString(),
    *mode,
    httpEnabled,
    httpHost,
    httpPort,
    toolProfile,
    configVersion,
  };
}

std::optional<McpBridgeConfig> readBridgeConfig(const QString& filePath, QString* error)
{
  const auto json = readConfigJson(filePath, error);
  if (!json)
  {
    return std::nullopt;
  }

  return bridgeConfigFromJson(*json, error);
}

bool writeBridgeConfig(
  const McpBridgeConfig& config, const QString& filePath, QString* error)
{
  if (!ensureParentDirectory(filePath, error))
  {
    return false;
  }

  auto file = QSaveFile{filePath};
  if (!file.open(QIODevice::WriteOnly))
  {
    if (error)
    {
      *error = QString{"Could not write MCP config: %1"}.arg(file.errorString());
    }
    return false;
  }

  file.write(QJsonDocument{toJson(config)}.toJson(QJsonDocument::Indented));
  if (!file.commit())
  {
    if (error)
    {
      *error = QString{"Could not commit MCP config: %1"}.arg(file.errorString());
    }
    return false;
  }

  if (!QFile::setPermissions(filePath, QFileDevice::ReadOwner | QFileDevice::WriteOwner))
  {
    qWarning().noquote()
      << QString{"Could not restrict MCP config permissions to the current user: %1"}.arg(
           filePath);
  }

  return true;
}

std::optional<McpBridgeConfig> readOrCreateBridgeConfig(
  const QString& filePath, QString* error)
{
  if (QFileInfo::exists(filePath))
  {
    const auto json = readConfigJson(filePath, error);
    if (!json)
    {
      return std::nullopt;
    }

    const auto config = bridgeConfigFromJson(*json, error);
    if (!config)
    {
      return std::nullopt;
    }

    if (json->contains("token") || config->configVersion < 2)
    {
      auto migrated = *config;
      // A configuration from before trusted Python existed must be re-enabled explicitly.
      migrated.mode = McpMode::Off;
      migrated.configVersion = 2;
      auto migrationError = QString{};
      if (!writeBridgeConfig(migrated, filePath, &migrationError))
      {
        qWarning().noquote() << QString{"Could not migrate MCP config: %1"}.arg(
          migrationError);
      }
      return migrated;
    }
    return config;
  }

  auto config = defaultBridgeConfig();
  if (!writeBridgeConfig(config, filePath, error))
  {
    return std::nullopt;
  }
  return config;
}

} // namespace tb::mcp
