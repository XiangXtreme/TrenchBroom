#include "ui/MapViewport.h"

#include <QJsonArray>
#include <QStringList>

#include "base/PreferenceManager.h"
#include "gl/Camera.h"
#include "mdl/Grid.h"
#include "mdl/Map.h"
#include "prefs/Preferences.h"
#include "ui/MapDocument.h"
#include "ui/MapView3D.h"
#include "ui/MapWindow.h"

#include <array>
#include <cmath>
#include <stdexcept>

namespace tb::ui
{
namespace
{
struct ViewBooleanOption
{
  const char* name;
  Preference<bool>* preference;
};

const auto BooleanOptions = std::array{
  ViewBooleanOption{"show_edges", &Preferences::ShowEdges},
  ViewBooleanOption{"shade_faces", &Preferences::ShadeFaces},
  ViewBooleanOption{"show_fog", &Preferences::ShowFog},
  ViewBooleanOption{"show_sky", &Preferences::ShowSky},
  ViewBooleanOption{"show_entity_classnames", &Preferences::ShowEntityClassnames},
  ViewBooleanOption{"show_group_bounds", &Preferences::ShowGroupBounds},
  ViewBooleanOption{"show_brush_entity_bounds", &Preferences::ShowBrushEntityBounds},
  ViewBooleanOption{"show_point_entity_bounds", &Preferences::ShowPointEntityBounds},
  ViewBooleanOption{"show_point_entity_models", &Preferences::ShowPointEntityModels},
  ViewBooleanOption{"show_point_entities", &Preferences::ShowPointEntities},
  ViewBooleanOption{"show_brushes", &Preferences::ShowBrushes},
};
} // namespace

QJsonObject mapViewportOptions(MapWindow& window)
{
  auto result = QJsonObject{
    {"show_grid", window.document().map().grid().visible()},
    {"face_render_mode", QString::fromStdString(pref(Preferences::FaceRenderMode))},
    {"entity_link_mode", QString::fromStdString(pref(Preferences::EntityLinkMode))},
  };
  for (const auto& option : BooleanOptions)
    result.insert(option.name, pref(*option.preference));
  return result;
}

void setMapViewportOptions(MapWindow& window, const QJsonObject& options)
{
  const auto before = mapViewportOptions(window);
  // Validate the whole patch before changing preferences or document grid state.
  for (auto it = options.begin(); it != options.end(); ++it)
  {
    if (!before.contains(it.key()) || before.value(it.key()).type() != it.value().type())
      throw std::invalid_argument{
        "Unknown viewport option or incorrect value type: " + it.key().toStdString()};
    if (
      it.key() == "face_render_mode"
      && !QStringList{"textured", "flat", "skip"}.contains(it.value().toString()))
      throw std::invalid_argument{"face_render_mode must be textured, flat, or skip"};
    if (
      it.key() == "entity_link_mode"
      && !QStringList{"all", "transitive", "direct", "none"}.contains(
        it.value().toString()))
      throw std::invalid_argument{
        "entity_link_mode must be all, transitive, direct, or none"};
  }
  for (const auto& option : BooleanOptions)
    if (options.contains(option.name))
      setPref(*option.preference, options.value(option.name).toBool());
  if (options.contains("face_render_mode"))
    setPref(
      Preferences::FaceRenderMode,
      options.value("face_render_mode").toString().toStdString());
  if (options.contains("entity_link_mode"))
    setPref(
      Preferences::EntityLinkMode,
      options.value("entity_link_mode").toString().toStdString());
  auto& grid = window.document().map().grid();
  if (
    options.contains("show_grid")
    && options.value("show_grid").toBool() != grid.visible())
    grid.toggleVisible();
  window.currentMapViewBase()->refreshViews();
}

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
    {"options", mapViewportOptions(window)},
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
