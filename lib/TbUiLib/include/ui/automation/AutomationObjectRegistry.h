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
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <map>
#include <optional>

namespace tb::mdl
{
class Map;
class Node;
struct NodePath;
} // namespace tb::mdl

namespace tb::ui::automation
{

/**
 * Stable map object identity for automation adapters.
 *
 * This service deliberately knows nothing about MCP transport or Python. The
 * identifier format remains stable while transport adapters migrate, so
 * moving the service does not invalidate a live editor session.
 */
class AutomationObjectRegistry
{
public:
  struct ResolveResult
  {
    bool ok = false;
    QString objectId;
    QString legacyPathId;
    QString error;
    QJsonObject diagnostic;
  };

private:
  struct Record
  {
    QString stableId;
    QString legacyPathId;
    QString type;
    int documentEpoch = 0;
    QString documentFingerprint;
    QString creationFingerprint;
    quintptr nodeAddress = 0;
    QJsonObject summary;
  };

  mutable int m_documentEpoch = 1;
  mutable int m_nextSequence = 1;
  mutable quintptr m_currentMapAddress = 0;
  mutable quintptr m_currentWorldAddress = 0;
  mutable std::map<QString, Record> m_records;
  mutable std::map<QString, QString> m_legacyToStable;

public:
  void clear();
  size_t retainDocumentFingerprints(const QStringList& documentFingerprints);
  size_t recordCount() const;

  int documentEpoch(mdl::Map& map) const;
  QString documentFingerprint(mdl::Map& map) const;

  QString registerNode(mdl::Map& map, mdl::Node& node) const;
  QString externalIdForLegacy(mdl::Map& map, const QString& legacyPathId) const;

  ResolveResult resolveExternalId(mdl::Map& map, const QString& objectId) const;
  QJsonObject liveStateJson(
    mdl::Map& map,
    const QStringList& objectIds,
    bool undone,
    bool includeDiagnostics = false) const;

  std::optional<QJsonObject> internalizeParams(
    mdl::Map& map, const QJsonObject& params, QString& error) const;
  QJsonObject externalizeResult(mdl::Map& map, const QJsonObject& result) const;

  static bool isStableObjectId(const QString& id);
  static bool isLegacyObjectId(const QString& id);
  static std::optional<mdl::NodePath> parseLegacyObjectId(const QString& id);
};

} // namespace tb::ui::automation
