/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QThread>

#include "ui/AssetLibraryWatcher.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{
namespace
{
template <typename Predicate>
bool waitFor(Predicate predicate, const int timeout = 3000)
{
  auto timer = QElapsedTimer{};
  timer.start();
  while (!predicate() && timer.elapsed() < timeout)
  {
    QCoreApplication::processEvents();
    QThread::msleep(10);
  }
  return predicate();
}
void write(const QString& path, const QByteArray& bytes)
{
  auto output = QSaveFile{path};
  REQUIRE(output.open(QIODevice::WriteOnly));
  REQUIRE(output.write(bytes) == bytes.size());
  REQUIRE(output.commit());
}
} // namespace
TEST_CASE("AssetLibraryWatcher")
{
  auto temporary = QTemporaryDir{};
  REQUIRE(temporary.isValid());
  const auto root = temporary.filePath("library");
  REQUIRE(QDir{}.mkdir(root));
  const auto source = root + "/model.mdl";
  write(source, "original");
  auto watcher = AssetLibraryWatcher{};
  auto changes = 0;
  QObject::connect(&watcher, &AssetLibraryWatcher::foldersChanged, [&]() { ++changes; });
  watcher.setFolders({root});

  SECTION("replacement files remain watched after the first event")
  {
    write(source, "replacement");
    REQUIRE(waitFor([&]() { return changes > 0; }));
    const auto previous = changes;
    write(source, "another replacement");
    REQUIRE(waitFor([&]() { return changes > previous; }));
  }
  SECTION("new subdirectories and files are discovered")
  {
    const auto nested = root + "/nested";
    REQUIRE(QDir{}.mkdir(nested));
    write(nested + "/sound.wav", "first");
    REQUIRE(waitFor([&]() { return changes > 0; }));
    const auto previous = changes;
    write(nested + "/sound.wav", "second longer");
    REQUIRE(waitFor([&]() { return changes > previous; }));
  }
  SECTION("removed folders can be recreated and watched again")
  {
    REQUIRE(QFile::remove(source));
    REQUIRE(QDir{}.rmdir(root));
    REQUIRE(waitFor([&]() { return changes > 0; }));
    const auto removed = changes;
    REQUIRE(QDir{}.mkdir(root));
    write(source, "restored");
    REQUIRE(waitFor([&]() { return changes > removed; }));
    const auto restored = changes;
    write(source, "restored and edited");
    REQUIRE(waitFor([&]() { return changes > restored; }));
  }
  SECTION("SQLite sidecars do not prompt but a catalog change does")
  {
    write(root + "/.tbassets-wal", "journal");
    CHECK_FALSE(waitFor([&]() { return changes > 0; }, 700));
    write(root + "/.tbassets", "catalog");
    REQUIRE(waitFor([&]() { return changes > 0; }));
  }
  SECTION("acknowledging an explicit rescan absorbs its own disk changes")
  {
    write(root + "/.tbassets", "rescan result");
    watcher.acknowledgeChanges();
    CHECK_FALSE(waitFor([&]() { return changes > 0; }, 700));
    write(source, "new source edit");
    REQUIRE(waitFor([&]() { return changes > 0; }));
  }
  SECTION("changing the watched roots stops observing the old library")
  {
    watcher.setFolders({});
    write(source, "unwatched edit");
    CHECK_FALSE(waitFor([&]() { return changes > 0; }, 700));
  }
}
} // namespace tb::ui
