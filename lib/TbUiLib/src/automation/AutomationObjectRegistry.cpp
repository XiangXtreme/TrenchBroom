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

#include "ui/automation/AutomationObjectRegistry.h"

#include <QHash>

#include "mdl/GameInfo.h"
#include "mdl/Map.h"
#include "mdl/Node.h"
#include "ui/QPathUtils.h"

namespace tb::ui::automation
{

int AutomationObjectRegistry::documentEpoch(mdl::Map& map) const
{
  const auto mapAddress = reinterpret_cast<quintptr>(&map);
  const auto worldAddress = reinterpret_cast<quintptr>(&map.worldNode());
  if (m_currentMapAddress == 0)
  {
    m_currentMapAddress = mapAddress;
    m_currentWorldAddress = worldAddress;
  }
  else if (m_currentMapAddress != mapAddress || m_currentWorldAddress != worldAddress)
  {
    ++m_documentEpoch;
    m_nextSequence = 1;
    m_nodeIds.clear();
    m_currentMapAddress = mapAddress;
    m_currentWorldAddress = worldAddress;
  }
  return m_documentEpoch;
}

QString AutomationObjectRegistry::documentFingerprint(mdl::Map& map) const
{
  auto hash = qHash(QString::fromStdString(map.filename()));
  const auto documentPath = map.path().empty() ? QString{} : pathAsQString(map.path());
  hash ^= qHash(documentPath) + 0x9e3779b9u + (hash << 6) + (hash >> 2);
  hash ^= qHash(QString::fromStdString(map.gameInfo().gameConfig.name)) + 0x9e3779b9u
          + (hash << 6) + (hash >> 2);
  hash ^= qHash(QString::number(reinterpret_cast<quintptr>(&map), 16)) + 0x9e3779b9u
          + (hash << 6) + (hash >> 2);
  return QString{"doc:%1"}.arg(static_cast<quint64>(hash), 16, 16, QLatin1Char{'0'});
}

QString AutomationObjectRegistry::registerNode(mdl::Map& map, mdl::Node& node) const
{
  const auto epoch = documentEpoch(map);
  const auto address = reinterpret_cast<quintptr>(&node);
  if (const auto it = m_nodeIds.find(address); it != m_nodeIds.end())
  {
    return it->second;
  }

  const auto id = QString{"object:%1:%2"}.arg(epoch).arg(m_nextSequence++);
  m_nodeIds.emplace(address, id);
  return id;
}

} // namespace tb::ui::automation
