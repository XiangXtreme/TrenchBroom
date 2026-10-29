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

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class QLockFile;

namespace tb::ui
{
class MapDocument;

/**
 * A new capture whose project path is already occupied by another asset.
 *
 * Two project resources may not publish the same name: maps and the compiler resolve
 * names, not library identities. The caller decides whether the existing resource is
 * replaced or the capture is cancelled.
 */
struct CaptureConflict
{
  std::size_t planIndex = 0;
  std::string displayName;
  std::string path;
  std::optional<project::Asset> existing;
};

enum class CaptureConflictChoice
{
  Replace,
  Cancel,
};

using CaptureConflictResolver =
  std::function<CaptureConflictChoice(const CaptureConflict&)>;

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
  std::map<std::string, std::shared_ptr<MapDocument>> m_retainedDocuments;
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

  /** Outcome of attaching a folder, including what had to be created to do it. */
  struct LibraryAttachSummary
  {
    std::string libraryId;
    bool created = false;
    std::size_t assets = 0;
    std::size_t groups = 0;
    std::size_t invalid = 0;
    std::vector<std::string> warnings;
  };

  ~ProjectSession() override;
  static Result<std::shared_ptr<ProjectSession>> create(
    const std::filesystem::path& path, const std::string& name, const std::string& game);
  static Result<std::shared_ptr<ProjectSession>> open(const std::filesystem::path& path);

  const std::filesystem::path& path() const;
  const project::ProjectStore& store() const;
  bool modified() const;
  bool hasOpenMaps() const;
  std::vector<std::shared_ptr<MapDocument>> documents() const;
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
  /**
   * Restores a version into the project.
   *
   * Live edits of open maps are preserved by the safety version the store writes first,
   * then every open map is reloaded from the restored snapshot so the editor shows what
   * was restored. A version that does not contain an open map is rejected: the map would
   * have no content to show and no dirty state would make that visible. Preparation
   * failures preserve the live catalog, cache files, documents and undo histories.
   */
  Result<void> restore(const std::string& id);

  // Asset library integration.
  /**
   * Attaches a library folder to the project.
   *
   * A folder that is not a library yet is turned into one when `createIfMissing` is set;
   * otherwise the folder is rejected so a wrong selection cannot leave a catalog behind.
   */
  Result<LibraryAttachSummary> attachLibrary(
    const std::filesystem::path& folder, bool createIfMissing = false);
  /**
   * Points an attached library at its new folder after the folder was moved.
   *
   * The selected folder must contain the same library identity; a different library is
   * rejected so a wrong selection cannot silently replace the project's provenance.
   * Captured snapshots are unaffected because they no longer depend on the library.
   */
  Result<void> relocateLibrary(
    const std::string& libraryId, const std::filesystem::path& folder);
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
  /**
   * Writes a fixed compile input for one project map.
   *
   * The folder contains the map source, a WAD with exactly the textures that map
   * references, a WAD list for the compiler and the non-texture resources the map uses.
   * It is a snapshot: the project and its libraries may change afterwards without
   * affecting a compile started from it, and nothing in it points at a library.
   *
   * `mapFileName` names the exported map inside the folder. The default keeps the
   * documented `map.map` contract for a hand exported snapshot; a compilation names it
   * after the map so profiles that derive outputs from `${MAP_BASE_NAME}` keep working.
   */
  Result<void> exportCompileSnapshot(
    const std::string& mapAssetId,
    const std::filesystem::path& folder,
    const std::string& mapFileName = "map.map");

  // Reads live edits without selecting revisions, saving the project or clearing dirty
  // state.
  Result<std::vector<std::uint8_t>> workingMapContents(const std::string& id) const;
  Result<std::filesystem::path> assetFile(const project::Asset& asset) const;
  /**
   * Builds the capture plan for the given library resources.
   *
   * Every requested resource is refreshed; resources it depends on are added only when
   * they are missing from the project, and are otherwise linked by their existing ID, so
   * a closure can never silently rewrite an unrelated snapshot. `planIndices` receives
   * the plan position of each request, in request order.
   */
  Result<std::vector<project::AssetCapture>> buildCapturePlan(
    const std::string& libraryId,
    const std::vector<std::string>& libraryAssetIds,
    std::vector<std::size_t>& planIndices);
  /** New plan entries whose path overlaps an existing project asset. */
  Result<std::vector<CaptureConflict>> captureConflicts(
    const std::vector<project::AssetCapture>& plan) const;
  /**
   * Applies a conflict decision to the plan.
   *
   * Replace retargets the entry at the existing asset and keeps its project path, so map
   * references stay valid and the previous revision remains in the project history.
   * Cancel changes nothing; the caller aborts the capture.
   */
  void resolveCaptureConflict(
    std::vector<project::AssetCapture>& plan,
    const CaptureConflict& conflict,
    CaptureConflictChoice choice);
  /**
   * Captures one library resource, asking `resolveConflict` before replacing a project
   * resource that publishes the same name. Returns nullopt when the capture is cancelled.
   */
  Result<std::optional<project::Asset>> captureLibraryAssetResolving(
    const std::string& libraryId,
    const std::string& libraryAssetId,
    const CaptureConflictResolver& resolveConflict);
  Result<void> registerDocument(MapDocument& document, const std::string& id);
  /**
   * Keeps a document alive in the session when no view shows it.
   *
   * Closing a map view destroys the view, not the editing session: the project keeps the
   * unsaved work, saving still writes it, and reopening the map restores the same
   * document instance instead of reloading it from disk.
   */
  Result<void> retainDocument(
    std::shared_ptr<MapDocument> document, const std::string& id);
  std::shared_ptr<MapDocument> retainedDocument(const std::string& id) const;
  /** Drops a retained document and ends its editing session. */
  Result<void> releaseDocument(const std::string& id);
  void unregisterDocument(MapDocument& document);
  Result<std::vector<project::Asset>> applyAssetCapturePlan(
    const std::vector<project::AssetCapture>& plan);
  Result<void> save();
  Result<void> saveCopy(const std::filesystem::path& path);
  /**
   * Storage maintenance: revisions that neither the current assets nor a version use.
   *
   * Removing them frees the content only those revisions referenced. Only explicit
   * cleanup does this; scanning a project or a library never deletes stored revisions.
   */
  Result<project::RevisionCleanup> unreferencedRevisions() const;
  Result<project::RevisionCleanup> pruneUnreferencedRevisions();

signals:
  void runtimeError(const QString& message);
  void contentsChanged();
  void stateChanged();
  /** A capture changed which library revisions the project depends on. */
  void libraryLinksChanged();
};
} // namespace tb::ui
