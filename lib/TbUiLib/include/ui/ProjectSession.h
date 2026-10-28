/*
  Copyright (C) 2026 XiangXtreme
  This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QByteArray>
#include <QObject>
#include <QTemporaryDir>

#include "base/NotifierConnection.h"
#include "project/AssetLibrary.h"
#include "project/LibrarySync.h"
#include "project/ProjectStore.h"

#include <map>
#include <memory>

class QLockFile;

namespace tb::ui
{
class MapDocument;

/** Owns the editable working copy, map registrations and disposable asset files.
 * The user's project is replaced only after an entire save succeeds.
 *
 * Asset library support follows one rule: the project keeps its own snapshot. A library
 * is only read when the user captures a resource or explicitly asks whether a newer
 * library version exists, so a project always opens, previews and compiles without any
 * library being reachable.
 */
class ProjectSession : public QObject, public std::enable_shared_from_this<ProjectSession>
{
  Q_OBJECT
  struct Document
  {
    std::string assetId;
    NotifierConnection connection;
  };
  std::filesystem::path m_path;
  QTemporaryDir m_workspace;
  std::unique_ptr<QLockFile> m_lock;
  std::unique_ptr<project::ProjectStore> m_store;
  QByteArray m_savedDigest;
  std::map<MapDocument*, Document> m_documents;
  std::vector<project::Asset> m_stagedMaps;
  std::vector<project::Asset> m_runtimeAssets;
  std::unique_ptr<QTemporaryDir> m_runtime;
  bool m_modified = false;
  bool m_libraryUpdatesDeferred = false;

  explicit ProjectSession(std::filesystem::path path);
  Result<void> initialize(bool create, const std::string& name, const std::string& game);
  Result<void> stageMaps();
  Result<void> unstageMaps();
  Result<void> refreshRuntime();
  Result<void> writeProject(const std::filesystem::path& path, bool overwrite);
  void bindDocument(MapDocument& document);
  void changed();

  Result<std::unique_ptr<project::AssetLibrary>> openLibrary(
    const std::string& libraryId) const;
  /** Builds the snapshot import for a library asset without writing anything. */
  Result<project::AssetImport> captureImport(
    const project::AssetLibrary& library,
    const project::LibraryAsset& asset,
    const std::optional<project::Asset>& existing) const;

public:
  /** Attached library plus whether its folder could be opened right now. */
  struct LibraryConnection
  {
    project::LibraryInfo info;
    bool available = false;
    std::string error;
  };

  ~ProjectSession() override;
  static Result<std::shared_ptr<ProjectSession>> create(
    const std::filesystem::path& path, const std::string& name, const std::string& game);
  static Result<std::shared_ptr<ProjectSession>> open(const std::filesystem::path& path);

  const std::filesystem::path& path() const;
  const project::ProjectStore& store() const;
  bool modified() const;
  bool hasOpenMaps() const;
  MapDocument* documentFor(const std::string& assetId) const;
  std::optional<std::string> assetFor(const MapDocument& document) const;

  Result<project::Asset> importAsset(const project::AssetImport& asset);
  Result<project::Asset> replaceAsset(
    const std::string& id, const project::AssetImport& asset);
  Result<project::Asset> renameAsset(const std::string& id, const std::string& name);
  Result<project::Asset> moveAsset(const std::string& id, const std::string& path);
  Result<void> removeAsset(const std::string& id);
  Result<project::Checkpoint> checkpoint(const std::string& name);
  Result<project::ProjectChanges> compare(const std::string& id);
  Result<void> restore(const std::string& id);

  // Asset library integration.
  Result<void> attachLibrary(const std::filesystem::path& folder);
  Result<std::vector<LibraryConnection>> libraries() const;
  /** Reads one library's current catalog; `rescan` also picks up external file changes.
   */
  Result<project::LibraryState> libraryState(
    const std::string& libraryId, bool rescan) const;
  /** Copies the current library revision into the project immediately. */
  Result<project::Asset> captureLibraryAsset(
    const std::string& libraryId, const std::string& libraryAssetId);
  /** Project snapshot already captured from a library resource, if any. */
  Result<std::optional<project::Asset>> projectAssetForLibraryAsset(
    const std::string& libraryId, const std::string& libraryAssetId) const;
  /**
   * Materializes the current library revision in the session cache for browsing and
   * preview. Nothing is added to the project and the cache is disposable.
   */
  Result<std::filesystem::path> libraryAssetFile(
    const std::string& libraryId, const std::string& libraryAssetId);
  /** Compares captured snapshots with the libraries; never modifies the project. */
  Result<std::vector<project::LibraryUpdate>> checkLibraryUpdates(bool rescan = false);
  /** Applies selected updates as one project transaction, or none at all. */
  Result<std::vector<project::Asset>> updateLibraryAssets(
    const std::vector<std::string>& projectAssetIds);
  Result<void> ignoreLibraryUpdate(
    const std::string& projectAssetId, const std::string& libraryHash);
  /** Suppresses the update prompt until the next explicit check. */
  void deferLibraryUpdates();
  bool libraryUpdatesDeferred() const;
  /** Returns one asset to an earlier snapshot revision, keeping the newer ones. */
  Result<project::Asset> restoreAssetRevision(
    const std::string& projectAssetId, std::uint64_t revision);

  Result<std::filesystem::path> prepareMap(const std::string& id);
  // Reads live edits without selecting revisions, saving the project or clearing dirty
  // state.
  Result<std::vector<std::uint8_t>> workingMapContents(const std::string& id) const;
  Result<std::filesystem::path> assetFile(const project::Asset& asset) const;
  Result<void> registerDocument(MapDocument& document, const std::string& id);
  void unregisterDocument(MapDocument& document);
  Result<void> save();
  Result<void> saveCopy(const std::filesystem::path& path);

signals:
  void runtimeError(const QString& message);
  void contentsChanged();
  void stateChanged();
  /** A capture changed which library revisions the project depends on. */
  void libraryLinksChanged();
};
} // namespace tb::ui
