/*
 Copyright (C) 2026 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "mdl/Entity.h"
#include "mdl/GameConfig.h"
#include "render/GoldSrcEntityRenderer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace tb::render
{
TEST_CASE("GoldSrcEntityRenderer")
{
  SECTION("only games loading GoldSrc mip textures enable the preview")
  {
    auto config = mdl::GameConfig{};
    config.materialConfig.extensions = {".mip", ".png"};
    CHECK_FALSE(usesGoldSrcRendering(config));
    config.materialConfig.extensions.push_back(".C");
    CHECK(usesGoldSrcRendering(config));
  }

  SECTION("render properties distinguish normal, blended, and masked modes")
  {
    for (auto mode = 0; mode <= 5; ++mode)
    {
      auto entity = mdl::Entity{
        {{"rendermode", std::to_string(mode)},
         {"renderamt", "100"},
         {"rendercolor", "255 128 0"}}};
      const auto settings = goldSrcEntityRenderSettings(&entity);
      CHECK(settings.mode == mode);
      CHECK(settings.transparent() == (mode != 0 && mode != 4));
      CHECK(
        settings.alpha == Catch::Approx(mode == 0 || mode == 4 ? 1.0f : 100.0f / 255.0f));
      CHECK(settings.color == vm::vec3f{1.0f, 128.0f / 255.0f, 0.0f});
    }
  }

  SECTION("missing and malformed properties have finite usable defaults")
  {
    const auto defaults = goldSrcEntityRenderSettings(nullptr);
    CHECK(defaults.mode == 0);
    CHECK(defaults.alpha == 1.0f);
    CHECK(defaults.color == vm::vec3f{1.0f, 1.0f, 1.0f});
    auto entity = mdl::Entity{{{"rendermode", "nan"}, {"renderamt", "nan"}}};
    CHECK(goldSrcEntityRenderSettings(&entity).mode == 0);
    entity.addOrUpdateProperty("rendermode", "2");
    CHECK(goldSrcEntityRenderSettings(&entity).alpha == 1.0f);
    entity.addOrUpdateProperty("renderamt", "999");
    CHECK(goldSrcEntityRenderSettings(&entity).alpha == 1.0f);
    entity.addOrUpdateProperty("renderamt", "-1");
    CHECK(goldSrcEntityRenderSettings(&entity).alpha == 0.0f);
  }

  SECTION("conveyor scroll uses texture S, width and signed speed")
  {
    auto entity = mdl::Entity{{{"classname", "func_conveyor"}, {"speed", "64"}}};
    auto settings = goldSrcEntityRenderSettings(&entity);
    CHECK(goldSrcConveyorOffset(settings, "SCROLLarrows", 128, 0.5) == 0.75f);
    CHECK(goldSrcConveyorOffset(settings, "{scrollarrows", 64, 0.5) == 0.5f);
    CHECK(goldSrcConveyorOffset(settings, "arrows", 128, 0.5) == 0.0f);
    CHECK(goldSrcConveyorOffset(settings, "scrollarrows", 0, 0.5) == 0.0f);
    entity.addOrUpdateProperty("speed", "-64");
    settings = goldSrcEntityRenderSettings(&entity);
    CHECK(goldSrcConveyorOffset(settings, "scrollarrows", 128, 0.5) == 0.25f);
    CHECK(goldSrcConveyorOffset(settings, "scrollarrows", 128, 2000000.5) == 0.25f);
    entity.addOrUpdateProperty("speed", "0");
    CHECK(goldSrcEntityRenderSettings(&entity).conveyorSpeed == 100.0f);
    entity.setClassname("func_wall");
    CHECK(goldSrcEntityRenderSettings(&entity).conveyorSpeed == 0.0f);
  }
}
} // namespace tb::render
