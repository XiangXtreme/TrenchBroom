/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/BuiltinAssetLibrary.h"

#include <QLockFile>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "project/AssetLibrary.h"
#include "project/GoldSrcWad.h"
#include "ui/QPathUtils.h"
#include "ui/TerrainBaker.h"

#include <algorithm>
#include <array>

namespace tb::ui
{
Result<project::LibraryInfo> ensureBuiltinAssetLibrary(
  const std::filesystem::path& folder)
{
  auto error = std::error_code{};
  std::filesystem::create_directories(folder.parent_path(), error);
  if (error)
    return Error{"Cannot create the built-in asset directory: " + error.message()};
  auto lock = QLockFile{pathAsQString(folder) + ".lock"};
  if (!lock.tryLock(5000))
    return Error{"Cannot lock the built-in asset library."};
  if (project::AssetLibrary::isLibraryFolder(folder))
  {
    auto library = project::AssetLibrary::open(folder);
    if (library.is_error())
      return std::get<Error>(library.error());
    if (library.value()->info().id != BuiltinAssetLibraryId)
      return Error{"Unexpected built-in asset library identity."};
    return library.value()->info();
  }

  // A failed initialization leaves no partial catalog for the next startup to use.
  auto staging = QTemporaryDir{pathAsQString(folder) + "-XXXXXX"};
  if (!staging.isValid())
    return Error{"Cannot stage the built-in asset library."};
  auto created = project::AssetLibrary::create(
    pathFromQString(staging.path()),
    "TrenchBroom Built-in",
    nullptr,
    std::string{BuiltinAssetLibraryId});
  if (created.is_error())
    return std::get<Error>(created.error());
  auto library = std::move(created).value();
  auto entries = std::vector<std::vector<std::uint8_t>>{};
  for (const auto& texture : builtinTerrainTextures())
    entries.push_back(texture.miptex);
  auto wad = project::buildGoldSrcWad(entries);
  if (wad.is_error())
    return std::get<Error>(wad.error());
  auto imported = library->importFiles(
    {{"terrain.wad", "textures/terrain/terrain.wad", std::move(wad).value()}});
  if (imported.is_error())
    return std::get<Error>(imported.error());
  const auto textures = library->assets(project::AssetType::Texture);
  if (textures.is_error())
    return std::get<Error>(textures.error());
  auto definition = project::AssetLibrary::TerrainMaterialDefinition{};
  definition.name = "Default Terrain";
  definition.folder = "materials/terrain";
  constexpr auto labels = std::array{"Grass", "Dirt", "Rock"};
  for (auto slot = size_t{0}; slot < labels.size(); ++slot)
  {
    const auto found = std::ranges::find(
      textures.value(),
      builtinTerrainTextures()[slot].name,
      &project::LibraryAsset::sourceMember);
    if (found == textures.value().end())
      return Error{"Missing built-in terrain texture."};
    definition.materialSlots[slot] = {labels[slot], found->id};
  }
  auto material = library->createTerrainMaterial(definition);
  if (material.is_error())
    return std::get<Error>(material.error());
  library.reset(); // Close SQLite before publishing the completed directory on Windows.
  std::filesystem::rename(pathFromQString(staging.path()), folder, error);
  if (error)
    return Error{"Cannot publish the built-in asset library: " + error.message()};
  staging.setAutoRemove(false);
  auto published = project::AssetLibrary::open(folder);
  if (published.is_error())
    return std::get<Error>(published.error());
  return published.value()->info();
}

Result<project::LibraryInfo> builtinAssetLibraryInfo()
{
  // Bump the folder version when bundled content changes. The portable catalog ID
  // and path-derived asset IDs keep captures linked across installations.
  static const auto info = ensureBuiltinAssetLibrary(
    pathFromQString(
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
    / "builtin-assets-v1");
  return info;
}
} // namespace tb::ui
