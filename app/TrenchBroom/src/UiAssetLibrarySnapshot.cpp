/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */

#include "UiAssetLibrarySnapshot.h"

#include <QColor>
#include <QDir>
#include <QImage>
#include <QLinearGradient>
#include <QObject>
#include <QPainter>
#include <QTemporaryDir>

#include "mdl/MapFormat.h"
#include "mdl/MapHeader.h"
#include "project/AssetLibrary.h"
#include "ui/AssetInspection.h"
#include "ui/AssetLibraryImport.h"
#include "ui/AssetLibraryWindow.h"
#include "ui/BuiltinAssetLibrary.h"
#include "ui/ProjectReference.h"
#include "ui/ProjectSession.h"
#include "ui/QPathUtils.h"
#include "ui/TerrainMaterial.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace tb::ui
{
namespace
{
/**
 * Owns the disposable library folder for as long as the browser window lives.
 *
 * The window is created as a childless dialog, so the holder is attached to it as a
 * QObject child: Qt then removes the fixture after the window released its source.
 */
class TemporaryLibrary : public QObject
{
  QTemporaryDir m_directory;
  std::shared_ptr<ProjectSession> m_project;

public:
  explicit TemporaryLibrary(QObject* parent)
    : QObject{parent}
  {
    m_directory.setAutoRemove(true);
  }

  std::filesystem::path folder() const { return pathFromQString(m_directory.path()); }
  void setProject(std::shared_ptr<ProjectSession> project)
  {
    m_project = std::move(project);
  }
};

void writeBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
  auto error = std::error_code{};
  std::filesystem::create_directories(path.parent_path(), error);
  auto stream = std::ofstream{path, std::ios::binary};
  stream.write(
    reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

void writeText(const std::filesystem::path& path, const std::string& text)
{
  writeBytes(path, std::vector<std::uint8_t>{text.begin(), text.end()});
}

/**
 * Writes a readable tile so every thumbnail differs at a glance: a diagonal two tone
 * gradient with a lighter inset square, which reads well scaled down to 72 pixels.
 */
bool writeTexture(const std::filesystem::path& path, const QColor& from, const QColor& to)
{
  auto error = std::error_code{};
  std::filesystem::create_directories(path.parent_path(), error);
  auto image = QImage{128, 128, QImage::Format_RGB32};
  auto painter = QPainter{&image};
  painter.setRenderHint(QPainter::Antialiasing, true);
  auto gradient = QLinearGradient{0, 0, 128, 128};
  gradient.setColorAt(0.0, from);
  gradient.setColorAt(1.0, to);
  painter.fillRect(image.rect(), gradient);
  painter.setPen(Qt::NoPen);
  auto accent = QColor{255, 255, 255, 46};
  painter.setBrush(accent);
  painter.drawRect(QRect{16, 16, 96, 96});
  painter.setBrush(QColor{0, 0, 0, 40});
  painter.drawRect(QRect{16, 16, 96, 18});
  painter.end();
  return image.save(QString::fromStdString(path.string()));
}

void createTextures(const std::filesystem::path& folder)
{
  const auto textures = folder / "textures";
  writeTexture(
    textures / "wall01.png", QColor{0x4a, 0x6f, 0x9c}, QColor{0x24, 0x3b, 0x57});
  writeTexture(
    textures / "wall02.png", QColor{0x7c, 0x5a, 0x92}, QColor{0x3d, 0x2a, 0x4d});
  writeTexture(
    textures / "floor_dirt.png", QColor{0x8a, 0x6f, 0x4a}, QColor{0x4b, 0x39, 0x24});
  writeTexture(
    textures / "floor_grass.png", QColor{0x5c, 0x8a, 0x4a}, QColor{0x2f, 0x4a, 0x24});
  writeTexture(
    textures / "trim_metal.png", QColor{0x78, 0x80, 0x88}, QColor{0x3a, 0x40, 0x46});
  writeTexture(
    textures / "trim_rust.png", QColor{0x9c, 0x5a, 0x3a}, QColor{0x52, 0x2c, 0x1c});
  writeTexture(
    textures / "sign_arrow.png", QColor{0xc9, 0xb4, 0x5a}, QColor{0x6b, 0x5c, 0x24});
  writeTexture(
    textures / "grate.png", QColor{0x55, 0x55, 0x5c}, QColor{0x2a, 0x2a, 0x30});

  // Six faces with a shared stem make one logical skybox asset.
  const auto faces = std::array<std::pair<const char*, QColor>, 6>{
    std::pair{"bk", QColor{0x35, 0x47, 0x6b}},
    std::pair{"rt", QColor{0x3d, 0x52, 0x78}},
    std::pair{"lf", QColor{0x32, 0x42, 0x62}},
    std::pair{"ft", QColor{0x3a, 0x4d, 0x72}},
    std::pair{"up", QColor{0x1f, 0x2a, 0x44}},
    std::pair{"dn", QColor{0x4a, 0x5c, 0x82}}};
  for (const auto& [suffix, color] : faces)
  {
    const auto name = std::string{"night"} + suffix + ".png";
    writeTexture(textures / name, color, color.darker(160));
  }
}

void createOtherSources(const std::filesystem::path& folder)
{
  // The catalog records whatever bytes it finds; only the extension decides the type.
  writeText(folder / "models" / "barrel.mdl", "IDST snapshot fixture model");
  writeText(folder / "models" / "crate.mdl", "IDST snapshot fixture model");
  writeText(folder / "sprites" / "explosion.spr", "IDSP snapshot fixture sprite");
  writeText(folder / "sound" / "ric1.wav", "RIFF snapshot fixture sound");
  writeText(folder / "sound" / "ric2.wav", "RIFF snapshot fixture sound");
  writeText(folder / "sound" / "ric3.wav", "RIFF snapshot fixture sound");
  writeText(
    folder / "prefabs" / "crate.tbprefab",
    "{\n"
    "\"classname\" \"worldspawn\"\n"
    "{\n"
    "( 0 0 0 ) ( 0 1 0 ) ( 0 0 1 ) brick01 0 0 0 1 1\n"
    "( 0 0 0 ) ( 0 0 1 ) ( 1 0 0 ) brick01 0 0 0 1 1\n"
    "( 0 0 0 ) ( 1 0 0 ) ( 0 1 0 ) brick01 0 0 0 1 1\n"
    "( 64 64 64 ) ( 64 65 64 ) ( 65 64 64 ) brick01 0 0 0 1 1\n"
    "( 64 64 64 ) ( 65 64 64 ) ( 64 64 65 ) brick01 0 0 0 1 1\n"
    "( 64 64 64 ) ( 64 64 65 ) ( 64 65 64 ) brick01 0 0 0 1 1\n"
    "}\n"
    "}\n"
    "{\n"
    "\"classname\" \"cycler\"\n"
    "\"model\" \"models/barrel.mdl\"\n"
    "}\n");
}

/** Converts the first `count` unconverted textures so the fixture shows both states. */
void convertSomeTextures(project::AssetLibrary& library, const int count)
{
  const auto assets = library.assets();
  if (assets.is_error())
    return;

  auto converted = 0;
  for (const auto& asset : assets.value())
  {
    if (converted >= count)
      return;
    if (asset.type != project::AssetType::Texture || asset.missing)
      continue;
    if (const auto result = convertLibraryImage(library, asset.id);
        result.is_success() && result.value())
      ++converted;
  }
}

/** Deletes one source after the scan so the browser shows a missing asset. */
void dropOneSource(const std::filesystem::path& folder)
{
  auto error = std::error_code{};
  std::filesystem::remove(folder / "textures" / "grate.png", error);
}

std::optional<std::string> createTerrainMaterial(project::AssetLibrary& library)
{
  const auto assets = library.assets(project::AssetType::Texture);
  if (assets.is_error())
    return {};
  const auto paths = std::array{
    "textures/floor_grass.png", "textures/floor_dirt.png", "textures/wall01.png"};
  auto definition = project::AssetLibrary::TerrainMaterialDefinition{};
  definition.name = "courtyard_terrain";
  auto images = std::array<QImage, 3>{};
  for (auto slot = size_t{0}; slot < paths.size(); ++slot)
  {
    const auto asset = std::ranges::find_if(assets.value(), [&](const auto& entry) {
      return entry.virtualPath == paths[slot];
    });
    if (
      asset == assets.value().end() || convertLibraryImage(library, asset->id).is_error())
      return {};
    const auto content = library.readContent(asset->id);
    if (content.is_error())
      return {};
    const auto image = inspection::decodeImage(content.value());
    if (image.is_error())
      return {};
    definition.materialSlots[slot].texture = asset->id;
    images[slot] = image.value();
  }
  const auto material = prepareTerrainMaterial(std::move(images));
  if (material.is_error())
    return {};
  auto package = encodeTerrainMaterialPackage(material.value());
  if (package.empty())
    return {};
  const auto created = library.createTerrainMaterial(definition, std::move(package));
  return created.is_success() ? std::optional{created.value().id} : std::nullopt;
}
} // namespace

std::unique_ptr<QWidget> createUiAssetLibrarySnapshot(
  const UiAssetLibrarySnapshotKind kind)
{
  auto holder = std::unique_ptr<TemporaryLibrary>{};
  const auto folder = [&]() {
    // The holder needs a parent, but the window does not exist yet. Construct it first
    // with no parent, then re-parent it once the window is alive.
    holder = std::make_unique<TemporaryLibrary>(nullptr);
    return holder->folder();
  }();

  if (kind == UiAssetLibrarySnapshotKind::Builtin)
  {
    const auto info = ensureBuiltinAssetLibrary(folder / "builtin");
    if (info.is_error())
      return {};
    auto library = project::AssetLibrary::open(folder / "builtin");
    if (library.is_error())
      return {};
    const auto materials = library.value()->assets(project::AssetType::Material);
    if (materials.is_error() || materials.value().size() != 1)
      return {};
    auto window =
      std::make_unique<AssetLibraryWindow>(pathAsQString(folder / "builtin"), false);
    if (window->revealAsset(info.value().id, materials.value().front().id).is_error())
      return {};
    holder->setParent(window.get());
    return window;
  }

  auto created = project::AssetLibrary::create(folder, "Snapshot Library");
  if (created.is_error())
    return {};

  auto& library = *created.value();
  createTextures(folder);
  createOtherSources(folder);

  if (library.importDirectory(library.folder()).is_error())
    return {};

  if (library.createFolder("textures/Environment/Unsorted").is_error())
    return {};

  convertSomeTextures(library, 3);
  dropOneSource(folder);
  if (library.importDirectory(library.folder()).is_error())
    return {};

  auto terrainId = std::optional<std::string>{};
  if (kind == UiAssetLibrarySnapshotKind::TerrainMaterial)
  {
    terrainId = createTerrainMaterial(library);
    if (!terrainId)
      return {};
  }
  const auto assets = library.assets();
  if (assets.is_error())
    return {};
  const auto chosen = std::ranges::find_if(assets.value(), [&](const auto& asset) {
    if (terrainId)
      return asset.id == *terrainId;
    return kind == UiAssetLibrarySnapshotKind::Project
             ? asset.type == project::AssetType::Skybox
             : asset.type == project::AssetType::Texture
                 && asset.nativeFormat == "goldsrc-wad" && !asset.missing;
  });
  if (chosen == assets.value().end())
    return {};
  if (
    library.setFavorite(chosen->id, true).is_error()
    || library.setTags(chosen->id, {"Environment", "Courtyard"}).is_error())
    return {};
  auto session = std::shared_ptr<ProjectSession>{};
  if (kind == UiAssetLibrarySnapshotKind::Project)
  {
    auto createdProject =
      ProjectSession::create(folder / "review.tbproj", "Courtyard", "Half-Life");
    if (createdProject.is_error())
      return {};
    session = std::move(createdProject).value();
    if (session->attachLibrary(folder).is_error())
      return {};
    const auto captured = session->captureLibraryAsset(library.info().id, chosen->id);
    if (captured.is_error())
      return {};
    const auto skyName = gameName(*session, captured.value());
    if (skyName.is_error())
      return {};
    auto stream = std::ostringstream{};
    mdl::writeMapHeader(stream, "Half-Life", mdl::MapFormat::Valve, "Snapshot");
    stream << "{\"classname\" \"worldspawn\" \"skyname\" \"" << skyName.value() << "\"}";
    const auto text = stream.str();
    auto map = project::AssetImport{};
    map.type = project::AssetType::Map;
    map.virtualPath = "maps/courtyard.map";
    map.displayName = "Courtyard";
    map.original = map.compatible = {text.begin(), text.end()};
    if (session->importAsset(map).is_error() || session->save().is_error())
      return {};
    holder->setProject(session);
  }
  auto window = std::make_unique<AssetLibraryWindow>(pathAsQString(folder), false);
  if (session)
    window->setProjectContext(session);
  if (window->revealAsset(library.info().id, chosen->id).is_error())
    return {};
  holder->setParent(window.get());
  window->resize(1180, 760);
  return window;
}

} // namespace tb::ui
