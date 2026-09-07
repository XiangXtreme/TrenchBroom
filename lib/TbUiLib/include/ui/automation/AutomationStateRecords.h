/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 */

#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace tb::ui::automation
{

/** Protocol-neutral automation metadata attached to a live map object. */
struct AutomationObjectMetadataRecord
{
  QString objectId;
  QString documentFingerprint;
  QJsonObject metadata;
  bool stale = false;
};

/** Protocol-neutral state for a generated or recovered map module. */
struct AutomationModuleRecord
{
  QString moduleId;
  QString documentFingerprint;
  QStringList objectIds;
  QStringList operationIds;
  QJsonObject metadata;
  int revision = 0;
  QString activeOperationId;
  QString contentHash;
  QJsonObject qualityPolicy;
};

/** A bounded preview of an IR file. The owner determines retention policy. */
struct AutomationIrPreviewRecord
{
  QString previewId;
  QString sourcePath;
  QString irHash;
  QString documentFingerprint;
  QString activeDocumentPath;
  qint64 createdAtMs = 0;
  qint64 expiresAtMs = 0;
  QJsonObject preview;
};

} // namespace tb::ui::automation
