/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QByteArray>
#include <QObject>
#include <QTemporaryDir>

#include "base/NotifierConnection.h"
#include "project/ProjectStore.h"

#include <map>
#include <memory>

class QLockFile;

namespace tb::ui
{
class MapDocument;

/** Owns the editable working copy, map registrations and disposable asset files.
 * The user's project is replaced only after an entire save succeeds.
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

  explicit ProjectSession(std::filesystem::path path);
  Result<void> initialize(bool create, const std::string& name, const std::string& game);
  Result<void> stageMaps();
  Result<void> unstageMaps();
  Result<void> refreshRuntime();
  Result<void> writeProject(const std::filesystem::path& path, bool overwrite);
  void bindDocument(MapDocument& document);
  void changed();

public:
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

  Result<std::filesystem::path> prepareMap(const std::string& id);
  Result<std::filesystem::path> assetFile(const project::Asset& asset) const;
  Result<void> registerDocument(MapDocument& document, const std::string& id);
  void unregisterDocument(MapDocument& document);
  Result<void> save();
  Result<void> saveCopy(const std::filesystem::path& path);

signals:
  void runtimeError(const QString& message);
  void contentsChanged();
  void stateChanged();
};
} // namespace tb::ui
