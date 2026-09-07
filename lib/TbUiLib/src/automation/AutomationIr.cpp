/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#include "ui/automation/AutomationIr.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>

namespace tb::ui::automation
{
namespace
{

bool validateQualityPolicy(const QJsonObject& ir, QString& error)
{
  const auto value = ir.value("qualityPolicy");
  if (value.isUndefined() || value.isNull())
  {
    return true;
  }
  if (!value.isObject())
  {
    error = "qualityPolicy must be an object";
    return false;
  }
  const auto quality = value.toObject();
  const auto intent = quality.value("intent").toString("balanced").trimmed().toLower();
  if (intent != "draft" && intent != "balanced" && intent != "smooth")
  {
    error = "qualityPolicy.intent must be draft, balanced, or smooth";
    return false;
  }
  for (const auto& key :
       {"maxDirectionChangeDegrees", "maxSagitta", "maxSnapDisplacement"})
  {
    const auto limit = quality.value(key);
    if (
      !limit.isUndefined()
      && (!limit.isDouble() || !std::isfinite(limit.toDouble()) || limit.toDouble() <= 0.0))
    {
      error = QString{"qualityPolicy.%1 must be a positive finite number"}.arg(key);
      return false;
    }
  }
  return true;
}

bool validateAutomationIr(QJsonObject& ir, QString& error, QJsonArray& warnings)
{
  const auto schemaVersion = ir.value("schemaVersion");
  if (schemaVersion.isUndefined())
  {
    ir.insert("schemaVersion", CurrentIrSchemaVersion);
    warnings.push_back("legacyUnversionedIr");
  }
  else if (
    !schemaVersion.isDouble()
    || schemaVersion.toDouble() != static_cast<double>(schemaVersion.toInt()))
  {
    error = "IR schemaVersion must be an integer";
    return false;
  }
  else if (schemaVersion.toInt() < 1)
  {
    error = "IR schemaVersion must be at least 1";
    return false;
  }
  else if (schemaVersion.toInt() > CurrentIrSchemaVersion)
  {
    error = QString{"Unsupported IR schemaVersion %1; current version is %2"}.arg(
      schemaVersion.toInt(), CurrentIrSchemaVersion);
    return false;
  }

  if (!validateQualityPolicy(ir, error))
  {
    return false;
  }
  const auto applyMode = ir.value("applyMode").toString("create").trimmed().toLower();
  if (applyMode != "create" && applyMode != "replace_module")
  {
    error = "IR applyMode must be create or replace_module";
    return false;
  }
  ir.insert("applyMode", applyMode);
  if (
    ir.contains("requireMaterialAvailable")
    && !ir.value("requireMaterialAvailable").isBool())
  {
    error = "IR requireMaterialAvailable must be boolean";
    return false;
  }

  const auto operations = ir.value("operations");
  const auto entities = ir.value("entities");
  const auto hasOperations = !operations.isUndefined() && !operations.isNull();
  const auto hasEntities = !entities.isUndefined() && !entities.isNull();
  if (!hasOperations && !hasEntities)
  {
    error = "IR requires at least one operations or entities array";
    return false;
  }
  if ((hasOperations && !operations.isArray()) || (hasEntities && !entities.isArray()))
  {
    error =
      hasOperations ? "IR operations must be an array" : "IR entities must be an array";
    return false;
  }
  for (auto i = 0; i < operations.toArray().size(); ++i)
  {
    const auto operation = operations.toArray().at(i);
    if (
      !operation.isObject()
      || operation.toObject().value("type").toString().trimmed().isEmpty())
    {
      error = QString{"IR operations[%1] requires type on an object"}.arg(i);
      return false;
    }
  }
  for (auto i = 0; i < entities.toArray().size(); ++i)
  {
    const auto entity = entities.toArray().at(i);
    if (
      !entity.isObject()
      || entity.toObject().value("classname").toString().trimmed().isEmpty())
    {
      error = QString{"IR entities[%1] requires an object with classname"}.arg(i);
      return false;
    }
  }
  if (operations.toArray().isEmpty() && entities.toArray().isEmpty())
  {
    error = "IR operations/entities must not both be empty";
    return false;
  }
  return true;
}

int estimateOperationCount(const QJsonObject& operation)
{
  const auto type = operation.value("type").toString();
  if (type == "curved_corridor")
  {
    return std::max(1, operation.value("segments").toInt(12)) * 4;
  }
  if (type == "path_ribbon")
  {
    const auto points = operation.value("points3d").isArray()
                          ? operation.value("points3d").toArray()
                          : operation.value("points2d").toArray();
    return std::max(0, static_cast<int>(points.size()) - 1);
  }
  if (type == "arc_ramp" || type == "helical_ramp")
  {
    return std::max(1, operation.value("segments").toInt(12));
  }
  return 1;
}

} // namespace

AutomationIrParseResult parseAutomationIr(const QJsonObject& request)
{
  auto result = AutomationIrParseResult{};
  auto ir = request.value("ir").toObject();
  if (request.value("ir").isUndefined())
  {
    for (const auto& key :
         {"operations",
          "entities",
          "schemaVersion",
          "name",
          "moduleId",
          "defaultMetadata",
          "material",
          "grid",
          "qualityPolicy",
          "applyMode",
          "requireMaterialAvailable"})
    {
      if (request.contains(key))
      {
        ir.insert(key, request.value(key));
      }
    }
  }
  else if (!request.value("ir").isObject())
  {
    result.error = "IR field must be an object";
    return result;
  }
  if (request.value("qualityPolicy").isObject())
  {
    ir.insert("qualityPolicy", request.value("qualityPolicy"));
  }
  if (request.contains("applyMode"))
  {
    ir.insert("applyMode", request.value("applyMode"));
  }
  if (request.contains("requireMaterialAvailable"))
  {
    ir.insert("requireMaterialAvailable", request.value("requireMaterialAvailable"));
  }
  if (!validateAutomationIr(ir, result.error, result.warnings))
  {
    return result;
  }
  result.ir = std::move(ir);
  return result;
}

AutomationIrParseResult parseAutomationIrFile(const QString& path)
{
  auto result = AutomationIrParseResult{};
  const auto info = QFileInfo{path};
  if (!info.isAbsolute())
  {
    result.error = "file-based IR path must be absolute";
    return result;
  }
  if (!info.isFile() || !info.isReadable())
  {
    result.error = QString{"IR file is not readable: %1"}.arg(path);
    return result;
  }
  if (info.size() > 10 * 1024 * 1024)
  {
    result.error = "IR file is too large; maximum size is 10 MiB";
    return result;
  }

  auto file = QFile{path};
  if (!file.open(QIODevice::ReadOnly))
  {
    result.error = QString{"Could not open IR file: %1"}.arg(path);
    return result;
  }
  auto parseError = QJsonParseError{};
  const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject())
  {
    result.error =
      QString{"IR file must contain a JSON object: %1"}.arg(parseError.errorString());
    return result;
  }
  return parseAutomationIr(QJsonObject{{"ir", document.object()}});
}

QString canonicalAutomationIrHash(const QJsonObject& ir)
{
  return QString{"sha256:%1"}.arg(QString::fromLatin1(
    QCryptographicHash::hash(
      QJsonDocument{ir}.toJson(QJsonDocument::Compact), QCryptographicHash::Sha256)
      .toHex()));
}

QJsonObject previewAutomationIr(const QJsonObject& ir)
{
  const auto operations = ir.value("operations").toArray();
  auto estimatedBrushCount = 0;
  for (const auto& operation : operations)
  {
    estimatedBrushCount += estimateOperationCount(operation.toObject());
  }
  return QJsonObject{
    {"valid", true},
    {"schemaVersion", ir.value("schemaVersion").toInt(CurrentIrSchemaVersion)},
    {"operationCount", operations.size()},
    {"entityCount", ir.value("entities").toArray().size()},
    {"estimatedBrushCount", estimatedBrushCount},
    {"estimatedObjectCount", estimatedBrushCount + ir.value("entities").toArray().size()},
    {"moduleId", ir.value("moduleId").toString()},
    {"applyMode", ir.value("applyMode").toString("create")},
    {"irHash", canonicalAutomationIrHash(ir)},
  };
}

} // namespace tb::ui::automation
