/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/AssetLibraryWatcher.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>

#include "ui/QPathUtils.h"

#include <filesystem>
#include <map>
#include <utility>

namespace tb::ui
{
class AssetLibraryWatcher::Impl
{
public:
  AssetLibraryWatcher& owner;
  QFileSystemWatcher watcher;
  QTimer debounce;
  QTimer fallback;
  QStringList folders;
  using Stamp = std::pair<std::uintmax_t, std::filesystem::file_time_type>;
  using Snapshot = std::map<QString, Stamp>;
  Snapshot previous;

  explicit Impl(AssetLibraryWatcher& i_owner)
    : owner{i_owner}
  {
    debounce.setSingleShot(true);
    debounce.setInterval(250);
    fallback.setInterval(5000);
    QObject::connect(
      &watcher, &QFileSystemWatcher::fileChanged, &owner, [this]() { debounce.start(); });
    QObject::connect(&watcher, &QFileSystemWatcher::directoryChanged, &owner, [this]() {
      debounce.start();
    });
    QObject::connect(&debounce, &QTimer::timeout, &owner, [this]() { check(); });
    QObject::connect(&fallback, &QTimer::timeout, &owner, [this]() { check(); });
  }

  Snapshot snapshot(QStringList& paths) const
  {
    auto result = Snapshot{};
    for (const auto& folder : folders)
    {
      auto parent = QFileInfo{folder}.dir();
      while (!parent.exists() && parent.cdUp())
      {
      }
      if (parent.exists())
        paths.push_back(parent.absolutePath());
      // An existence marker also detects losing an empty library folder.
      result[folder] = {QDir{folder}.exists() ? 1u : 0u, {}};
      if (!QDir{folder}.exists())
        continue;
      paths.push_back(folder);
      auto entries = QDirIterator{
        folder,
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::NoSymLinks,
        QDirIterator::Subdirectories};
      while (entries.hasNext())
      {
        const auto path = entries.next();
        const auto info = entries.fileInfo();
        if (info.isDir())
        {
          paths.push_back(path);
          // Directory timestamps include SQLite journal activity, which is not a
          // resource change. Only their existence contributes to this snapshot.
          result[path] = {1u, {}};
          continue;
        }
        // Watch the catalog itself, but ignore transient SQLite sidecars.
        if (info.fileName().startsWith(".tbassets-"))
          continue;
        auto error = std::error_code{};
        const auto nativePath = pathFromQString(path);
        const auto size = std::filesystem::file_size(nativePath, error);
        if (error)
          continue;
        const auto time = std::filesystem::last_write_time(nativePath, error);
        if (error)
          continue;
        result[path] = {size, time};
        paths.push_back(path);
      }
    }
    paths.removeDuplicates();
    return result;
  }

  void rearm(const QStringList& desired)
  {
    const auto current = watcher.files() + watcher.directories();
    auto remove = QStringList{};
    auto add = QStringList{};
    for (const auto& path : current)
      if (!desired.contains(path))
        remove.push_back(path);
    for (const auto& path : desired)
      if (!current.contains(path))
        add.push_back(path);
    if (!remove.isEmpty())
      watcher.removePaths(remove);
    // Qt stops watching a renamed/replaced file. Reconcile after every event, including
    // events whose metadata did not change. Poll only if the OS refuses a watch.
    const auto failed = add.isEmpty() ? QStringList{} : watcher.addPaths(add);
    if (!failed.isEmpty())
      fallback.start();
    else
      fallback.stop();
  }

  void check()
  {
    auto paths = QStringList{};
    auto current = snapshot(paths);
    const auto changed = current != previous;
    previous = std::move(current);
    rearm(paths);
    if (changed)
      emit owner.foldersChanged();
  }

  void acknowledge()
  {
    debounce.stop();
    auto paths = QStringList{};
    previous = snapshot(paths);
    rearm(paths);
  }
};

AssetLibraryWatcher::AssetLibraryWatcher(QObject* parent)
  : QObject{parent}
  , m_impl{std::make_unique<Impl>(*this)}
{
}
AssetLibraryWatcher::~AssetLibraryWatcher() = default;
void AssetLibraryWatcher::setFolders(QStringList folders)
{
  for (auto& folder : folders)
    folder = QDir::cleanPath(QFileInfo{folder}.absoluteFilePath());
  folders.removeDuplicates();
  folders.sort();
  if (folders == m_impl->folders)
    return;
  m_impl->folders = std::move(folders);
  m_impl->acknowledge();
}
void AssetLibraryWatcher::acknowledgeChanges()
{
  m_impl->acknowledge();
}
} // namespace tb::ui
