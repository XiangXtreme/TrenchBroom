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
using ModelPreviewTexture = mdl::GoldSrcStudioTexture;

struct ModelPreviewFrame
{
  std::string name;
  std::vector<vm::vec3f> positions;
  std::vector<std::array<float, 2>> texCoords;
  std::vector<std::array<size_t, 3>> triangles;
  std::vector<std::size_t> triangleTextures;
  std::vector<ModelPreviewTexture> textures;
};
struct ModelPreviewScene
{
  std::vector<ModelPreviewFrame> frames;
  // Studio metadata drives the inspector's on-demand sequence and body controls.
  std::optional<mdl::GoldSrcStudioModelInfo> studioModel;
};

/**
 * Loads the bind pose of a GoldSrc studio model.
 *
 * The entity model loaders do not understand the GoldSrc studio format, so the asset
 * browser reads it directly. The scene starts with sequence 0, frame 0 and default skin
 * textures. Returns nullopt for other formats or unreadable model metadata.
 */
std::optional<ModelPreviewScene> loadGoldSrcStudioPreviewScene(
  const std::vector<std::uint8_t>& bytes);

/**
 * Builds one frame of one sequence, for the inspector's playback controls.
 *
 * The scene loader only builds the first frame; playback asks for frames on demand so a
 * model with dozens of sequences does not decode all of them up front.
 * Pass decodeTexturePixels=false to reuse pixels already decoded for this model.
 */
std::optional<ModelPreviewFrame> loadGoldSrcStudioFrame(
  const std::vector<std::uint8_t>& bytes,
  std::size_t sequence,
  std::size_t frame,
  const std::vector<std::size_t>& variants = {},
  bool decodeTexturePixels = true);

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
 * Renders the first frame of a GoldSrc studio model into a textured thumbnail.
 *
 * TrenchBroom renders models through the GPU for the model browser, which needs a game
 * context and a GL surface. The asset library browses files that no game owns, so it gets
 * a small software projection through TbMdlLib's read-only geometry accessors instead.
 * Uses embedded default skin textures; blended materials are not simulated.
 */
QImage renderModelThumbnail(
  const std::filesystem::path& folder,
  const std::filesystem::path& relativePath,
  int size);

} // namespace tb::ui
