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

#include "ui/mcp/McpHttpServer.h"

#include <QAbstractSocket>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#include "mcp/McpJsonRpc.h"

#include <optional>
#include <utility>

namespace tb::ui
{
namespace
{

constexpr auto EndpointPath = "/mcp";

QByteArray reasonPhrase(const int statusCode)
{
  switch (statusCode)
  {
  case 200:
    return "OK";
  case 204:
    return "No Content";
  case 202:
    return "Accepted";
  case 400:
    return "Bad Request";
  case 403:
    return "Forbidden";
  case 404:
    return "Not Found";
  case 405:
    return "Method Not Allowed";
  case 408:
    return "Request Timeout";
  case 413:
    return "Content Too Large";
  case 431:
    return "Request Header Fields Too Large";
  case 503:
    return "Service Unavailable";
  default:
    return "Internal Server Error";
  }
}

QByteArray jsonBody(const QString& message)
{
  return QJsonDocument{QJsonObject{{"error", message}}}.toJson(QJsonDocument::Compact);
}

QList<QByteArray> headerValues(const QList<QByteArray>& lines, const QByteArray& key)
{
  const auto lowerKey = key.toLower() + ":";
  auto values = QList<QByteArray>{};
  for (int i = 1; i < lines.size(); ++i)
  {
    const auto line = lines[i];
    if (line.toLower().startsWith(lowerKey))
    {
      values.push_back(line.mid(lowerKey.size()).trimmed());
    }
  }
  return values;
}

QString headerValue(const QList<QByteArray>& lines, const QByteArray& key)
{
  return QString::fromUtf8(headerValues(lines, key).value(0));
}

std::optional<QByteArray> allowedOrigin(const QList<QByteArray>& lines)
{
  const auto origin = headerValue(lines, "Origin");
  if (origin.isEmpty())
  {
    return QByteArray{};
  }

  const auto url = QUrl{origin};
  if (
    url.host() == "127.0.0.1"
    || url.host().compare("localhost", Qt::CaseInsensitive) == 0)
  {
    return origin.toUtf8();
  }
  return std::nullopt;
}

QString methodFromRequestLine(const QByteArray& requestLine)
{
  return QString::fromUtf8(requestLine.split(' ').value(0));
}

QString pathFromRequestLine(const QByteArray& requestLine)
{
  return QString::fromUtf8(requestLine.split(' ').value(1));
}

} // namespace

McpHttpServer::McpHttpServer(McpBridgeServer& bridgeServer, QObject* parent)
  : McpHttpServer{bridgeServer, McpHttpServerLimits{}, parent}
{
}

McpHttpServer::McpHttpServer(
  McpBridgeServer& bridgeServer, McpHttpServerLimits limits, QObject* parent)
  : QObject{parent}
  , m_bridgeServer{bridgeServer}
  , m_limits{std::move(limits)}
{
}

McpHttpServer::~McpHttpServer()
{
  stop();
}

bool McpHttpServer::start(const mcp::McpBridgeConfig& config, QString* error)
{
  stop();
  m_config = config;

  if (m_config.mode == mcp::McpMode::Off || !m_config.httpEnabled)
  {
    return true;
  }

  if (m_config.httpHost != "127.0.0.1" && m_config.httpHost != "localhost")
  {
    if (error)
    {
      *error = "MCP HTTP server only supports localhost in this version";
    }
    return false;
  }

  m_server = std::make_unique<QTcpServer>();
  m_server->setMaxPendingConnections(
    m_limits.maxOrdinaryConnections + m_limits.maxSseConnections);
  connect(
    m_server.get(),
    &QTcpServer::newConnection,
    this,
    &McpHttpServer::handleNewConnection);

  if (!m_server->listen(QHostAddress::LocalHost, m_config.httpPort))
  {
    if (error)
    {
      *error =
        m_server->serverError() == QAbstractSocket::AddressInUseError
          ? QString{"Another TrenchBroom MCP instance is already listening on %1"}.arg(
              url())
          : m_server->errorString();
    }
    m_server.reset();
    return false;
  }

  return true;
}

void McpHttpServer::stop()
{
  if (m_server)
  {
    m_server->close();
    m_server.reset();
  }

  const auto sockets = findChildren<QTcpSocket*>();
  for (auto* socket : sockets)
  {
    socket->disconnectFromHost();
    socket->deleteLater();
  }
  m_requestDeadlines.clear();
  m_sseConnections.clear();
  m_connections.clear();
}

bool McpHttpServer::isListening() const
{
  return m_server != nullptr && m_server->isListening();
}

QString McpHttpServer::url() const
{
  return QString{"http://127.0.0.1:%1%2"}.arg(port()).arg(EndpointPath);
}

quint16 McpHttpServer::port() const
{
  return m_server && m_server->isListening() ? m_server->serverPort() : m_config.httpPort;
}

int McpHttpServer::ordinaryConnectionCount() const
{
  return static_cast<int>(m_connections.size() - m_sseConnections.size());
}

void McpHttpServer::startRequestDeadline(QTcpSocket& socket)
{
  auto* timer = new QTimer{&socket};
  timer->setSingleShot(true);
  connect(timer, &QTimer::timeout, this, [this, socketPtr = &socket]() {
    if (
      !m_connections.contains(socketPtr)
      || socketPtr->property("tbMcpHttpRequestComplete").toBool())
    {
      return;
    }
    socketPtr->setProperty("tbMcpHttpRequestComplete", true);
    writeHttpResponse(
      *socketPtr,
      408,
      reasonPhrase(408),
      "application/json",
      jsonBody("HTTP request timed out"));
    socketPtr->disconnectFromHost();
  });
  m_requestDeadlines.insert(&socket, timer);
  timer->start(m_limits.incompleteRequestTimeoutMs);
}

void McpHttpServer::completeRequest(QTcpSocket& socket)
{
  socket.setProperty("tbMcpHttpRequestComplete", true);
  if (auto* timer = m_requestDeadlines.value(&socket))
  {
    timer->stop();
  }
}

void McpHttpServer::removeConnection(QTcpSocket& socket)
{
  m_requestDeadlines.remove(&socket);
  m_sseConnections.remove(&socket);
  m_connections.remove(&socket);
  socket.deleteLater();
}

void McpHttpServer::handleNewConnection()
{
  while (auto* socket = m_server->nextPendingConnection())
  {
    socket->setParent(this);
    connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
      if (m_connections.contains(socket))
      {
        removeConnection(*socket);
      }
      else
      {
        socket->deleteLater();
      }
    });

    if (ordinaryConnectionCount() >= m_limits.maxOrdinaryConnections)
    {
      writeHttpResponse(
        *socket,
        503,
        reasonPhrase(503),
        "application/json",
        jsonBody("Too many MCP HTTP connections"));
      socket->disconnectFromHost();
      continue;
    }

    m_connections.insert(socket);
    socket->setReadBufferSize(m_limits.maxHeaderBytes + 4 + m_limits.maxBodyBytes + 1);
    startRequestDeadline(*socket);
    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
      handleSocketReadyRead(*socket);
    });
  }
}

void McpHttpServer::handleSocketReadyRead(QTcpSocket& socket)
{
  if (socket.property("tbMcpHttpRequestComplete").toBool())
  {
    return;
  }

  const auto maxRequestBytes = m_limits.maxHeaderBytes + 4 + m_limits.maxBodyBytes;
  if (socket.bytesAvailable() > maxRequestBytes)
  {
    socket.setProperty("tbMcpHttpRequestComplete", true);
    socket.readAll();
    writeHttpResponse(
      socket,
      413,
      reasonPhrase(413),
      "application/json",
      jsonBody("HTTP request too large"));
    // Give the peer a chance to read the response before Windows resets a socket with
    // unread inbound data.
    QTimer::singleShot(25, &socket, [&socket]() { socket.disconnectFromHost(); });
    return;
  }

  const auto requestBytes = socket.peek(maxRequestBytes);
  const auto headerEnd = requestBytes.indexOf("\r\n\r\n");
  if (headerEnd < 0)
  {
    if (requestBytes.size() > m_limits.maxHeaderBytes)
    {
      socket.setProperty("tbMcpHttpRequestComplete", true);
      writeHttpResponse(
        socket,
        431,
        reasonPhrase(431),
        "application/json",
        jsonBody("HTTP header too large"));
      socket.disconnectFromHost();
    }
    return;
  }

  if (headerEnd > m_limits.maxHeaderBytes)
  {
    socket.setProperty("tbMcpHttpRequestComplete", true);
    writeHttpResponse(
      socket,
      431,
      reasonPhrase(431),
      "application/json",
      jsonBody("HTTP header too large"));
    socket.disconnectFromHost();
    return;
  }

  const auto headerBytes = requestBytes.left(headerEnd);
  const auto lines = headerBytes.split('\n');
  if (lines.isEmpty())
  {
    writeHttpResponse(
      socket, 400, reasonPhrase(400), "application/json", jsonBody("Empty HTTP request"));
    socket.disconnectFromHost();
    return;
  }

  const auto requestLine = lines.first().trimmed();
  const auto method = methodFromRequestLine(requestLine);
  const auto path = pathFromRequestLine(requestLine);

  if (!headerValues(lines, "Transfer-Encoding").isEmpty())
  {
    socket.setProperty("tbMcpHttpRequestComplete", true);
    writeHttpResponse(
      socket,
      400,
      reasonPhrase(400),
      "application/json",
      jsonBody("Transfer-Encoding is not supported"));
    socket.disconnectFromHost();
    return;
  }

  const auto contentLengthValues = headerValues(lines, "Content-Length");
  if (contentLengthValues.size() > 1)
  {
    socket.setProperty("tbMcpHttpRequestComplete", true);
    writeHttpResponse(
      socket,
      400,
      reasonPhrase(400),
      "application/json",
      jsonBody("Duplicate Content-Length"));
    socket.disconnectFromHost();
    return;
  }

  const auto contentLengthText = contentLengthValues.value(0);
  auto ok = false;
  const auto contentLength =
    contentLengthText.isEmpty() ? qlonglong{0} : contentLengthText.toLongLong(&ok);
  if (!contentLengthText.isEmpty() && (!ok || contentLength < 0))
  {
    socket.setProperty("tbMcpHttpRequestComplete", true);
    writeHttpResponse(
      socket,
      400,
      reasonPhrase(400),
      "application/json",
      jsonBody("Invalid Content-Length"));
    socket.disconnectFromHost();
    return;
  }

  if (contentLength > m_limits.maxBodyBytes)
  {
    socket.setProperty("tbMcpHttpRequestComplete", true);
    writeHttpResponse(
      socket,
      413,
      reasonPhrase(413),
      "application/json",
      jsonBody("HTTP body too large"));
    socket.disconnectFromHost();
    return;
  }

  const auto totalLength = qsizetype{headerEnd + 4} + contentLength;
  if (requestBytes.size() < totalLength)
  {
    return;
  }

  socket.read(totalLength);
  completeRequest(socket);

  if (path != EndpointPath)
  {
    writeHttpResponse(
      socket,
      404,
      reasonPhrase(404),
      "application/json",
      jsonBody("Unknown MCP endpoint"));
    socket.disconnectFromHost();
    return;
  }

  const auto responseOrigin = allowedOrigin(lines);
  if (!responseOrigin)
  {
    writeHttpResponse(
      socket, 403, reasonPhrase(403), "application/json", jsonBody("Invalid Origin"));
    socket.disconnectFromHost();
    return;
  }

  if (method == "OPTIONS")
  {
    writeHttpResponse(socket, 204, reasonPhrase(204), {}, {}, *responseOrigin);
    socket.disconnectFromHost();
    return;
  }

  if (method == "GET")
  {
    if (m_sseConnections.size() >= m_limits.maxSseConnections)
    {
      writeHttpResponse(
        socket,
        503,
        reasonPhrase(503),
        "application/json",
        jsonBody("Too many MCP SSE connections"),
        *responseOrigin);
      socket.disconnectFromHost();
      return;
    }
    m_sseConnections.insert(&socket);
    writeSseStream(socket, *responseOrigin);
    return;
  }

  if (method != "POST")
  {
    writeHttpResponse(
      socket,
      405,
      reasonPhrase(405),
      "application/json",
      jsonBody("Method not allowed"),
      *responseOrigin);
    socket.disconnectFromHost();
    return;
  }

  auto parseError = QJsonParseError{};
  const auto body = requestBytes.mid(headerEnd + 4, contentLength);
  const auto document = QJsonDocument::fromJson(body, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject())
  {
    writeHttpResponse(
      socket,
      400,
      reasonPhrase(400),
      "application/json",
      QJsonDocument{mcp::jsonRpcError({}, -32700, "Parse error")}.toJson(
        QJsonDocument::Compact),
      *responseOrigin);
    socket.disconnectFromHost();
    return;
  }

  const auto response = mcp::handleMcpJsonRpcRequest(
    document.object(),
    m_config.mode,
    [this](const QString& toolName, const QJsonObject& params) {
      const auto request = mcp::McpBridgeRequest{
        "http",
        toolName,
        params,
        m_config.mode,
      };
      return m_bridgeServer.dispatchRequest(request);
    });

  if (!response)
  {
    writeHttpResponse(socket, 202, reasonPhrase(202), "text/plain", {}, *responseOrigin);
    socket.disconnectFromHost();
    return;
  }

  writeHttpResponse(
    socket,
    200,
    reasonPhrase(200),
    "application/json",
    QJsonDocument{*response}.toJson(QJsonDocument::Compact),
    *responseOrigin);
  socket.disconnectFromHost();
}

void McpHttpServer::writeSseStream(
  QTcpSocket& socket, const QByteArray& allowedOrigin) const
{
  auto response = QByteArray{};
  response += "HTTP/1.1 200 " + reasonPhrase(200) + "\r\n";
  response += "Connection: keep-alive\r\n";
  response += "Cache-Control: no-cache, no-transform\r\n";
  response += "Content-Type: text/event-stream\r\n";
  if (!allowedOrigin.isEmpty())
  {
    response += "Access-Control-Allow-Origin: " + allowedOrigin + "\r\n";
    response += "Vary: Origin\r\n";
  }
  response += "\r\n";
  response += ": TrenchBroom MCP stream ready\r\n\r\n";
  socket.write(response);
  socket.flush();
}

void McpHttpServer::writeHttpResponse(
  QTcpSocket& socket,
  const int statusCode,
  const QByteArray& reason,
  const QByteArray& contentType,
  const QByteArray& body,
  const QByteArray& allowedOrigin,
  const QByteArray& extraHeaders) const
{
  auto response = QByteArray{};
  response += "HTTP/1.1 " + QByteArray::number(statusCode) + " " + reason + "\r\n";
  response += "Connection: close\r\n";
  if (!allowedOrigin.isEmpty())
  {
    response += "Access-Control-Allow-Origin: " + allowedOrigin + "\r\n";
    response += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    response += "Access-Control-Allow-Headers: Content-Type, MCP-Protocol-Version\r\n";
    response += "Vary: Origin\r\n";
  }
  response += extraHeaders;
  if (!contentType.isEmpty())
  {
    response += "Content-Type: " + contentType + "\r\n";
  }
  response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
  response += "\r\n";
  response += body;
  socket.write(response);
  socket.flush();
}

} // namespace tb::ui
