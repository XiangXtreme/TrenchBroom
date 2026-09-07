/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <optional>

namespace tb::ui::automation
{

inline constexpr auto CurrentIrSchemaVersion = 1;

struct AutomationIrParseResult
{
  std::optional<QJsonObject> ir;
  QJsonArray warnings;
  QString error;
};

/**
 * Read and validate an IR JSON document from an absolute local file.
 *
 * File transport is shared by MCP and Python adapters, so neither adapter can
 * silently diverge on path, size, JSON, or schema validation.
 */
AutomationIrParseResult parseAutomationIrFile(const QString& path);
AutomationIrParseResult parseAutomationIr(const QJsonObject& request);
QString canonicalAutomationIrHash(const QJsonObject& ir);
QJsonObject previewAutomationIr(const QJsonObject& ir);

} // namespace tb::ui::automation
