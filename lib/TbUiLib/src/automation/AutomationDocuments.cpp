/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 */

#include "ui/automation/AutomationDocuments.h"

#include "fs/DiskIO.h"
#include "mdl/ExportOptions.h"
#include "mdl/GameInfo.h"
#include "mdl/GameManager.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/MapHeader.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"

#include <optional>
#include <string>
#include <tuple>

namespace tb::ui
{
namespace
{

template <typename ResultType>
std::string errorMessage(const ResultType& result)
{
  auto message = std::string{};
  static_cast<void>(result.if_error([&](const auto& error) { message = error.msg; }));
  return message;
}

Result<std::tuple<std::string, mdl::MapFormat>> detectGameAndFormat(
  AppController& appController,
  const std::filesystem::path& path,
  const mdl::GameInfo* const fallbackGameInfo)
{
  const auto header = fs::Disk::withInputStream(path, mdl::readMapHeader);
  if (header.is_error())
  {
    return Error{errorMessage(header)};
  }

  auto [gameName, mapFormat] = header.value();
  if (!gameName)
  {
    return Error{
      "Could not autodetect map game. Add a TrenchBroom map header before opening this "
      "map through automation."};
  }
  if (mapFormat == mdl::MapFormat::Unknown)
  {
    return Error{
      "Could not autodetect map format. Add a TrenchBroom map header before opening this "
      "map through automation."};
  }
  if (
    appController.gameManager().gameInfo(*gameName) == nullptr
    && (fallbackGameInfo == nullptr || fallbackGameInfo->gameConfig.name != *gameName))
  {
    return Error{
      "Autodetected game '" + *gameName
      + "' is not available in this TrenchBroom configuration."};
  }

  return std::tuple{std::move(*gameName), mapFormat};
}

} // namespace

Result<void> openAutomationDocument(
  AppController& appController,
  const std::filesystem::path path,
  const mdl::GameInfo* const fallbackGameInfo)
{
  const auto detected = detectGameAndFormat(appController, path, fallbackGameInfo);
  if (detected.is_error())
  {
    return Error{errorMessage(detected)};
  }

  const auto& [gameName, mapFormat] = detected.value();
  const auto* gameInfo = appController.gameManager().gameInfo(gameName);
  if (
    gameInfo == nullptr && fallbackGameInfo != nullptr
    && fallbackGameInfo->gameConfig.name == gameName)
  {
    gameInfo = fallbackGameInfo;
  }
  if (gameInfo == nullptr)
  {
    return Error{"Autodetected game is no longer available after detection."};
  }

  return appController.mapWindowManager().loadDocument(
    *gameInfo, mapFormat, MapDocument::DefaultWorldBounds, path);
}

Result<void> saveAutomationDocument(
  mdl::Map& map, const std::optional<std::filesystem::path> path)
{
  return path ? map.saveAs(*path) : map.save();
}

Result<void> exportAutomationDocument(
  const mdl::Map& map, const std::filesystem::path& path, const bool stripTbProperties)
{
  return map.exportAs(mdl::MapExportOptions{
    path,
    stripTbProperties,
    std::nullopt,
    std::nullopt,
  });
}

bool activateAutomationDocument(AppController& appController, MapWindow& mapWindow)
{
  return appController.mapWindowManager().activateMapWindow(mapWindow);
}

void closeAutomationDocument(MapWindow& mapWindow, const bool discardChanges)
{
  mapWindow.closeDocument(discardChanges);
}

} // namespace tb::ui
