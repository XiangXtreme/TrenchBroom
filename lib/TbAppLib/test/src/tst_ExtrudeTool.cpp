/*
 Copyright (C) 2010 Kristian Duske
 Copyright (C) 2020 Eric Wasylishen

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

#include "Matchers.h"
#include "gl/Camera.h"
#include "gl/Material.h"
#include "gl/MaterialCollection.h"
#include "gl/MaterialManager.h"
#include "gl/OrthographicCamera.h"
#include "gl/PerspectiveCamera.h"
#include "gl/TextureResource.h"
#include "mdl/Brush.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushFaceHandle.h"
#include "mdl/BrushNode.h"
#include "mdl/CatchConfig.h"
#include "mdl/CommandProcessor.h"
#include "mdl/EditorContext.h"
#include "mdl/EntityNode.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Geometry.h"
#include "mdl/Map_Groups.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Picking.h"
#include "mdl/Map_Selection.h"
#include "mdl/ModelUtils.h"
#include "mdl/NodeQueries.h"
#include "mdl/PickResult.h"
#include "mdl/TestUtils.h"
#include "mdl/UvUtils.h"
#include "mdl/WorldNode.h"
#include "ui/ExtrudeTool.h"
#include "ui/InputState.h"
#include "ui/MapDocument.h"
#include "ui/MapDocumentFixture.h"
#include "ui/MoveObjectsTool.h"

#include "kd/result.h"

#include "vm/approx.h"
#include "vm/ray.h"
#include "vm/vec.h"

#include <algorithm>
#include <filesystem>
#include <ranges>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_range_equals.hpp>

namespace tb::ui
{
using namespace Catch::Matchers;

namespace
{

vm::vec3d n(const vm::vec3d& v)
{
  return vm::normalize(v);
}

vm::vec3f upFor(const vm::vec3f& direction)
{
  return vm::abs(direction.z()) < 0.9f ? vm::vec3f{0, 0, 1} : vm::vec3f{0, 1, 0};
}

/**
 * Build a camera positioned at the pick ray's origin looking along its direction, so
 * that the handle radius scaling used by the edge picking is realistic.
 */
gl::PerspectiveCamera perspectiveCameraFor(const vm::ray3d& pickRay)
{
  const auto viewport = gl::Camera::Viewport{0, 0, 1920, 1080};
  const auto direction = vm::vec3f{vm::normalize(pickRay.direction)};
  return gl::PerspectiveCamera{
    90.0f,
    1.0f,
    8000.0f,
    viewport,
    vm::vec3f{pickRay.origin},
    direction,
    upFor(direction)};
}

gl::OrthographicCamera orthographicCameraFor(const vm::ray3d& pickRay)
{
  const auto viewport = gl::Camera::Viewport{0, 0, 1920, 1080};
  const auto direction = vm::vec3f{vm::normalize(pickRay.direction)};
  return gl::OrthographicCamera{
    1.0f, 8000.0f, viewport, vm::vec3f{pickRay.origin}, direction, upFor(direction)};
}

mdl::PickResult performPick(mdl::Map& map, ExtrudeTool& tool, const vm::ray3d& pickRay)
{
  auto pickResult = mdl::PickResult::byDistance();
  pick(map, pickRay, pickResult);

  const auto hit = tool.pick3D(pickRay, perspectiveCameraFor(pickRay), pickResult);
  CHECK(hit.type() == ExtrudeTool::ExtrudeHitType);
  CHECK(!vm::is_nan(hit.hitPoint()));

  REQUIRE(hit.isMatch());
  pickResult.addHit(hit);

  REQUIRE(tool.proposedDragHandles().empty());
  tool.updateProposedDragHandles(pickResult);
  REQUIRE_FALSE(tool.proposedDragHandles().empty());

  return pickResult;
}

} // namespace

TEST_CASE("ExtrudeTool")
{
  auto fixture = MapDocumentFixture{};

  SECTION("pick2D")
  {
    auto& document = fixture.create();
    auto& map = document.map();

    auto tool = ExtrudeTool{document};

    constexpr auto brushBounds = vm::bbox3d{16.0};

    auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
    auto* brushNode1 =
      new mdl::BrushNode{builder.createCuboid(brushBounds, "material") | kdl::value()};

    addNodes(map, {{map.editorContext().currentLayer(), {brushNode1}}});
    selectNodes(map, {brushNode1});

    SECTION("Pick ray hits brush directly")
    {
      constexpr auto pickRay = vm::ray3d{{0, 0, 32}, {0, 0, -1}};

      auto pickResult = mdl::PickResult{};
      pick(map, pickRay, pickResult);

      REQUIRE(pickResult.all().size() == 1);

      const auto hit = tool.pick2D(pickRay, orthographicCameraFor(pickRay), pickResult);
      CHECK(!hit.isMatch());
    }

    SECTION("Pick ray does not hit brush directly")
    {
      using T =
        std::tuple<vm::vec3d, vm::vec3d, vm::vec3d, vm::vec3d, vm::plane3d, vm::vec3d>;

      // clang-format off
      const auto
      [origin,     direction,     expectedFaceNormal, expectedHitPoint, expectedDragReference,          expectedHandlePosition] = GENERATE(values<T>({
      // shoot from above downwards just past the top west edge, picking the west face
      {{-17, 0, 32}, { 0, 0, -1}, {-1, 0, 0},         {-17, 0, 16},     {{-16, 0, 16}, {0, 0, -1}},     {-16, 0, 16}},
      // shoot diagonally past the top west edge, picking the west face
      {{ -1, 0, 33}, {-1, 0, -1}, {-1, 0, 0},         {-17, 0, 17},     {{-16, 0, 16}, n({-1, 0, -1})}, {-16, 0, 16}},
      }));
      // clang-format on

      CAPTURE(brushBounds, origin, direction);

      const auto pickRay = vm::ray3d{origin, vm::normalize(direction)};
      const auto hit = tool.pick2D(pickRay, orthographicCameraFor(pickRay), {});

      CHECK(hit.isMatch());
      CHECK(hit.type() == ExtrudeTool::ExtrudeHitType);
      CHECK(hit.hitPoint() == expectedHitPoint);
      CHECK(hit.distance() == vm::approx{vm::length(expectedHitPoint - origin)});

      CHECK(
        hit.target<ExtrudeHitData>()
        == ExtrudeHitData{
          {brushNode1, *brushNode1->brush().findFace(expectedFaceNormal)},
          expectedDragReference,
          expectedHandlePosition});
    }
  }

  SECTION("pick3D")
  {
    auto& document = fixture.create();
    auto& map = document.map();

    auto tool = ExtrudeTool{document};

    constexpr auto brushBounds = vm::bbox3d{16.0};

    auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
    auto* brushNode1 =
      new mdl::BrushNode{builder.createCuboid(brushBounds, "material") | kdl::value()};

    addNodes(map, {{map.editorContext().currentLayer(), {brushNode1}}});
    selectNodes(map, {brushNode1});

    SECTION("Pick ray hits brush directly")
    {
      const auto pickRay = vm::ray3d{{0, 0, 24}, vm::normalize(vm::vec3d{-1, 0, -1})};

      auto pickResult = mdl::PickResult{};
      pick(map, pickRay, pickResult);

      REQUIRE(pickResult.all().size() == 1);

      const auto hit = tool.pick3D(pickRay, perspectiveCameraFor(pickRay), pickResult);

      CHECK(hit.isMatch());
      CHECK(hit.type() == ExtrudeTool::ExtrudeHitType);
      CHECK(hit.hitPoint() == vm::vec3d{-8, 0, 16});
      CHECK(hit.distance() == vm::approx{vm::length(hit.hitPoint() - pickRay.origin)});

      CHECK(
        hit.target<ExtrudeHitData>()
        == ExtrudeHitData{
          {brushNode1, *brushNode1->brush().findFace(vm::vec3d{0, 0, 1})},
          vm::line3d{hit.hitPoint(), {0, 0, 1}},
          hit.hitPoint()});
    }

    SECTION("Pick ray does not hit brush directly")
    {
      using T =
        std::tuple<vm::vec3d, vm::vec3d, vm::vec3d, vm::vec3d, vm::plane3d, vm::vec3d>;

      // clang-format off
      const auto
      [origin,     direction,     expectedFaceNormal, expectedHitPoint, expectedDragReference,     expectedHandlePosition] = GENERATE(values<T>({
      // shoot from above downwards just past the top west edge, picking the west face
      {{-17, 0, 32}, { 0, 0, -1}, {-1, 0, 0},         {-17, 0, 16},     {{-16, 0, 16}, {0, 0, 1}}, {-16, 0, 16}},
      // shoot diagonally past the top west edge, picking the west face
      {{ -1, 0, 33}, {-1, 0, -1}, {-1, 0, 0},         {-17, 0, 17},     {{-16, 0, 16}, {0, 0, 1}}, {-16, 0, 16}},
      }));
      // clang-format on

      CAPTURE(brushBounds, origin, direction);

      const auto pickRay = vm::ray3d{origin, vm::normalize(direction)};
      const auto hit = tool.pick3D(pickRay, perspectiveCameraFor(pickRay), {});

      CHECK(hit.isMatch());
      CHECK(hit.type() == ExtrudeTool::ExtrudeHitType);
      CHECK(hit.hitPoint() == expectedHitPoint);
      CHECK(hit.distance() == vm::approx{vm::length(expectedHitPoint - origin)});

      CHECK(
        hit.target<ExtrudeHitData>()
        == ExtrudeHitData{
          {brushNode1, *brushNode1->brush().findFace(expectedFaceNormal)},
          expectedDragReference,
          expectedHandlePosition});
    }
  }

  SECTION("Pick coplanar faces of two brushes")
  {
    auto& document = fixture.create();
    auto& map = document.map();

    auto tool = ExtrudeTool{document};

    auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};

    // two brushes side by side along the X axis, sharing a coplanar top face at z = 16
    auto* brushNode1 = new mdl::BrushNode{
      builder.createCuboid(vm::bbox3d{{-16, -16, -16}, {16, 16, 16}}, "material")
      | kdl::value()};
    auto* brushNode2 = new mdl::BrushNode{
      builder.createCuboid(vm::bbox3d{{16, -16, -16}, {48, 16, 16}}, "material")
      | kdl::value()};

    addNodes(map, {{map.editorContext().currentLayer(), {brushNode1, brushNode2}}});
    selectNodes(map, {brushNode1, brushNode2});

    // shoot straight down at the top face of the first brush
    const auto pickRay = vm::ray3d{{0, 0, 32}, {0, 0, -1}};

    const auto pickResult = performPick(map, tool, pickRay);

    // both coplanar top faces are chosen as drag handles, one per brush
    CHECK_THAT(
      tool.proposedDragHandles()
        | std::views::transform([](const auto& h) { return h.faceHandle.node(); }),
      UnorderedRangeEquals(std::vector<mdl::BrushNode*>{brushNode1, brushNode2}));

    CHECK_THAT(
      tool.proposedDragHandles() | std::views::transform([](const auto& h) {
        return h.faceAtDragStart().normal();
      }),
      RangeEquals(std::vector<vm::vec3d>{{0, 0, 1}, {0, 0, 1}}));
  }

  SECTION("Pick opposing coplanar faces of two brushes")
  {
    auto& document = fixture.create();
    auto& map = document.map();

    auto tool = ExtrudeTool{document};

    auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};

    // brushNode2 sits on top of brushNode1: brushNode1's top face (+Z) and brushNode2's
    // bottom face (-Z) are coincident at z = 16 but face in opposite directions.
    // brushNode2 is offset along +X so that the western half of brushNode1's top face
    // remains exposed and can be picked directly.
    auto* brushNode1 = new mdl::BrushNode{
      builder.createCuboid(vm::bbox3d{{-16, -16, -16}, {16, 16, 16}}, "material")
      | kdl::value()};
    auto* brushNode2 = new mdl::BrushNode{
      builder.createCuboid(vm::bbox3d{{0, -16, 16}, {32, 16, 48}}, "material")
      | kdl::value()};

    addNodes(map, {{map.editorContext().currentLayer(), {brushNode1, brushNode2}}});
    selectNodes(map, {brushNode1, brushNode2});

    // shoot straight down at the exposed part of the first brush's top face
    const auto pickRay = vm::ray3d{{-8, 0, 32}, {0, 0, -1}};

    const auto pickResult = performPick(map, tool, pickRay);

    // both coincident faces are chosen as drag handles, one per brush
    CHECK_THAT(
      tool.proposedDragHandles()
        | std::views::transform([](const auto& h) { return h.faceHandle.node(); }),
      UnorderedRangeEquals(std::vector<mdl::BrushNode*>{brushNode1, brushNode2}));

    // the picked coplanar face faces +Z, the opposing face faces -Z
    CHECK_THAT(
      tool.proposedDragHandles() | std::views::transform([](const auto& h) {
        return h.faceAtDragStart().normal();
      }),
      UnorderedRangeEquals(std::vector<vm::vec3d>{{0, 0, 1}, {0, 0, -1}}));
  }

  SECTION("Does not pick opposing coplanar faces that don't overlap")
  {
    auto& document = fixture.create();
    auto& map = document.map();

    auto tool = ExtrudeTool{document};

    auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};

    // brushNode1's top face (+Z) and brushNode2's bottom face (-Z) are coplanar at
    // z = 16 and face in opposite directions, but their footprints are disjoint in X
    // (brushNode1: x in [-16, 16], brushNode2: x in [64, 96]), so they don't actually
    // touch. They must NOT be linked as opposing drag handles.
    auto* brushNode1 = new mdl::BrushNode{
      builder.createCuboid(vm::bbox3d{{-16, -16, -16}, {16, 16, 16}}, "material")
      | kdl::value()};
    auto* brushNode2 = new mdl::BrushNode{
      builder.createCuboid(vm::bbox3d{{64, -16, 16}, {96, 16, 48}}, "material")
      | kdl::value()};

    addNodes(map, {{map.editorContext().currentLayer(), {brushNode1, brushNode2}}});
    selectNodes(map, {brushNode1, brushNode2});

    // shoot straight down onto brushNode1's top face
    const auto pickRay = vm::ray3d{{0, 0, 32}, {0, 0, -1}};

    const auto pickResult = performPick(map, tool, pickRay);

    // only brushNode1's top face is a drag handle; brushNode2's far-away bottom face
    // must not be linked in
    CHECK_THAT(
      tool.proposedDragHandles()
        | std::views::transform([](const auto& h) { return h.faceHandle.node(); }),
      RangeEquals(std::vector<mdl::BrushNode*>{brushNode1}));

    CHECK_THAT(
      tool.proposedDragHandles() | std::views::transform([](const auto& h) {
        return h.faceAtDragStart().normal();
      }),
      RangeEquals(std::vector<vm::vec3d>{{0, 0, 1}}));
  }

  SECTION("Pick a horizon edge handle directly")
  {
    using namespace mdl::HitFilters;

    auto& document = fixture.create();
    auto& map = document.map();

    auto tool = ExtrudeTool{document};

    auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};

    // Two brushes meeting at the plane y = 16 with identical vertices: frontBrush's +Y
    // face and backBrush's -Y face are coincident but face in opposite directions. This
    // shared seam is hidden between the brushes and cannot be picked as a face.
    auto* frontBrush = new mdl::BrushNode{
      builder.createCuboid(vm::bbox3d{{-16, -16, -16}, {16, 16, 16}}, "material")
      | kdl::value()};
    auto* backBrush = new mdl::BrushNode{
      builder.createCuboid(vm::bbox3d{{-16, 16, -16}, {16, 48, 16}}, "material")
      | kdl::value()};

    addNodes(map, {{map.editorContext().currentLayer(), {frontBrush, backBrush}}});
    selectNodes(map, {frontBrush, backBrush});

    SECTION("edge handle wins over the adjacent face it shares")
    {
      // Look at the seam from the -Y side and slightly above. The ray hits frontBrush's
      // top face just short of the seam, but passes within the handle radius of the
      // seam's top edge (a horizon edge: top face visible, +Y seam face hidden). The
      // adjacent top face is nearer than the edge, yet the edge handle still wins.
      const auto pickRay = vm::ray3d{{0, -556, 586}, vm::normalize(vm::vec3d{0, 1, -1})};

      const auto pickResult = performPick(map, tool, pickRay);

      // the hidden +Y seam face of frontBrush is chosen for extrusion
      const auto extrudeHit = pickResult.first(type(ExtrudeTool::ExtrudeHitType));
      CHECK(
        extrudeHit.target<ExtrudeHitData>().face
        == mdl::BrushFaceHandle{
          frontBrush, *frontBrush->brush().findFace(vm::vec3d{0, 1, 0})});

      // both coincident seam faces become drag handles, one per brush
      CHECK_THAT(
        tool.proposedDragHandles()
          | std::views::transform([](const auto& h) { return h.faceHandle.node(); }),
        UnorderedRangeEquals(std::vector<mdl::BrushNode*>{frontBrush, backBrush}));
      CHECK_THAT(
        tool.proposedDragHandles() | std::views::transform([](const auto& h) {
          return h.faceAtDragStart().normal();
        }),
        UnorderedRangeEquals(std::vector<vm::vec3d>{{0, 1, 0}, {0, -1, 0}}));
    }

    SECTION("an occluding face in front of the edge wins")
    {
      // A third brush sits in front of the seam, closer to the camera, so the ray hits
      // its face before reaching the seam edge. It is not adjacent to the edge and is
      // nearer, so it wins over the edge handle.
      auto* occluderBrush = new mdl::BrushNode{
        builder.createCuboid(vm::bbox3d{{-16, -100, 114}, {16, -68, 146}}, "material")
        | kdl::value()};
      addNodes(map, {{map.editorContext().currentLayer(), {occluderBrush}}});
      selectNodes(map, {occluderBrush});

      const auto pickRay = vm::ray3d{{0, -556, 586}, vm::normalize(vm::vec3d{0, 1, -1})};

      const auto pickResult = performPick(map, tool, pickRay);

      const auto extrudeHit = pickResult.first(type(ExtrudeTool::ExtrudeHitType));
      CHECK(
        extrudeHit.target<ExtrudeHitData>().face
        == mdl::BrushFaceHandle{
          occluderBrush, *occluderBrush->brush().findFace(vm::vec3d{0, -1, 0})});
    }

    SECTION("a face interior away from any edge is picked normally")
    {
      // Straight down onto the middle of frontBrush's top face, far from any edge.
      const auto pickRay = vm::ray3d{{0, 0, 32}, {0, 0, -1}};

      const auto pickResult = performPick(map, tool, pickRay);

      const auto extrudeHit = pickResult.first(type(ExtrudeTool::ExtrudeHitType));
      CHECK(
        extrudeHit.target<ExtrudeHitData>().face
        == mdl::BrushFaceHandle{
          frontBrush, *frontBrush->brush().findFace(vm::vec3d{0, 0, 1})});
    }
  }

  SECTION("Pick a horizon edge handle directly in 2D")
  {
    auto& document = fixture.create();
    auto& map = document.map();

    auto tool = ExtrudeTool{document};

    auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};

    // Two brushes meeting at the plane x = 16: leftBrush's +X face and rightBrush's -X
    // face are coincident but face in opposite directions.
    auto* leftBrush = new mdl::BrushNode{
      builder.createCuboid(vm::bbox3d{{-16, -16, -16}, {16, 16, 16}}, "material")
      | kdl::value()};
    auto* rightBrush = new mdl::BrushNode{
      builder.createCuboid(vm::bbox3d{{16, -16, -16}, {48, 16, 16}}, "material")
      | kdl::value()};

    addNodes(map, {{map.editorContext().currentLayer(), {leftBrush, rightBrush}}});
    selectNodes(map, {leftBrush, rightBrush});

    // Top view (looking -Z). The ray hits leftBrush's top face just short of the seam at
    // x = 16, but passes within the handle radius of the seam's top edge.
    const auto pickRay = vm::ray3d{{12, 0, 32}, {0, 0, -1}};

    auto pickResult = mdl::PickResult::byDistance();
    pick(map, pickRay, pickResult);

    const auto hit = tool.pick2D(pickRay, orthographicCameraFor(pickRay), pickResult);
    REQUIRE(hit.isMatch());
    pickResult.addHit(hit);
    tool.updateProposedDragHandles(pickResult);

    // both coincident seam faces become drag handles, one per brush
    CHECK_THAT(
      tool.proposedDragHandles()
        | std::views::transform([](const auto& h) { return h.faceHandle.node(); }),
      UnorderedRangeEquals(std::vector<mdl::BrushNode*>{leftBrush, rightBrush}));
    CHECK_THAT(
      tool.proposedDragHandles() | std::views::transform([](const auto& h) {
        return h.faceAtDragStart().normal();
      }),
      UnorderedRangeEquals(std::vector<vm::vec3d>{{1, 0, 0}, {-1, 0, 0}}));
  }

  SECTION("A single brush's own edge does not override its adjacent face")
  {
    using namespace mdl::HitFilters;

    auto& document = fixture.create();
    auto& map = document.map();

    auto tool = ExtrudeTool{document};

    auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};

    // A single brush with no touching neighbor. Its own top/+Y edge is still a horizon
    // edge for this ray (top face visible, +Y face back facing) and is within the handle
    // radius of the seam's top edge -- the same ray used in "edge handle wins over the
    // adjacent face it shares" above, which does have a touching backBrush. With no other
    // brush to prove this is a genuine touching seam, the face must win instead of the
    // edge.
    auto* frontBrush = new mdl::BrushNode{
      builder.createCuboid(vm::bbox3d{{-16, -16, -16}, {16, 16, 16}}, "material")
      | kdl::value()};

    addNodes(map, {{map.editorContext().currentLayer(), {frontBrush}}});
    selectNodes(map, {frontBrush});

    const auto pickRay = vm::ray3d{{0, -556, 586}, vm::normalize(vm::vec3d{0, 1, -1})};

    const auto pickResult = performPick(map, tool, pickRay);

    const auto extrudeHit = pickResult.first(type(ExtrudeTool::ExtrudeHitType));
    CHECK(
      extrudeHit.target<ExtrudeHitData>().face
      == mdl::BrushFaceHandle{
        frontBrush, *frontBrush->brush().findFace(vm::vec3d{0, 0, 1})});
  }

  SECTION("A non-collinear edge of a different touching brush does not override the face")
  {
    using namespace mdl::HitFilters;

    auto& document = fixture.create();
    auto& map = document.map();

    auto tool = ExtrudeTool{document};

    auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};

    auto* frontBrush = new mdl::BrushNode{
      builder.createCuboid(vm::bbox3d{{-16, -16, -16}, {16, 16, 16}}, "material")
      | kdl::value()};

    // Touches frontBrush's +X face, at a right angle to the +Y seam edge under test, so
    // its edges are not collinear with it and cannot prove that edge is a genuine seam.
    auto* sideBrush = new mdl::BrushNode{
      builder.createCuboid(vm::bbox3d{{16, -16, -16}, {48, 16, 16}}, "material")
      | kdl::value()};

    addNodes(map, {{map.editorContext().currentLayer(), {frontBrush, sideBrush}}});
    selectNodes(map, {frontBrush, sideBrush});

    const auto pickRay = vm::ray3d{{0, -556, 586}, vm::normalize(vm::vec3d{0, 1, -1})};

    const auto pickResult = performPick(map, tool, pickRay);

    const auto extrudeHit = pickResult.first(type(ExtrudeTool::ExtrudeHitType));
    CHECK(
      extrudeHit.target<ExtrudeHitData>().face
      == mdl::BrushFaceHandle{
        frontBrush, *frontBrush->brush().findFace(vm::vec3d{0, 0, 1})});
  }

  SECTION("findDragFaces")
  {
    // https://github.com/TrenchBroom/TrenchBroom/issues/3726

    using T = std::tuple<std::filesystem::path, std::vector<std::string>>;

    // clang-format off
    const auto 
    [mapName,                              expectedDragFaceMaterialNames] = GENERATE(values<T>({
    {"findDragFaces_noCoplanarFaces.map",  {"larger_top_face"}},
    {"findDragFaces_twoCoplanarFaces.map", {"larger_top_face", "smaller_top_face"}}
    }));
    // clang-format on

    const auto mapPath = "test/ui/ExtrudeTool" / mapName;
    auto& document = fixture.load(mapPath, {.mapFormat = mdl::MapFormat::Valve});
    auto& map = document.map();

    auto tool = ExtrudeTool{document};

    selectAllNodes(map);

    auto brushes = map.selection().brushes;
    REQUIRE(brushes.size() == 2);

    const auto brushIt =
      std::ranges::find_if(brushes, [](const mdl::BrushNode* brushNode) {
        return brushNode->brush().findFace("larger_top_face").has_value();
      });
    REQUIRE(brushIt != std::end(brushes));

    const auto* brushNode = *brushIt;
    const auto& largerTopFace =
      brushNode->brush().face(brushNode->brush().findFace("larger_top_face").value());

    // Find the entity defining the camera position for our test
    auto* cameraEntity =
      (map.selection().entities | std::views::filter([](const auto* e) {
         return e->entity().classname() == "trigger_relay";
       }))
        .front();

    // Fire a pick ray at largerTopFace
    const auto pickRay = vm::ray3d{
      cameraEntity->entity().origin(),
      vm::normalize(largerTopFace.center() - cameraEntity->entity().origin())};

    const auto pickResult = performPick(map, tool, pickRay);
    REQUIRE(
      pickResult.all().front().target<mdl::BrushFaceHandle>().face() == largerTopFace);

    CHECK_THAT(
      tool.proposedDragHandles() | std::views::transform([](const auto& h) {
        return h.faceAtDragStart().materialName();
      }),
      UnorderedRangeEquals(expectedDragFaceMaterialNames));
  }

  SECTION("splitBrushes")
  {
    using namespace mdl::HitFilters;


    const auto mapPath = "test/ui/ExtrudeTool/splitBrushes.map";
    auto& document = fixture.load(mapPath, {.mapFormat = mdl::MapFormat::Valve});
    auto& map = document.map();

    auto tool = ExtrudeTool{document};

    selectAllNodes(map);

    auto brushes = map.selection().brushes;
    REQUIRE(brushes.size() == 2);

    // Find the entity defining the camera position for our test
    const auto* cameraEntity =
      (map.selection().entities | std::views::filter([](const auto* node) {
         return node->entity().classname() == "trigger_relay";
       }))
        .front();

    const auto* cameraTarget =
      (map.selection().entities | std::views::filter([](const auto* node) {
         return node->entity().classname() == "info_null";
       }))
        .front();

    const auto* funcDetailNode =
      (mdl::filterEntityNodes(mdl::collectDescendants({&map.worldNode()}))
       | std::views::filter(
         [](const auto* node) { return node->entity().classname() == "func_detail"; }))
        .front();

    // Fire a pick ray at cameraTarget
    const auto pickRay = vm::ray3d(
      cameraEntity->entity().origin(),
      vm::normalize(cameraTarget->entity().origin() - cameraEntity->entity().origin()));

    const auto pickResult = performPick(map, tool, pickRay);

    // We are going to drag the 2 faces with +Y normals
    CHECK_THAT(
      tool.proposedDragHandles() | std::views::transform([](const auto& h) {
        return h.faceAtDragStart().normal();
      }),
      RangeEquals(std::vector<vm::vec3d>{{0, 1, 0}, {0, 1, 0}}));

    const auto hit = pickResult.first(type(ExtrudeTool::ExtrudeHitType));
    auto dragState = ExtrudeDragState{
      tool.proposedDragHandles(),
      ExtrudeTool::getDragFaces(tool.proposedDragHandles()),
      false,
      vm::vec3d{0, 0, 0}};

    SECTION("reuse split pieces under different parents as pieces disappear and return")
    {
      dragState.splitBrushes = true;
      const auto initialSelection = map.selection().nodes;
      tool.beginExtrude();
      for (const auto distance : {-16.0, -32.0, -48.0, -56.0, -32.0, 8.0, 16.0})
      {
        CAPTURE(distance);
        REQUIRE(tool.extrude({0, distance, 0}, dragState));
        REQUIRE(dragState.splitBrushNodes.size() == 2);
        for (size_t i = 0; i < dragState.initialDragHandles.size(); ++i)
        {
          const auto& handle = dragState.initialDragHandles[i];
          const auto initialBounds = handle.brushAtDragStart.bounds();
          auto* source = handle.faceHandle.node();
          auto* preview = dragState.splitBrushNodes[i];
          const auto cut = initialBounds.max.y() + distance;
          if (cut <= initialBounds.min.y())
          {
            CHECK(preview == nullptr);
            CHECK(source->logicalBounds() == initialBounds);
          }
          else
          {
            REQUIRE(preview != nullptr);
            CHECK(preview->parent() == source->parent());
            auto expectedPreview = initialBounds;
            expectedPreview.max[1] = cut;
            if (distance > 0.0)
            {
              expectedPreview.min[1] = initialBounds.max.y();
            }
            CHECK(preview->logicalBounds() == expectedPreview);
            auto expectedSource = initialBounds;
            if (distance < 0.0)
            {
              expectedSource.min[1] = cut;
            }
            CHECK(source->logicalBounds() == expectedSource);
          }
        }
      }
      tool.cancel();
      CHECK(map.selection().nodes == initialSelection);
      for (const auto& handle : dragState.initialDragHandles)
      {
        CHECK(handle.faceHandle.node()->brush() == handle.brushAtDragStart);
      }
    }

    SECTION("split brushes inwards 32 units towards -Y")
    {
      const auto delta = vm::vec3d(0, -32, 0);

      dragState.splitBrushes = true;
      tool.beginExtrude();

      REQUIRE(tool.extrude(delta, dragState));
      tool.commit(dragState);

      CHECK(map.selection().brushes.size() == 4);

      SECTION("check 2 resulting worldspawn brushes")
      {
        const auto nodes =
          mdl::filterBrushNodes(map.editorContext().currentLayer()->children());
        const auto bounds = nodes | std::views::transform([](const auto* node) {
                              return node->logicalBounds();
                            });

        CHECK_THAT(
          bounds,
          UnorderedRangeEquals(std::vector<vm::bbox3d>{
            {{-32, 144, 16}, {-16, 192, 32}},
            {{-32, 192, 16}, {-16, 224, 32}},
          }));
      }

      SECTION("check 2 resulting func_detail brushes")
      {
        const auto nodes = mdl::filterBrushNodes(funcDetailNode->children());
        const auto bounds = nodes | std::views::transform([](const auto* node) {
                              return node->logicalBounds();
                            });

        CHECK_THAT(
          bounds,
          UnorderedRangeEquals(std::vector<vm::bbox3d>{
            {{-16, 176, 16}, {16, 192, 32}},
            {{-16, 192, 16}, {16, 224, 32}},
          }));
      }

      CHECK_THAT(
        map.selection().brushes | std::views::transform([](const auto* brushNode) {
          return brushNode->linkId();
        }) | kdl::ranges::to<std::vector>(),
        AllDifferent<std::vector<std::string>>());
    }

    SECTION("split brushes inwards 48 units towards -Y")
    {
      const auto delta = vm::vec3d(0, -48, 0);

      dragState.splitBrushes = true;
      tool.beginExtrude();

      REQUIRE(tool.extrude(delta, dragState));
      tool.commit(dragState);

      CHECK(map.selection().brushes.size() == 3);

      SECTION("check 2 resulting worldspawn brushes")
      {
        const auto nodes =
          mdl::filterBrushNodes(map.editorContext().currentLayer()->children());
        const auto bounds = nodes | std::views::transform([](const auto* node) {
                              return node->logicalBounds();
                            });

        CHECK_THAT(
          bounds,
          UnorderedRangeEquals(std::vector<vm::bbox3d>{
            {{-32, 144, 16}, {-16, 176, 32}},
            {{-32, 176, 16}, {-16, 224, 32}},
          }));
      }

      SECTION("check 1 resulting func_detail brush")
      {
        const auto nodes = mdl::filterBrushNodes(funcDetailNode->children());
        const auto bounds = nodes | std::views::transform([](const auto* node) {
                              return node->logicalBounds();
                            });

        CHECK_THAT(
          bounds,
          UnorderedRangeEquals(std::vector<vm::bbox3d>{{{-16, 176, 16}, {16, 224, 32}}}));
      }
    }

    SECTION("extrude inwards 32 units towards -Y")
    {
      const auto delta = vm::vec3d{0, -32, 0};

      dragState.splitBrushes = false;
      tool.beginExtrude();

      REQUIRE(tool.extrude(delta, dragState));
      tool.commit(dragState);

      CHECK(map.selection().brushes.size() == 2);

      SECTION("check 1 resulting worldspawn brushes")
      {
        const auto nodes =
          mdl::filterBrushNodes(map.editorContext().currentLayer()->children());
        const auto bounds = nodes | std::views::transform([](const auto* node) {
                              return node->logicalBounds();
                            });

        CHECK_THAT(
          bounds,
          UnorderedRangeEquals(std::vector<vm::bbox3d>{
            {{-32, 144, 16}, {-16, 192, 32}},
          }));
      }

      SECTION("check 1 resulting func_detail brush")
      {
        const auto nodes = mdl::filterBrushNodes(funcDetailNode->children());
        const auto bounds = nodes | std::views::transform([](const auto* node) {
                              return node->logicalBounds();
                            });

        CHECK_THAT(
          bounds,
          UnorderedRangeEquals(std::vector<vm::bbox3d>{{{-16, 176, 16}, {16, 192, 32}}}));
      }
    }

    SECTION("split brushes outwards 16 units towards +Y")
    {
      const auto delta = vm::vec3d{0, 16, 0};

      dragState.splitBrushes = true;
      tool.beginExtrude();

      REQUIRE(tool.extrude(delta, dragState));
      tool.commit(dragState);

      CHECK(map.selection().brushes.size() == 2);

      SECTION("check 1 resulting worldspawn brush")
      {
        const auto bounds =
          mdl::filterBrushNodes(map.editorContext().currentLayer()->children())
          | std::views::filter([](const auto* node) { return node->selected(); })
          | std::views::transform([](const auto* node) { return node->logicalBounds(); })
          | kdl::ranges::to<std::vector>();

        CHECK_THAT(
          bounds,
          UnorderedEquals(std::vector<vm::bbox3d>{
            {{-32, 224, 16}, {-16, 240, 32}},
          }));
      }

      SECTION("check 1 resulting func_detail brush")
      {
        const auto bounds =
          mdl::filterBrushNodes(funcDetailNode->children())
          | std::views::filter([](const auto* node) { return node->selected(); })
          | std::views::transform([](const auto* node) { return node->logicalBounds(); })
          | kdl::ranges::to<std::vector>();

        CHECK_THAT(
          bounds,
          UnorderedEquals(std::vector<vm::bbox3d>{{{-16, 224, 16}, {16, 240, 32}}}));
      }

      CHECK_THAT(
        map.selection().brushes | std::views::transform([](const auto* brushNode) {
          return brushNode->linkId();
        }) | kdl::ranges::to<std::vector>(),
        AllDifferent<std::vector<std::string>>());
    }
  }

  SECTION("split preview reuses nodes and collates updates")
  {
    auto& document = fixture.create();
    auto& map = document.map();
    auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
    auto* source = new mdl::BrushNode{
      builder.createCuboid(vm::bbox3d{32.0}, "material") | kdl::value()};
    addNodes(map, {{map.editorContext().currentLayer(), {source}}});
    selectNodes(map, {source});
    map.setIsCommandCollationEnabled(true);

    const auto inward = GENERATE(false, true);
    const auto cancel = GENERATE(false, true);
    CAPTURE(inward, cancel);
    auto tool = ExtrudeTool{document};
    performPick(map, tool, vm::ray3d{{0, 0, 64}, {0, 0, -1}});
    auto state = ExtrudeDragState{
      tool.proposedDragHandles(),
      ExtrudeTool::getDragFaces(tool.proposedDragHandles()),
      true};
    const auto initialCount = source->parent()->childCount();
    const auto initialModificationCount = map.modificationCount();
    const auto delta = [&](const double distance) {
      return vm::vec3d{0, 0, inward ? -distance : distance};
    };
    tool.beginExtrude();
    REQUIRE(tool.extrude(delta(8), state));
    auto* preview = state.currentDragFaces.front().node();
    REQUIRE(preview != source);
    const auto selection = map.selection().nodes;
    const auto linkId = preview->linkId();

    auto added = 0;
    auto removed = 0;
    auto selectionChanges = 0;
    auto contentChanges = 0;
    auto rollbacks = 0;
    auto connection = NotifierConnection{};
    connection += map.nodesWereAddedNotifier.connect([&](const auto&) { ++added; });
    connection += map.nodesWereRemovedNotifier.connect([&](const auto&) { ++removed; });
    connection +=
      map.selectionDidChangeNotifier.connect([&](const auto&) { ++selectionChanges; });
    connection +=
      map.nodesDidChangeNotifier.connect([&](const auto&) { ++contentChanges; });
    connection += map.commandProcessor().transactionUndoneNotifier.connect(
      [&](const auto&, const auto, const auto) { ++rollbacks; });

    for (int i = 0; i < 100; ++i)
    {
      const auto distance = double(9 + i % 16);
      REQUIRE(tool.extrude(delta(distance), state));
      REQUIRE(state.currentDragFaces.size() == 1);
      CHECK(state.currentDragFaces.front().node() == preview);
      CHECK(preview->logicalBounds().max.z() == 32.0 + delta(distance).z());
      CHECK(preview->linkId() == linkId);
      CHECK(map.selection().nodes == selection);
    }
    CHECK(added == 0);
    CHECK(removed == 0);
    CHECK(selectionChanges == 0);
    CHECK(rollbacks == 0);
    CHECK(contentChanges == 100);

    const auto finalPreview = preview->brush();
    const auto finalSource = source->brush();
    contentChanges = 0;
    if (cancel)
    {
      tool.cancel();
    }
    else
    {
      tool.commit(state);
      map.undoCommand();
    }
    // Undo work stays bounded even after a long drag.
    CHECK(contentChanges <= 2);
    CHECK(source->logicalBounds() == vm::bbox3d{32.0});
    CHECK(source->parent()->childCount() == initialCount);
    CHECK(map.selection().nodes == std::vector<mdl::Node*>{source});
    CHECK(map.modificationCount() == initialModificationCount);
    if (!cancel)
    {
      map.redoCommand();
      CHECK(source->parent()->childCount() == initialCount + 1);
      CHECK(map.selection().nodes == selection);
      CHECK(preview->brush() == finalPreview);
      CHECK(source->brush() == finalSource);
    }
  }

  SECTION("split extrusion and a subsequent move have separate undo steps")
  {
    auto& document = fixture.create();
    auto& map = document.map();
    auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
    auto* source = new mdl::BrushNode{
      builder.createCuboid(vm::bbox3d{32.0}, "material") | kdl::value()};
    addNodes(map, {{map.editorContext().currentLayer(), {source}}});
    selectNodes(map, {source});
    // Match the editor: fixture defaults disable collation between undo entries.
    map.setIsCommandCollationEnabled(true);

    const auto inward = GENERATE(false, true);
    CAPTURE(inward);
    auto tool = ExtrudeTool{document};
    performPick(map, tool, vm::ray3d{{0, 0, 64}, {0, 0, -1}});
    auto state = ExtrudeDragState{
      tool.proposedDragHandles(),
      ExtrudeTool::getDragFaces(tool.proposedDragHandles()),
      true};
    tool.beginExtrude();
    REQUIRE(tool.extrude({0, 0, inward ? -8.0 : 8.0}, state));
    REQUIRE(tool.extrude({0, 0, inward ? -16.0 : 16.0}, state));
    tool.commit(state);
    REQUIRE(source->parent()->childCount() == 2);
    const auto selectedBrushes = [&] {
      return map.selection().brushes
             | std::views::transform([](const auto* node) { return node->brush(); })
             | kdl::ranges::to<std::vector>();
    };
    const auto splitBrushes = selectedBrushes();
    auto moveTool = MoveObjectsTool{document};
    auto input = InputState{0.0f, 0.0f};
    REQUIRE(moveTool.startMove(input));
    REQUIRE(moveTool.move(input, {0, 0, 8}) == MoveObjectsTool::MoveResult::Continue);
    moveTool.endMove(input);
    const auto movedBrushes = selectedBrushes();

    // No delay: both drags finish inside the normal command collation interval.
    map.undoCommand();
    CHECK(source->parent()->childCount() == 2);
    CHECK(selectedBrushes() == splitBrushes);
    map.undoCommand();
    CHECK(source->parent()->childCount() == 1);
    CHECK(source->logicalBounds() == vm::bbox3d{32.0});
    CHECK(map.selection().nodes == std::vector<mdl::Node*>{source});
    map.redoCommand();
    CHECK(source->parent()->childCount() == 2);
    CHECK(selectedBrushes() == splitBrushes);
    map.redoCommand();
    CHECK(source->parent()->childCount() == 2);
    CHECK(selectedBrushes() == movedBrushes);
  }

  SECTION("split preview handles direction changes, clipping and invalid positions")
  {
    auto& document = fixture.create();
    auto& map = document.map();
    auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
    auto* source = new mdl::BrushNode{
      builder.createCuboid(vm::bbox3d{32.0}, "material") | kdl::value()};
    addNodes(map, {{map.editorContext().currentLayer(), {source}}});
    selectNodes(map, {source});
    auto tool = ExtrudeTool{document};
    performPick(map, tool, vm::ray3d{{0, 0, 64}, {0, 0, -1}});
    auto state = ExtrudeDragState{
      tool.proposedDragHandles(),
      ExtrudeTool::getDragFaces(tool.proposedDragHandles()),
      true};
    const auto initialCount = source->parent()->childCount();
    tool.beginExtrude();
    for (const auto distance : {8.0, 16.0, -8.0, -16.0, -64.0, -72.0, -8.0, 8.0})
    {
      CAPTURE(distance);
      REQUIRE(tool.extrude({0, 0, distance}, state));
      if (distance <= -64.0)
      {
        CHECK(state.currentDragFaces.empty());
        CHECK(source->parent()->childCount() == initialCount);
        CHECK(source->logicalBounds() == vm::bbox3d{32.0});
      }
      else
      {
        REQUIRE(state.currentDragFaces.size() == 1);
        CHECK(source->parent()->childCount() == initialCount + 1);
        const auto* preview = state.currentDragFaces.front().node();
        CHECK(preview->logicalBounds().max.z() == 32.0 + distance);
        CHECK(
          source->logicalBounds().min.z() == (distance < 0.0 ? 32.0 + distance : -32.0));
      }
    }

    const auto faces = state.currentDragFaces;
    const auto selection = map.selection().nodes;
    REQUIRE_FALSE(tool.extrude({0, 0, 1e9}, state));
    CHECK(state.currentDragFaces == faces);
    CHECK(state.totalDelta == vm::vec3d{0, 0, 8});
    CHECK(map.selection().nodes == selection);
    CHECK(faces.front().node()->logicalBounds().max.z() == 40.0);

    REQUIRE(tool.extrude(vm::vec3d::zero(), state));
    CHECK(source->parent()->childCount() == initialCount);
    CHECK(source->logicalBounds() == vm::bbox3d{32.0});
    CHECK(state.totalDelta == vm::vec3d::zero());
    CHECK(state.splitBrushNodes.empty());
    CHECK(map.selection().nodes == std::vector<mdl::Node*>{source});
    tool.commit(state);
  }

  SECTION("split preview propagates content changes to linked groups")
  {
    auto& document = fixture.create();
    auto& map = document.map();
    auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
    auto* source = new mdl::BrushNode{
      builder.createCuboid(vm::bbox3d{32.0}, "material") | kdl::value()};
    addNodes(map, {{map.editorContext().currentLayer(), {source}}});
    selectNodes(map, {source});
    auto* group = groupSelectedNodes(map, "source");
    REQUIRE(group != nullptr);
    auto* linked = createLinkedDuplicate(map);
    REQUIRE(linked != nullptr);
    deselectAll(map);
    openGroup(map, *group);
    selectNodes(map, {source});
    auto tool = ExtrudeTool{document};
    performPick(map, tool, vm::ray3d{{0, 0, 64}, {0, 0, -1}});
    auto state = ExtrudeDragState{
      tool.proposedDragHandles(),
      ExtrudeTool::getDragFaces(tool.proposedDragHandles()),
      true};
    REQUIRE(state.initialDragHandles.size() == 1);
    const auto inward = GENERATE(false, true);
    const auto bounds = [](const mdl::GroupNode* node) {
      return mdl::filterBrushNodes(node->children())
             | std::views::transform(
               [](const auto* brush) { return brush->logicalBounds(); })
             | kdl::ranges::to<std::vector>();
    };
    tool.beginExtrude();
    for (const auto distance : {8.0, 16.0, 24.0})
    {
      REQUIRE(tool.extrude({0, 0, inward ? -distance : distance}, state));
      REQUIRE(group->childCount() == 2);
      CHECK_THAT(bounds(linked), UnorderedRangeEquals(bounds(group)));
    }
    tool.commit(state);
    map.undoCommand();
    CHECK(group->childCount() == 1);
    CHECK_THAT(bounds(linked), UnorderedRangeEquals(bounds(group)));
    map.redoCommand();
    CHECK(group->childCount() == 2);
    CHECK_THAT(bounds(linked), UnorderedRangeEquals(bounds(group)));
  }

  SECTION("split preview preserves the last valid state when a linked update fails")
  {
    auto& document = fixture.create();
    auto& map = document.map();
    auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
    auto* source = new mdl::BrushNode{
      builder.createCuboid(vm::bbox3d{32.0}, "material") | kdl::value()};
    addNodes(map, {{map.editorContext().currentLayer(), {source}}});
    selectNodes(map, {source});
    auto* group = groupSelectedNodes(map, "source");
    REQUIRE(group != nullptr);
    auto* linked = createLinkedDuplicate(map);
    REQUIRE(linked != nullptr);
    deselectAll(map);
    selectNodes(map, {linked});
    REQUIRE(translateSelection(map, {0, 0, map.worldBounds().max.z() - 48.0}));
    deselectAll(map);
    openGroup(map, *group);
    selectNodes(map, {source});
    auto tool = ExtrudeTool{document};
    performPick(map, tool, vm::ray3d{{0, 0, 64}, {0, 0, -1}});
    auto state = ExtrudeDragState{
      tool.proposedDragHandles(),
      ExtrudeTool::getDragFaces(tool.proposedDragHandles()),
      true};
    REQUIRE(state.initialDragHandles.size() == 1);
    const auto previousDistance = GENERATE(0.0, 8.0, -8.0);
    CAPTURE(previousDistance);
    tool.beginExtrude();
    REQUIRE(tool.extrude({0, 0, previousDistance}, state));
    const auto previousBrush = state.currentDragFaces.front().node()->brush();
    const auto previousSource = source->brush();
    const auto previousLinkedBounds = linked->logicalBounds();
    const auto previousCount = group->childCount();
    // Valid in the source group, but the linked piece would exceed world bounds.
    REQUIRE_FALSE(tool.extrude({0, 0, 24}, state));
    CHECK(state.totalDelta == vm::vec3d{0, 0, previousDistance});
    REQUIRE(state.currentDragFaces.size() == 1);
    CHECK(state.currentDragFaces.front().node()->brush() == previousBrush);
    CHECK(source->brush() == previousSource);
    CHECK(linked->logicalBounds() == previousLinkedBounds);
    CHECK(group->childCount() == previousCount);
    CHECK(linked->childCount() == previousCount);
    REQUIRE(tool.extrude({0, 0, 12}, state));
    CHECK(state.currentDragFaces.front().node()->logicalBounds().max.z() == 44.0);
    tool.cancel();
    CHECK(group->childCount() == 1);
    CHECK(linked->childCount() == 1);
    CHECK(source->logicalBounds() == vm::bbox3d{32.0});
    CHECK(map.selection().nodes == std::vector<mdl::Node*>{source});
  }

  SECTION("stamp")
  {
    auto& document = fixture.create();
    auto& map = document.map();

    auto tool = ExtrudeTool{document};

    constexpr auto brushBounds = vm::bbox3d{16.0};

    auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
    auto brush =
      builder.createCuboid(brushBounds, "left", "right", "front", "back", "top", "bottom")
      | kdl::value();
    for (size_t i = 0; i < brush.faceCount(); ++i)
    {
      auto& face = brush.face(i);
      REQUIRE(face
                .setUvAttributes(mdl::UvAttributes{
                  vm::vec2f{float(i) + 0.25f, float(i) + 0.5f},
                  vm::vec2f{0.5f + float(i) * 0.125f, 1.25f + float(i) * 0.125f},
                  15.0f + float(i) * 7.0f})
                .is_success());
      auto surfaceAttributes = face.surfaceAttributes();
      surfaceAttributes.flags = int(i) + 1;
      face.setSurfaceAttributes(surfaceAttributes);
    }
    auto* brushNode1 = new mdl::BrushNode{std::move(brush)};

    addNodes(map, {{map.editorContext().currentLayer(), {brushNode1}}});
    selectNodes(map, {brushNode1});

    // shoot straight down at the top face (+Z, material "top")
    const auto pickRay = vm::ray3d{{0, 0, 32}, {0, 0, -1}};
    const auto pickResult = performPick(map, tool, pickRay);

    auto dragState = ExtrudeDragState{
      tool.proposedDragHandles(),
      ExtrudeTool::getDragFaces(tool.proposedDragHandles()),
      false,
      vm::vec3d{0, 0, 0}};

    const auto delta = vm::vec3d{0, 0, 16};

    tool.beginStamp();
    REQUIRE(tool.stamp(delta, dragState));
    tool.commit(dragState);

    SECTION("the original brush is left unchanged and deselected")
    {
      CHECK(brushNode1->logicalBounds() == brushBounds);
      CHECK(!brushNode1->selected());
    }

    SECTION(
      "a new brush is created and selected, shaped as the convex hull of the "
      "dragged face's original and translated vertices")
    {
      const auto brushes = map.selection().brushes;
      REQUIRE(brushes.size() == 1);

      auto* newBrushNode = brushes.front();
      CHECK(newBrushNode != brushNode1);
      CHECK(newBrushNode->logicalBounds() == vm::bbox3d{{-16, -16, 16}, {16, 16, 32}});

      // The caps inherit the dragged face, while each side continues the material of
      // the source brush face adjacent to the corresponding seam edge.
      CHECK_THAT(
        newBrushNode->brush().faces() | std::views::transform([](const auto& face) {
          return face.materialName();
        }) | kdl::ranges::to<std::vector>(),
        UnorderedRangeEquals(
          std::vector<std::string>{"left", "right", "front", "back", "top", "top"}));

      const auto& sourceCap =
        brushNode1->brush().face(*brushNode1->brush().findFace("top"));
      for (const auto* sourceEdge : sourceCap.edges())
      {
        const auto* sourceSideGeometry = sourceEdge->firstFace() == sourceCap.geometry()
                                           ? sourceEdge->secondFace()
                                           : sourceEdge->firstFace();
        const auto sourceSideIndex = sourceSideGeometry->payload();
        REQUIRE(sourceSideIndex);
        const auto& sourceSide = brushNode1->brush().face(*sourceSideIndex);
        const auto targetSideIndex =
          newBrushNode->brush().findFace(sourceSide.materialName());
        REQUIRE(targetSideIndex);
        const auto& targetSide = newBrushNode->brush().face(*targetSideIndex);
        const auto seam = sourceEdge->segment();

        CHECK(targetSide.uvAttributes().scale == sourceSide.uvAttributes().scale);
        CHECK(targetSide.uvAttributes().rotation == sourceSide.uvAttributes().rotation);
        CHECK(targetSide.surfaceAttributes() == sourceSide.surfaceAttributes());
        CHECK(
          targetSide.uvCoords(seam.start())
          == vm::approx{sourceSide.uvCoords(seam.start()), 0.0001f});
        CHECK(
          targetSide.uvCoords(seam.end())
          == vm::approx{sourceSide.uvCoords(seam.end()), 0.0001f});
      }
    }
  }

  SECTION("stamp wraps UVs across angled side faces")
  {
    auto& document = fixture.create();
    auto& map = document.map();

    auto tool = ExtrudeTool{document};
    auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
    auto brush = builder.createBrush(
                   std::vector<vm::vec3d>{
                     {-16, -16, -16},
                     {16, -16, -16},
                     {16, 16, -16},
                     {-16, 16, -16},
                     {-8, -16, 16},
                     {24, -16, 16},
                     {24, 16, 16},
                     {-8, 16, 16},
                   },
                   "material")
                 | kdl::value();

    const auto sourceCapIndex = brush.findFace(vm::vec3d{0, 0, 1});
    REQUIRE(sourceCapIndex);
    for (size_t i = 0; i < brush.faceCount(); ++i)
    {
      auto& face = brush.face(i);
      face.setMaterialName(i == *sourceCapIndex ? "cap" : "side" + std::to_string(i));
      REQUIRE(face
                .setUvAttributes(mdl::UvAttributes{
                  vm::vec2f{float(i) + 0.25f, float(i) + 0.5f},
                  vm::vec2f{0.5f + float(i) * 0.125f, 1.25f + float(i) * 0.125f},
                  15.0f + float(i) * 7.0f})
                .is_success());
    }

    auto* brushNode = new mdl::BrushNode{std::move(brush)};
    addNodes(map, {{map.editorContext().currentLayer(), {brushNode}}});
    selectNodes(map, {brushNode});

    const auto pickResult = performPick(map, tool, vm::ray3d{{4, 0, 32}, {0, 0, -1}});
    auto dragState = ExtrudeDragState{
      tool.proposedDragHandles(),
      ExtrudeTool::getDragFaces(tool.proposedDragHandles()),
      false,
      vm::vec3d{0, 0, 0}};

    tool.beginStamp();
    REQUIRE(tool.stamp(vm::vec3d{0, 0, 16}, dragState));
    tool.commit(dragState);

    const auto brushes = map.selection().brushes;
    REQUIRE(brushes.size() == 1u);
    const auto& stampedBrush = brushes.front()->brush();
    const auto& sourceCap = brushNode->brush().face(*sourceCapIndex);

    auto foundAngledSide = false;
    for (const auto* sourceEdge : sourceCap.edges())
    {
      const auto* sourceSideGeometry = sourceEdge->firstFace() == sourceCap.geometry()
                                         ? sourceEdge->secondFace()
                                         : sourceEdge->firstFace();
      const auto sourceSideIndex = sourceSideGeometry->payload();
      REQUIRE(sourceSideIndex);
      const auto& sourceSide = brushNode->brush().face(*sourceSideIndex);
      const auto stampedSideIndex = stampedBrush.findFace(sourceSide.materialName());
      REQUIRE(stampedSideIndex);
      const auto& stampedSide = stampedBrush.face(*stampedSideIndex);
      const auto seam = sourceEdge->segment();

      foundAngledSide =
        foundAngledSide || !vm::is_parallel(sourceSide.normal(), stampedSide.normal());
      CHECK(
        stampedSide.uvCoords(seam.start())
        == vm::approx{sourceSide.uvCoords(seam.start()), 0.0001f});
      CHECK(
        stampedSide.uvCoords(seam.end())
        == vm::approx{sourceSide.uvCoords(seam.end()), 0.0001f});
    }
    CHECK(foundAngledSide);
  }

  SECTION("stamp removes inherited UV skew while preserving the seam")
  {
    auto& document = fixture.create({.mapFormat = mdl::MapFormat::Valve});
    auto& map = document.map();

    auto materials = std::vector<gl::Material>{};
    materials.emplace_back("skew-side", gl::createTextureResource(gl::Texture{64, 32}));
    auto materialCollections = std::vector<gl::MaterialCollection>{};
    materialCollections.emplace_back(std::move(materials));
    map.materialManager().setMaterialCollections(std::move(materialCollections));

    auto tool = ExtrudeTool{document};
    auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
    auto brush =
      builder.createCuboid(
        vm::bbox3d{16.0}, "left", "right", "skew-side", "back", "cap", "bottom")
      | kdl::value();

    const auto sourceCapIndex = brush.findFace("cap");
    const auto sourceSideIndex = brush.findFace("skew-side");
    REQUIRE(sourceCapIndex);
    REQUIRE(sourceSideIndex);
    auto& sourceSide = brush.face(*sourceSideIndex);
    sourceSide.restoreUvCoordSystemSnapshot(
      mdl::UvCoordSystemSnapshot{vm::vec3d{1, 0, 0}, vm::vec3d{0.5, 0, 1}});
    REQUIRE(sourceSide
              .setUvAttributes(
                mdl::UvAttributes{vm::vec2f{13.0f, -7.0f}, vm::vec2f{1.0f, 1.0f}, 0.0f})
              .is_success());

    const auto sourceSkew =
      mdl::measureUvSkew(sourceSide.uAxis(), sourceSide.vAxis(), sourceSide.normal());
    REQUIRE(sourceSkew);
    CHECK(*sourceSkew > 20.0f);

    auto* brushNode = new mdl::BrushNode{std::move(brush)};
    addNodes(map, {{map.editorContext().currentLayer(), {brushNode}}});
    selectNodes(map, {brushNode});

    performPick(map, tool, vm::ray3d{{0, 0, 32}, {0, 0, -1}});
    auto dragState = ExtrudeDragState{
      tool.proposedDragHandles(),
      ExtrudeTool::getDragFaces(tool.proposedDragHandles()),
      false,
      vm::vec3d{0, 0, 0}};

    tool.beginStamp();
    REQUIRE(tool.stamp(vm::vec3d{0, 0, 16}, dragState));
    tool.commit(dragState);

    const auto brushes = map.selection().brushes;
    REQUIRE(brushes.size() == 1u);
    const auto& stampedBrush = brushes.front()->brush();
    const auto stampedSideIndex = stampedBrush.findFace("skew-side");
    REQUIRE(stampedSideIndex);
    const auto& stampedSide = stampedBrush.face(*stampedSideIndex);

    const auto stampedSkew =
      mdl::measureUvSkew(stampedSide.uAxis(), stampedSide.vAxis(), stampedSide.normal());
    REQUIRE(stampedSkew);
    CHECK(vm::is_equal(*stampedSkew, 0.0f, 0.0001f));

    const auto& originalSide = brushNode->brush().face(*sourceSideIndex);
    const auto& originalCap = brushNode->brush().face(*sourceCapIndex);
    auto seam = std::optional<vm::segment3d>{};
    for (const auto* edge : originalCap.edges())
    {
      const auto segment = edge->segment();
      if (originalSide.geometry()->findEdge(
            segment.start(), segment.end(), vm::Cd::almost_zero()))
      {
        seam = segment;
        break;
      }
    }
    REQUIRE(seam);
    CHECK(
      stampedSide.uvCoords(seam->start())
      == vm::approx{originalSide.uvCoords(seam->start()), 0.0001f});
    CHECK(
      stampedSide.uvCoords(seam->end())
      == vm::approx{originalSide.uvCoords(seam->end()), 0.0001f});
  }

  SECTION("stamp is denied when dragged inward")
  {
    auto& document = fixture.create();
    auto& map = document.map();

    auto tool = ExtrudeTool{document};

    constexpr auto brushBounds = vm::bbox3d{16.0};

    auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
    auto* brushNode =
      new mdl::BrushNode{builder.createCuboid(brushBounds, "material") | kdl::value()};

    addNodes(map, {{map.editorContext().currentLayer(), {brushNode}}});
    selectNodes(map, {brushNode});

    // shoot straight down at the top face (+Z)
    const auto pickRay = vm::ray3d{{0, 0, 32}, {0, 0, -1}};
    const auto pickResult = performPick(map, tool, pickRay);

    auto dragState = ExtrudeDragState{
      tool.proposedDragHandles(),
      ExtrudeTool::getDragFaces(tool.proposedDragHandles()),
      false,
      vm::vec3d{0, 0, 0}};

    // dragging the top face (+Z normal) downward is inward — stamp must be denied
    const auto inwardDelta = vm::vec3d{0, 0, -8};

    tool.beginStamp();
    CHECK(!tool.stamp(inwardDelta, dragState));
    tool.cancel();

    // original brush is untouched; no new brushes were created
    CHECK(brushNode->logicalBounds() == brushBounds);
    CHECK(map.selection().brushes.size() == 1);
    CHECK(map.selection().brushes.front() == brushNode);
  }

  SECTION("stamp with multiple drag handles denied when any face is dragged inward")
  {
    auto& document = fixture.create();
    auto& map = document.map();

    auto tool = ExtrudeTool{document};

    auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};

    // brushNode2 sits on top of brushNode1: brushNode1's top face (+Z) and
    // brushNode2's bottom face (-Z) are coincident at z=16 but face in opposite
    // directions. The upward delta (0,0,8) is outward for the +Z face but inward for
    // the -Z face, so stamping must be denied.
    auto* brushNode1 = new mdl::BrushNode{
      builder.createCuboid(vm::bbox3d{{-16, -16, -16}, {16, 16, 16}}, "material")
      | kdl::value()};
    auto* brushNode2 = new mdl::BrushNode{
      builder.createCuboid(vm::bbox3d{{0, -16, 16}, {32, 16, 48}}, "material")
      | kdl::value()};

    addNodes(map, {{map.editorContext().currentLayer(), {brushNode1, brushNode2}}});
    selectNodes(map, {brushNode1, brushNode2});

    const auto pickRay = vm::ray3d{{-8, 0, 32}, {0, 0, -1}};
    const auto pickResult = performPick(map, tool, pickRay);
    REQUIRE(tool.proposedDragHandles().size() == 2);

    auto dragState = ExtrudeDragState{
      tool.proposedDragHandles(),
      ExtrudeTool::getDragFaces(tool.proposedDragHandles()),
      false,
      vm::vec3d{0, 0, 0}};

    const auto delta = vm::vec3d{0, 0, 8};

    tool.beginStamp();
    CHECK(!tool.stamp(delta, dragState));
    tool.cancel();

    // both original brushes are left unchanged; no new brushes were created
    CHECK(brushNode1->logicalBounds() == vm::bbox3d{{-16, -16, -16}, {16, 16, 16}});
    CHECK(brushNode2->logicalBounds() == vm::bbox3d{{0, -16, 16}, {32, 16, 48}});
    CHECK(map.selection().brushes.size() == 2);
  }

  SECTION("stamp with multiple same-direction drag handles")
  {
    auto& document = fixture.create();
    auto& map = document.map();

    auto tool = ExtrudeTool{document};

    auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};

    // Two side-by-side brushes whose top faces are coplanar at z=16 and both face +Z.
    // Any upward delta is outward for both handles, so stamping succeeds.
    auto* brushNode1 = new mdl::BrushNode{
      builder.createCuboid(vm::bbox3d{{-16, -16, -16}, {0, 16, 16}}, "material")
      | kdl::value()};
    auto* brushNode2 = new mdl::BrushNode{
      builder.createCuboid(vm::bbox3d{{0, -16, -16}, {16, 16, 16}}, "material")
      | kdl::value()};

    addNodes(map, {{map.editorContext().currentLayer(), {brushNode1, brushNode2}}});
    selectNodes(map, {brushNode1, brushNode2});

    // Pick from above — both top faces (coplanar at z=16, normal +Z) are selected
    const auto pickRay = vm::ray3d{{-8, 0, 32}, {0, 0, -1}};
    const auto pickResult = performPick(map, tool, pickRay);
    REQUIRE(tool.proposedDragHandles().size() == 2);

    auto dragState = ExtrudeDragState{
      tool.proposedDragHandles(),
      ExtrudeTool::getDragFaces(tool.proposedDragHandles()),
      false,
      vm::vec3d{0, 0, 0}};

    const auto delta = vm::vec3d{0, 0, 8};

    tool.beginStamp();
    REQUIRE(tool.stamp(delta, dragState));
    tool.commit(dragState);

    // original brushes are unchanged
    CHECK(brushNode1->logicalBounds() == vm::bbox3d{{-16, -16, -16}, {0, 16, 16}});
    CHECK(brushNode2->logicalBounds() == vm::bbox3d{{0, -16, -16}, {16, 16, 16}});

    // one new brush stamped per handle, each spanning from z=16 to z=24
    const auto bounds =
      map.selection().brushes
      | std::views::transform([](const auto* node) { return node->logicalBounds(); });
    CHECK_THAT(
      bounds,
      UnorderedRangeEquals(std::vector<vm::bbox3d>{
        {{-16, -16, 16}, {0, 16, 24}},
        {{0, -16, 16}, {16, 16, 24}},
      }));
  }

  SECTION("slide")
  {
    auto& document = fixture.create();
    auto& map = document.map();

    auto tool = ExtrudeTool{document};

    constexpr auto brushBounds = vm::bbox3d{16.0};

    auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
    auto* brushNode =
      new mdl::BrushNode{builder.createCuboid(brushBounds, "material") | kdl::value()};

    addNodes(map, {{map.editorContext().currentLayer(), {brushNode}}});
    selectNodes(map, {brushNode});

    // shoot straight down at the top face (+Z)
    const auto pickRay = vm::ray3d{{0, 0, 32}, {0, 0, -1}};
    const auto pickResult = performPick(map, tool, pickRay);

    auto dragState = ExtrudeDragState{
      tool.proposedDragHandles(),
      ExtrudeTool::getDragFaces(tool.proposedDragHandles()),
      false,
      vm::vec3d{0, 0, 0}};

    SECTION("slide outward grows the brush")
    {
      const auto delta = vm::vec3d{0, 0, 8};

      tool.beginSlide();
      CHECK(tool.slide(delta, dragState));
      tool.commit(dragState);

      // original brush was modified in place
      CHECK(brushNode->logicalBounds() == vm::bbox3d{{-16, -16, -16}, {16, 16, 24}});
      CHECK(brushNode->selected());
    }

    SECTION("slide inward shrinks the brush")
    {
      const auto delta = vm::vec3d{0, 0, -8};

      tool.beginSlide();
      CHECK(tool.slide(delta, dragState));
      tool.commit(dragState);

      CHECK(brushNode->logicalBounds() == vm::bbox3d{{-16, -16, -16}, {16, 16, 8}});
    }

    SECTION("slide can return to original position")
    {
      // Dragging back to the start produces a zero delta. Without the fix, transformFaces
      // with a zero translation returns false, leaving totalDelta at the previous
      // non-zero value, so commit() would incorrectly apply that offset.
      tool.beginSlide();
      CHECK(tool.slide({0, 0, 8}, dragState)); // move outward
      CHECK(tool.slide({0, 0, 0}, dragState)); // return to origin
      tool.commit(dragState);

      CHECK(brushNode->logicalBounds() == brushBounds);
    }

    SECTION("slide restores to last valid position when the brush would become invalid")
    {
      const auto validDelta = vm::vec3d{0, 0, 8};
      const auto collapseDelta = vm::vec3d{0, 0, -10000};

      tool.beginSlide();
      CHECK(tool.slide(validDelta, dragState));    // totalDelta = validDelta
      CHECK(tool.slide(collapseDelta, dragState)); // fails; restores to validDelta
      tool.commit(dragState);

      CHECK(brushNode->logicalBounds() == vm::bbox3d{{-16, -16, -16}, {16, 16, 24}});
    }
  }
}

} // namespace tb::ui
