/*
 Copyright (C) 2026 XiangXtreme

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

#include <QJsonObject>

#include "mcp/McpBridgeMessages.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::mcp
{

TEST_CASE("McpBridgeMessages")
{
  SECTION("request json roundtrip")
  {
    const auto request = McpBridgeRequest{
      "1",
      "tb_inspect",
      QJsonObject{{"verbose", true}},
      McpMode::ReadOnly,
    };

    const auto parsed = bridgeRequestFromJson(toJson(request));

    REQUIRE(parsed);
    CHECK(parsed->id == request.id);
    CHECK(parsed->tool == request.tool);
    CHECK(parsed->params.value("verbose").toBool());
    REQUIRE(parsed->requestedMode);
    CHECK(*parsed->requestedMode == McpMode::ReadOnly);
    CHECK_FALSE(toJson(request).contains("token"));
    CHECK_FALSE(toJson(request).contains("type"));
  }

  SECTION("requires a tool name")
  {
    auto error = QString{};
    const auto parsed = bridgeRequestFromJson(
      QJsonObject{
        {"id", "missing-tool"},
        {"params", QJsonObject{}},
      },
      &error);

    CHECK_FALSE(parsed);
    CHECK(error.contains("tool"));
  }

  SECTION("rejects invalid request params")
  {
    auto error = QString{};
    auto json = QJsonObject{
      {"id", "1"},
      {"tool", "tb_inspect"},
      {"params", true},
    };

    CHECK(!bridgeRequestFromJson(json, &error));
    CHECK(error.contains("params"));
  }

  SECTION("success response json roundtrip")
  {
    const auto response =
      McpBridgeResponse::success("1", QJsonObject{{"mode", "ReadOnly"}});

    const auto parsed = bridgeResponseFromJson(toJson(response));

    REQUIRE(parsed);
    CHECK(parsed->id == response.id);
    CHECK(parsed->ok);
    CHECK(parsed->result.value("mode").toString() == "ReadOnly");
    CHECK(!parsed->error);
  }

  SECTION("failure response json roundtrip")
  {
    const auto response = McpBridgeResponse::failure(
      "1",
      McpError{
        McpErrorCode::Forbidden,
        "denied",
        QJsonObject{{"stderrBytes", 12}},
      });

    const auto parsed = bridgeResponseFromJson(toJson(response));

    REQUIRE(parsed);
    CHECK(parsed->id == response.id);
    CHECK(!parsed->ok);
    REQUIRE(parsed->error);
    CHECK(parsed->error->code == McpErrorCode::Forbidden);
    CHECK(parsed->error->message == "denied");
    CHECK(parsed->error->details.value("stderrBytes").toInt() == 12);
  }
}

} // namespace tb::mcp
