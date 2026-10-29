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

#include "base/Uuid.h"
#include "mdl/GameInfo.h"
#include "mdl/Map.h"
#include "mdl/Node.h"
#include "ui/QPathUtils.h"
#include "ui/python/PythonHandleRegistry.h"

namespace tb::ui::automation
{

int AutomationObjectRegistry::documentEpoch(mdl::Map& map) const
{
  return map.identity();
}

QString AutomationObjectRegistry::documentFingerprint(mdl::Map& map) const
{
  static const auto processIdentity = QString::fromStdString(generateUuid());
  return QString{"doc:%1:%2"}.arg(processIdentity).arg(map.identity());
}

QString AutomationObjectRegistry::registerNode(mdl::Map& map, mdl::Node& node) const
{
  return QString{"object:%1:%2:%3"}
    .arg(map.identity())
    .arg(reinterpret_cast<quintptr>(&node), 0, 16)
    .arg(PythonHandleRegistry::instance().nodeLifetimeGeneration(&node));
}

} // namespace tb::ui::automation
