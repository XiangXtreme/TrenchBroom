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

#pragma once

#include <QJsonObject>
#include <QString>

#include "mcp/McpMode.h"

#include <optional>

namespace tb::mcp
{

struct McpBridgeConfig
{
  QString pipeName;
  McpMode mode = McpMode::Off;
  bool httpEnabled = true;
  QString httpHost = "127.0.0.1";
  quint16 httpPort = 37666;
  int configVersion = 2;
};

QString defaultConfigDirectory();
QString defaultConfigPath();
McpBridgeConfig defaultBridgeConfig();

QJsonObject toJson(const McpBridgeConfig& config);
std::optional<McpBridgeConfig> bridgeConfigFromJson(
  const QJsonObject& json, QString* error = nullptr);

std::optional<McpBridgeConfig> readBridgeConfig(
  const QString& filePath, QString* error = nullptr);
bool writeBridgeConfig(
  const McpBridgeConfig& config, const QString& filePath, QString* error = nullptr);
std::optional<McpBridgeConfig> readOrCreateBridgeConfig(
  const QString& filePath, QString* error = nullptr);

} // namespace tb::mcp
