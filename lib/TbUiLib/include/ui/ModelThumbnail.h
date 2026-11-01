/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom, licensed under GPL version 3 or later.
*/
#pragma once

#include "mdl/GoldSrcStudioModel.h"

#include "vm/vec.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

class QImage;

namespace tb::ui
{
struct ModelPreviewFrame
{
  std::string name;
  std::vector<vm::vec3f> positions;
  std::vector<std::array<size_t, 3>> triangles;
};
struct ModelPreviewScene
{
  std::vector<ModelPreviewFrame> frames;
  // Set for GoldSrc studio models: the bind pose is the only decoded frame, but the
  // inspector can offer the authored body variants from this metadata.
  std::optional<mdl::GoldSrcStudioModelInfo> studioModel;
};

/**
 * Loads the bind pose of a GoldSrc studio model.
 *
 * The entity model loaders do not understand the GoldSrc studio format, so the asset
 * browser reads it directly. Animation, skins and bone controllers are not decoded; the
 * mesh is the authored rest pose. Returns nullopt for other formats or unreadable models.
 */
std::optional<ModelPreviewScene> loadGoldSrcStudioPreviewScene(
  const std::vector<std::uint8_t>& bytes);

std::optional<ModelPreviewScene> loadModelPreviewScene(
  const std::filesystem::path& folder,
  const std::filesystem::path& relativePath,
  bool firstOnly = false);
QImage renderModelPreview(
  const ModelPreviewFrame& mesh,
  int size,
  float yaw = 0.55f,
  float pitch = 0.38f,
  bool bounds = false,
  bool origin = false);

/**
 * Renders the first frame of a GoldSrc studio model into a flat shaded thumbnail.
 *
 * TrenchBroom renders models through the GPU for the model browser, which needs a game
 * context and a GL surface. The asset library browses files that no game owns, so it gets
 * a small software projection through TbMdlLib's read-only geometry accessors instead.
 * Textures, animation and blended materials are ignored: the thumbnail only has to say
 * "this is a barrel".
 */
QImage renderModelThumbnail(
  const std::filesystem::path& folder,
  const std::filesystem::path& relativePath,
  int size);

} // namespace tb::ui
