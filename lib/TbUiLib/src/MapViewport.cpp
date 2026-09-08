#include "ui/MapViewport.h"

#include <QJsonArray>

#include "gl/Camera.h"
#include "mdl/Map.h"
#include "ui/MapDocument.h"
#include "ui/MapView3D.h"
#include "ui/MapWindow.h"

#include <cmath>
#include <stdexcept>

namespace tb::ui
{
QJsonObject mapViewportState(MapWindow& window)
{
  auto* view = window.currentMapViewBase();
  const auto& camera = view->camera();
  const auto vector = [](const auto& v) { return QJsonArray{v.x(), v.y(), v.z()}; };
  return {
    {"projection", camera.perspectiveProjection() ? "perspective" : "orthographic"},
    {"position", vector(camera.position())},
    {"direction", vector(camera.direction())},
    {"up", vector(camera.up())},
    {"zoom", camera.zoom()},
    {"width", view->width()},
    {"height", view->height()},
  };
}

void setMapViewportCamera(
  MapWindow& window,
  const vm::vec3f& position,
  const vm::vec3f& target,
  const vm::vec3f& up)
{
  const auto finite = [](const auto& v) {
    return std::isfinite(v.x()) && std::isfinite(v.y()) && std::isfinite(v.z());
  };
  const auto direction = target - position;
  const auto right = vm::cross(direction, up);
  const auto directionLength = vm::squared_length(direction);
  const auto upLength = vm::squared_length(up);
  const auto rightLength = vm::squared_length(right);
  if (
    !finite(position) || !finite(target) || !finite(up) || !finite(direction)
    || !finite(right) || !std::isfinite(directionLength) || !std::isfinite(upLength)
    || !std::isfinite(rightLength) || directionLength < 1e-8f || upLength < 1e-8f
    || rightLength < 1e-8f)
  {
    throw std::invalid_argument{
      "Camera requires finite, distinct position/target and a nonparallel up vector"};
  }
  MapViewBase& view = window.activate3DMapView();
  view.stopCameraAnimation();
  view.camera().moveTo(position);
  view.camera().setDirection(vm::normalize(direction), vm::normalize(up));
  view.refreshViews();
}

void focusMapViewportSelection(MapWindow& window)
{
  if (!window.document().map().selectionBounds())
  {
    throw std::invalid_argument{"Select objects before focusing the viewport"};
  }
  MapViewBase& view = window.activate3DMapView();
  view.stopCameraAnimation();
  view.focusCameraOnSelection(false);
  view.refreshViews();
}
} // namespace tb::ui
