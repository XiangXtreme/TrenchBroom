/*
 Copyright (C) 2010 Kristian Duske

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

#include "ui/MapWindowManager.h"

#include <QApplication>
#include <QFile>
#include <QTemporaryDir>

#include "gl/GlManager.h"
#include "mdl/GameConfig.h"
#include "mdl/GameInfo.h"
#include "mdl/GameManager.h"
#include "mdl/Map.h"
#include "mdl/MapHeader.h"
#include "ui/AppController.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/ProjectSession.h"
#include "ui/QPathUtils.h"

#include "kd/contracts.h"
#include "kd/filesystem_utils.h"

#include <algorithm>
#include <memory>

namespace tb::ui
{

MapWindowManager::MapWindowManager(
  AppController& appController, const bool singleMapWindow, const bool showMapWindows)
  : QObject{&appController}
  , m_appController{appController}
  , m_singleMapWindow{singleMapWindow}
  , m_showMapWindows{showMapWindows}
{
  connect(qApp, &QApplication::focusChanged, this, &MapWindowManager::onFocusChange);
}

MapWindowManager::~MapWindowManager() = default;

std::vector<MapWindow*> MapWindowManager::mapWindows() const
{
  return m_mapWindows;
}

MapWindow* MapWindowManager::topMapWindow() const
{
  return m_mapWindows.empty() ? nullptr : m_mapWindows.front();
}

void MapWindowManager::closeMapView(MapWindow& mapWindow)
{
  removeMapWindow(&mapWindow);
  mapWindow.hide();
  mapWindow.deleteLater();
}

bool MapWindowManager::activateMapWindow(MapWindow& mapWindow)
{
  const auto it = std::ranges::find(m_mapWindows, &mapWindow);
  if (it == m_mapWindows.end())
  {
    return false;
  }

  if (it != m_mapWindows.begin())
  {
    std::rotate(m_mapWindows.begin(), it, std::next(it));
  }
  mapWindow.raise();
  mapWindow.activateWindow();
  return true;
}

Result<void> MapWindowManager::createDocument(
  const mdl::GameInfo& gameInfo, mdl::MapFormat mapFormat, const vm::bbox3d& worldBounds)
{
  if (shouldCreateWindowForDocument())
  {
    return MapDocument::createDocument(
             m_appController.environmentConfig(),
             gameInfo,
             mapFormat,
             worldBounds,
             m_appController.taskManager(),
             m_appController.glManager().resourceManager())
           | kdl::transform([&](auto document) { createMapWindow(std::move(document)); });
  }

  auto* mapWindow = topMapWindow();
  contract_assert(mapWindow != nullptr);

  return mapWindow->document().create(
    m_appController.environmentConfig(), gameInfo, mapFormat, worldBounds);
}

Result<void> MapWindowManager::loadDocument(
  const mdl::GameInfo& gameInfo,
  mdl::MapFormat mapFormat,
  const vm::bbox3d& worldBounds,
  std::filesystem::path path)
{
  if (shouldCreateWindowForDocument())
  {
    return MapDocument::loadDocument(
             m_appController.environmentConfig(),
             gameInfo,
             mapFormat,
             worldBounds,
             std::move(path),
             m_appController.taskManager(),
             m_appController.glManager().resourceManager())
           | kdl::transform([&](auto document) { createMapWindow(std::move(document)); });
  }

  auto* mapWindow = topMapWindow();
  contract_assert(mapWindow != nullptr);

  return mapWindow->document().load(
    m_appController.environmentConfig(),
    gameInfo,
    mapFormat,
    worldBounds,
    std::move(path));
}

Result<void> MapWindowManager::createProjectMap(
  std::shared_ptr<ProjectSession> project,
  const std::string& name,
  const mdl::MapFormat format)
{
  const auto* game =
    m_appController.gameManager().gameInfo(project->store().info().gameId);
  if (!game)
    return Error{"Project game configuration is unavailable."};
  auto created = MapDocument::createDocument(
    m_appController.environmentConfig(),
    *game,
    format,
    MapDocument::DefaultWorldBounds,
    m_appController.taskManager(),
    m_appController.glManager().resourceManager());
  if (created.is_error())
    return std::get<Error>(created.error());
  auto temporary = QTemporaryDir{};
  if (!temporary.isValid())
    return Error{"Cannot create temporary map directory."};
  const auto path = pathFromQString(temporary.path()) / "new.map";
  const auto saved = created.value()->map().saveTo(path);
  if (saved.is_error())
    return std::get<Error>(saved.error());
  auto file = QFile{pathAsQString(path)};
  if (!file.open(QIODevice::ReadOnly))
    return Error{file.errorString().toStdString()};
  const auto bytes = file.readAll();
  if (file.error() != QFileDevice::NoError)
    return Error{file.errorString().toStdString()};
  auto input = project::AssetImport{};
  input.type = project::AssetType::Map;
  input.virtualPath = "maps/" + name + ".map";
  input.displayName = name;
  input.original.assign(bytes.begin(), bytes.end());
  input.compatible = input.original;
  return project->importAsset(input) | kdl::and_then([&](const auto& asset) {
           return loadProjectMap(project, asset.id);
         });
}

Result<void> MapWindowManager::loadProjectMap(
  std::shared_ptr<ProjectSession> project, const std::string& assetId)
{
  if (const auto* existing = project->documentFor(assetId))
  {
    for (auto* window : m_mapWindows)
    {
      if (&window->document() == existing)
      {
        activateMapWindow(*window);
        return {};
      }
    }

    // The project keeps the document when its view closes, so reopening a map only has
    // to give the same editing state a new view.
    if (auto retained = project->retainedDocument(assetId))
    {
      auto* window = new MapWindow{m_appController, std::move(retained), project};
      window->positionOnScreen(topMapWindow());
      m_mapWindows.insert(m_mapWindows.begin(), window);
      if (m_showMapWindows)
      {
        window->show();
        window->activateWindow();
      }
      return {};
    }
    return {};
  }
  const auto* game =
    m_appController.gameManager().gameInfo(project->store().info().gameId);
  if (!game)
    return Error{"Project game configuration is unavailable."};
  return project->prepareMap(assetId)
         | kdl::and_then([&](const auto& path) -> Result<void> {
             auto header = kdl::with_istream(path, mdl::readMapHeader);
             if (header.is_error())
               return std::get<Error>(header.error());
             const auto& [gameName, format] = header.value();
             if (gameName && *gameName != project->store().info().gameId)
               return Error{"Map game does not match the project's game."};
             if (format == mdl::MapFormat::Unknown)
               return Error{
                 "Map needs a TrenchBroom map-format header. Open and save it as a "
                 "standalone map before importing."};
             auto loaded = MapDocument::loadDocument(
               m_appController.environmentConfig(),
               *game,
               format,
               MapDocument::DefaultWorldBounds,
               path,
               m_appController.taskManager(),
               m_appController.glManager().resourceManager());
             if (loaded.is_error())
               return std::get<Error>(loaded.error());
             auto document = std::move(loaded).value();
             auto registered = project->registerDocument(*document, assetId);
             if (registered.is_error())
               return std::get<Error>(registered.error());
             auto* window = new MapWindow{
               m_appController,
               std::shared_ptr<MapDocument>{std::move(document)},
               project};
             window->positionOnScreen(topMapWindow());
             m_mapWindows.insert(m_mapWindows.begin(), window);
             if (m_showMapWindows)
             {
               window->show();
               window->activateWindow();
             }
             return {};
           });
}

bool MapWindowManager::allMapWindowsClosed() const
{
  return m_mapWindows.empty();
}

void MapWindowManager::onFocusChange(QWidget* /* old */, QWidget* now)
{
  if (now)
  {
    // The QApplication::focusChanged signal also notifies us of focus changes between
    // child widgets, so get the top-level widget with QWidget::window()
    if (auto* mapWindow = dynamic_cast<MapWindow*>(now->window()))
    {
      if (auto it = std::ranges::find(m_mapWindows, mapWindow);
          it != m_mapWindows.end() && it != m_mapWindows.begin())
      {
        std::rotate(m_mapWindows.begin(), it, std::next(it));
      }
    }
  }
}

bool MapWindowManager::shouldCreateWindowForDocument() const
{
  return !m_singleMapWindow || m_mapWindows.empty() || topMapWindow()->project();
}

MapWindow* MapWindowManager::createMapWindow(std::unique_ptr<MapDocument> document)
{
  contract_pre(document != nullptr);

  auto* mapWindow =
    new MapWindow{m_appController, std::shared_ptr<MapDocument>{std::move(document)}};
  mapWindow->positionOnScreen(topMapWindow());
  m_mapWindows.insert(m_mapWindows.begin(), mapWindow);

  if (m_showMapWindows)
  {
    mapWindow->show();
    mapWindow->activateWindow();
  }
  return mapWindow;
}

void MapWindowManager::removeMapWindow(MapWindow* mapWindow)
{
  // This is called from MapWindow::closeEvent
  if (auto it = std::ranges::find(m_mapWindows, mapWindow); it != m_mapWindows.end())
  {
    m_mapWindows.erase(it);
    // MapWindow uses Qt::WA_DeleteOnClose so we don't need to delete it here
  }
}

} // namespace tb::ui
