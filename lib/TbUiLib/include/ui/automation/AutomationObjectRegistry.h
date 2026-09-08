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

#include <QString>

#include <map>

namespace tb::mdl
{
class Map;
class Node;
} // namespace tb::mdl

namespace tb::ui::automation
{

/**
 * Document identity and short-lived object labels shared by Python execution
 *
 * contexts. Handle validity itself belongs to PythonHandleRegistry.
 */
class AutomationObjectRegistry
{
private:
  mutable int m_documentEpoch = 1;
  mutable int m_nextSequence = 1;
  mutable quintptr m_currentMapAddress = 0;
  mutable quintptr m_currentWorldAddress = 0;
  mutable std::map<quintptr, QString> m_nodeIds;

public:
  int documentEpoch(mdl::Map& map) const;
  QString documentFingerprint(mdl::Map& map) const;

  QString registerNode(mdl::Map& map, mdl::Node& node) const;
};

} // namespace tb::ui::automation
