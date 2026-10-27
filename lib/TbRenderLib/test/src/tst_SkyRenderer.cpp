/*
 Copyright (C) 2026 Kristian Duske

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

#include "gl/Material.h"
#include "mdl/BrushFaceHandle.h"
#include "mdl/BrushNode.h"
#include "mdl/MapFixture.h"
#include "mdl/Map_NodeLocking.h"
#include "mdl/Map_NodeVisibility.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/TestFactory.h"
#include "render/SkyRenderer.h"

#include <filesystem>

#include <catch2/catch_test_macros.hpp>

namespace tb::render
{

TEST_CASE("SkyRenderer.skyMaterialNames")
{
  CHECK(
    skyMaterialNames("desert")
    == SkyMaterialNames{
      "desertrt", "desertbk", "desertlf", "desertft", "desertup", "desertdn"});
}

TEST_CASE("SkyRenderer.looseSkyMaterialPaths")
{
  CHECK(
    looseSkyMaterialPaths("2namekdn")
    == std::array<std::filesystem::path, 6>{
      std::filesystem::path{"gfx"} / "env" / "2namekdn.tga",
      std::filesystem::path{"gfx"} / "env" / "2namekdn.bmp",
      std::filesystem::path{"gfx"} / "env" / "2namekdn.png",
      std::filesystem::path{"gfx"} / "env" / "2namekdn.jpg",
      std::filesystem::path{"gfx"} / "env" / "2namekdn.jpeg",
      std::filesystem::path{"gfx"} / "env" / "2namekdn.dds"});
}

TEST_CASE("SkyRenderer.shouldRenderSky")
{
  CHECK_FALSE(shouldRenderSky(false, true, "desert"));
  CHECK_FALSE(shouldRenderSky(true, false, "desert"));
  CHECK_FALSE(shouldRenderSky(true, true, ""));
  CHECK(shouldRenderSky(true, true, "desert"));
}

TEST_CASE("SkyRenderer.skyMaterialsReady")
{
  const auto material = reinterpret_cast<const gl::Material*>(0x1);
  CHECK(skyMaterialsReady({material, material, material, material, material, material}));
  CHECK_FALSE(
    skyMaterialsReady({material, material, nullptr, material, material, material}));
  CHECK_FALSE(skyMaterialsReady({}));
}

TEST_CASE("SkyRenderer.skyBrushFaceVertexCount")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create();
  auto* skyBrush = mdl::createBrushNode(map, "sky");
  mdl::addNodes(map, {{&mdl::parentForNodes(map), {static_cast<mdl::Node*>(skyBrush)}}});

  CHECK(skyBrushFaceVertexCount(map) > 0);
  CHECK(selectedSkyBrushFaceVertexCount(map) == 0);

  SECTION("selected sky brush is marked for tinting")
  {
    mdl::selectNodes(map, {skyBrush});

    CHECK(selectedSkyBrushFaceVertexCount(map) == skyBrushFaceVertexCount(map));
  }

  SECTION("selected sky face is marked for tinting")
  {
    mdl::selectBrushFaces(map, {mdl::BrushFaceHandle{*skyBrush, 0u}});

    CHECK(selectedSkyBrushFaceVertexCount(map) > 0);
    CHECK(selectedSkyBrushFaceVertexCount(map) < skyBrushFaceVertexCount(map));
  }

  SECTION("locked sky brush is still collected")
  {
    mdl::lockNodes(map, std::vector<mdl::Node*>{skyBrush});

    CHECK(skyBrushFaceVertexCount(map) > 0);
  }

  SECTION("invisible sky brush is not collected")
  {
    mdl::hideNodes(map, std::vector<mdl::Node*>{skyBrush});

    CHECK(skyBrushFaceVertexCount(map) == 0);
  }
}

TEST_CASE("SkyRenderer.skyBrushFaceVertexCount recognizes uppercase material names")
{
  auto fixture = mdl::MapFixture{};
  auto& map = fixture.create();
  auto* skyBrush = mdl::createBrushNode(map, "SKY");
  mdl::addNodes(map, {{&mdl::parentForNodes(map), {static_cast<mdl::Node*>(skyBrush)}}});

  CHECK(skyBrushFaceVertexCount(map) > 0);
}

} // namespace tb::render
