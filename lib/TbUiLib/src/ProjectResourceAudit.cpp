/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/ProjectResourceAudit.h"

#include <QObject>

#include "base/FileLocation.h"
#include "base/Logger.h"
#include "base/SimpleParserStatus.h"
#include "mdl/BrushFace.h"
#include "mdl/EntityProperties.h"
#include "mdl/MapHeader.h"
#include "mdl/StandardMapParser.h"
#include "ui/ProjectReference.h"
#include "ui/ProjectSession.h"
#include "ui/TerrainChunks.h"
#include "ui/TerrainRegion.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <set>
#include <sstream>
#include <unordered_set>

namespace tb::ui
{
QString projectResourceStatusLabel(const ProjectResourceStatus status)
{
  switch (status)
  {
  case ProjectResourceStatus::Embedded:
    return QObject::tr("Embedded");
  case ProjectResourceStatus::Missing:
    return QObject::tr("Not embedded");
  case ProjectResourceStatus::Ambiguous:
    return QObject::tr("Ambiguous");
  case ProjectResourceStatus::InvalidPath:
    return QObject::tr("Invalid path");
  case ProjectResourceStatus::Unsupported:
    return QObject::tr("Needs lookup");
  case ProjectResourceStatus::ScanError:
    return QObject::tr("Scan failed");
  }
  return {};
}
namespace
{
enum class ReferenceKind
{
  File,
  Texture,
  TerrainMaterial,
  InvalidTerrain,
  Unsupported
};
struct Reference
{
  size_t line;
  std::string location;
  std::string value;
  ReferenceKind kind;
};

class ReferenceParser : public mdl::StandardMapParser
{
  std::string m_classname;
  bool m_hasWorld = false;
  bool m_includeEditorReferences;
  std::vector<Reference> m_references;

public:
  ReferenceParser(
    const std::string& text,
    const mdl::MapFormat format,
    const bool includeEditorReferences)
    : StandardMapParser{text, format, format}
    , m_includeEditorReferences{includeEditorReferences}
  {
  }

  Result<std::vector<Reference>> read()
  {
    auto logger = NullLogger{};
    auto status = SimpleParserStatus{logger};
    return parseEntities(status) | kdl::and_then([&]() -> Result<std::vector<Reference>> {
             if (!m_hasWorld)
               return Error{"MAP has no worldspawn entity."};
             return std::move(m_references);
           });
  }

private:
  void onBeginEntity(
    const FileLocation& location,
    std::vector<mdl::EntityProperty> properties,
    ParserStatus&) override
  {
    m_classname = mdl::findEntityPropertyOrDefault(properties, "classname");
    m_hasWorld |= m_classname == "worldspawn";
    auto terrainPaint = mdl::findEntityPropertyOrDefault(
      properties, mdl::EntityPropertyKeys::TbTerrainPaint);
    if (!terrainPaint.empty() && terrainPaint != terrainChunkGroupMarker())
    {
      // Serialized group properties are split into 900-byte chunks.
      for (auto index = 1;; ++index)
      {
        const auto chunk = mdl::findEntityPropertyOrDefault(
          properties, mdl::EntityPropertyKeys::TbTerrainPaint + std::to_string(index));
        if (chunk.empty())
          break;
        terrainPaint += chunk;
      }
      const auto region = TerrainRegion::decode(terrainPaint);
      const auto where = m_classname + "." + mdl::EntityPropertyKeys::TbTerrainPaint;
      if (region.is_error())
        m_references.push_back({location.line, where, {}, ReferenceKind::InvalidTerrain});
      else if (!region.value().materialAssetId.empty())
        m_references.push_back(
          {location.line,
           where,
           region.value().materialAssetId,
           ReferenceKind::TerrainMaterial});
    }
    for (const auto& property : properties)
    {
      const auto& key = property.key();
      const auto& value = property.value();
      if (value.empty())
        continue;
      const auto where = m_classname + "." + key;
      if (m_classname == "worldspawn" && key == "skyname")
      {
        for (const auto* suffix : {"bk", "dn", "ft", "lf", "rt", "up"})
          m_references.push_back(
            {location.line,
             where,
             "gfx/env/" + value + suffix + ".tga",
             ReferenceKind::File});
        continue;
      }
      const auto sound = (m_classname == "ambient_generic" && key == "message")
                         || (m_classname == "env_message" && key == "messagesound");
      if (sound)
      {
        m_references.push_back(
          {location.line,
           where,
           value.front() == '!' ? value : "sound/" + value,
           value.front() == '!' ? ReferenceKind::Unsupported : ReferenceKind::File});
        continue;
      }
      // Class/key pairs are deliberate: player-start model choices and FGD preview
      // models are editor data, not runtime dependencies. See native-project-usage.md.
      if (isGoldSrcResourceProperty(m_classname, key))
        m_references.push_back({location.line, where, value, ReferenceKind::File});
      else if (m_classname == "infodecal" && key == "texture")
        m_references.push_back(
          {location.line,
           where,
           value,
           m_includeEditorReferences ? ReferenceKind::Texture
                                     : ReferenceKind::Unsupported});
      else if (m_includeEditorReferences && normalizeGamePath(value))
        m_references.push_back({location.line, where, value, ReferenceKind::File});
    }
  }
  void onEndEntity(const FileLocation&, ParserStatus&) override {}
  void onBeginBrush(const FileLocation&, ParserStatus&) override {}
  void onEndBrush(const FileLocation&, ParserStatus&) override {}
  void texture(const FileLocation& location, std::string name)
  {
    m_references.push_back(
      {location.line,
       m_classname + " brush face",
       std::move(name),
       ReferenceKind::Texture});
  }
  void onStandardBrushFace(
    const FileLocation& location,
    mdl::MapFormat,
    const vm::vec3d&,
    const vm::vec3d&,
    const vm::vec3d&,
    std::string material,
    const mdl::UvAttributes&,
    const mdl::SurfaceAttributes&,
    ParserStatus&) override
  {
    texture(location, std::move(material));
  }
  void onValveBrushFace(
    const FileLocation& location,
    mdl::MapFormat,
    const vm::vec3d&,
    const vm::vec3d&,
    const vm::vec3d&,
    std::string material,
    const mdl::UvAttributes&,
    const mdl::SurfaceAttributes&,
    const vm::vec3d&,
    const vm::vec3d&,
    ParserStatus&) override
  {
    texture(location, std::move(material));
  }
  void onPatch(
    const FileLocation& location,
    const FileLocation&,
    mdl::MapFormat,
    size_t,
    size_t,
    std::vector<vm::vec<double, 5>>,
    std::string material,
    ParserStatus&) override
  {
    texture(location, std::move(material));
  }
};

Result<std::vector<Reference>> scan(
  const std::vector<std::uint8_t>& bytes, const bool includeEditorReferences)
{
  const auto text = std::string{bytes.begin(), bytes.end()};
  auto stream = std::istringstream{text};
  return mdl::readMapHeader(stream)
         | kdl::and_then([&](const auto& header) -> Result<std::vector<Reference>> {
             const auto format = header.second;
             if (format != mdl::MapFormat::Standard && format != mdl::MapFormat::Valve)
               return Error{
                 "Resource checking requires a Standard or Valve MAP format header."};
             return ReferenceParser{text, format, includeEditorReferences}.read();
           });
}

Result<ProjectResourceAudit> auditProjectResources(
  const ProjectSession& session,
  const std::optional<std::string>& mapId,
  const bool includeEditorReferences)
{
  const auto index = ProjectReferenceIndex::build(session);
  if (index.is_error())
    return std::get<Error>(index.error());
  const auto assets = session.store().assets();
  if (assets.is_error())
    return std::get<Error>(assets.error());

  auto result = ProjectResourceAudit{};
  for (const auto& problem : index.value().unreadableWads())
    result.uses.push_back(
      {{},
       {},
       0,
       "WAD directory",
       problem.virtualPath,
       ProjectResourceStatus::ScanError,
       {problem.assetId},
       problem.message});

  for (const auto& map : assets.value())
  {
    if (map.type != project::AssetType::Map || (mapId && map.id != *mapId))
      continue;
    const auto references =
      session.workingMapContents(map.id) | kdl::and_then([&](const auto& bytes) {
        return scan(bytes, includeEditorReferences);
      });
    if (references.is_error())
    {
      result.uses.push_back(
        {map.id,
         map.virtualPath,
         0,
         "MAP",
         {},
         ProjectResourceStatus::ScanError,
         {},
         std::get<Error>(references.error()).msg});
      continue;
    }
    ++result.mapsScanned;
    for (const auto& reference : references.value())
    {
      auto use = ProjectResourceUse{
        map.id,
        map.virtualPath,
        reference.line,
        reference.location,
        reference.value,
        ProjectResourceStatus::Missing,
        {},
        {}};
      if (reference.kind == ReferenceKind::InvalidTerrain)
      {
        use.status = ProjectResourceStatus::ScanError;
        use.detail = "The terrain material reference could not be read.";
      }
      else if (reference.kind == ReferenceKind::TerrainMaterial)
      {
        const auto asset = session.store().findAsset(reference.value);
        if (asset.is_error())
          return std::get<Error>(asset.error());
        if (asset.value() && asset.value()->type == project::AssetType::Material)
        {
          use.status = ProjectResourceStatus::Embedded;
          use.assetIds.push_back(asset.value()->id);
        }
        else
          use.detail = "The terrain material is missing from this project.";
      }
      else if (reference.kind == ReferenceKind::Unsupported)
      {
        use.status = ProjectResourceStatus::Unsupported;
        use.detail = "Sentence/decal lookup requires a separate resource catalog.";
      }
      else if (normalizeGamePath(reference.value))
      {
        const auto match = reference.kind == ReferenceKind::Texture
                             ? index.value().resolveTexture(reference.value)
                             : index.value().resolvePath(reference.value);
        use.assetIds.reserve(match.assets.size());
        for (const auto& asset : match.assets)
          use.assetIds.push_back(asset.id);
        use.status =
          match.status == ReferenceStatus::Resolved    ? ProjectResourceStatus::Embedded
          : match.status == ReferenceStatus::Ambiguous ? ProjectResourceStatus::Ambiguous
                                                       : ProjectResourceStatus::Missing;
        if (use.status == ProjectResourceStatus::Missing)
          use.detail =
            "Not embedded in this project; may be supplied by the installed game.";
        else if (use.status == ProjectResourceStatus::Ambiguous)
          use.detail =
            "Multiple embedded assets match; resolve the conflict before publishing.";
      }
      else
      {
        use.status = ProjectResourceStatus::InvalidPath;
        use.detail =
          "Expected a safe relative game path without empty, . or .. components.";
      }
      result.uses.push_back(std::move(use));
    }
  }
  return result;
}

} // namespace

Result<ProjectResourceAudit> auditGoldSrcProjectResources(
  const ProjectSession& session, const std::optional<std::string>& mapId)
{
  return auditProjectResources(session, mapId, false);
}

Result<std::map<std::string, std::vector<std::string>>> projectAssetMapReferences(
  const ProjectSession& session)
{
  const auto audit = auditProjectResources(session, std::nullopt, true);
  if (audit.is_error())
    return std::get<Error>(audit.error());
  auto roots = std::map<std::string, std::set<std::string>>{};
  auto unreadableWads = std::set<std::string>{};
  for (const auto& use : audit.value().uses)
  {
    if (use.status == ProjectResourceStatus::ScanError && use.mapId.empty())
    {
      // A broken WAD cannot prove its texture names unused, but does not make unrelated
      // model/sound references unknowable. Keep it while any map remains.
      unreadableWads.insert(use.assetIds.begin(), use.assetIds.end());
      continue;
    }
    if (
      use.status == ProjectResourceStatus::ScanError
      || use.status == ProjectResourceStatus::Unsupported)
      return Error{QObject::tr("Cannot verify project map references: %1\n%2")
                     .arg(QString::fromStdString(use.mapPath))
                     .arg(QString::fromStdString(use.detail))
                     .toStdString()};
    roots[use.mapPath].insert(use.assetIds.begin(), use.assetIds.end());
  }
  // Explicit catalog dependencies can also originate on a map itself.
  const auto assets = session.store().assets();
  if (assets.is_error())
    return std::get<Error>(assets.error());
  for (const auto& asset : assets.value())
    if (asset.type == project::AssetType::Map)
    {
      roots[asset.virtualPath].insert(unreadableWads.begin(), unreadableWads.end());
      const auto dependencies = session.store().dependencies(asset.id);
      if (dependencies.is_error())
        return std::get<Error>(dependencies.error());
      for (const auto& dependency : dependencies.value())
        roots[asset.virtualPath].insert(dependency.dependencyId);
    }
  auto result = std::map<std::string, std::vector<std::string>>{};
  auto dependencyCache = std::map<std::string, std::vector<project::AssetDependency>>{};
  for (const auto& [mapPath, ids] : roots)
  {
    auto pending = std::vector<std::string>{ids.begin(), ids.end()};
    auto visited = std::set<std::string>{};
    while (!pending.empty())
    {
      auto id = std::move(pending.back());
      pending.pop_back();
      if (!visited.insert(id).second)
        continue;
      result[id].push_back(mapPath);
      auto cached = dependencyCache.find(id);
      if (cached == dependencyCache.end())
      {
        auto dependencies = session.store().dependencies(id);
        if (dependencies.is_error())
          return std::get<Error>(dependencies.error());
        cached = dependencyCache.emplace(id, std::move(dependencies).value()).first;
      }
      for (const auto& dependency : cached->second)
        pending.push_back(dependency.dependencyId);
    }
  }
  return result;
}

Result<void> validateGoldSrcCompileResources(
  const ProjectResourceAudit& audit, const std::string& mapId)
{
  static const auto ToolTextures = std::set<std::string>{
    "aaatrigger",
    "bevel",
    "clip",
    "hint",
    "hintskip",
    "light",
    "null",
    "origin",
    "skip",
    "split",
    "trigger",
    "waterskip"};
  auto problems = std::vector<std::string>{};
  auto emptyMaterial = mdl::BrushFace::NoMaterialName;
  std::ranges::transform(emptyMaterial, emptyMaterial.begin(), [](const unsigned char c) {
    return char(std::tolower(c));
  });
  for (const auto& use : audit.uses)
  {
    if (use.mapId != mapId)
      continue;
    auto name = use.reference;
    std::ranges::transform(
      name, name.begin(), [](const unsigned char c) { return char(std::tolower(c)); });
    auto reason = std::string{};
    switch (use.status)
    {
    case ProjectResourceStatus::Embedded:
    case ProjectResourceStatus::Unsupported:
      break;
    case ProjectResourceStatus::Missing:
      // Compiler tool/empty textures and installed engine skies are intentionally
      // external. All other explicit references must be captured before compiling.
      if (
        name != emptyMaterial && !ToolTextures.contains(name)
        && !name.starts_with("gfx/env/"))
        reason = " is not embedded in the project";
      break;
    case ProjectResourceStatus::Ambiguous:
      reason = " matches more than one project asset";
      break;
    case ProjectResourceStatus::InvalidPath:
      reason = " is not a usable game path";
      break;
    case ProjectResourceStatus::ScanError:
      reason = " could not be scanned";
      break;
    }
    if (!reason.empty())
      problems.push_back(
        use.reference + " (" + use.mapPath + ":" + std::to_string(use.line) + ")"
        + reason);
  }
  if (problems.empty())
    return {};
  auto text = std::string{};
  const auto count = std::min(problems.size(), std::size_t{12});
  for (auto i = std::size_t{0}; i < count; ++i)
    text += "\n  " + problems[i];
  if (problems.size() > count)
    text += "\n  ... and " + std::to_string(problems.size() - count) + " more";
  return Error{
    "The compile snapshot would be incomplete or ambiguous:" + text
    + "\nCapture the missing resources into the project first."};
}

std::vector<BrowserAsset> browserAssetsUsedInMap(
  const std::vector<BrowserAsset>& assets,
  const ProjectResourceAudit& audit,
  const std::string& mapId)
{
  auto ids = std::unordered_set<std::string>{};
  auto paths = std::unordered_set<std::string>{};
  for (const auto& use : audit.uses)
  {
    if (
      use.mapId != mapId || use.status == ProjectResourceStatus::ScanError
      || use.status == ProjectResourceStatus::InvalidPath
      || use.status == ProjectResourceStatus::Unsupported)
      continue;
    ids.insert(use.assetIds.begin(), use.assetIds.end());
    if (const auto path = normalizeGamePath(use.reference))
      paths.insert(*path);
  }
  auto result = std::vector<BrowserAsset>{};
  for (const auto& asset : assets)
  {
    const auto& id =
      asset.source == BrowserSource::AssetLibrary ? asset.projectAssetId : asset.assetId;
    // Project identity wins; a different snapshot with the same path is not a use.
    const auto gamePath = asset.source == BrowserSource::GameFiles
                            ? normalizeGamePath(asset.path.generic_string())
                            : std::nullopt;
    if ((!id.empty() && ids.contains(id)) || (gamePath && paths.contains(*gamePath)))
      result.push_back(asset);
  }
  return result;
}
} // namespace tb::ui
