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

#include <catch2/catch_test_macros.hpp>

namespace tb::mcp
{

TEST_CASE("McpMode")
{
  CHECK(modeName(McpMode::Off) == "Off");
  CHECK(modeName(McpMode::ReadOnly) == "ReadOnly");
  CHECK(modeName(McpMode::Edit) == "Edit");

  CHECK(parseMode("off") == McpMode::Off);
  CHECK(parseMode("ReadOnly") == McpMode::ReadOnly);
  CHECK(parseMode("Edit") == McpMode::Edit);
  CHECK(!parseMode("Danger"));
  CHECK(!parseMode("Unknown"));

  CHECK(!allowsMode(McpMode::Off, McpMode::ReadOnly));
  CHECK(allowsMode(McpMode::ReadOnly, McpMode::ReadOnly));
  CHECK(!allowsMode(McpMode::ReadOnly, McpMode::Edit));
  CHECK(allowsMode(McpMode::Edit, McpMode::ReadOnly));
}

} // namespace tb::mcp
