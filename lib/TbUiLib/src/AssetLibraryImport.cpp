/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom, licensed under GPL version 3 or later.
*/
#include "ui/AssetLibraryImport.h"

#include <QFile>
#include <QSaveFile>

#include "img/DecodeImage.h"
#include "img/GoldSrcTexture.h"
#include "project/AssetLibrary.h"
#include "ui/QPathUtils.h"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>
#include <system_error>

namespace tb::ui
{
namespace
{
std::string lowerExtension(const std::filesystem::path& file)
{
  auto extension = file.extension().string();
  std::ranges::transform(extension, extension.begin(), [](const char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  });
  return extension;
}

std::string stemOfVirtualPath(const std::string& virtualPath)
{
  return std::filesystem::path{virtualPath}.stem().string();
}
} // namespace

std::filesystem::path librarySubfolderForFile(const std::filesystem::path& file)
{
  const auto extension = lowerExtension(file);
  if (
    extension == ".png" || extension == ".jpg" || extension == ".jpeg"
    || extension == ".bmp" || extension == ".tga" || extension == ".wad")
    return "textures";
  if (extension == ".mdl")
    return "models";
  if (extension == ".spr")
    return "sprites";
  if (extension == ".wav")
    return "sound";
  if (extension == ".tbprefab")
    return "prefabs";
  return {};
}

std::string goldSrcTextureNameFromStem(const std::string& stem, const bool masked)
{
  auto name = std::string{};
  for (const auto c : stem)
  {
    const auto allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                         || (c >= '0' && c <= '9') || c == '_' || c == '-';
    name += allowed ? c : '_';
  }
  if (name.empty())
    name = "texture";
  if (masked)
    name.insert(name.begin(), '{');
  if (name.size() > 15)
    name.resize(15);
  return name;
}

std::string goldSrcTextureRecipe(
  const std::string& name,
  const std::uint32_t width,
  const std::uint32_t height,
  const bool masked)
{
  return fmt::format(
    R"({{"importer":"goldsrc-texture","version":1,"name":"{}","width":{},"height":{},"masked":{},"alphaThreshold":128,"formatValidated":true}})",
    name,
    width,
    height,
    masked ? "true" : "false");
}

Result<std::string> importFileIntoLibrary(
  const std::filesystem::path& libraryFolder,
  const std::filesystem::path& source,
  const std::string& relativePath,
  const bool overwrite)
{
  const auto relative = std::filesystem::path{relativePath};
  if (
    relative.empty() || relative.is_absolute() || relative.has_root_name()
    || std::ranges::find(relative, std::filesystem::path{".."}) != relative.end())
    return Error{"The import path must stay inside the asset library."};
  const auto target = libraryFolder / std::filesystem::path{relativePath};
  auto error = std::error_code{};
  if (std::filesystem::exists(target, error) && !overwrite)
    return Error{fmt::format("'{}' already exists in the library.", relativePath)};
  if (error)
    return Error{fmt::format("Cannot inspect '{}': {}.", relativePath, error.message())};

  std::filesystem::create_directories(target.parent_path(), error);
  if (error)
    return Error{fmt::format("Cannot create '{}': {}.", relativePath, error.message())};

  auto input = QFile{pathAsQString(source)};
  if (!input.open(QIODevice::ReadOnly))
    return Error{input.errorString().toStdString()};
  // Commit replaces the target only after the complete source was copied successfully.
  auto output = QSaveFile{pathAsQString(target)};
  if (!output.open(QIODevice::WriteOnly))
    return Error{output.errorString().toStdString()};
  while (!input.atEnd())
  {
    const auto chunk = input.read(1024 * 1024);
    if (input.error() != QFileDevice::NoError)
      return Error{input.errorString().toStdString()};
    if (output.write(chunk) != chunk.size())
      return Error{output.errorString().toStdString()};
  }
  if (!output.commit())
    return Error{output.errorString().toStdString()};
  return relativePath;
}

Result<bool> convertLibraryImage(
  project::AssetLibrary& library, const std::string& assetId)
{
  const auto asset = library.findAsset(assetId);
  if (asset.is_error())
    return std::get<Error>(asset.error());
  if (!asset.value())
    return Error{"The library no longer contains that asset."};
  if (asset.value()->nativeFormat != "image")
    return false;

  const auto original = library.readOriginal(assetId);
  if (original.is_error())
    return std::get<Error>(original.error());

  const auto decoded = img::decodeImage(original.value().data(), original.value().size());
  if (decoded.is_error())
    return std::get<Error>(decoded.error());
  const auto& image = decoded.value();

  const auto stem = stemOfVirtualPath(asset.value()->virtualPath);
  const auto proposed =
    goldSrcTextureNameFromStem(stem, image.alphaDomain != img::ImageAlphaDomain::Opaque);

  const auto converted = img::convertGoldSrcTexture(image, proposed);
  if (converted.is_error())
    return std::get<Error>(converted.error());
  const auto& texture = converted.value();

  const auto published = library.publishCompatibleContent(
    assetId,
    texture.wad,
    goldSrcTextureRecipe(
      texture.name,
      static_cast<std::uint32_t>(texture.preview.width),
      static_cast<std::uint32_t>(texture.preview.height),
      texture.masked));
  if (published.is_error())
    return std::get<Error>(published.error());
  return true;
}
} // namespace tb::ui
