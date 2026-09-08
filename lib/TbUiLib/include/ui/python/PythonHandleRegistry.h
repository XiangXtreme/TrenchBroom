#pragma once

#include <cstddef>
#include <vector>

namespace tb::mdl
{
class Node;
}

namespace tb::ui
{
class MapDocument;

class PythonHandleRegistry
{
private:
  PythonHandleRegistry();

public:
  static PythonHandleRegistry& instance();

  size_t documentGeneration(MapDocument* document);
  void invalidateDocument(MapDocument* document);

  size_t nodeLifetimeGeneration(mdl::Node* node);
  void invalidateNodeLifetimes(const std::vector<mdl::Node*>& nodes);
};

} // namespace tb::ui
