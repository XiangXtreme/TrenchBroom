/*
 Copyright (C) 2026 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "TestEnvironment.h"
#include "TestParserStatus.h"
#include "base/Logger.h"
#include "fs/DiskFileSystem.h"
#include "fs/TestUtils.h"
#include "gl/CreateResource.h"
#include "gl/Material.h"
#include "gl/Texture.h"
#include "mdl/Entity.h"
#include "mdl/EntityDefinition.h"
#include "mdl/EntityModel.h"
#include "mdl/EntityModelManager.h"
#include "mdl/EnvironmentConfig.h"
#include "mdl/FgdParser.h"
#include "mdl/GameConfig.h"
#include "mdl/GameFileSystem.h"
#include "mdl/GameInfo.h"
#include "mdl/ModelSpecification.h"
#include "mdl/ParseGameConfig.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

namespace tb::mdl
{
namespace
{

GameInfo makeTestGameInfo()
{
  return makeGameInfo(GameConfig{
    .name = "Test",
    .path = {},
    .icon = {},
    .fileFormats = {},
    .fileSystemConfig = {},
    .materialConfig = MaterialConfig{"", {}, "palette.lmp", {}, "", {}},
    .entityConfig = {},
    .faceAttribsConfig = {},
    .smartTags = {},
    .softMapBounds = {},
    .compilationTools = {},
  });
}

} // namespace

TEST_CASE("EntityModelManager")
{
  auto logger = NullLogger{};
  auto fileSystem = fs::DiskFileSystem{getFixtureRoot() / "test/mdl/LoadMdlModel"};
  const auto gameInfo = makeTestGameInfo();

  auto manager = EntityModelManager{
    gameInfo,
    fileSystem,
    [](auto resourceLoader) { return gl::createResourceSync(std::move(resourceLoader)); },
    logger};

  SECTION("model")
  {
    SECTION("returns nullptr for an empty path")
    {
      CHECK(manager.model({}) == nullptr);
    }

    SECTION("loads a valid model")
    {
      const auto* model = manager.model("armor.mdl");
      REQUIRE(model != nullptr);
      CHECK(model->data() != nullptr);
    }

    SECTION("returns the same model instance on subsequent calls")
    {
      const auto* model1 = manager.model("armor.mdl");
      const auto* model2 = manager.model("armor.mdl");
      CHECK(model1 == model2);
    }

    SECTION("loads a model whose data is null if the underlying file does not exist")
    {
      // model() always returns a non-null EntityModel; the load failure only surfaces
      // once the (synchronously loaded) data resource is inspected.
      const auto* model = manager.model("does_not_exist.mdl");
      REQUIRE(model != nullptr);
      CHECK(model->data() == nullptr);
    }
  }

  SECTION("GoldSrc sprites do not require an external palette")
  {
    auto spriteFs = fs::DiskFileSystem{getFixtureRoot() / "test/mdl/LoadSpriteModel"};
    auto info = makeTestGameInfo();
    info.gameConfig.materialConfig.palette = "does_not_exist.lmp";
    auto spriteManager = EntityModelManager{
      info,
      spriteFs,
      [](auto loader) { return gl::createResourceSync(std::move(loader)); },
      logger};

    const auto spriteName = GENERATE(
      "RenderModeNormal.spr",
      "RenderModeAlphaTest.spr",
      "RenderModeAdditive.spr",
      "RenderModeIndexAlpha.spr");
    const auto* sprite = spriteManager.model(spriteName);
    REQUIRE(sprite);
    REQUIRE(sprite->data());
    CHECK(sprite->data()->frameCount() == 1u);
    CHECK(spriteManager.renderer({spriteName, 0, 0}));
    CHECK(spriteManager.model("ViewPlaneParallel.spr")->data() == nullptr);
  }

  SECTION("Bundled GoldSrc entity icons load without game assets")
  {
    const auto definitionFile = GENERATE("CounterStrike16.fgd", "HalfLife.fgd");
    const auto className = GENERATE("multi_manager", "env_shake", "trigger_changetarget");
    CAPTURE(definitionFile, className);
    auto resources = fs::DiskFileSystem{getFixtureRoot() / "games/Halflife"};
    const auto configPath = getFixtureRoot() / "games/Halflife/GameConfig.cfg";
    const auto config =
      parseGameConfig(
        fs::readTextFile(resources, "GameConfig.cfg") | kdl::value(), configPath)
      | kdl::value();
    auto gameFs = GameFileSystem{};
    gameFs.initialize({}, config, getFixtureRoot(), {}, logger);

    const auto fgd = fs::readTextFile(resources, definitionFile) | kdl::value();
    auto parser = FgdParser{fgd, RgbaF{1, 1, 1, 1}};
    auto status = TestParserStatus{};
    const auto definitions = parser.parseDefinitions(status) | kdl::value();
    const auto definition =
      std::ranges::find(definitions, className, &EntityDefinition::name);
    REQUIRE(definition != definitions.end());
    auto entity = Entity{};
    entity.setDefinition(&*definition);
    const auto spec = entity.modelSpecification() | kdl::value();
    CHECK(
      spec.path == std::filesystem::path{"editor"} / (std::string{className} + ".png"));

    const auto info = makeGameInfo(config);
    auto iconManager = EntityModelManager{
      info,
      gameFs,
      [](auto loader) { return gl::createResourceSync(std::move(loader)); },
      logger};
    const auto* icon = iconManager.model(spec.path);
    REQUIRE(icon);
    REQUIRE(icon->data());
    CHECK(icon->data()->orientation() == Orientation::ViewPlaneParallel);
    CHECK(icon->data()->frameCount() == 1u);
    const auto* texture = icon->data()->surfaces().front().skin(0)->texture();
    REQUIRE(texture);
    CHECK(texture->width() == 32u);
    CHECK(texture->height() == 32u);
    CHECK(texture->alphaDomain() == img::ImageAlphaDomain::Graduated);
    CHECK(iconManager.renderer(spec));
  }

  SECTION("frame")
  {
    SECTION("returns nullptr for a model that does not exist")
    {
      CHECK(manager.frame({"does_not_exist.mdl", 0, 0}) == nullptr);
    }

    SECTION("returns the frame for a valid model")
    {
      CHECK(manager.frame({"armor.mdl", 0, 0}) != nullptr);
    }

    SECTION("returns nullptr for an invalid frame index")
    {
      CHECK(manager.frame({"armor.mdl", 0, 99}) == nullptr);
    }
  }

  SECTION("renderer")
  {
    const auto spec = ModelSpecification{"armor.mdl", 0, 0};

    SECTION("returns nullptr for a model that does not exist")
    {
      CHECK(manager.renderer(ModelSpecification{"does_not_exist.mdl", 0, 0}) == nullptr);
    }

    SECTION("builds a renderer for a valid model")
    {
      CHECK(manager.renderer(spec) != nullptr);
    }

    SECTION("returns the same renderer instance on subsequent calls")
    {
      auto* renderer1 = manager.renderer(spec);
      auto* renderer2 = manager.renderer(spec);
      CHECK(renderer1 == renderer2);
    }

    SECTION("falls back to skin 0 for an out-of-range skin index")
    {
      // armor.mdl has 3 skins; an out-of-range index is not an error, it just falls
      // back to skin 0 (see EntityModelData::buildRenderer).
      CHECK(manager.renderer(ModelSpecification{"armor.mdl", 99, 0}) != nullptr);
    }

    SECTION("returns nullptr for an invalid frame index and caches the failure")
    {
      // armor.mdl has only 1 frame.
      const auto badSpec = ModelSpecification{"armor.mdl", 0, 99};
      CHECK(manager.renderer(badSpec) == nullptr);

      // second call takes the cached-mismatch path instead of trying to build again
      CHECK(manager.renderer(badSpec) == nullptr);
    }
  }

  SECTION("clear allows a model to be reloaded")
  {
    const auto* modelBeforeClear = manager.model("armor.mdl");
    REQUIRE(modelBeforeClear != nullptr);
    REQUIRE(modelBeforeClear->data() != nullptr);

    manager.clear();

    const auto* modelAfterClear = manager.model("armor.mdl");
    CHECK(modelAfterClear != nullptr);
    CHECK(modelAfterClear->data() != nullptr);
  }

  SECTION("findEntityModelsByTextureResourceId")
  {
    const auto* model = manager.model("armor.mdl");
    REQUIRE(model != nullptr);
    const auto id = model->dataResource().id();

    CHECK_THAT(
      manager.findEntityModelsByTextureResourceId({id}),
      Catch::Matchers::Equals(std::vector<const EntityModel*>{model}));
    CHECK(manager.findEntityModelsByTextureResourceId({}).empty());
  }
}

} // namespace tb::mdl
