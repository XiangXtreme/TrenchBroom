/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#include <QApplication>
#include <QMenu>
#include <QTabBar>
#include <QTemporaryDir>
#include <QToolButton>

#include "mdl/GameManager.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/Map_Entities.h"
#include "ui/AppControllerFixture.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/ProjectSession.h"
#include "ui/ProjectWindow.h"
#include "ui/QPathUtils.h"

#include <functional>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{

const auto GameName = std::string{"Workspace"};

const auto GameConfig = R"({
    "version": 9,
    "name": "Workspace",
    "icon": "Icon.png",
    "fileformats": [
        { "format": "Valve" }
    ],
    "filesystem": {
        "searchpath": "valve",
        "packageformat": { "extension": "pak", "format": "idpak" }
    },
    "materials": {
        "root": "textures",
        "extensions": [".D"],
        "palette": "gfx/palette.lmp",
        "attribute": "wad"
    },
    "entities": {
        "definitions": [],
        "defaultcolor": "0.6 0.6 0.6 1.0",
        "modelformats": [ "mdl" ]
    },
    "tags": {
        "brush": [],
        "brushface": []
    }
})";

template <typename T>
T require(Result<T> result)
{
  if (result.is_error())
    FAIL(std::get<Error>(result.error()).msg);
  return std::move(result).value();
}

void require(Result<void> result)
{
  if (result.is_error())
    FAIL(std::get<Error>(result.error()).msg);
}

MapWindow* windowFor(AppController& appController, const MapDocument& document)
{
  for (auto* window : appController.mapWindowManager().mapWindows())
    if (&window->document() == &document)
      return window;
  return nullptr;
}

// Emitting the signal is what a click on the tab's close button does.
void closeTab(QTabBar& tabs, const int index)
{
  QMetaObject::invokeMethod(&tabs, "tabCloseRequested", Q_ARG(int, index));
  QApplication::processEvents();
  QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QApplication::processEvents();
}

} // namespace

TEST_CASE("ProjectWindow")
{
  auto fixture = AppControllerFixture{[](const auto& writeGameConfig) {
    writeGameConfig(GameName, GameConfig, std::nullopt, std::nullopt);
  }};
  auto& appController = fixture.appController();

  auto directory = QTemporaryDir{};
  REQUIRE(directory.isValid());
  auto session = require(ProjectSession::create(
    pathFromQString(directory.filePath("workspace.tbproj")), "Workspace", GameName));

  auto& manager = appController.mapWindowManager();
  require(manager.createProjectMap(session, "alpha", mdl::MapFormat::Valve));
  require(manager.createProjectMap(session, "beta", mdl::MapFormat::Valve));

  auto window = ProjectWindow{appController, session};
  window.resize(1000, 700);
  window.show();
  QApplication::processEvents();

  // Tear down in a defined order: the project window first, then the map windows. Letting
  // the fixture destroy them would delete the views after the window that routes their
  // focus changes.
  const auto teardown = [&]() {
    window.hide();
    for (auto* mapWindow : manager.mapWindows())
    {
      if (mapWindow->document().map().modified())
        continue;
      mapWindow->close();
    }
    QApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();
  };
  struct TeardownGuard
  {
    std::function<void()> fn;
    ~TeardownGuard()
    {
      if (fn)
        fn();
    }
  };
  auto teardownGuard = TeardownGuard{teardown};

  auto* tabs = window.findChild<QTabBar*>("ProjectMapTabs");
  REQUIRE(tabs != nullptr);
  REQUIRE(tabs->count() == 2);
  CHECK(tabs->tabText(0) == "alpha");
  CHECK(tabs->tabText(1) == "beta");

  const auto alphaId = tabs->tabData(0).toString().toStdString();
  auto* alphaDocument = session->documentFor(alphaId);
  REQUIRE(alphaDocument != nullptr);
  auto* alphaWindow = windowFor(appController, *alphaDocument);
  REQUIRE(alphaWindow != nullptr);
  CHECK(alphaWindow->isVisible());

  SECTION("closing a tab keeps the document and its modifications")
  {
    closeTab(*tabs, 0);
    // The view is gone from the workspace, but the session keeps the document alive.
    CHECK(tabs->count() == 1);
    CHECK(tabs->tabText(0) == "beta");
    CHECK(windowFor(appController, *alphaDocument) == nullptr);
    CHECK(session->documentFor(alphaId) == alphaDocument);
    CHECK(session->retainedDocument(alphaId) != nullptr);
    CHECK(session->assetFor(*alphaDocument) == std::optional{alphaId});

    // Editing the closed map still dirties the project, because the working copy lives in
    // the session, not in the view.
    REQUIRE(mdl::setEntityProperty(alphaDocument->map(), "message", "kept"));
    QApplication::processEvents();
    CHECK(session->modified());

    // Saving writes the closed map too.
    require(session->save());
    CHECK_FALSE(session->modified());
    CHECK_FALSE(alphaDocument->map().modified());
  }

  SECTION("a closed tab is reopened as the same document")
  {
    closeTab(*tabs, 0);
    auto* reopen = window.findChild<QToolButton*>("ProjectReopenMaps");
    REQUIRE(reopen != nullptr);
    REQUIRE(reopen->menu() != nullptr);
    REQUIRE(reopen->menu()->actions().size() == 1);
    CHECK(reopen->menu()->actions().front()->text() == "alpha");

    reopen->menu()->actions().front()->trigger();
    QApplication::processEvents();
    CHECK(tabs->count() == 2);
    // Reopening restores the same editing state in a new view instead of reloading.
    CHECK(session->documentFor(alphaId) == alphaDocument);
    auto* reopenedWindow = windowFor(appController, *alphaDocument);
    REQUIRE(reopenedWindow != nullptr);
    CHECK(reopenedWindow->isVisible());
  }

  SECTION("the tab strip follows the map modification state")
  {
    REQUIRE(mdl::setEntityProperty(alphaDocument->map(), "message", "edited"));
    QApplication::processEvents();
    const auto index =
      tabs->count() > 0 && tabs->tabData(0).toString().toStdString() == alphaId ? 0 : 1;
    CAPTURE(tabs->tabText(index).toStdString());
    CHECK(tabs->tabText(index).endsWith("*"));

    require(session->save());
    QApplication::processEvents();
    const auto savedIndex = tabs->tabData(0).toString().toStdString() == alphaId ? 0 : 1;
    CHECK_FALSE(tabs->tabText(savedIndex).endsWith("*"));
  }
}

} // namespace tb::ui
