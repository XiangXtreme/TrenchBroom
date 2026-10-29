/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include <QFile>
#include <QTemporaryDir>

#include "base/Logger.h"
#include "mdl/GameConfig.h"
#include "mdl/GameInfo.h"
#include "ui/CatalogModelPreview.h"
#include "ui/QPathUtils.h"

#include <catch2/catch_test_macros.hpp>
namespace tb::ui
{
TEST_CASE("CatalogModelPreview")
{
  auto directory = QTemporaryDir{};
  REQUIRE(directory.isValid());
  const auto file = directory.filePath("triangle.obj");
  auto output = QFile{file};
  REQUIRE(output.open(QIODevice::WriteOnly));
  REQUIRE(output.write("v 0 0 0\nv 8 0 0\nv 0 8 0\nf 1 2 3\n") > 0);
  output.close();
  auto config = mdl::GameConfig{};
  config.name = "Preview";
  const auto game = mdl::makeGameInfo(std::move(config));
  auto logger = NullLogger{};
  const auto create = [](auto loader) {
    return gl::createResourceSync(std::move(loader));
  };
  auto preview = CatalogModelPreview{pathFromQString(file), game, create, logger};
  CHECK(preview.specification().path == "triangle.obj");
  const auto* frame = preview.models().frame(preview.specification());
  REQUIRE(frame != nullptr);
  const auto size = frame->bounds().size();
  CHECK(vm::dot(size, size) == 128.0f);
  auto otherDirectory = QTemporaryDir{};
  REQUIRE(otherDirectory.isValid());
  const auto otherFile = otherDirectory.filePath("triangle.obj");
  auto otherOutput = QFile{otherFile};
  REQUIRE(otherOutput.open(QIODevice::WriteOnly));
  REQUIRE(otherOutput.write("v 0 0 0\nv 16 0 0\nv 0 16 0\nf 1 2 3\n") > 0);
  otherOutput.close();
  auto other = CatalogModelPreview{pathFromQString(otherFile), game, create, logger};
  const auto* otherFrame = other.models().frame(other.specification());
  REQUIRE(otherFrame != nullptr);
  const auto otherSize = otherFrame->bounds().size();
  CHECK(vm::dot(otherSize, otherSize) == 512.0f);
  CHECK(vm::dot(frame->bounds().size(), frame->bounds().size()) == 128.0f);
  auto missing = CatalogModelPreview{
    pathFromQString(directory.filePath("missing.obj")), game, create, logger};
  CHECK(missing.models().frame(missing.specification()) == nullptr);
}
} // namespace tb::ui
