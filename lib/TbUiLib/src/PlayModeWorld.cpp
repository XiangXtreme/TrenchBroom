/*
 Copyright (C) 2026 KreedzBroom contributors

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

#include "ui/PlayModeWorld.h"

#include "mdl/Brush.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/GroupNode.h"
#include "mdl/Layer.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/PatchNode.h"
#include "mdl/WorldNode.h"
#include "cs/scs.h"

#include "kd/overload.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>
#include <string_view>

namespace tb::ui
{
namespace
{

// What a brush entity turns into after compiling (hlcsg/hlbsp semantics).
enum class EntityRole
{
  World,  // merged into worldspawn
  Solid,  // own BSP model, SOLID_BSP
  Ladder, // own BSP model, SOLID_NOT with CONTENTS_LADDER
  Liquid, // own BSP model, SOLID_NOT with liquid contents
  Ignore, // non-solid for the player
};

std::string toLower(std::string_view str)
{
  auto result = std::string{str};
  std::ranges::transform(result, result.begin(), [](const unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return result;
}

EntityRole entityRole(const std::string& classname)
{
  const auto name = toLower(classname);
  if (name == "worldspawn" || name == "func_group" || name == "func_detail")
  {
    return EntityRole::World;
  }
  if (name == "func_ladder")
  {
    return EntityRole::Ladder;
  }
  if (name == "func_water")
  {
    return EntityRole::Liquid;
  }
  if (
    name.starts_with("trigger_") || name == "func_illusionary" || name == "func_buyzone"
    || name == "func_bomb_target" || name == "func_hostage_rescue"
    || name == "func_escapezone" || name == "func_vip_safetyzone"
    || name == "func_weaponcheck" || name == "env_bubbles")
  {
    return EntityRole::Ignore;
  }
  return EntityRole::Solid;
}

// The texture name the compile tools see, lowercase.
std::string textureName(const std::string& materialName)
{
  // TrenchBroom may prefix the collection, keep the texture name only.
  const auto slash = materialName.find_last_of('/');
  return toLower(
    slash == std::string::npos ? std::string_view{materialName}
                               : std::string_view{materialName}.substr(slash + 1));
}

// hlcsg's TextureContents(), reduced to what the player can feel.
enum class FaceContents
{
  Solid,
  Clip,
  Skip, // ORIGIN / BOUNDINGBOX: removes the whole brush
  Water,
  Slime,
  Lava,
  Tool, // HINT / SKIP: does not create a brush on its own
};

FaceContents faceContents(std::string_view name, const bool zhlt)
{
  // animated textures (+0name) keep their contents prefix after the frame number
  if (name.size() > 2 && name[0] == '+')
  {
    name = name.substr(2);
  }

  if (name.starts_with("!lava"))
  {
    return FaceContents::Lava;
  }
  if (name.starts_with("!slime"))
  {
    return FaceContents::Slime;
  }
  if (name.starts_with('!'))
  {
    return FaceContents::Water;
  }
  if (name.starts_with("origin") || (!zhlt && name.starts_with("boundingbox")))
  {
    return FaceContents::Skip;
  }
  if (name.starts_with("clip"))
  {
    return FaceContents::Clip;
  }
  if (name.starts_with("hint") || name.starts_with("skip"))
  {
    return FaceContents::Tool;
  }
  return FaceContents::Solid;
}

// How a brush enters the collision world.
struct BrushInfo
{
  int32_t contents = SCS_CONTENTS_SOLID;
  uint32_t hullMask = 0; // scs_brush::hull_mask, 0 = all hulls
  std::vector<bool> bevelFaces;
};

// hlcsg's ParseBrush / CheckBrushContents: brush contents plus the tool
// textures that shape the clip hulls. VHLT knows CLIPHULLn, NOCLIP, BEVEL,
// BEVELBRUSH, CLIPBEVEL(BRUSH) and BOUNDINGBOX; ZHLT only BEVEL faces, and its
// TRANSLUCENT / @ brushes get no clip hulls. Returns nullopt if the brush does
// not exist for the player.
std::optional<BrushInfo> brushInfo(
  const mdl::Brush& brush, const bool entityNoclip, const bool zhlt)
{
  auto info = BrushInfo{};
  auto contents = std::optional<FaceContents>{};
  auto noclip = entityNoclip;
  auto bevelBrush = false;
  auto clipHulls = 0u; // CLIPHULLn bits
  for (const auto& face : brush.faces())
  {
    const auto name = textureName(face.materialName());
    const auto faceType = faceContents(name, zhlt);
    if (faceType == FaceContents::Skip)
    {
      return std::nullopt; // origin brushes are removed at csg time
    }
    if (faceType == FaceContents::Clip)
    {
      contents = FaceContents::Clip;
    }
    else if (faceType != FaceContents::Tool && !contents)
    {
      contents = faceType;
    }

    if (zhlt)
    {
      noclip = noclip || name.starts_with("translucent") || name.starts_with('@');
      info.bevelFaces.push_back(name.starts_with("bevel"));
      continue;
    }
    noclip = noclip || name.starts_with("noclip") || name.starts_with("nullnoclip");
    bevelBrush =
      bevelBrush || name.starts_with("bevelbrush") || name.starts_with("clipbevelbrush");
    info.bevelFaces.push_back(name.starts_with("bevel") || name.starts_with("clipbevel"));
    if (
      name.starts_with("cliphull") && name.size() > 8 && name[8] >= '1' && name[8] <= '3')
    {
      clipHulls |= 1u << (name[8] - '0');
    }
  }

  if (!contents)
  {
    return std::nullopt; // all faces are HINT / SKIP
  }
  if (bevelBrush)
  {
    info.bevelFaces.assign(info.bevelFaces.size(), true);
  }

  switch (*contents)
  {
  case FaceContents::Clip:
    if (zhlt && noclip)
    {
      return std::nullopt; // ZHLT checks zhlt_noclip first, leaving nothing
    }
    // Clip brushes only exist in the clip hulls: the CLIPHULLn ones, or all.
    info.contents = SCS_CONTENTS_CLIP;
    info.hullMask = clipHulls != 0 ? clipHulls : 0b1110u;
    return info;
  case FaceContents::Water:
    info.contents = SCS_CONTENTS_WATER;
    return info;
  case FaceContents::Slime:
    info.contents = SCS_CONTENTS_SLIME;
    return info;
  case FaceContents::Lava:
    info.contents = SCS_CONTENTS_LAVA;
    return info;
  case FaceContents::Solid:
  case FaceContents::Skip:
  case FaceContents::Tool:
    break;
  }
  if (noclip)
  {
    info.hullMask = 0b0001u; // drawn and in hull 0, but no clip hulls
  }
  return info;
}

// hlcsg IntForKey: atoi, so a missing or non-numeric value reads as 0.
int intForKey(const mdl::Entity& entity, const std::string& key)
{
  if (const auto* value = entity.property(key))
  {
    try
    {
      return std::stoi(*value);
    }
    catch (const std::exception&)
    {
    }
  }
  return 0;
}

// hlcsg's info_compile_parameters "cliptype": 0-3 as named, anything else legacy.
int32_t compileParametersClipType(const mdl::Entity& entity)
{
  const auto value = intForKey(entity, "cliptype");
  return value >= SCS_CLIPTYPE_SMALLEST && value <= SCS_CLIPTYPE_PRECISE
           ? value
           : SCS_CLIPTYPE_LEGACY;
}

int32_t liquidContents(const mdl::Entity& entity)
{
  // func_water stores its contents in "skin" (-3 water, -4 slime, -5 lava)
  if (const auto* skin = entity.property("skin"))
  {
    try
    {
      const auto value = std::stoi(*skin);
      if (
        value == SCS_CONTENTS_WATER || value == SCS_CONTENTS_SLIME
        || value == SCS_CONTENTS_LAVA)
      {
        return value;
      }
    }
    catch (const std::exception&)
    {
    }
  }
  return SCS_CONTENTS_WATER;
}

vm::vec3f spawnAngles(const mdl::Entity& entity)
{
  if (const auto* angles = entity.property("angles"))
  {
    auto stream = std::istringstream{*angles};
    auto result = vm::vec3f{};
    if (stream >> result[0] >> result[1] >> result[2])
    {
      return result;
    }
  }
  if (const auto* angle = entity.property("angle"))
  {
    auto stream = std::istringstream{*angle};
    auto yaw = 0.0f;
    if (stream >> yaw)
    {
      return {0.0f, yaw, 0.0f};
    }
  }
  return {};
}

// Owns the plane arrays that scs_brush points into.
class BrushCollector
{
private:
  std::vector<std::vector<scs_plane>> m_planes;
  std::vector<scs_brush> m_brushes;

public:
  void add(const mdl::Brush& brush, const BrushInfo& info, const int32_t contents)
  {
    auto& planes = m_planes.emplace_back();
    planes.reserve(brush.faceCount());
    for (size_t i = 0; i < brush.faceCount(); ++i)
    {
      // Same convention as scs_plane: outward normal, inside <=> dot(n, p) <= d.
      const auto& boundary = brush.faces()[i].boundary();
      planes.push_back(
        {{boundary.normal.x(), boundary.normal.y(), boundary.normal.z()},
         boundary.distance,
         info.bevelFaces[i] ? SCS_PLANE_BEVEL : 0u});
    }
    m_brushes.push_back(
      {nullptr, static_cast<uint32_t>(planes.size()), contents, info.hullMask});
  }

  bool empty() const { return m_brushes.empty(); }
  size_t size() const { return m_brushes.size(); }

  // Pointers are fixed up last because m_planes may reallocate while collecting.
  const scs_brush* data()
  {
    for (size_t i = 0; i < m_brushes.size(); ++i)
    {
      m_brushes[i].planes = m_planes[i].data();
    }
    return m_brushes.data();
  }
};

struct CollectedEntity
{
  BrushCollector brushes;
  int32_t entityContents = 0;
};

} // namespace

void ScsWorldDeleter::operator()(scs_world* world) const
{
  scs_world_release(world);
}

PlayModeWorld buildPlayModeWorld(const mdl::Map& map, const PlayModeWorldOptions& options)
{
  auto result = PlayModeWorld{};
  result.clipType = options.clipType;
  const auto zhlt = options.compiler == SCS_COMPILER_ZHLT;
  auto worldBrushes = BrushCollector{};
  auto entities = std::vector<CollectedEntity>{};

  const auto addWorldBrush = [&](const mdl::BrushNode& brushNode, const bool noclip) {
    if (const auto info = brushInfo(brushNode.brush(), noclip, zhlt))
    {
      worldBrushes.add(brushNode.brush(), *info, info->contents);
    }
    else
    {
      ++result.skippedBrushCount;
    }
  };
  const auto worldNoclip = intForKey(map.worldNode().entity(), "zhlt_noclip") != 0;

  map.worldNode().accept(
    kdl::overload(
      [](auto&& thisLambda, const mdl::WorldNode& worldNode) {
        worldNode.visitChildren(thisLambda);
      },
      [](auto&& thisLambda, const mdl::LayerNode& layerNode) {
        if (!layerNode.layer().omitFromExport())
        {
          layerNode.visitChildren(thisLambda);
        }
      },
      [](auto&& thisLambda, const mdl::GroupNode& groupNode) {
        groupNode.visitChildren(thisLambda);
      },
      [&](const mdl::EntityNode& entityNode) {
        const auto& entity = entityNode.entity();
        if (entity.pointEntity())
        {
          const auto classname = toLower(entity.classname());
          if (classname == "info_player_start" || classname == "info_player_deathmatch")
          {
            result.spawnPoints.push_back(
              {classname, vm::vec3f{entity.origin()}, spawnAngles(entity)});
          }
          else if (classname == "info_compile_parameters" && options.useMapClipType)
          {
            result.clipType = compileParametersClipType(entity);
            result.clipTypeFromMap = true;
          }
          return;
        }

        const auto role = entityRole(entity.classname());
        if (role == EntityRole::Ignore)
        {
          return;
        }

        const auto noclip = intForKey(entity, "zhlt_noclip") != 0;
        // Brush entities are collected in the order NodeWriter writes them to the .map
        // file, so their indices match the compiled map's models and physents. The
        // order matters: traces keep the first model's plane on ties (pixelwalk).
        auto* collected = role == EntityRole::World ? nullptr : &entities.emplace_back();
        if (collected)
        {
          collected->entityContents = role == EntityRole::Ladder ? SCS_CONTENTS_LADDER
                                      : role == EntityRole::Liquid
                                        ? liquidContents(entity)
                                        : 0;
        }

        entityNode.visitChildren(
          kdl::overload(
            [](const mdl::WorldNode&) {},
            [](const mdl::LayerNode&) {},
            [](const mdl::GroupNode&) {},
            [](const mdl::EntityNode&) {},
            [&](const mdl::BrushNode& brushNode) {
              if (!brushNode.visible())
              {
                return;
              }
              if (!collected)
              {
                addWorldBrush(brushNode, noclip);
              }
              else if (const auto info = brushInfo(brushNode.brush(), noclip, zhlt))
              {
                // brush entity contents come from the entity, not the material
                collected->brushes.add(brushNode.brush(), *info, SCS_CONTENTS_SOLID);
              }
            },
            [](const mdl::PatchNode&) {}));
      },
      [&](const mdl::BrushNode& brushNode) {
        if (brushNode.visible())
        {
          addWorldBrush(brushNode, worldNoclip);
        }
      },
      [](const mdl::PatchNode&) {}));

  std::erase_if(entities, [](const auto& entity) { return entity.brushes.empty(); });

  auto entityDescs = std::vector<scs_brush_entity>{};
  entityDescs.reserve(entities.size());
  for (auto& entity : entities)
  {
    entityDescs.push_back(
      {entity.brushes.data(),
       static_cast<uint32_t>(entity.brushes.size()),
       entity.entityContents});
  }

  auto desc = scs_world_desc{};
  desc.world_brushes = worldBrushes.data();
  desc.world_brush_count = static_cast<uint32_t>(worldBrushes.size());
  desc.entities = entityDescs.data();
  desc.entity_count = static_cast<uint32_t>(entityDescs.size());
  desc.cliptype = result.clipType;
  desc.compiler = options.compiler;

  auto info = scs_world_info{};
  scs_world* world = nullptr;
  switch (scs_world_build(&desc, &world, &info))
  {
  case SCS_OK:
    break;
  case SCS_ERR_HULL_LIMIT:
    result.error = "collision hull too complex";
    return result;
  case SCS_ERR_OUT_OF_MEMORY:
    result.error = "out of memory";
    return result;
  default:
    result.error = "internal error while compiling collision hulls";
    return result;
  }

  result.world = ScsWorldPtr{world};
  result.worldBrushCount = worldBrushes.size();
  result.entityCount = entities.size();
  result.skippedBrushCount += info.skipped_brushes;
  result.buildMilliseconds = info.build_milliseconds;
  return result;
}

} // namespace tb::ui
