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

#include <QAbstractButton>
#include <QListWidget>
#include <QStackedLayout>

#include "TestEnvironment.h"
#include "fs/DiskIO.h"
#include "fs/TestFileSystem.h"
#include "mdl/EntityProperties.h"
#include "mdl/GameFileSystem.h"
#include "mdl/Map.h"
#include "mdl/WorldNode.h"
#include "ui/CatchConfig.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"
#include "ui/SmartAssetPropertyEditor.h"
#include "ui/SmartPropertyEditorManager.h"
#include "ui/SmartSkyboxEditor.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("SmartSkyboxEditor")
{
  SECTION("extracts skybox name and GoldSrc suffix")
  {
    CHECK(
      skyboxBaseAndSuffix("gfx/env/2namekrt.tga")
      == std::optional{std::pair<std::string, std::string>{"2namek", "rt"}});
    CHECK(
      skyboxBaseAndSuffix("gfx/env/desertUP.TGA")
      == std::optional{std::pair<std::string, std::string>{"desert", "up"}});
  }

  SECTION("ignores unsupported files")
  {
    CHECK(skyboxBaseAndSuffix("gfx/env/desertxx.tga") == std::nullopt);
    CHECK(skyboxBaseAndSuffix("gfx/env/desertrt.txt") == std::nullopt);
    CHECK(skyboxBaseAndSuffix("gfx/env/rt.tga") == std::nullopt);
  }

  SECTION("property editor manager routes worldspawn skyname to the shared asset editor")
  {
    auto fixture = MapDocumentFixture{};
    auto& document = fixture.create();
    auto& map = document.map();
    auto& worldNode = map.worldNode();

    auto manager = SmartPropertyEditorManager{document};
    auto* stackedLayout = qobject_cast<QStackedLayout*>(manager.layout());
    REQUIRE(stackedLayout != nullptr);

    manager.switchEditor(
      mdl::EntityPropertyKeys::Skyname, std::vector<mdl::EntityNodeBase*>{&worldNode});
    CHECK(qobject_cast<SmartSkyboxEditor*>(stackedLayout->currentWidget()) == nullptr);
    CHECK(
      dynamic_cast<SmartAssetPropertyEditor*>(stackedLayout->currentWidget()) != nullptr);
    CHECK_FALSE(manager.isDefaultEditorActive());

    manager.switchEditor("targetname", std::vector<mdl::EntityNodeBase*>{&worldNode});
    CHECK(manager.isDefaultEditorActive());
  }

  SECTION("uses a compact wrapping thumbnail grid")
  {
    auto fixture = MapDocumentFixture{};
    auto& document = fixture.create();

    auto editor = SmartSkyboxEditor{document};
    const auto* list = editor.findChild<QListWidget*>("smartSkyboxList");
    const auto* refreshButton =
      editor.findChild<QAbstractButton*>("smartSkyboxRefreshButton");

    REQUIRE(list != nullptr);
    REQUIRE(refreshButton != nullptr);
    CHECK(editor.minimumHeight() == 210);
    CHECK(editor.maximumHeight() == 210);
    CHECK(list->viewMode() == QListView::IconMode);
    CHECK(list->movement() == QListView::Static);
    CHECK(list->resizeMode() == QListView::Adjust);
    CHECK(list->flow() == QListView::LeftToRight);
    CHECK(list->isWrapping());
    CHECK(list->uniformItemSizes());
    CHECK(list->iconSize() == QSize{70, 36});
    CHECK(list->gridSize() == QSize{172, 52});
    CHECK(list->itemDelegate()->sizeHint({}, {}) == QSize{172, 52});
    CHECK(refreshButton->text().isEmpty());
    CHECK(refreshButton->toolTip() == "Refresh skyboxes");
  }

  SECTION("loads previews through the virtual game file system")
  {
    auto fixture = MapDocumentFixture{};
    auto& document = fixture.create();

    const auto imagePath = getTestExecutableDir() / "images/AppIcon.png";
    auto imageFileResult = fs::Disk::openFile(imagePath);
    REQUIRE(imageFileResult.is_success());
    const auto imageFile = imageFileResult.value();

    auto skyboxFiles = std::vector<fs::Entry>{};
    for (const auto* suffix : {"rt", "bk", "lf", "ft", "up", "dn"})
    {
      skyboxFiles.push_back(
        fs::FileEntry{"memory_sky" + std::string{suffix} + ".png", imageFile});
    }

    document.map().gameFileSystem().mount(
      {},
      std::make_unique<fs::TestFileSystem>(
        fs::DirectoryEntry{
          "",
          {fs::DirectoryEntry{
            "gfx", {fs::DirectoryEntry{"env", std::move(skyboxFiles)}}}}},
        std::unordered_map<std::string, fs::FileSystemMetadata>{},
        "Z:/not-a-real-skybox-path"));

    auto editor = SmartSkyboxEditor{document};
    editor.activate(mdl::EntityPropertyKeys::Skyname);
    editor.update({&document.map().worldNode()});

    const auto* list = editor.findChild<QListWidget*>("smartSkyboxList");
    auto* refreshButton = editor.findChild<QAbstractButton*>("smartSkyboxRefreshButton");
    REQUIRE(list != nullptr);
    REQUIRE(refreshButton != nullptr);
    refreshButton->click();
    REQUIRE(list->count() == 1);
    CHECK(list->item(0)->text() == "memory_sky");
    CHECK_FALSE(list->item(0)->icon().isNull());
    CHECK(list->item(0)->icon().actualSize(QSize{70, 36}) == QSize{70, 36});
  }

  SECTION("decodes compressed DDS previews")
  {
    auto fixture = MapDocumentFixture{};
    auto& document = fixture.create();

    const auto imagePath = std::filesystem::path{__FILE__}
                             .parent_path()
                             .parent_path()
                             .parent_path()
                             .parent_path()
                           / "TbMdlLib/test/fixture/mdl/LoadDdsTexture/dds_bc1.dds";
    auto imageFileResult = fs::Disk::openFile(imagePath);
    REQUIRE(imageFileResult.is_success());
    const auto imageFile = imageFileResult.value();

    auto skyboxFiles = std::vector<fs::Entry>{};
    for (const auto* suffix : {"rt", "bk", "lf", "ft", "up", "dn"})
    {
      skyboxFiles.push_back(
        fs::FileEntry{"memory_dds" + std::string{suffix} + ".dds", imageFile});
    }

    document.map().gameFileSystem().mount(
      {},
      std::make_unique<fs::TestFileSystem>(
        fs::DirectoryEntry{
          "",
          {fs::DirectoryEntry{
            "gfx", {fs::DirectoryEntry{"env", std::move(skyboxFiles)}}}}},
        std::unordered_map<std::string, fs::FileSystemMetadata>{},
        "Z:/not-a-real-dds-skybox-path"));

    auto editor = SmartSkyboxEditor{document};
    editor.activate(mdl::EntityPropertyKeys::Skyname);
    editor.update({&document.map().worldNode()});

    const auto* list = editor.findChild<QListWidget*>("smartSkyboxList");
    auto* refreshButton = editor.findChild<QAbstractButton*>("smartSkyboxRefreshButton");
    REQUIRE(list != nullptr);
    REQUIRE(refreshButton != nullptr);
    refreshButton->click();
    REQUIRE(list->count() == 1);
    CHECK_FALSE(list->item(0)->icon().isNull());
    CHECK(list->item(0)->icon().actualSize(QSize{70, 36}) == QSize{70, 36});
  }
}

} // namespace tb::ui
