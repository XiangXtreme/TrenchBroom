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

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "mcp/McpBridgeClient.h"
#include "mcp/McpJsonRpc.h"

#include <memory>

#include <catch2/catch_test_macros.hpp>

namespace tb::mcp
{
namespace
{

struct ConnectionState
{
  bool connected = true;
  bool bytesWritten = true;
  bool readyRead = true;
  bool lineAvailable = false;
  QString error = "test transport error";
  QString pipeName;
  int connectTimeoutMs = -1;
  int writeTimeoutMs = -1;
  int readyReadTimeoutMs = -1;
  QByteArray written;
  QByteArray response;
};

class TestConnection : public McpBridgeConnection
{
private:
  std::shared_ptr<ConnectionState> m_state;

public:
  explicit TestConnection(std::shared_ptr<ConnectionState> state)
    : m_state{std::move(state)}
  {
  }

  void connectToServer(const QString& pipeName) override { m_state->pipeName = pipeName; }

  bool waitForConnected(const int timeoutMs) override
  {
    m_state->connectTimeoutMs = timeoutMs;
    return m_state->connected;
  }

  qint64 write(const QByteArray& data) override
  {
    m_state->written += data;
    return data.size();
  }

  bool waitForBytesWritten(const int timeoutMs) override
  {
    m_state->writeTimeoutMs = timeoutMs;
    return m_state->bytesWritten;
  }

  bool canReadLine() const override { return m_state->lineAvailable; }

  bool waitForReadyRead(const int timeoutMs) override
  {
    m_state->readyReadTimeoutMs = timeoutMs;
    if (m_state->readyRead)
    {
      m_state->lineAvailable = true;
    }
    return m_state->readyRead;
  }

  QByteArray readLine() override { return m_state->response; }

  QString errorString() const override { return m_state->error; }
};

McpBridgeConfig testConfig()
{
  return McpBridgeConfig{"test-pipe", McpMode::Edit};
}

McpBridgeClient makeClient(
  const std::shared_ptr<ConnectionState>& state,
  const McpBridgeClientTimeouts& timeouts = {})
{
  return McpBridgeClient{
    [state] { return std::make_unique<TestConnection>(state); }, timeouts};
}

} // namespace

TEST_CASE("McpBridgeClient", "[McpStdioClient]")
{
  SECTION("uses catalog cost classes and response timeouts")
  {
    CHECK(toolCostClassForName("tb_inspect") == McpToolCostClass::Fast);
    CHECK(toolCostClassForName("tb_api") == McpToolCostClass::Fast);
    CHECK(toolCostClassForName("tb_capture") == McpToolCostClass::Normal);
    CHECK(toolCostClassForName("tb_execute_python") == McpToolCostClass::Long);
    CHECK(toolCostClassForName("unknown_tool") == McpToolCostClass::Normal);

    CHECK(toolResponseTimeoutMs(McpToolCostClass::Fast) == 5'000);
    CHECK(toolResponseTimeoutMs(McpToolCostClass::Normal) == 30'000);
    CHECK(toolResponseTimeoutMs(McpToolCostClass::Long) == 90'000);
  }

  SECTION("uses five second connection and write limits")
  {
    const auto state = std::make_shared<ConnectionState>();
    state->lineAvailable = true;
    state->response = QJsonDocument{toJson(McpBridgeResponse::success(
                                      "request-1", QJsonObject{{"count", 1}}))}
                        .toJson(QJsonDocument::Compact)
                      + '\n';
    const auto client = makeClient(state);

    const auto response = client.request(
      testConfig(), McpBridgeRequestType::ToolCall, "documents_list", {}, "request-1");

    REQUIRE(response.ok);
    CHECK(response.id == "request-1");
    CHECK(response.result.value("count").toInt() == 1);
    CHECK(state->pipeName == "test-pipe");
    CHECK(state->connectTimeoutMs == 5'000);
    CHECK(state->writeTimeoutMs == 5'000);

    const auto request = QJsonDocument::fromJson(state->written.trimmed()).object();
    CHECK(request.value("id").toString() == "request-1");
    CHECK_FALSE(request.contains("token"));
    CHECK(request.value("type").toString() == "tool_call");
    CHECK(request.value("tool").toString() == "documents_list");
    CHECK(request.value("mode").toString() == "Edit");
  }

  SECTION("serializes typed resource list requests")
  {
    const auto state = std::make_shared<ConnectionState>();
    state->lineAvailable = true;
    state->response =
      QJsonDocument{toJson(McpBridgeResponse::success(
                      "resource-list-1", QJsonObject{{"resources", QJsonArray{}}}))}
        .toJson(QJsonDocument::Compact)
      + '\n';
    const auto client = makeClient(state);

    const auto response = client.request(
      testConfig(),
      McpBridgeRequestType::ResourcesList,
      {},
      QJsonObject{{"cursor", "opaque-cursor"}},
      "resource-list-1");

    REQUIRE(response.ok);
    const auto request = QJsonDocument::fromJson(state->written.trimmed()).object();
    CHECK(request.value("type").toString() == "resources_list");
    CHECK_FALSE(request.contains("tool"));
    CHECK(
      request.value("params").toObject().value("cursor").toString() == "opaque-cursor");
  }

  SECTION("serializes typed resource read requests")
  {
    const auto state = std::make_shared<ConnectionState>();
    state->lineAvailable = true;
    state->response =
      QJsonDocument{toJson(McpBridgeResponse::success(
                      "resource-read-1", QJsonObject{{"operationId", "mcp-op-1"}}))}
        .toJson(QJsonDocument::Compact)
      + '\n';
    const auto client = makeClient(state);

    const auto response = client.request(
      testConfig(),
      McpBridgeRequestType::ResourceRead,
      {},
      QJsonObject{{"uri", "tbmcp://operation/mcp-op-1"}},
      "resource-read-1");

    REQUIRE(response.ok);
    const auto request = QJsonDocument::fromJson(state->written.trimmed()).object();
    CHECK(request.value("type").toString() == "resource_read");
    CHECK_FALSE(request.contains("tool"));
    CHECK(
      request.value("params").toObject().value("uri").toString()
      == "tbmcp://operation/mcp-op-1");
  }

  SECTION("returns structured recovery details after a response timeout")
  {
    const auto state = std::make_shared<ConnectionState>();
    state->readyRead = false;
    const auto timeouts = McpBridgeClientTimeouts{11, 13, 17, 19, 23};
    const auto client = makeClient(state, timeouts);

    const auto response = client.request(
      testConfig(), McpBridgeRequestType::ToolCall, "tb_execute_python", {}, "request-long");

    REQUIRE_FALSE(response.ok);
    REQUIRE(response.error);
    CHECK(state->readyReadTimeoutMs == 23);
    CHECK(response.error->details.value("tool").toString() == "tb_execute_python");
    CHECK(response.error->details.value("requestId").toString() == "request-long");
    CHECK(response.error->details.value("timeoutMs").toInt() == 23);
    CHECK(response.error->details.value("mutatedDocument").toString() == "unknown");
    CHECK_FALSE(response.error->details.value("retrySafe").toBool(true));
    CHECK(response.error->details.value("recoveryActions").toArray().size() == 3);

    const auto toolResult = mcpToolCallResult(
      QJsonObject{
        {"name", "tb_execute_python"},
        {"arguments", QJsonObject{}},
      },
      [&](McpBridgeRequestType, const QString&, const QJsonObject&) { return response; });
    const auto structured = toolResult.value("structuredContent").toObject();
    CHECK(structured.value("tool").toString() == "tb_execute_python");
    CHECK(structured.value("requestId").toString() == "request-long");
    CHECK(structured.value("timeoutMs").toInt() == 23);
    CHECK(structured.value("mutatedDocument").toString() == "unknown");
    CHECK_FALSE(structured.value("retrySafe").toBool(true));
    CHECK(structured.value("details").toObject() == response.error->details);
  }

  SECTION("rejects a response for a different request")
  {
    const auto state = std::make_shared<ConnectionState>();
    state->lineAvailable = true;
    state->response =
      QJsonDocument{toJson(McpBridgeResponse::success("other-request"))}.toJson(
        QJsonDocument::Compact)
      + '\n';
    const auto client = makeClient(state);

    const auto response = client.request(
      testConfig(), McpBridgeRequestType::ToolCall, "tb_status", {}, "request-1");

    REQUIRE_FALSE(response.ok);
    REQUIRE(response.error);
    CHECK(response.id == "request-1");
    CHECK(response.error->code == McpErrorCode::InvalidRequest);
    CHECK(response.error->message.contains("does not match"));
  }
}

} // namespace tb::mcp
