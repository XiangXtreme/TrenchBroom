/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 */

#pragma once

#include "base/Result.h"

#include <filesystem>

namespace tb::mdl
{
struct GameInfo;
}

namespace tb::ui
{
class AppController;

/**
 * Opens a persistent map with its header-declared game and map format.
 * This automation path never opens an interactive game-selection dialog.
 */
Result<void> openAutomationDocument(
  AppController& appController,
  std::filesystem::path path,
  const mdl::GameInfo* fallbackGameInfo = nullptr);

} // namespace tb::ui
