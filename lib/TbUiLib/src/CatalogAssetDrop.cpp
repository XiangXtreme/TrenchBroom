/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/CatalogAssetDrop.h"

#include <QJsonDocument>
#include <QJsonObject>

#include "ui/ProjectReference.h"
#include "ui/ProjectSession.h"

#include <algorithm>
namespace tb::ui
{
namespace
{
constexpr auto Prefix = "tb-asset:";
std::string kind(const project::AssetType type)
{
  switch (type)
  {
  case project::AssetType::Model:
    return "model";
  case project::AssetType::Sprite:
    return "sprite";
  case project::AssetType::Sound:
    return "sound";
  case project::AssetType::Texture:
    return "texture";
  case project::AssetType::Skybox:
    return "skybox";
  default:
    return {};
  }
}
} // namespace
bool isCatalogAssetDrag(const QString& text)
{
  return text.startsWith(Prefix);
}
QString catalogAssetDragData(const BrowserAsset& asset, const std::string& projectId)
{
  if (asset.source == BrowserSource::GameFiles || projectId.empty())
    return {};
  auto type = QString{};
  switch (asset.type)
  {
  case BrowserCellType::Model:
    type = "model";
    break;
  case BrowserCellType::Sprite:
    type = "sprite";
    break;
  case BrowserCellType::Sound:
    type = "sound";
    break;
  case BrowserCellType::Texture:
    type = "texture";
    break;
  default:
    return {};
  }
  // A skybox group is browsed as a texture cell but drops as a whole skybox: the payload
  // must not name one face as if it were a standalone texture.
  if (asset.skybox)
    type = "skybox";
  const auto library = isLibraryAsset(asset);
  const auto id = library ? asset.libraryAssetId : asset.assetId;
  const auto revision = library ? asset.libraryRevision : asset.revision;
  if (id.empty() || revision == 0 || (library && asset.libraryId.empty()))
    return {};
  return QString::fromLatin1(Prefix)
         + QString::fromUtf8(
           QJsonDocument{QJsonObject{
                           {"version", 1},
                           {"projectId", QString::fromStdString(projectId)},
                           {"source", library ? "library" : "project"},
                           {"type", type},
                           {"assetId", QString::fromStdString(id)},
                           {"revision", QString::number(qulonglong(revision))},
                           {"libraryId", QString::fromStdString(asset.libraryId)}}}
             .toJson(QJsonDocument::Compact));
}
Result<std::unique_ptr<PreparedAssetDrop>> PreparedAssetDrop::prepare(
  std::shared_ptr<ProjectSession> session, const QString& text)
{
  if (!session || !isCatalogAssetDrag(text))
    return Error{"Catalog resources require a project map."};
  auto parseError = QJsonParseError{};
  const auto document = QJsonDocument::fromJson(
    text.mid(QString::fromLatin1(Prefix).size()).toUtf8(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject())
    return Error{"Invalid catalog drag payload."};
  const auto object = document.object();
  auto validRevision = false;
  const auto revision = object.value("revision").toString().toULongLong(&validRevision);
  const auto id = object.value("assetId").toString().toStdString();
  if (
    object.value("version").toInt() != 1 || !validRevision || revision == 0 || id.empty())
    return Error{"Invalid catalog resource identity or revision."};
  if (object.value("projectId").toString().toStdString() != session->store().info().id)
    return Error{"The resource belongs to another project."};
  auto result = std::make_unique<PreparedAssetDrop>();
  result->m_session = std::move(session);
  result->m_original = text;
  auto type = std::string{};
  auto path = std::string{};
  const auto source = object.value("source").toString();
  if (source == "project")
  {
    const auto asset = result->m_session->store().findAsset(id);
    if (asset.is_error())
      return std::get<Error>(asset.error());
    if (!asset.value() || asset.value()->revision != revision)
      return Error{"The project resource revision changed. Drag it again."};
    const auto& projectAsset = *asset.value();
    result->m_expected.push_back(projectAsset);
    type = kind(projectAsset.type);
    if (
      projectAsset.type == project::AssetType::Texture
      || projectAsset.type == project::AssetType::Skybox)
    {
      const auto name = gameName(*result->m_session, projectAsset);
      if (name.is_error())
        return std::get<Error>(name.error());
      path = name.value();
    }
    else
      path = projectAsset.publishPath;
  }
  else if (source == "library")
  {
    const auto libraryId = object.value("libraryId").toString().toStdString();
    const auto state = result->m_session->libraryState(libraryId, false);
    if (state.is_error())
      return std::get<Error>(state.error());
    if (!state.value().available)
      return Error{"The asset library is unavailable."};
    const auto* asset = state.value().find(id);
    if (!asset || asset->missing || asset->revision != revision)
      return Error{"The library resource revision changed or is missing. Drag it again."};
    type = kind(asset->type);
    if (type.empty() || type != object.value("type").toString().toStdString())
      return Error{"Unsupported catalog drop type."};
    auto indices = std::vector<std::size_t>{};
    auto plan = result->m_session->buildCapturePlan(libraryId, {id}, indices);
    if (plan.is_error())
      return std::get<Error>(plan.error());
    result->m_plan = std::move(plan).value();
    {
      auto conflicts = result->m_session->captureConflicts(result->m_plan);
      if (conflicts.is_error())
        return std::get<Error>(conflicts.error());
      result->m_conflicts = std::move(conflicts).value();
    }
    const auto& root = result->m_plan.at(indices.front());
    if (root.import.library.libraryRevision != revision)
      return Error{"The library revision changed while preparing the drop."};
    path = root.import.virtualPath;
    for (const auto& capture : result->m_plan)
      if (capture.assetId)
      {
        const auto existing = result->m_session->store().findAsset(*capture.assetId);
        if (existing.is_error())
          return std::get<Error>(existing.error());
        if (!existing.value())
          return Error{"A project resource disappeared."};
        result->m_expected.push_back(*existing.value());
        if (capture.assetId == root.assetId)
          path = existing.value()->publishPath;
      }
    // Dependencies already captured by the project are linked by ID. Guard those
    // revisions as well, so a nested UI action cannot change the prepared closure.
    for (const auto& capture : result->m_plan)
      for (const auto& dependency : capture.dependencies)
      {
        if (
          dependency.assetId.empty()
          || std::ranges::any_of(result->m_expected, [&](const auto& expected) {
               return expected.id == dependency.assetId;
             }))
          continue;
        const auto existing = result->m_session->store().findAsset(dependency.assetId);
        if (existing.is_error())
          return std::get<Error>(existing.error());
        if (!existing.value())
          return Error{"A project dependency disappeared."};
        result->m_expected.push_back(*existing.value());
      }
    if (type == "texture" || type == "skybox")
    {
      // A texture capture publishes a single miptex and a skybox publishes its group
      // name, so the name is fixed before the project is written; that keeps a drop
      // preview honest.
      const auto name = capturedGameName(root.import);
      if (name.is_error())
        return std::get<Error>(name.error());
      path = name.value();
    }
  }
  else
    return Error{"Invalid catalog source."};
  if (
    type.empty() || type != object.value("type").toString().toStdString() || path.empty())
    return Error{"Unsupported catalog drop type."};
  result->m_nativePayload = type + ":" + path;
  return result;
}
const std::string& PreparedAssetDrop::nativePayload() const
{
  return m_nativePayload;
}
bool PreparedAssetDrop::matches(const QString& text) const
{
  return m_original == text;
}
const std::vector<CaptureConflict>& PreparedAssetDrop::conflicts() const
{
  return m_conflicts;
}
void PreparedAssetDrop::resolveConflicts(const CaptureConflictChoice choice)
{
  for (const auto& conflict : m_conflicts)
    m_session->resolveCaptureConflict(m_plan, conflict, choice);
  m_conflicts.clear();
}
Result<void> PreparedAssetDrop::commit()
{
  if (m_committed)
    return Error{"The resource drop was already committed."};
  if (!m_conflicts.empty())
    return Error{
      "The resource would replace a project resource with the same name. Confirm the "
      "replacement before dropping it."};
  for (const auto& expected : m_expected)
  {
    const auto current = m_session->store().findAsset(expected.id);
    if (current.is_error())
      return std::get<Error>(current.error());
    if (!current.value() || *current.value() != expected)
      return Error{"The project resource changed during the drag. Drag it again."};
  }
  if (!m_plan.empty())
  {
    const auto applied = m_session->applyAssetCapturePlan(m_plan);
    if (applied.is_error())
      return std::get<Error>(applied.error());
    m_plan.clear();
  }
  m_committed = true;
  return {};
}
} // namespace tb::ui
