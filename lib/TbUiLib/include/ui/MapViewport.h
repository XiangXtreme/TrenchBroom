#pragma once

#include <QJsonObject>

#include "vm/vec.h"

namespace tb::ui
{
class MapWindow;
QJsonObject mapViewportState(MapWindow& window);
void setMapViewportCamera(
  MapWindow& window,
  const vm::vec3f& position,
  const vm::vec3f& target,
  const vm::vec3f& up);
void focusMapViewportSelection(MapWindow& window);
} // namespace tb::ui
