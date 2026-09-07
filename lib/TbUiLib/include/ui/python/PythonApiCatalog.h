#pragma once

#include <compare>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

namespace tb::ui
{

enum class PythonApiType
{
  Module,
  Documents,
  Objects,
  Entities,
  Brushes,
  Faces,
  Materials,
  History,
  Actions,
  Vec3,
  Plane,
  Document,
  Selection,
  Entity,
  Brush,
  Face,
  Material,
  MaterialCollection,
  Transaction,
  PluginPanel,
};

enum class PythonApiSymbolKind
{
  Class,
  Function,
  Property,
  Method,
};

struct PythonApiValueType
{
  PythonApiType type;
  size_t sequenceDepth = 0u;

  auto operator<=>(const PythonApiValueType&) const = default;
};

struct PythonApiSymbol
{
  std::string_view name;
  PythonApiSymbolKind kind;
  std::string_view detail;
  std::optional<PythonApiValueType> resultType = std::nullopt;
};

struct PythonApiTypeInfo
{
  PythonApiType type;
  std::string_view name;
};

std::span<const PythonApiTypeInfo> pythonApiTypes();
std::span<const PythonApiSymbol> pythonApiSymbols(PythonApiType type);
std::span<const std::string_view> pythonConsoleHelperNames();

} // namespace tb::ui
