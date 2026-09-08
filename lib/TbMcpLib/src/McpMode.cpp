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

#include "mcp/McpMode.h"

namespace tb::mcp
{

QString modeName(const McpMode mode)
{
  switch (mode)
  {
  case McpMode::Off:
    return "Off";
  case McpMode::ReadOnly:
    return "ReadOnly";
  case McpMode::Edit:
    return "Edit";
  }

  return "Off";
}

std::optional<McpMode> parseMode(const QString& value)
{
  if (value.compare("Off", Qt::CaseInsensitive) == 0)
  {
    return McpMode::Off;
  }
  if (value.compare("ReadOnly", Qt::CaseInsensitive) == 0)
  {
    return McpMode::ReadOnly;
  }
  if (value.compare("Edit", Qt::CaseInsensitive) == 0)
  {
    return McpMode::Edit;
  }
  return std::nullopt;
}

bool allowsMode(const McpMode currentMode, const McpMode requiredMode)
{
  const auto rank = [](const McpMode mode) {
    switch (mode)
    {
    case McpMode::Off:
      return 0;
    case McpMode::ReadOnly:
      return 1;
    case McpMode::Edit:
      return 2;
    }

    return 0;
  };

  return currentMode != McpMode::Off && rank(currentMode) >= rank(requiredMode);
}

} // namespace tb::mcp
