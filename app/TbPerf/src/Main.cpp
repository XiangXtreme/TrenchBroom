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

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include "mcp/McpJsonRpc.h"
#include "mcp/McpToolCatalog.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <numeric>
#include <optional>
#include <unordered_map>
#include <vector>

namespace
{

using Clock = std::chrono::steady_clock;

struct PerfOptions
{
  QString suite;
  QString benchmark;
  QString jsonPath;
  QString baselinePath;
  int iterations = 200;
  int warmup = 20;
  double maxRegressionPercent = 15.0;
  bool list = false;
  bool help = false;
};

struct PerfCase
{
  QString suite;
  QString name;
  QString description;
  std::function<std::uint64_t()> run;
};

struct PerfResult
{
  QString suite;
  QString name;
  QString description;
  int iterations = 0;
  double minNs = 0.0;
  double medianNs = 0.0;
  double p95Ns = 0.0;
  double maxNs = 0.0;
  double meanNs = 0.0;
  std::uint64_t checksum = 0;
};

std::uint64_t jsonCost(const QJsonValue& value)
{
  const auto bytes = QJsonDocument{value.toObject()}.toJson(QJsonDocument::Compact);
  return static_cast<std::uint64_t>(bytes.size());
}

std::uint64_t jsonArrayCost(const QJsonArray& value)
{
  const auto bytes =
    QJsonDocument{QJsonObject{{"items", value}}}.toJson(QJsonDocument::Compact);
  return static_cast<std::uint64_t>(bytes.size());
}

std::vector<PerfCase> perfCases()
{
  return {
    {
      "mcp",
      "mcp.default_tool_catalog",
      "Return the static MCP tool catalog.",
      [] {
        const auto& catalog = tb::mcp::defaultToolCatalog();
        std::uint64_t checksum = catalog.size();
        for (const auto& tool : catalog)
        {
          checksum += static_cast<std::uint64_t>(tool.name.size());
          checksum += tool.mutatesDocument ? 1u : 0u;
        }
        return checksum;
      },
    },
    {
      "mcp",
      "mcp.tools_list_json_edit",
      "Serialize the implemented MCP tool list for Edit mode.",
      [] { return jsonArrayCost(tb::mcp::toolsListJson(tb::mcp::McpMode::Edit)); },
    },
    {
      "mcp",
      "mcp.initialize_result",
      "Build the MCP initialize result.",
      [] { return jsonCost(tb::mcp::mcpInitializeResult({})); },
    },
    {
      "mcp",
      "mcp.tools_list_result_edit",
      "Build the JSON-RPC tools/list result for Edit mode.",
      [] { return jsonCost(tb::mcp::mcpToolsListResult(tb::mcp::McpMode::Edit)); },
    },
    {
      "mcp",
      "mcp.find_all_tools",
      "Lookup every tool definition by name.",
      [] {
        const auto& catalog = tb::mcp::defaultToolCatalog();
        std::uint64_t checksum = 0;
        for (const auto& tool : catalog)
        {
          const auto found = tb::mcp::findToolDefinition(tool.name);
          checksum += found ? static_cast<std::uint64_t>(found->name.size()) : 0u;
        }
        return checksum;
      },
    },
  };
}

QString formatNs(const double ns)
{
  if (ns >= 1'000'000.0)
  {
    return QString::number(ns / 1'000'000.0, 'f', 3) + " ms";
  }
  if (ns >= 1'000.0)
  {
    return QString::number(ns / 1'000.0, 'f', 3) + " us";
  }
  return QString::number(ns, 'f', 1) + " ns";
}

void printUsage(QTextStream& out)
{
  out
    << "Usage: TbPerf [options]\n\n"
    << "Options:\n"
    << "  --list                         List available benchmarks.\n"
    << "  --suite <name>                 Run only one suite, for example mcp.\n"
    << "  --benchmark <name>             Run only one benchmark.\n"
    << "  --iterations <count>           Timed iterations per benchmark (default 200).\n"
    << "  --warmup <count>               Warmup iterations per benchmark (default 20).\n"
    << "  --json <path>                  Write machine-readable results.\n"
    << "  --baseline <path>              Compare against a previous JSON result.\n"
    << "  --max-regression-percent <n>   Fail if median regression exceeds n (default "
       "15).\n"
    << "  --help                         Show this help.\n";
}

std::optional<int> parseInt(const QString& text)
{
  bool ok = false;
  const auto value = text.toInt(&ok);
  if (!ok || value <= 0)
  {
    return std::nullopt;
  }
  return value;
}

std::optional<double> parseDouble(const QString& text)
{
  bool ok = false;
  const auto value = text.toDouble(&ok);
  if (!ok || value < 0.0)
  {
    return std::nullopt;
  }
  return value;
}

std::optional<PerfOptions> parseOptions(const QStringList& args, QTextStream& err)
{
  PerfOptions options;
  for (int i = 1; i < args.size(); ++i)
  {
    const auto& arg = args[i];
    const auto requireValue = [&](const QString& option) -> std::optional<QString> {
      if (i + 1 >= args.size())
      {
        err << "Missing value for " << option << "\n";
        return std::nullopt;
      }
      return args[++i];
    };

    if (arg == "--help" || arg == "-h")
    {
      options.help = true;
    }
    else if (arg == "--list")
    {
      options.list = true;
    }
    else if (arg == "--suite")
    {
      const auto value = requireValue(arg);
      if (!value)
      {
        return std::nullopt;
      }
      options.suite = *value;
    }
    else if (arg == "--benchmark")
    {
      const auto value = requireValue(arg);
      if (!value)
      {
        return std::nullopt;
      }
      options.benchmark = *value;
    }
    else if (arg == "--json")
    {
      const auto value = requireValue(arg);
      if (!value)
      {
        return std::nullopt;
      }
      options.jsonPath = *value;
    }
    else if (arg == "--baseline")
    {
      const auto value = requireValue(arg);
      if (!value)
      {
        return std::nullopt;
      }
      options.baselinePath = *value;
    }
    else if (arg == "--iterations")
    {
      const auto value = requireValue(arg);
      if (!value)
      {
        return std::nullopt;
      }
      const auto parsed = parseInt(*value);
      if (!parsed)
      {
        err << "Invalid iteration count: " << *value << "\n";
        return std::nullopt;
      }
      options.iterations = *parsed;
    }
    else if (arg == "--warmup")
    {
      const auto value = requireValue(arg);
      if (!value)
      {
        return std::nullopt;
      }
      const auto parsed = parseInt(*value);
      if (!parsed)
      {
        err << "Invalid warmup count: " << *value << "\n";
        return std::nullopt;
      }
      options.warmup = *parsed;
    }
    else if (arg == "--max-regression-percent")
    {
      const auto value = requireValue(arg);
      if (!value)
      {
        return std::nullopt;
      }
      const auto parsed = parseDouble(*value);
      if (!parsed)
      {
        err << "Invalid regression threshold: " << *value << "\n";
        return std::nullopt;
      }
      options.maxRegressionPercent = *parsed;
    }
    else
    {
      err << "Unknown option: " << arg << "\n";
      return std::nullopt;
    }
  }
  return options;
}

PerfResult runCase(const PerfCase& perfCase, const PerfOptions& options)
{
  std::uint64_t checksum = 0;
  for (int i = 0; i < options.warmup; ++i)
  {
    checksum ^= perfCase.run();
  }

  std::vector<double> timings;
  timings.reserve(static_cast<size_t>(options.iterations));
  for (int i = 0; i < options.iterations; ++i)
  {
    const auto start = Clock::now();
    checksum ^= perfCase.run() + static_cast<std::uint64_t>(i);
    const auto stop = Clock::now();
    timings.push_back(static_cast<double>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count()));
  }

  std::sort(timings.begin(), timings.end());
  const auto sum = std::accumulate(timings.begin(), timings.end(), 0.0);
  const auto p95Index = std::min<size_t>(
    timings.size() - 1,
    static_cast<size_t>(std::ceil(static_cast<double>(timings.size()) * 0.95)) - 1);

  return PerfResult{
    perfCase.suite,
    perfCase.name,
    perfCase.description,
    options.iterations,
    timings.front(),
    timings[timings.size() / 2],
    timings[p95Index],
    timings.back(),
    sum / static_cast<double>(timings.size()),
    checksum,
  };
}

QJsonObject resultToJson(const PerfResult& result)
{
  return QJsonObject{
    {"suite", result.suite},
    {"name", result.name},
    {"description", result.description},
    {"iterations", result.iterations},
    {"minNs", result.minNs},
    {"medianNs", result.medianNs},
    {"p95Ns", result.p95Ns},
    {"maxNs", result.maxNs},
    {"meanNs", result.meanNs},
    {"checksum", QString::number(result.checksum)},
  };
}

bool writeJsonResults(
  const QString& path,
  const PerfOptions& options,
  const std::vector<PerfResult>& results,
  QTextStream& err)
{
  const auto absolutePath = QFileInfo{path}.absoluteFilePath();
  const auto parentDir = QFileInfo{absolutePath}.absoluteDir();
  if (!QDir{}.mkpath(parentDir.absolutePath()))
  {
    err << "Could not create output directory: " << parentDir.absolutePath() << "\n";
    return false;
  }

  QJsonArray benchmarkResults;
  for (const auto& result : results)
  {
    benchmarkResults.push_back(resultToJson(result));
  }

  const auto document = QJsonDocument{QJsonObject{
    {"format", "trenchbroom-perf-v1"},
    {"iterations", options.iterations},
    {"warmup", options.warmup},
    {"benchmarks", benchmarkResults},
  }};

  QFile file{absolutePath};
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
  {
    err << "Could not write JSON result: " << absolutePath << "\n";
    return false;
  }
  file.write(document.toJson(QJsonDocument::Indented));
  return true;
}

std::unordered_map<QString, double> readBaselineMedians(
  const QString& path, QTextStream& err)
{
  std::unordered_map<QString, double> medians;
  QFile file{path};
  if (!file.open(QIODevice::ReadOnly))
  {
    err << "Could not read baseline JSON: " << path << "\n";
    return medians;
  }

  const auto document = QJsonDocument::fromJson(file.readAll());
  const auto benchmarks = document.object().value("benchmarks").toArray();
  for (const auto& value : benchmarks)
  {
    const auto object = value.toObject();
    const auto name = object.value("name").toString();
    const auto median = object.value("medianNs").toDouble(-1.0);
    if (!name.isEmpty() && median > 0.0)
    {
      medians.emplace(name, median);
    }
  }
  return medians;
}

bool printBaselineComparison(
  const PerfOptions& options,
  const std::vector<PerfResult>& results,
  QTextStream& out,
  QTextStream& err)
{
  if (options.baselinePath.isEmpty())
  {
    return true;
  }

  const auto baseline = readBaselineMedians(options.baselinePath, err);
  if (baseline.empty())
  {
    return false;
  }

  bool passed = true;
  out << "\nBaseline comparison:\n";
  for (const auto& result : results)
  {
    const auto baselineMedian = baseline.find(result.name);
    if (baselineMedian == baseline.end())
    {
      out << "  " << result.name << ": no baseline\n";
      continue;
    }

    const auto deltaPercent =
      ((result.medianNs - baselineMedian->second) / baselineMedian->second) * 100.0;
    const auto status = deltaPercent > options.maxRegressionPercent ? "REGRESSION" : "ok";
    if (deltaPercent > options.maxRegressionPercent)
    {
      passed = false;
    }
    out << "  " << result.name << ": " << QString::number(deltaPercent, 'f', 1) << "% ("
        << status << ")\n";
  }
  return passed;
}

} // namespace

int main(int argc, char* argv[])
{
  QCoreApplication app{argc, argv};
  QTextStream out{stdout};
  QTextStream err{stderr};

  const auto parsedOptions = parseOptions(app.arguments(), err);
  if (!parsedOptions)
  {
    printUsage(err);
    return 1;
  }

  const auto options = *parsedOptions;
  const auto cases = perfCases();

  if (options.help)
  {
    printUsage(out);
    return 0;
  }

  if (options.list)
  {
    for (const auto& perfCase : cases)
    {
      out << perfCase.name << " [" << perfCase.suite << "] - " << perfCase.description
          << "\n";
    }
    return 0;
  }

  std::vector<PerfCase> selectedCases;
  std::copy_if(
    cases.begin(),
    cases.end(),
    std::back_inserter(selectedCases),
    [&](const auto& perfCase) {
      if (!options.suite.isEmpty() && perfCase.suite != options.suite)
      {
        return false;
      }
      if (!options.benchmark.isEmpty() && perfCase.name != options.benchmark)
      {
        return false;
      }
      return true;
    });

  if (selectedCases.empty())
  {
    err << "No benchmarks matched the requested filters.\n";
    return 1;
  }

  out << "Running " << selectedCases.size()
      << " benchmark(s), iterations=" << options.iterations
      << ", warmup=" << options.warmup << "\n\n";

  std::vector<PerfResult> results;
  for (const auto& perfCase : selectedCases)
  {
    const auto result = runCase(perfCase, options);
    results.push_back(result);
    out << result.name << "\n"
        << "  median " << formatNs(result.medianNs) << ", p95 " << formatNs(result.p95Ns)
        << ", min " << formatNs(result.minNs) << ", max " << formatNs(result.maxNs)
        << "\n";
  }

  const auto baselinePassed = printBaselineComparison(options, results, out, err);
  if (!baselinePassed)
  {
    return 2;
  }

  if (
    !options.jsonPath.isEmpty()
    && !writeJsonResults(options.jsonPath, options, results, err))
  {
    return 1;
  }

  if (!options.jsonPath.isEmpty())
  {
    out << "\nJSON written to " << QFileInfo{options.jsonPath}.absoluteFilePath() << "\n";
  }
  return 0;
}
