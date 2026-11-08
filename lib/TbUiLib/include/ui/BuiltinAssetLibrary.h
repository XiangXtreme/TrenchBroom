/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include "base/Result.h"
#include "project/ProjectAsset.h"

#include <filesystem>
#include <string_view>

namespace tb::ui
{
inline constexpr std::string_view BuiltinAssetLibraryId =
  "c6e7912c-5608-4e2f-a3e0-970a61726439";

/** Materialize the versioned, application-owned catalog atomically. */
Result<project::LibraryInfo> ensureBuiltinAssetLibrary(
  const std::filesystem::path& folder);
/** Shared by both browsers and project captures; never added to the user's library. */
Result<project::LibraryInfo> builtinAssetLibraryInfo();
} // namespace tb::ui
