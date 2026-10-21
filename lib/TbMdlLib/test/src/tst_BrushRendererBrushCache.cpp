/*
 Copyright (C) 2026 TrenchBroom contributors

 This file is part of TrenchBroom, licensed under the GNU General Public License,
 version 3 or (at your option) any later version. See <http://www.gnu.org/licenses/>.
 */

#include "gl/Material.h"
#include "gl/Texture.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/BrushRendererBrushCache.h"
#include "mdl/MapFormat.h"

#include "kd/result.h"

#include "vm/mat_ext.h"

#include <algorithm>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

namespace tb::mdl
{

TEST_CASE("BrushRendererBrushCache")
{
  const auto format = GENERATE(MapFormat::Standard, MapFormat::Valve);
  const auto lockMaterials = GENERATE(false, true);
  const auto worldBounds = vm::bbox3d{65536.0};
  auto firstMaterial =
    gl::Material{"first", gl::createTextureResource(gl::Texture{128, 32})};
  auto secondMaterial =
    gl::Material{"second", gl::createTextureResource(gl::Texture{32, 256})};
  auto brush = BrushBuilder{format, worldBounds}.createCuboid(
                 vm::bbox3d{{12000, -3000, 100}, {12301, -2701, 357}}, "first")
               | kdl::value();
  for (auto& face : brush.faces())
  {
    REQUIRE(face.setUvAttributes({{13.25f, -7.75f}, {-0.75f, 2.5f}, 32.0f}).is_success());
    face.setMaterial(&firstMaterial);
  }
  auto node = BrushNode{std::move(brush)};
  const auto verify = [&] {
    auto& cache = node.brushRendererBrushCache();
    cache.validateVertexCache(node);
    const auto& vertices = cache.cachedVertices();
    REQUIRE(vertices.size() == 2 * node.brush().edgeCount());
    REQUIRE(cache.cachedFacesSortedByMaterial().size() == node.brush().faceCount());
    REQUIRE(cache.cachedEdges().size() == node.brush().edgeCount());
    CHECK(std::ranges::is_sorted(
      cache.cachedFacesSortedByMaterial(), {}, [](const auto& face) {
        return face.material;
      }));
    for (const auto& cachedFace : cache.cachedFacesSortedByMaterial())
    {
      REQUIRE(std::ranges::any_of(
        node.brush().faces(), [&](const auto& f) { return &f == cachedFace.face; }));
      const auto& face = *cachedFace.face;
      CHECK(cachedFace.material == face.material());
      const auto positions = face.vertexPositions();
      REQUIRE(cachedFace.vertexCount == positions.size());
      for (size_t i = 0; i < positions.size(); ++i)
      {
        const auto& position = positions[positions.size() - i - 1];
        const auto& vertex = vertices[cachedFace.indexOfFirstVertexRelativeToBrush + i];
        CHECK(vertex.attr == vm::vec3f{position});
        CHECK(vertex.rest.attr == vm::vec3f{face.boundary().normal});
        CHECK(vertex.rest.rest.attr == face.uvCoords(position));
      }
    }
    size_t i = 0;
    for (const auto* edge : node.brush().edges())
    {
      const auto& cachedEdge = cache.cachedEdges()[i++];
      CHECK(
        vertices[cachedEdge.vertexIndex1RelativeToBrush].attr
        == vm::vec3f{edge->firstVertex()->position()});
      CHECK(
        vertices[cachedEdge.vertexIndex2RelativeToBrush].attr
        == vm::vec3f{edge->secondVertex()->position()});
      CHECK(cachedEdge.face1 == &node.brush().face(*edge->firstFace()->payload()));
      CHECK(cachedEdge.face2 == &node.brush().face(*edge->secondFace()->payload()));
    }
  };

  SECTION("rebuild after movement and undo uses current face pointers and UVs")
  {
    verify();
    auto moved = node.brush();
    REQUIRE(moved
              .transform(
                worldBounds, vm::translation_matrix(vm::vec3d{17, -31, 9}), lockMaterials)
              .is_success());
    auto previous = node.setBrush(std::move(moved));
    verify();
    node.setBrush(std::move(previous));
    verify();
  }

  SECTION("material replacement and missing textures update UV normalization")
  {
    verify();
    node.setFaceMaterial(0, &secondMaterial);
    node.setFaceMaterial(1, nullptr);
    verify();
  }
}

} // namespace tb::mdl
