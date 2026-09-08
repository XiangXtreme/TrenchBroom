#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include "McpThinBridgeTools.h"
#include "ui/python/PythonApiDocumentation.h"
#include "ui/python/PythonRuntime.h"

#include <cmath>
#include <algorithm>
#include <limits>
#include <vector>

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
  struct Match
  {
    QJsonObject entry;
    int rank;
  };
  auto matches = std::vector<Match>{};
  const auto terms = query.split(
    QRegularExpression{"[^\\p{L}\\p{N}]+"}, Qt::SkipEmptyParts);
  for (const auto& value : pythonApiDocumentation())
  {
    const auto entry = value.toObject();
    const auto name = entry.value("symbol").toString();
    if (!exact.isEmpty())
    {
      if (name == exact)
      {
        matches.push_back({entry, 0});
      }
      continue;
    }
    const auto normalizedName = name.toLower();
    const auto description = entry.value("description").toString().toLower();
    const auto matchesName = std::ranges::all_of(terms, [&](const auto& term) {
      return normalizedName.contains(term.toLower());
    });
    const auto matchesDescription = std::ranges::all_of(terms, [&](const auto& term) {
      return (normalizedName + "\n" + description).contains(term.toLower());
    });
    if (terms.empty() || matchesDescription)
    {
      matches.push_back({entry, matchesName ? 0 : 1});
    }
  }
  std::ranges::stable_sort(matches, [](const auto& lhs, const auto& rhs) {
    if (lhs.rank != rhs.rank)
    {
      return lhs.rank < rhs.rank;
    }
    return lhs.entry.value("symbol").toString() < rhs.entry.value("symbol").toString();
  });
  auto symbols = QJsonArray{};
  auto bytes = qsizetype{0};
  auto next = offset;
  while (next < static_cast<int>(matches.size()) && symbols.size() < limit)
  {
    const auto& entry = matches[static_cast<size_t>(next)].entry;
    const auto size = QJsonDocument{entry}.toJson(QJsonDocument::Compact).size();
    if (bytes + size > 15 * 1024)
    {
      break;
    }
    symbols.append(entry);
    bytes += size + 1;
    ++next;
  }
  const auto truncated = next < static_cast<int>(matches.size());
  return McpBridgeToolResult::success(QJsonObject{
    {"symbols", symbols},
    {"total", static_cast<qint64>(matches.size())},
    {"offset", offset},
    {"limit", limit},
    {"truncated", truncated},
    {"nextOffset", truncated ? QJsonValue{next} : QJsonValue{QJsonValue::Null}},
  });
}
} // namespace tb::ui
