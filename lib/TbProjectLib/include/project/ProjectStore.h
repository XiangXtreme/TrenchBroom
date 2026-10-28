/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#pragma once

#include "project/ProjectAsset.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tb::project
{

/**
 * Single-file project catalog. Each mutation commits atomically; callers must serialize
 * access to a store. Content is immutable and loaded separately from catalog metadata.
 * Maps participate in the catalog and dependency graph as AssetType::Map documents.
 * ProjectSession owns the editor working copy, dirty state and explicit disk saves.
 */
class ProjectStore
{
private:
  class Impl;
  std::unique_ptr<Impl> m_impl;

  explicit ProjectStore(std::unique_ptr<Impl> impl);

public:
  ~ProjectStore();
  ProjectStore(ProjectStore&&) noexcept;
  ProjectStore& operator=(ProjectStore&&) noexcept;
  ProjectStore(const ProjectStore&) = delete;
  ProjectStore& operator=(const ProjectStore&) = delete;

  static Result<std::unique_ptr<ProjectStore>> create(
    const std::filesystem::path& path,
    const std::string& projectName,
    const std::string& gameId);
  static Result<std::unique_ptr<ProjectStore>> open(const std::filesystem::path& path);

  const std::filesystem::path& path() const;
  const ProjectInfo& info() const;

  Result<Asset> importAsset(const AssetImport& import);
  // Reimport replaces content and recipe, preserving ID, type, name and folder.
  Result<Asset> replaceAsset(const std::string& id, const AssetImport& import);
  // Applies every update in one storage transaction. A single rejected update leaves all
  // assets, revisions and references exactly as they were, which is what makes a
  // multi-asset library update safe to cancel or retry.
  Result<std::vector<Asset>> updateAssets(const std::vector<AssetUpdate>& updates);
  Result<Asset> renameAsset(const std::string& id, const std::string& displayName);
  Result<Asset> moveAsset(const std::string& id, const std::string& virtualPath);
  Result<void> removeAsset(const std::string& id);
  // Atomically select existing immutable revisions, used to roll back temporary
  // document staging without discarding checkpoints that reference those revisions.
  Result<void> selectRevisions(const std::vector<Asset>& assets);
  Result<std::vector<Asset>> assets(
    std::optional<AssetType> type = std::nullopt,
    const std::string& virtualFolder = {}) const;
  Result<std::optional<Asset>> findAsset(const std::string& id) const;
  Result<std::optional<Asset>> findAssetByPath(const std::string& virtualPath) const;
  Result<std::vector<std::uint8_t>> readAsset(
    const std::string& id,
    ContentKind kind,
    std::optional<std::uint64_t> revision = std::nullopt) const;

  // Snapshot provenance. A project only ever knows the library revisions it captured.
  Result<std::vector<Asset>> libraryLinkedAssets() const;
  // Finds an already captured asset for a library asset, so selecting the same library
  // entry twice updates the snapshot instead of duplicating it.
  Result<std::optional<Asset>> findAssetByLibraryAsset(
    const std::string& libraryId, const std::string& libraryAssetId) const;
  // Local hints for locating a library by ID. Paths are never asset identities.
  Result<void> attachLibrary(
    const std::string& id, const std::string& name, const std::string& lastPath);
  Result<std::vector<LibraryInfo>> libraries() const;
  Result<void> ignoreLibraryUpdate(
    const std::string& assetId, const std::string& libraryHash);
  Result<std::vector<std::string>> ignoredLibraryUpdates(
    const std::string& assetId) const;

  Result<void> addDependency(
    const std::string& assetId, const std::string& dependencyId, const std::string& kind);
  Result<std::vector<AssetDependency>> dependencies(const std::string& assetId) const;
  Result<std::vector<AssetDependency>> dependents(const std::string& dependencyId) const;

  Result<Checkpoint> checkpoint(
    const std::string& name, const std::string& description = {});
  Result<std::vector<Checkpoint>> checkpoints() const;
  // Compare a saved checkpoint to current state (including dependency changes).
  Result<ProjectChanges> compare(const std::string& checkpointId) const;
  // Atomically preserve current state in a safety checkpoint, then restore the target.
  Result<Checkpoint> restore(const std::string& checkpointId);
  // Consistent standalone copy. Never overwrites an existing destination.
  Result<void> saveCopy(const std::filesystem::path& destination) const;
};

} // namespace tb::project
