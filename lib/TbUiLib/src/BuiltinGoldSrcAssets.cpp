/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/BuiltinGoldSrcAssets.h"

#include <QFile>
#include <QResource>

#include "base/Uuid.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/Entity.h"
#include "mdl/EntityProperties.h"
#include "mdl/Map.h"
#include "mdl/NodeQueries.h"
#include "mdl/WadPropertyUtils.h"
#include "mdl/WorldNode.h"
#include "project/GoldSrcWad.h"
#include "project/ProjectStore.h"
#include "ui/QPathUtils.h"

#include "kd/string_compare.h"

#include <algorithm>

static void initializeBuiltinGoldSrcResources()
{
  Q_INIT_RESOURCE(builtin_goldsrc);
}

namespace tb::ui
{
Result<std::vector<std::uint8_t>> builtinEmptyTextureWad()
{
  static const auto initialized = [] {
    initializeBuiltinGoldSrcResources();
    return true;
  }();
  (void)initialized;
  auto file = QFile{QStringLiteral(":/builtin-goldsrc/tb_empty.wad")};
  if (!file.open(QIODevice::ReadOnly))
    return Error{"Cannot load the built-in placeholder texture."};
  const auto bytes = file.readAll();
  if (file.error() != QFileDevice::NoError)
    return Error{"Cannot read the built-in placeholder texture."};
  return std::vector<std::uint8_t>{bytes.begin(), bytes.end()};
}

bool mapUsesEmptyTexture(const mdl::Map& map)
{
  return mdl::findNodeOrDescendant<mdl::BrushNode>(
           map.worldNode().children(),
           [](const auto& node) {
             return std::ranges::any_of(node.brush().faces(), [](const auto& face) {
               return face.materialName().empty()
                      || kdl::ci::str_is_equal(
                        face.materialName(), mdl::BrushFace::NoMaterialName);
             });
           })
         != nullptr;
}

Result<std::filesystem::path> prepareLightmapProject(
  const std::filesystem::path& projectPath, const std::filesystem::path& copyPath)
{
  auto source = project::ProjectStore::open(projectPath);
  if (source.is_error())
    return std::get<Error>(source.error());
  const auto textures = source.value()->assets(project::AssetType::Texture);
  if (textures.is_error())
    return std::get<Error>(textures.error());
  for (const auto& texture : textures.value())
  {
    const auto bytes =
      source.value()->readAsset(texture.id, project::ContentKind::Compatible);
    if (bytes.is_error())
      return std::get<Error>(bytes.error());
    const auto wad = project::readGoldSrcWadView(bytes.value());
    if (
      wad.is_success()
      && std::ranges::any_of(wad.value().textures, [](const auto& entry) {
           return entry.valid
                  && kdl::ci::str_is_equal(entry.name, mdl::BrushFace::NoMaterialName);
         }))
      return projectPath;
  }

  const auto saved = source.value()->saveCopy(copyPath);
  if (saved.is_error())
    return std::get<Error>(saved.error());
  auto copy = project::ProjectStore::open(copyPath);
  if (copy.is_error())
    return std::get<Error>(copy.error());
  const auto wad = builtinEmptyTextureWad();
  if (wad.is_error())
    return std::get<Error>(wad.error());

  // Keep this synthetic asset in the scratch project only. Its random name avoids
  // colliding with user project paths and with extracted WAD file names.
  auto input = project::AssetImport{};
  input.type = project::AssetType::Texture;
  const auto id = generateUuid();
  input.virtualPath = "__tb_lightmap_builtin/" + id + ".wad";
  input.displayName = "__tb_lightmap_builtin_" + id;
  input.sourceName = "tb_empty.wad";
  input.original = wad.value();
  input.compatible = wad.value();
  const auto imported = copy.value()->importAsset(input);
  if (imported.is_error())
    return std::get<Error>(imported.error());
  return copyPath;
}

Result<std::filesystem::path> prepareLightmapWads(
  const mdl::Map& map, const std::filesystem::path& scratchDirectory)
{
  const auto wad = builtinEmptyTextureWad();
  if (wad.is_error())
    return std::get<Error>(wad.error());
  const auto wadPath = pathAsQString(scratchDirectory / "tb_empty.wad");
  auto wadFile = QFile{wadPath};
  if (
    !wadFile.open(QIODevice::WriteOnly)
    || wadFile.write(
         reinterpret_cast<const char*>(wad.value().data()), qint64(wad.value().size()))
         != qint64(wad.value().size()))
    return Error{"Cannot write the built-in placeholder texture."};
  wadFile.close();

  // hltools accepts quoted paths in its WAD configuration; it does not interpret
  // escapes inside those quotes.
  const auto quote = [](const std::string& value) -> Result<std::string> {
    if (value.find_first_of("\"\r\n") != std::string::npos)
      return Error{"A WAD path cannot be represented in the lightmap configuration."};
    return '"' + value + '"';
  };
  const auto fallback = quote(wadPath.toUtf8().toStdString());
  if (fallback.is_error())
    return std::get<Error>(fallback.error());

  const auto& world = map.worldNode().entity();
  const auto* named = world.property("wadconfig");
  const auto* config = world.property("wadcfgfile");
  const auto hasNamed = named && !named->empty();
  auto contents = std::string{};
  if ((config && !config->empty()) || hasNamed)
  {
    const auto configPath = config && !config->empty() ? *config : std::string{"wad.cfg"};
    auto input =
      QFile{QString::fromUtf8(configPath.data(), qsizetype(configPath.size()))};
    if (!input.open(QIODevice::ReadOnly))
      return Error{"Cannot open WAD configuration for lightmap preview: " + configPath};
    contents = input.readAll().toStdString();
    if (input.error() != QFileDevice::NoError)
      return Error{"Cannot read WAD configuration for lightmap preview: " + configPath};
  }
  else if (const auto* paths = world.property(mdl::EntityPropertyKeys::Wad))
  {
    for (const auto& path : mdl::splitWadProperty(*paths))
    {
      if (path.empty())
        continue;
      const auto quoted = quote(path);
      if (quoted.is_error())
        return std::get<Error>(quoted.error());
      contents += quoted.value() + '\n';
    }
  }

  if (hasNamed)
  {
    // The compiler's named WAD configurations are flat brace-delimited lists.
    // Add the fallback to every list so the selected configuration keeps its order.
    auto result = std::string{};
    auto quoted = false;
    for (const auto c : contents)
    {
      if (c == '"')
        quoted = !quoted;
      if (c == '}' && !quoted)
        result += "\n" + fallback.value() + "\n";
      result += c;
    }
    contents = std::move(result);
  }
  else
    contents += "\n" + fallback.value() + "\n";

  const auto configPath = scratchDirectory / "wads.cfg";
  auto output = QFile{pathAsQString(configPath)};
  if (
    !output.open(QIODevice::WriteOnly)
    || output.write(contents.data(), qint64(contents.size())) != qint64(contents.size()))
    return Error{"Cannot write the lightmap WAD configuration."};
  return configPath;
}
} // namespace tb::ui
