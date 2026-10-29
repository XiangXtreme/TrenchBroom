#pragma once

#include <QPointer>

#include <memory>
#include <string>

namespace tb::ui
{
class MapDocument;
class MapWindow;
class ProjectSession;

/** Per-call document ownership and optional view capability. Never a sticky MCP target.
 */
struct MapEditingContext
{
  std::shared_ptr<MapDocument> document;
  std::shared_ptr<ProjectSession> project;
  QPointer<MapWindow> view;
};
} // namespace tb::ui
