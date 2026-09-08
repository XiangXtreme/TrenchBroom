#include <QJsonDocument>
#include <QJsonObject>

#include "McpThinBridgeTools.h"
#include "ui/python/PythonApiDocumentation.h"
#include "ui/python/PythonRuntime.h"

#include <cmath>
#include <limits>

namespace tb::ui
{
McpBridgeToolResult pythonApiResult(const QJsonObject& params)
{
  const auto limitValue = params.value("limit").toDouble(8);
  const auto offsetValue = params.value("offset").toDouble(0);
  if (
    limitValue < 1 || limitValue > 50 || std::floor(limitValue) != limitValue
    || offsetValue < 0 || offsetValue > std::numeric_limits<int>::max()
    || std::floor(offsetValue) != offsetValue)
  {
    return invalidParamsFailure("tb_api requires integer limit 1..50 and offset >= 0");
  }
  if (!PythonRuntime::instance().ensureInitialized())
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not initialize Python API documentation");
  }
  const auto limit = static_cast<int>(limitValue);
  const auto offset = static_cast<int>(offsetValue);
  const auto query = params.value("query").toString().trimmed();
  auto exact = params.value("symbol").toString().trimmed();
  if (exact.startsWith("tb."))
  {
    exact = "trenchbroom." + exact.mid(3);
  }
  else if (!exact.isEmpty() && !exact.startsWith("trenchbroom."))
  {
    exact.prepend("trenchbroom.");
  }
  auto matches = QJsonArray{};
  for (const auto& value : pythonApiDocumentation())
  {
    const auto entry = value.toObject();
    const auto name = entry.value("symbol").toString();
    if (!exact.isEmpty() ? name == exact : name.contains(query, Qt::CaseInsensitive))
    {
      matches.append(entry);
    }
  }
  auto symbols = QJsonArray{};
  auto bytes = qsizetype{0};
  auto next = offset;
  while (next < matches.size() && symbols.size() < limit)
  {
    const auto entry = matches.at(next).toObject();
    const auto size = QJsonDocument{entry}.toJson(QJsonDocument::Compact).size();
    if (bytes + size > 15 * 1024)
    {
      break;
    }
    symbols.append(entry);
    bytes += size + 1;
    ++next;
  }
  const auto truncated = next < matches.size();
  return McpBridgeToolResult::success(QJsonObject{
    {"symbols", symbols},
    {"total", matches.size()},
    {"offset", offset},
    {"limit", limit},
    {"truncated", truncated},
    {"nextOffset", truncated ? QJsonValue{next} : QJsonValue{QJsonValue::Null}},
  });
}
} // namespace tb::ui
