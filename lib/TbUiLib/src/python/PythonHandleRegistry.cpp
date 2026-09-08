#include "ui/python/PythonHandleRegistry.h"

#include "mdl/Node.h"

#include <unordered_map>

namespace tb::ui
{
namespace
{
std::unordered_map<MapDocument*, size_t> g_documentGenerations;
std::unordered_map<mdl::Node*, size_t> g_nodeLifetimeGenerations;
} // namespace

PythonHandleRegistry::PythonHandleRegistry() = default;

PythonHandleRegistry& PythonHandleRegistry::instance()
{
  static auto instance = PythonHandleRegistry{};
  return instance;
}

size_t PythonHandleRegistry::documentGeneration(MapDocument* document)
{
  if (document == nullptr)
  {
    return 0;
  }
  return g_documentGenerations[document];
}

void PythonHandleRegistry::invalidateDocument(MapDocument* document)
{
  if (document != nullptr)
  {
    ++g_documentGenerations[document];
  }
}

size_t PythonHandleRegistry::nodeLifetimeGeneration(mdl::Node* node)
{
  if (node == nullptr)
  {
    return 0;
  }
  return g_nodeLifetimeGenerations[node];
}

void PythonHandleRegistry::invalidateNodeLifetimes(const std::vector<mdl::Node*>& nodes)
{
  for (auto* node : nodes)
  {
    if (node != nullptr)
    {
      ++g_nodeLifetimeGenerations[node];
    }
  }
}

} // namespace tb::ui
