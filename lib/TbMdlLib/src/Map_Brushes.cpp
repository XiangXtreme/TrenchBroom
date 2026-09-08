/*
 Copyright (C) 2025 Kristian Duske

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

#include "mdl/Map_Brushes.h"

#include "base/Logger.h"
#include "mdl/ApplyAndSwap.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/GameConfig.h"
#include "mdl/GameInfo.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/Transaction.h"
#include "mdl/UpdateBrushFaceAttributes.h"
#include "mdl/UvAlignment.h"
#include "mdl/UvUtils.h"
#include "mdl/WorldNode.h"

#include "vm/scalar.h"

#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>

namespace tb::mdl
{
namespace
{

auto invertHorizontalAxis(
  const mdl::UvAxis horizontalUvAxis,
  const vm::vec3f& uAxis,
  const vm::vec3f& vAxis,
  const vm::vec3f& rightAxis)
{
  switch (horizontalUvAxis)
  {
  case mdl::UvAxis::u:
    return vm::dot(uAxis, rightAxis) >= 0.0f;
  case mdl::UvAxis::v:
    return vm::dot(vAxis, rightAxis) >= 0.0f;
    switchDefault();
  }
}

auto invertVerticalAxis(
  const mdl::UvAxis horizontalUvAxis,
  const vm::vec3f& uAxis,
  const vm::vec3f& vAxis,
  const vm::vec3f& upAxis)
{
  switch (horizontalUvAxis)
  {
  case mdl::UvAxis::u:
    return vm::dot(vAxis, upAxis) >= 0.0f;
  case mdl::UvAxis::v:
    return vm::dot(uAxis, upAxis) >= 0.0f;
    switchDefault();
  }
}

auto getDirectionAxes(
  const vm::vec3f& uAxis,
  const vm::vec3f& vAxis,
  const vm::vec3f& upAxis,
  const vm::vec3f& rightAxis)
{
  // Which UV axis corresponds is horizontal?
  const auto [horizontalUvAxis, verticalUvAxis] =
    vm::abs(vm::dot(uAxis, rightAxis)) >= vm::abs(vm::dot(vAxis, rightAxis))
      ? std::tuple{mdl::UvAxis::u, mdl::UvAxis::v}
      : std::tuple{mdl::UvAxis::v, mdl::UvAxis::u};

  const auto invertH = invertHorizontalAxis(horizontalUvAxis, uAxis, vAxis, rightAxis);
  const auto invertV = invertVerticalAxis(horizontalUvAxis, uAxis, vAxis, upAxis);

  return std::tuple{horizontalUvAxis, verticalUvAxis, invertH, invertV};
}

std::tuple<mdl::UvAxis, mdl::UvSign> convertJustifyDirection(
  const BrushFace& brushFace, const UvJustifyDirection uvJustifyDirection)
{
  const auto [cameraUp, cameraRight] = computeCameraAxesForFaceNormal(brushFace.normal());

  const auto uAxis = vm::vec3f{brushFace.uAxis()};
  const auto vAxis = vm::vec3f{brushFace.vAxis()};

  const auto [horizontalUvAxis, verticalUvAxis, invertH, invertV] =
    getDirectionAxes(uAxis, vAxis, vm::vec3f{cameraUp}, vm::vec3f{cameraRight});

  switch (uvJustifyDirection)
  {
  case UvJustifyDirection::Left:
    return std::tuple{horizontalUvAxis, invertH ? mdl::UvSign::minus : mdl::UvSign::plus};
  case UvJustifyDirection::Right:
    return std::tuple{horizontalUvAxis, invertH ? mdl::UvSign::plus : mdl::UvSign::minus};
  case UvJustifyDirection::Up:
    return std::tuple{verticalUvAxis, invertV ? mdl::UvSign::minus : mdl::UvSign::plus};
  case UvJustifyDirection::Down:
    return std::tuple{verticalUvAxis, invertV ? mdl::UvSign::plus : mdl::UvSign::minus};
    switchDefault();
  }
}

std::tuple<mdl::UvAxis, mdl::UvSign> convertFitDirection(
  const BrushFace& brushFace, const UvFitDirection fitDirection)
{
  const auto [cameraUp, cameraRight] = computeCameraAxesForFaceNormal(brushFace.normal());

  const auto uAxis = vm::vec3f{brushFace.uAxis()};
  const auto vAxis = vm::vec3f{brushFace.vAxis()};

  const auto [horizontalUvAxis, verticalUvAxis, invertH, invertV] =
    getDirectionAxes(uAxis, vAxis, vm::vec3f{cameraUp}, vm::vec3f{cameraRight});

  switch (fitDirection)
  {
  case UvFitDirection::Horizontal:
    return std::tuple{horizontalUvAxis, invertH ? mdl::UvSign::minus : mdl::UvSign::plus};
  case UvFitDirection::Vertical:
    return std::tuple{verticalUvAxis, invertV ? mdl::UvSign::minus : mdl::UvSign::plus};
    switchDefault();
  }
}

template <typename F>
void compensateOffset(
  BrushFace& brushFace, const std::optional<vm::vec3d>& vertex, const F& f)
{
  if (vertex)
  {
    const auto previousUvCoords = vm::vec2f{
      brushFace.toUvCoordSystemMatrix(
        brushFace.uvAttributes().offset, brushFace.uvAttributes().scale)
      * *vertex};

    f();

    const auto newUvCoords = vm::vec2f{
      brushFace.toUvCoordSystemMatrix(
        brushFace.uvAttributes().offset, brushFace.uvAttributes().scale)
      * *vertex};
    const auto delta = previousUvCoords - newUvCoords;

    // cannot fail: only compensates the offset of an already-valid face
    evaluate(
      UpdateBrushFaceAttributes{
        .xOffset = mdl::AddValue{delta.x()},
        .yOffset = mdl::AddValue{delta.y()},
      },
      brushFace)
      | kdl::ignore();
  }
  else
  {
    f();
  }
}

bool applyFaceUV(BrushFace& face, const FaceUVUpdate& update)
{
  if (face.vertexCount() != update.points.size())
  {
    return false;
  }

  const auto projection = solveFaceUVProjection(update.points, update.uvs);
  if (!projection)
  {
    return false;
  }

  if (!face.setUvAttributes(UvAttributes{
        .offset = projection->offset,
        .scale = vm::vec2f{1, 1},
        .rotation = 0.0f,
      }))
  {
    return false;
  }
  face.restoreUvCoordSystemSnapshot(
    UvCoordSystemSnapshot{projection->uAxis, projection->vAxis});
  if (update.materialName)
  {
    face.setMaterialName(*update.materialName);
  }
  return true;
}

bool applyFaceUVProjection(
  BrushFace& face,
  const FaceUVProjection& projection,
  const std::optional<std::string>& materialName)
{
  if (!face.setUvAttributes(UvAttributes{
        .offset = projection.offset,
        .scale = vm::vec2f{1, 1},
        .rotation = 0.0f,
      }))
  {
    return false;
  }
  face.restoreUvCoordSystemSnapshot(
    UvCoordSystemSnapshot{projection.uAxis, projection.vAxis});
  if (materialName)
  {
    face.setMaterialName(*materialName);
  }
  return true;
}

std::optional<FaceUVUpdate> triangleUpdateForFace(
  const BrushFaceHandle& faceHandle,
  const BrushFace& face,
  const FaceUVUpdate& quadUpdate)
{
  const auto vertices = face.vertexPositions();
  if (vertices.size() != 3u)
  {
    return std::nullopt;
  }

  auto uvs = std::vector<vm::vec2f>{};
  uvs.reserve(vertices.size());
  for (const auto& vertex : vertices)
  {
    const auto it = std::ranges::find_if(quadUpdate.points, [&](const auto& point) {
      return vm::is_equal(point, vertex, 0.01);
    });
    if (it == std::end(quadUpdate.points))
    {
      return std::nullopt;
    }
    uvs.push_back(
      quadUpdate.uvs[size_t(std::distance(std::begin(quadUpdate.points), it))]);
  }

  return FaceUVUpdate{
    faceHandle,
    vertices,
    std::move(uvs),
    quadUpdate.materialName,
  };
}

std::optional<std::array<Brush, 2>> splitBrushForUV(
  const Map& map, const Brush& source, const FaceUVUpdate& update)
{
  if (
    update.face.faceIndex() >= source.faceCount() || update.points.size() != 4u
    || update.uvs.size() != 4u)
  {
    return std::nullopt;
  }

  const auto& sourceFace = source.face(update.face.faceIndex());
  const auto clipMaterial = update.materialName.value_or(sourceFace.materialName());

  const auto vertices = source.vertexPositions();
  if (vertices.empty())
  {
    return std::nullopt;
  }
  auto interior = vm::vec3d{};
  for (const auto& vertex : vertices)
  {
    interior = interior + vertex;
  }
  interior = interior / double(vertices.size());

  auto front = source;
  auto back = source;
  const auto makeClipFace = [&](const bool reverse) {
    return reverse ? BrushFace::create(
                       update.points[0],
                       interior,
                       update.points[2],
                       clipMaterial,
                       sourceFace.uvAttributes(),
                       sourceFace.surfaceAttributes(),
                       map.worldNode().mapFormat())
                   : BrushFace::create(
                       update.points[0],
                       update.points[2],
                       interior,
                       clipMaterial,
                       sourceFace.uvAttributes(),
                       sourceFace.surfaceAttributes(),
                       map.worldNode().mapFormat());
  };

  auto frontFace = makeClipFace(false);
  auto backFace = makeClipFace(true);
  if (frontFace.is_error() || backFace.is_error())
  {
    return std::nullopt;
  }
  const auto frontClipResult = std::move(frontFace) | kdl::and_then([&](auto face) {
                                 return front.clip(map.worldBounds(), std::move(face));
                               });
  const auto backClipResult = std::move(backFace) | kdl::and_then([&](auto face) {
                                return back.clip(map.worldBounds(), std::move(face));
                              });
  if (frontClipResult.is_error() || backClipResult.is_error())
  {
    return std::nullopt;
  }
  return std::array<Brush, 2>{std::move(front), std::move(back)};
}

bool applyTriangleUV(BrushFace& face, const TriangleUVUpdate& update)
{
  if (face.vertexCount() != 3u)
  {
    return false;
  }

  return applyFaceUV(
    face,
    FaceUVUpdate{
      update.face,
      {update.points[0], update.points[1], update.points[2]},
      {update.uvs[0], update.uvs[1], update.uvs[2]},
    });
}

} // namespace

bool createBrush(Map& map, const std::vector<vm::vec3d>& points)
{
  const auto builder = BrushBuilder{
    map.worldNode().mapFormat(),
    map.worldBounds(),
    map.gameInfo().gameConfig.faceAttribsConfig.defaultUvAttributes,
    map.gameInfo().gameConfig.faceAttribsConfig.defaultSurfaceAttributes};

  return builder.createBrush(points, map.currentMaterialName())
         | kdl::and_then([&](auto b) -> Result<void> {
             auto* brushNode = new BrushNode{std::move(b)};

             auto transaction = Transaction{map, "Create Brush"};
             deselectAll(map);
             if (addNodes(map, {{&parentForNodes(map), {brushNode}}}).empty())
             {
               transaction.cancel();
               return Error{"Could not add brush to document"};
             }
             selectNodes(map, {brushNode});
             if (!transaction.commit())
             {
               return Error{"Could not add brush to document"};
             }

             return kdl::void_success;
           })
         | kdl::if_error(
           [&](auto e) { map.logger().error() << "Could not create brush: " << e.msg; })
         | kdl::is_success();
}

bool setBrushFaceAttributes(Map& map, const UpdateBrushFaceAttributes& update)
{
  return setBrushFaceAttributes(map, map.selection().allBrushFaces(), update);
}

bool setBrushFaceAttributes(
  Map& map,
  const std::vector<BrushFaceHandle>& faces,
  const UpdateBrushFaceAttributes& update)
{
  return applyAndSwap(map, "Change Face Attributes", faces, [&](auto& brushFace) {
    return evaluate(update, brushFace) | kdl::if_error([&](auto e) {
             map.logger().error() << "Could not set face attributes: " << e.msg;
           })
           | kdl::is_success();
  });
}

bool setTriangleUVs(Map& map, const std::vector<TriangleUVUpdate>& updates)
{
  if (!isParallelUvCoordSystem(map.worldNode().mapFormat()))
  {
    return false;
  }

  auto faceHandles = std::vector<BrushFaceHandle>{};
  faceHandles.reserve(updates.size());
  for (const auto& update : updates)
  {
    faceHandles.push_back(update.face);
  }

  auto updateIndex = size_t{0};
  return applyAndSwap(map, "Set Triangle UVs", faceHandles, [&](auto& face) {
    return applyTriangleUV(face, updates[updateIndex++]);
  });
}

bool setFaceUVs(Map& map, const std::vector<FaceUVUpdate>& updates)
{
  if (!isParallelUvCoordSystem(map.worldNode().mapFormat()))
  {
    return false;
  }

  auto faceHandles = std::vector<BrushFaceHandle>{};
  faceHandles.reserve(updates.size());
  for (const auto& update : updates)
  {
    faceHandles.push_back(update.face);
  }

  auto updateIndex = size_t{0};
  return applyAndSwap(map, "Set Face UVs", faceHandles, [&](auto& face) {
    const auto& update = updates[updateIndex++];
    return applyFaceUV(face, update);
  });
}

bool setFaceUVsWithSplit(Map& map, const std::vector<FaceUVUpdate>& updates)
{
  if (!isParallelUvCoordSystem(map.worldNode().mapFormat()) || updates.empty())
  {
    return false;
  }

  auto updatesByBrush =
    std::unordered_map<BrushNode*, std::vector<const FaceUVUpdate*>>{};
  for (const auto& update : updates)
  {
    if (
      update.face.faceIndex() >= update.face.node()->brush().faceCount()
      || update.points.size() != update.uvs.size())
    {
      return false;
    }
    updatesByBrush[update.face.node()].push_back(&update);
  }

  if (std::ranges::none_of(updates, [](const auto& update) {
        return !solveFaceUVProjection(update.points, update.uvs);
      }))
  {
    return setFaceUVs(map, updates);
  }

  auto nodesToSwap = std::vector<std::pair<Node*, NodeContents>>{};
  auto splitNodes = std::vector<BrushNode*>{};
  auto replacementBrushes = std::map<Node*, std::vector<std::unique_ptr<BrushNode>>>{};

  for (const auto& [brushNode, brushUpdates] : updatesByBrush)
  {
    const auto nonAffineCount =
      std::ranges::count_if(brushUpdates, [](const auto* update) {
        return !solveFaceUVProjection(update->points, update->uvs);
      });
    if (nonAffineCount > 1)
    {
      return false;
    }

    if (nonAffineCount == 0)
    {
      auto brush = brushNode->brush();
      for (const auto* update : brushUpdates)
      {
        if (
          update->face.faceIndex() >= brush.faceCount()
          || !applyFaceUV(brush.face(update->face.faceIndex()), *update))
        {
          return false;
        }
      }
      nodesToSwap.emplace_back(brushNode, NodeContents{std::move(brush)});
      continue;
    }

    const auto* splitUpdate = *std::ranges::find_if(brushUpdates, [](const auto* update) {
      return !solveFaceUVProjection(update->points, update->uvs);
    });
    const auto targetBoundary =
      brushNode->brush().face(splitUpdate->face.faceIndex()).boundary();
    auto pieces = splitBrushForUV(map, brushNode->brush(), *splitUpdate);
    if (!pieces)
    {
      return false;
    }

    for (auto& piece : *pieces)
    {
      for (const auto* update : brushUpdates)
      {
        const auto sourceBoundary =
          brushNode->brush().face(update->face.faceIndex()).boundary();
        const auto pieceFaceIndex = piece.findFace(sourceBoundary);
        if (!pieceFaceIndex)
        {
          continue;
        }

        auto& pieceFace = piece.face(*pieceFaceIndex);
        if (update == splitUpdate)
        {
          const auto triangleUpdate = triangleUpdateForFace(
            BrushFaceHandle{brushNode, *pieceFaceIndex}, pieceFace, *update);
          if (!triangleUpdate || !applyFaceUV(pieceFace, *triangleUpdate))
          {
            return false;
          }
        }
        else
        {
          const auto projection = solveFaceUVProjection(update->points, update->uvs);
          if (
            !projection
            || !applyFaceUVProjection(pieceFace, *projection, update->materialName))
          {
            return false;
          }
        }
      }

      const auto targetFaceIndex = piece.findFace(targetBoundary);
      if (!targetFaceIndex || piece.face(*targetFaceIndex).vertexCount() != 3u)
      {
        return false;
      }
      replacementBrushes[brushNode->parent()].push_back(
        std::make_unique<BrushNode>(std::move(piece)));
    }
    splitNodes.push_back(brushNode);
  }

  auto transaction = Transaction{map, "Set Face UVs with Split"};
  if (
    !nodesToSwap.empty()
    && !updateNodeContents(map, "Set Face UVs", std::move(nodesToSwap)))
  {
    transaction.cancel();
    return false;
  }

  auto nodesToAdd = std::map<Node*, std::vector<Node*>>{};
  auto replacementCount = size_t{0};
  for (auto& [parent, brushes] : replacementBrushes)
  {
    for (auto& brush : brushes)
    {
      nodesToAdd[parent].push_back(brush.release());
      ++replacementCount;
    }
  }
  const auto addedNodes = addNodes(map, nodesToAdd);
  if (addedNodes.size() != replacementCount)
  {
    transaction.cancel();
    return false;
  }

  deselectAll(map);
  const auto nodesToRemove =
    splitNodes | std::views::transform([](auto* node) -> Node* { return node; })
    | kdl::ranges::to<std::vector>();
  removeNodes(map, nodesToRemove);
  selectNodes(map, addedNodes);
  return transaction.commit();
}

bool copyUv(
  Map& map,
  const UvCoordSystemSnapshot& coordSystemSnapshot,
  const UvAttributes& uvAttributes,
  const vm::plane3d& sourceFacePlane,
  const WrapStyle wrapStyle)
{
  return applyAndSwap(
    map, "Copy UV Alignment", map.selection().allBrushFaces(), [&](auto& face) {
      face.copyUvCoordSystemFromFace(
        coordSystemSnapshot, uvAttributes, sourceFacePlane, wrapStyle);
      return true;
    });
}

bool translateUv(
  Map& map,
  const vm::vec3f& cameraUp,
  const vm::vec3f& cameraRight,
  const vm::vec2f& delta)
{
  return applyAndSwap(
    map, "Translate UV", map.selection().allBrushFaces(), [&](auto& face) {
      face.translateUv(vm::vec3d{cameraUp}, vm::vec3d{cameraRight}, delta);
      return true;
    });
}

bool rotateUv(Map& map, const float angle)
{
  return applyAndSwap(map, "Rotate UV", map.selection().allBrushFaces(), [&](auto& face) {
    return face.rotateUv(angle) | kdl::if_error([&](auto e) {
             map.logger().error() << "Could not rotate UV: " << e.msg;
           })
           | kdl::is_success();
  });
}

bool shearUv(Map& map, const vm::vec2f& factors)
{
  return applyAndSwap(map, "Shear UV", map.selection().allBrushFaces(), [&](auto& face) {
    face.shearUv(factors);
    return true;
  });
}

bool flipUv(
  Map& map,
  const vm::vec3f& cameraUp,
  const vm::vec3f& cameraRight,
  const vm::direction cameraRelativeFlipDirection)
{
  const bool isHFlip =
    (cameraRelativeFlipDirection == vm::direction::left
     || cameraRelativeFlipDirection == vm::direction::right);
  return applyAndSwap(
    map,
    isHFlip ? "Flip UV Horizontally" : "Flip UV Vertically",
    map.selection().allBrushFaces(),
    [&](auto& face) {
      face.flipUv(
        vm::vec3d{cameraUp}, vm::vec3d{cameraRight}, cameraRelativeFlipDirection);
      return true;
    });
}

void alignUv(Map& map, const UvPolicy uvPolicy)
{
  applyAndSwap(
    map, "Align Texture", map.selection().allBrushFaces(), [&](auto& brushFace) {
      // cannot fail: only adjusts the offset of an already-valid face
      evaluate(mdl::align(brushFace, uvPolicy), brushFace) | kdl::ignore();
      return true;
    });
}

void justifyUv(
  Map& map, const UvJustifyDirection uvJustifyDirection, const UvPolicy uvPolicy)
{
  applyAndSwap(
    map, "Justify Texture", map.selection().allBrushFaces(), [&](auto& brushFace) {
      const auto [uvAxis, uvSign] =
        convertJustifyDirection(brushFace, uvJustifyDirection);

      // cannot fail: only adjusts the offset of an already-valid face
      evaluate(mdl::justify(brushFace, uvAxis, uvSign, uvPolicy), brushFace)
        | kdl::ignore();
      return true;
    });
}

void fitUv(
  Map& map,
  const UvFitDirection uvFitDirection,
  const UvPolicy uvPolicy,
  const UvFitMode uvFitMode)
{
  applyAndSwap(map, "Fit Texture", map.selection().allBrushFaces(), [&](auto& brushFace) {
    const auto [uvAxis, uvSign] = convertFitDirection(brushFace, uvFitDirection);

    const auto invariantVertex = anchorVertex(brushFace, uvAxis, uvSign);
    compensateOffset(brushFace, invariantVertex, [&] {
      // cannot fail: only adjusts the scale/offset of an already-valid face
      evaluate(mdl::fit(brushFace, uvAxis, uvPolicy, uvFitMode), brushFace)
        | kdl::ignore();
    });
    return true;
  });
}

void autoFitUv(Map& map)
{
  applyAndSwap(
    map, "Auto Fit Texture", map.selection().allBrushFaces(), [&](auto& brushFace) {
      // cannot fail: these calls only adjust the scale/offset of an already-valid face
      evaluate(mdl::align(brushFace, UvPolicy::best), brushFace) | kdl::ignore();

      evaluate(
        mdl::justify(brushFace, UvAxis::u, UvSign::plus, UvPolicy::best), brushFace)
        | kdl::ignore();
      evaluate(
        mdl::justify(brushFace, UvAxis::v, UvSign::plus, UvPolicy::best), brushFace)
        | kdl::ignore();

      const auto invariantUVertex = anchorVertex(brushFace, UvAxis::u, UvSign::plus);
      compensateOffset(brushFace, invariantUVertex, [&] {
        evaluate(
          mdl::fit(brushFace, UvAxis::u, UvPolicy::best, UvFitMode::fitToFace), brushFace)
          | kdl::ignore();
      });

      const auto invariantVVertex = anchorVertex(brushFace, UvAxis::v, UvSign::plus);
      compensateOffset(brushFace, invariantVVertex, [&] {
        evaluate(
          mdl::fit(brushFace, UvAxis::v, UvPolicy::best, UvFitMode::fitToFace), brushFace)
          | kdl::ignore();
      });

      return true;
    });
}

} // namespace tb::mdl
