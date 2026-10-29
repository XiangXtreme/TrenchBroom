#include "ui/python/PythonHandleRegistry.h"

#include "base/NotifierConnection.h"
#include "mdl/Node.h"
#include "ui/MapDocument.h"

#include <unordered_map>

namespace tb::ui
{
namespace
{
std::unordered_map<MapDocument*, size_t> g_documentGenerations;
size_t g_nextDocumentGeneration = 1;
struct TrackedDocument
{
  const mdl::Map* map = nullptr;
  NotifierConnection connection;
};
std::unordered_map<MapDocument*, TrackedDocument> g_trackedDocuments;
std::unordered_map<mdl::Node*, size_t> g_nodeLifetimeGenerations;
} // namespace

PythonHandleRegistry::PythonHandleRegistry() = default;

PythonHandleRegistry& PythonHandleRegistry::instance()
{
  static auto instance = PythonHandleRegistry{};
  return instance;
}

void PythonHandleRegistry::trackDocument(MapDocument& document)
{
  if (g_trackedDocuments.contains(&document))
    return;
  invalidateDocument(&document);
  auto& tracked = g_trackedDocuments[&document];
  tracked.map = &document.map();
  tracked.connection += document.documentWasLoadedNotifier.connect([this, &document]() {
    auto& state = g_trackedDocuments.at(&document);
    if (state.map != &document.map())
    {
      state.map = &document.map();
      invalidateDocument(&document);
    }
  });
  tracked.connection +=
    document.documentWillBeDestroyedNotifier.connect([this, &document]() {
      invalidateDocument(&document);
      g_trackedDocuments.erase(&document);
    });
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
    g_documentGenerations[document] = g_nextDocumentGeneration++;
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
