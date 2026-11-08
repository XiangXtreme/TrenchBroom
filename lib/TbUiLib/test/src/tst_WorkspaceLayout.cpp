/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QDataStream>
#include <QIODevice>
#include <QSettings>
#include <QTemporaryDir>
#include <QVariantMap>

#include "ui/WorkspaceLayout.h"

#include <limits>

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
TEST_CASE("WorkspaceLayout")
{
  auto directory = QTemporaryDir{};
  REQUIRE(directory.isValid());
  auto settings = QSettings{directory.filePath("layout.ini"), QSettings::IniFormat};

  SECTION("project and standalone settings are independent and persist on disk")
  {
    auto editor = EditorLayout{};
    editor.inspectorVisible = false;
    editor.inspectorPage = InspectorPage::Outliner;
    editor.infoPage = InfoPanelPage::PythonConsole;
    editor.inspectorRatio = 0.25;
    saveEditorLayout(settings, WorkspaceKind::Project, editor);
    auto tools = ToolLayout{};
    tools.tool = "issues";
    tools.pinned = true;
    tools.keepOpen = true;
    tools.expanded = false;
    tools.heightRatio = 0.3;
    saveToolLayout(settings, WorkspaceKind::Project, tools);
    settings.sync();
    auto reopened = QSettings{settings.fileName(), QSettings::IniFormat};
    const auto project = loadEditorLayout(reopened, WorkspaceKind::Project);
    CHECK_FALSE(project.inspectorVisible);
    CHECK(project.inspectorPage == InspectorPage::Outliner);
    CHECK(project.infoPage == InfoPanelPage::PythonConsole);
    CHECK(project.inspectorRatio == 0.25);
    CHECK(loadEditorLayout(reopened, WorkspaceKind::Map).inspectorVisible);
    CHECK(
      loadEditorLayout(reopened, WorkspaceKind::Map).inspectorPage
      == InspectorPage::Outliner);
    const auto restored = loadToolLayout(reopened, WorkspaceKind::Project);
    CHECK(restored.pinned);
    CHECK(restored.keepOpen);
    CHECK_FALSE(restored.expanded);
    CHECK(restored.tool == "issues");
    CHECK(restored.heightRatio == 0.3);
    CHECK_FALSE(loadToolLayout(reopened, WorkspaceKind::Map).pinned);
  }

  SECTION("legacy settings are read only until canonical state is saved")
  {
    settings.setValue("ProjectTools/Pinned", true);
    auto bytes = QByteArray{};
    auto stream = QDataStream{&bytes, QIODevice::WriteOnly};
    stream << false;
    settings.setValue("Windows/Inspector/State", bytes);
    const auto migrated = loadEditorLayout(settings, WorkspaceKind::Project);
    CHECK_FALSE(migrated.inspectorVisible);
    const auto tools = loadToolLayout(settings, WorkspaceKind::Project);
    CHECK(tools.pinned);
    CHECK(tools.expanded);
    saveEditorLayout(settings, WorkspaceKind::Project, EditorLayout{});
    saveToolLayout(settings, WorkspaceKind::Project, ToolLayout{});
    CHECK(loadEditorLayout(settings, WorkspaceKind::Project).inspectorVisible);
    CHECK_FALSE(loadToolLayout(settings, WorkspaceKind::Project).pinned);
    CHECK(settings.value("Windows/Inspector/State").toByteArray() == bytes);
  }

  SECTION("saved map and entity pages migrate to scene without changing other page IDs")
  {
    for (const auto page : {InspectorPage::Map, InspectorPage::Entity})
    {
      auto bytes = QByteArray{};
      auto stream = QDataStream{&bytes, QIODevice::WriteOnly};
      stream << static_cast<int>(page);
      settings.remove("WorkspaceLayouts/Map/Editor");
      settings.setValue("Windows/Inspector_TabBook/State", bytes);
      CHECK(
        loadEditorLayout(settings, WorkspaceKind::Map).inspectorPage
        == InspectorPage::Outliner);
      settings.setValue(
        "WorkspaceLayouts/Map/Editor",
        QVariantMap{
          {"version", 1},
          {"inspectorPage", page == InspectorPage::Map ? "map" : "entity"}});
      CHECK(
        loadEditorLayout(settings, WorkspaceKind::Map).inspectorPage
        == InspectorPage::Outliner);
    }
    for (const auto page :
         {InspectorPage::Face,
          InspectorPage::Outliner,
          InspectorPage::Plugin,
          InspectorPage::Terrain})
    {
      auto layout = EditorLayout{};
      layout.inspectorPage = page;
      saveEditorLayout(settings, WorkspaceKind::Map, layout);
      CHECK(loadEditorLayout(settings, WorkspaceKind::Map).inspectorPage == page);
    }
  }

  SECTION("invalid ratios missing tools and unknown versions use defaults")
  {
    settings.setValue(
      "WorkspaceLayouts/Project/Editor",
      QVariantMap{
        {"version", 1},
        {"inspectorRatio", std::numeric_limits<double>::quiet_NaN()},
        {"infoRatio", -1.0},
        {"inspectorPage", "uninstalled-plugin"}});
    const auto editor = loadEditorLayout(settings, WorkspaceKind::Project);
    CHECK(editor.inspectorRatio == EditorLayout{}.inspectorRatio);
    CHECK(editor.infoRatio == EditorLayout{}.infoRatio);
    CHECK(editor.inspectorPage == InspectorPage::Outliner);
    settings.setValue(
      "WorkspaceLayouts/Project/Tools",
      QVariantMap{{"version", 1}, {"tool", "uninstalled-plugin"}, {"heightRatio", 50.0}});
    const auto tools = loadToolLayout(settings, WorkspaceKind::Project);
    CHECK(tools.tool == "content-browser");
    CHECK(tools.heightRatio == ToolLayout{}.heightRatio);
    settings.setValue(
      "WorkspaceLayouts/Project/Editor",
      QVariantMap{{"version", 900}, {"inspectorVisible", false}});
    CHECK(loadEditorLayout(settings, WorkspaceKind::Project).inspectorVisible);
    settings.setValue("WorkspaceLayouts/Project/Tools", QByteArray{"corrupt"});
    CHECK_FALSE(loadToolLayout(settings, WorkspaceKind::Project).pinned);
  }
}
} // namespace tb::ui
