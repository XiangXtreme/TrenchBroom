/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom, licensed under GPL version 3 or later.
*/
#pragma once

#include <filesystem>

class QImage;

namespace tb::ui
{

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
