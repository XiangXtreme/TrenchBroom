/*
 Copyright (C) 2010 Kristian Duske

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

#include "ui/MapView3D.h"

#include <QApplication>
#include <QCursor>
#include <QEventLoop>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QProgressDialog>
#include <QWheelEvent>

#include "base/PreferenceManager.h"
#include "gl/PerspectiveCamera.h"
#include "mdl/BezierPatch.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushNode.h"
#include "mdl/EntityNode.h"
#include "mdl/Grid.h"
#include "mdl/GroupNode.h"
#include "mdl/HitAdapter.h"
#include "mdl/HitFilter.h"
#include "mdl/LayerNode.h"
#include "mdl/Map_Picking.h"
#include "mdl/PatchNode.h"
#include "mdl/PickResult.h"
#include "mdl/PointTrace.h"
#include "mdl/WorldNode.h"
#include "prefs/Preferences.h"
#include "render/BoundsGuideRenderer.h"
#include "render/Compass3D.h"
#include "render/MapRenderer.h"
#include "render/RenderBatch.h"
#include "render/RenderContext.h"
#include "render/SelectionBoundsRenderer.h"
#include "ui/AnimationManager.h"
#include "ui/AssembleBrushToolController3D.h"
#include "ui/CameraAnimation.h"
#include "ui/CameraTool3D.h"
#include "ui/ChamferTool.h"
#include "ui/ChamferToolController.h"
#include "ui/ClipToolController.h"
#include "ui/ControlPointTool.h"
#include "ui/ControlPointToolController.h"
#include "ui/CreateEntityToolController.h"
#include "ui/DrawShapeToolController3D.h"
#include "ui/EdgeTool.h"
#include "ui/EdgeToolController.h"
#include "ui/ExtrudeToolController.h"
#include "ui/FaceTool.h" // IWYU pragma: keep
#include "ui/FaceToolController.h"
#include "ui/FlyModeHelper.h"
#include "ui/MapDocument.h"
#include "ui/MapViewToolBox.h"
#include "ui/MoveObjectsToolController.h"
#include "ui/PathToolController.h"
#include "ui/PlayModeController.h"
#include "ui/PlayModeRawMouse.h"
#include "ui/PrefabToolController.h"
#include "ui/RotateToolController.h"
#include "ui/ScaleToolController.h"
#include "ui/SelectionTool.h"
#include "ui/SetBrushFaceAttributesTool.h"
#include "ui/ShearToolController.h"
#include "ui/SweepToolController.h"
#include "ui/VertexTool.h"
#include "ui/VertexToolController.h"

#include "kd/contracts.h"
#include "kd/set_temp.h"

#include "vm/util.h"

#include <algorithm>
#include <cmath>

namespace tb::ui
{

MapView3D::MapView3D(
  AppController& appController, MapDocument& document, MapViewToolBox& toolBox)
  : MapViewBase{appController, document, toolBox}
  , m_camera{std::make_unique<gl::PerspectiveCamera>()}
  , m_flyModeHelper{std::make_unique<FlyModeHelper>(*m_camera)}
  , m_playMode{std::make_unique<PlayModeController>(document, *m_camera)}
  , m_rawMouse{std::make_unique<PlayModeRawMouse>(
      [&](const double dx, const double dy, const double timestampMs) {
        if (m_playMode->active())
        {
          m_playMode->rawLook(dx, dy, timestampMs);
        }
      })}
{
  bindEvents();
  connectObservers();
  initializeCamera();
  initializeToolChain(toolBox);

  m_camera->setFov(pref(Preferences::CameraFov));

  mapViewBaseVirtualInit();
}

MapView3D::~MapView3D()
{
  stopPlayMode();
}

QImage MapView3D::grabPrefabThumbnailFramebuffer(const std::vector<mdl::Node*>& nodes)
{
  const auto oldPosition = m_camera->position();
  const auto oldDirection = m_camera->direction();
  const auto oldUp = m_camera->up();
  const auto oldZoom = m_camera->zoom();

  if (!nodes.empty())
  {
    moveCameraToPosition(focusCameraOnObjectsPosition(nodes), false);
  }
  setRenderingThumbnail(true);
  repaint();

  auto image = grabFramebuffer();

  setRenderingThumbnail(false);
  m_camera->moveTo(oldPosition);
  m_camera->setDirection(oldDirection, oldUp);
  m_camera->setZoom(oldZoom);
  return image;
}

const gl::PerspectiveCamera& MapView3D::perspectiveCamera() const
{
  return *m_camera;
}

bool MapView3D::playModeActive() const
{
  return m_playMode->active();
}

void MapView3D::startPlayMode(const bool fromCamera)
{
  if (m_playMode->active() || m_playMode->starting())
  {
    return;
  }

  m_flyModeHelper->resetKeys();
  cancelMouseDrag();

  m_playModeBuildProgress = new QProgressDialog{this};
  m_playModeBuildProgress->setWindowTitle(tr("Play Mode"));
  m_playModeBuildProgress->setLabelText(tr("Compiling collision hulls..."));
  m_playModeBuildProgress->setRange(0, 0);
  m_playModeBuildProgress->setCancelButton(nullptr);
  m_playModeBuildProgress->setWindowModality(Qt::WindowModal);
  m_playModeBuildProgress->setMinimumDuration(0);
  m_playModeBuildProgress->show();
  m_playModeBuildProgress->raise();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

  m_playMode->start(fromCamera, [this](const bool started) {
    if (m_playModeBuildProgress)
    {
      m_playModeBuildProgress->close();
      m_playModeBuildProgress->deleteLater();
      m_playModeBuildProgress = nullptr;
    }
    if (!started)
    {
      return;
    }

    m_mouseTrackingBeforePlay = hasMouseTracking();
    setMouseTracking(true);
    setCursor(Qt::BlankCursor);
    setFocus(Qt::OtherFocusReason);

    const auto raw = pref(Preferences::PlayModeRawInput);
    const auto backend = m_rawMouse->start(*this, raw);
    m_warpCursor = true;
    m_grabbedMouse = !m_rawMouse->confinesPointer();
    if (m_grabbedMouse)
    {
      grabMouse();
    }

    const auto motion = std::string{raw ? "raw" : "accelerated"};
    const auto confined = m_rawMouse->confinesPointer() ? "" : ", not confined";
    switch (backend)
    {
    case PlayModeRawMouse::Backend::Wayland:
      m_playMode->setInputDescription(motion + ", Wayland locked pointer");
      break;
    case PlayModeRawMouse::Backend::XInput2:
      m_playMode->setInputDescription(motion + ", XInput2" + confined);
      break;
    case PlayModeRawMouse::Backend::RawInput:
      m_playMode->setInputDescription(motion + ", Raw Input" + confined);
      break;
    case PlayModeRawMouse::Backend::None:
      m_playMode->setInputDescription(
        std::string{"cursor warping (accelerated)"}
        + (m_rawMouse->confinesPointer() ? ", confined" : ""));
      break;
    }

    m_wheelAccumulator = 0;
    m_lastPlayMousePos = QCursor::pos() - mapToGlobal(QPoint{0, 0});
    m_awaitingCursorWarp = false;
    recenterPlayCursor();
    update();
  });
}

void MapView3D::recenterPlayCursor()
{
  if (m_warpCursor)
  {
    QCursor::setPos(mapToGlobal(rect().center()));
    m_lastPlayMousePos = QPointF{rect().center()};
    m_awaitingCursorWarp = true;
  }
}

void MapView3D::stopPlayMode()
{
  if (!m_playMode->active())
  {
    return;
  }

  m_playMode->stop();
  m_rawMouse->stop();
  if (m_grabbedMouse)
  {
    releaseMouse();
    m_grabbedMouse = false;
  }
  unsetCursor();
  setMouseTracking(m_mouseTrackingBeforePlay);
  update();
}

void MapView3D::initializeCamera()
{
  m_camera->moveTo(vm::vec3f{-80.0f, -128.0f, 96.0f});
  m_camera->lookAt(vm::vec3f{0, 0, 0}, vm::vec3f{0, 0, 1});
}

void MapView3D::initializeToolChain(MapViewToolBox& toolBox)
{
  addToolController(std::make_unique<CameraTool3D>(*m_camera));
  addToolController(
    std::make_unique<MoveObjectsToolController>(toolBox.moveObjectsTool()));
  addToolController(std::make_unique<RotateToolController3D>(toolBox.rotateTool()));
  addToolController(std::make_unique<SweepToolController3D>(toolBox.sweepTool()));
  addToolController(std::make_unique<ScaleToolController3D>(toolBox.scaleTool()));
  addToolController(std::make_unique<ShearToolController3D>(toolBox.shearTool()));
  addToolController(std::make_unique<ExtrudeToolController3D>(toolBox.extrudeTool()));
  addToolController(
    std::make_unique<AssembleBrushToolController3D>(toolBox.assembleBrushTool()));
  addToolController(std::make_unique<ClipToolController3D>(toolBox.clipTool()));
  addToolController(std::make_unique<VertexToolController>(toolBox.vertexTool()));
  addToolController(std::make_unique<EdgeToolController>(toolBox.edgeTool()));
  addToolController(std::make_unique<ChamferToolController>(toolBox.chamferTool()));
  addToolController(
    std::make_unique<VertexToolController>(toolBox.chamferTool().vertexTool()));
  addToolController(
    std::make_unique<EdgeToolController>(toolBox.chamferTool().edgeTool()));
  addToolController(std::make_unique<FaceToolController>(toolBox.faceTool()));
  addToolController(
    std::make_unique<ControlPointToolController>(toolBox.controlPointTool()));
  addToolController(
    std::make_unique<CreateEntityToolController3D>(toolBox.createEntityTool()));
  addToolController(std::make_unique<PrefabToolController3D>(toolBox.prefabTool()));
  addToolController(std::make_unique<SetBrushFaceAttributesTool>(m_document));
  addToolController(std::make_unique<PathToolController>(toolBox.pathTool()));
  addToolController(std::make_unique<SelectionTool>(m_document));
  addToolController(
    std::make_unique<DrawShapeToolController3D>(toolBox.drawShapeTool(), m_document));
}

void MapView3D::connectObservers()
{
  m_notifierConnection +=
    m_camera->cameraDidChangeNotifier.connect(this, &MapView3D::cameraDidChange);

  auto& prefs = PreferenceManager::instance();
  m_notifierConnection +=
    prefs.preferenceDidChangeNotifier.connect(this, &MapView3D::preferenceDidChange);
}

void MapView3D::cameraDidChange(const gl::Camera& /* camera */)
{
  if (!m_ignoreCameraChangeEvents)
  {
    // Don't refresh if the camera was changed in preRender!
    update();
  }
}

void MapView3D::preferenceDidChange(const std::filesystem::path& path)
{
  if (path == Preferences::CameraFov.path)
  {
    m_camera->setFov(pref(Preferences::CameraFov));
    update();
  }
}

bool MapView3D::event(QEvent* event)
{
  if (m_playMode->active() && event->type() == QEvent::ShortcutOverride)
  {
    // While playing, every key belongs to the game: accepting the override delivers it
    // as a key press instead of triggering editor shortcuts (E, R, Space, ...).
    event->accept();
    return true;
  }

  return MapViewBase::event(event);
}

void MapView3D::keyPressEvent(QKeyEvent* event)
{
  if (m_playMode->active())
  {
    if (
      !event->isAutoRepeat()
      && (event->key() == Qt::Key_Escape || event->key() == Qt::Key_F7))
    {
      stopPlayMode();
    }
    else
    {
      m_playMode->keyDown(*event);
    }
    return;
  }

  m_flyModeHelper->keyDown(event);

  MapViewBase::keyPressEvent(event);
}

void MapView3D::keyReleaseEvent(QKeyEvent* event)
{
  if (m_playMode->active())
  {
    m_playMode->keyUp(*event);
    return;
  }

  m_flyModeHelper->keyUp(event);

  MapViewBase::keyReleaseEvent(event);
}

void MapView3D::mouseMoveEvent(QMouseEvent* event)
{
  if (!m_playMode->active())
  {
    MapViewBase::mouseMoveEvent(event);
    return;
  }

  const auto position = event->position();
  const auto center = QPointF{rect().center()};
  const auto recenterDistance = 0.25 * static_cast<double>(std::min(width(), height()));
  const auto distanceToCenter = [&]() {
    const auto d = position - center;
    return std::sqrt(d.x() * d.x() + d.y() * d.y());
  };

  if (m_awaitingCursorWarp)
  {
    // Motion events queued before the cursor was warped are relative to the old cursor
    // position; using them would count that movement twice.
    if (distanceToCenter() > 0.5 * recenterDistance)
    {
      return;
    }
    m_awaitingCursorWarp = false;
  }

  const auto delta = position - m_lastPlayMousePos;
  m_lastPlayMousePos = position;
  if (!delta.isNull() && m_rawMouse->backend() == PlayModeRawMouse::Backend::None)
  {
    // Mouse counts correspond to device pixels.
    const auto scale = devicePixelRatioF();
    m_playMode->look(
      *event,
      static_cast<float>(delta.x() * scale),
      static_cast<float>(delta.y() * scale));
  }

  // Warp rarely: only when the cursor gets close to the edge of the view.
  if (distanceToCenter() > recenterDistance)
  {
    recenterPlayCursor();
  }
}

void MapView3D::mousePressEvent(QMouseEvent* event)
{
  if (!m_playMode->active())
  {
    MapViewBase::mousePressEvent(event);
  }
}

void MapView3D::mouseReleaseEvent(QMouseEvent* event)
{
  if (!m_playMode->active())
  {
    MapViewBase::mouseReleaseEvent(event);
  }
}

void MapView3D::mouseDoubleClickEvent(QMouseEvent* event)
{
  if (!m_playMode->active())
  {
    MapViewBase::mouseDoubleClickEvent(event);
  }
}

void MapView3D::wheelEvent(QWheelEvent* event)
{
  if (!m_playMode->active())
  {
    MapViewBase::wheelEvent(event);
    return;
  }

  // Every wheel notch (120 units) is one +action/-action pair, like a wheel bind in the
  // game. High resolution wheels report fractions of a notch, which are accumulated.
  constexpr auto Notch = 120;
  m_wheelAccumulator += event->angleDelta().y();
  while (std::abs(m_wheelAccumulator) >= Notch)
  {
    const auto up = m_wheelAccumulator > 0;
    m_playMode->wheel(*event, up);
    m_wheelAccumulator -= up ? Notch : -Notch;
  }
  event->accept();
}

void MapView3D::focusInEvent(QFocusEvent* event)
{
  m_flyModeHelper->resetKeys();

  MapViewBase::focusInEvent(event);
}

void MapView3D::focusOutEvent(QFocusEvent* event)
{
  m_flyModeHelper->resetKeys();
  stopPlayMode();

  MapViewBase::focusOutEvent(event);
}

void MapView3D::initializeGL()
{
  MapViewBase::initializeGL();
  setCompass(std::make_unique<render::Compass3D>());
}

void MapView3D::bindEvents()
{
  // Fly mode animation
  connect(this, &QOpenGLWidget::frameSwapped, this, &MapView3D::updateFlyMode);
}

void MapView3D::updateFlyMode()
{
  if (m_flyModeHelper->anyKeyDown() || m_playMode->active())
  {
    update();
  }
}

void MapView3D::resetFlyModeKeys()
{
  m_flyModeHelper->resetKeys();
}

PickRequest MapView3D::pickRequest(const float x, const float y) const
{
  return {vm::ray3d{m_camera->pickRay(x, y)}, *m_camera};
}

mdl::PickResult MapView3D::pick(const vm::ray3d& pickRay) const
{
  auto& map = m_document.map();
  auto pickResult = mdl::PickResult::byDistance();

  mdl::pick(map, pickRay, pickResult);
  return pickResult;
}

void MapView3D::updateViewport(
  const int x, const int y, const int width, const int height)
{
  m_camera->setViewport({x, y, width, height});
}

vm::vec3d MapView3D::pasteObjectsDelta(
  const vm::bbox3d& bounds, const vm::bbox3d& /* referenceBounds */) const
{
  using namespace mdl::HitFilters;

  auto& map = m_document.map();
  const auto& grid = map.grid();

  const auto pos = QCursor::pos();
  const auto clientCoords = mapFromGlobal(pos);

  if (QRect{0, 0, width(), height()}.contains(clientCoords))
  {
    const auto pickRay =
      vm::ray3d{m_camera->pickRay(float(clientCoords.x()), float(clientCoords.y()))};
    auto pickResult = mdl::PickResult::byDistance();

    mdl::pick(map, pickRay, pickResult);

    const auto& hit = pickResult.first(type(mdl::BrushNode::BrushHitType));
    if (const auto faceHandle = mdl::hitToFaceHandle(hit))
    {
      const auto& face = faceHandle->face();
      return grid.moveDeltaForBounds(face.boundary(), bounds, map.worldBounds(), pickRay);
    }
    else
    {
      const auto point = vm::vec3d{grid.snap(m_camera->defaultPoint(pickRay))};
      const auto targetPlane = vm::plane3d{point, -vm::vec3d{m_camera->direction()}};
      return grid.moveDeltaForBounds(targetPlane, bounds, map.worldBounds(), pickRay);
    }
  }
  else
  {
    const auto oldMin = bounds.min;
    const auto oldCenter = bounds.center();
    const auto newCenter = vm::vec3d{m_camera->defaultPoint()};
    const auto newMin = oldMin + (newCenter - oldCenter);
    return grid.snap(newMin);
  }
}

bool MapView3D::canSelectTall()
{
  return false;
}

void MapView3D::selectTall() {}

void MapView3D::reset2dCameras(const gl::Camera&, const bool)
{
  // nothing to do
}

void MapView3D::focusCameraOnSelection(const bool animate)
{
  const auto& map = m_document.map();
  if (const auto& nodes = map.selection().nodes; !nodes.empty())
  {
    const auto newPosition = focusCameraOnObjectsPosition(nodes);
    moveCameraToPosition(newPosition, animate);
  }
}

namespace
{

vm::vec3f computeCameraTargetPosition(const std::vector<mdl::Node*>& nodes)
{
  auto center = vm::vec3f{};
  size_t count = 0u;

  const auto handlePoint = [&](const auto& point) {
    center = center + vm::vec3f{point};
    ++count;
  };

  mdl::Node::visitAll(
    nodes,
    kdl::overload(
      [](auto&& thisLambda, mdl::WorldNode& worldNode) {
        worldNode.visitChildren(thisLambda);
      },
      [](auto&& thisLambda, mdl::LayerNode& layerNode) {
        layerNode.visitChildren(thisLambda);
      },
      [](auto&& thisLambda, mdl::GroupNode& groupNode) {
        groupNode.visitChildren(thisLambda);
      },
      [&](auto&& thisLambda, mdl::EntityNode& entityNode) {
        if (!entityNode.hasChildren())
        {
          entityNode.logicalBounds().for_each_vertex(handlePoint);
        }
        else
        {
          entityNode.visitChildren(thisLambda);
        }
      },
      [&](mdl::BrushNode& brushNode) {
        for (const auto* vertex : brushNode.brush().vertices())
        {
          handlePoint(vertex->position());
        }
      },
      [&](mdl::PatchNode& patchNode) {
        for (const auto& controlPoint : patchNode.patch().controlPoints())
        {
          handlePoint(controlPoint.xyz());
        }
      }));

  return center / float(count);
}

float computeCameraOffset(const gl::Camera& camera, const std::vector<mdl::Node*>& nodes)
{
  vm::plane3f frustumPlanes[4];
  camera.frustumPlanes(
    frustumPlanes[0], frustumPlanes[1], frustumPlanes[2], frustumPlanes[3]);

  auto offset = std::numeric_limits<float>::min();
  const auto handlePoint = [&](const vm::vec3d& point, const vm::plane3f& plane) {
    const auto ray = vm::ray3f{camera.position(), -camera.direction()};
    const auto newPlane =
      vm::plane3f{vm::vec3f{point} + 64.0f * plane.normal, plane.normal};
    if (const auto dist = vm::intersect_ray_plane(ray, newPlane); dist && *dist > 0.0f)
    {
      offset = std::max(offset, *dist);
    }
  };

  mdl::Node::visitAll(
    nodes,
    kdl::overload(
      [](auto&& thisLambda, mdl::WorldNode& worldNode) {
        worldNode.visitChildren(thisLambda);
      },
      [](auto&& thisLambda, mdl::LayerNode& layerNode) {
        layerNode.visitChildren(thisLambda);
      },
      [](auto&& thisLambda, mdl::GroupNode& groupNode) {
        groupNode.visitChildren(thisLambda);
      },
      [&](auto&& thisLambda, mdl::EntityNode& entityNode) {
        if (!entityNode.hasChildren())
        {
          for (size_t i = 0u; i < 4u; ++i)
          {
            entityNode.logicalBounds().for_each_vertex(
              [&](const auto& point) { handlePoint(point, frustumPlanes[i]); });
          }
        }
        else
        {
          entityNode.visitChildren(thisLambda);
        }
      },
      [&](mdl::BrushNode& brushNode) {
        for (const auto* vertex : brushNode.brush().vertices())
        {
          for (size_t i = 0u; i < 4u; ++i)
          {
            handlePoint(vertex->position(), frustumPlanes[i]);
          }
        }
      },
      [&](mdl::PatchNode& patchNode) {
        for (const auto& controlPoint : patchNode.patch().controlPoints())
        {
          for (size_t i = 0u; i < 4u; ++i)
          {
            handlePoint(controlPoint.xyz(), frustumPlanes[i]);
          }
        }
      }));

  return offset;
}

} // namespace

vm::vec3f MapView3D::focusCameraOnObjectsPosition(const std::vector<mdl::Node*>& nodes)
{
  const auto newPosition = computeCameraTargetPosition(nodes);

  // act as if the camera were there already:
  const auto oldPosition = m_camera->position();
  m_camera->moveTo(vm::vec3f{newPosition});

  const auto offset = computeCameraOffset(*m_camera, nodes);

  // jump back
  m_camera->moveTo(oldPosition);
  return newPosition - m_camera->direction() * offset;
}

void MapView3D::moveCameraToPosition(const vm::vec3f& position, const bool animate)
{
  if (animate)
  {
    animateCamera(position, m_camera->direction(), m_camera->up(), m_camera->zoom());
  }
  else
  {
    m_camera->moveTo(position);
  }
}

void MapView3D::animateCamera(
  const vm::vec3f& position,
  const vm::vec3f& direction,
  const vm::vec3f& up,
  const float zoom,
  const int duration)
{
  auto animation =
    std::make_unique<CameraAnimation>(*m_camera, position, direction, up, zoom, duration);
  m_animationManager->runAnimation(std::move(animation), true);
}

void MapView3D::moveCameraToCurrentTracePoint()
{
  contract_pre(m_document.isPointFileLoaded());

  if (const auto* pointTrace = m_document.pointTrace())
  {
    const auto position = pointTrace->currentPoint() + vm::vec3f{0.0f, 0.0f, 16.0f};
    const auto direction = pointTrace->currentDirection();
    animateCamera(position, direction, vm::vec3f{0, 0, 1}, m_camera->zoom());
  }
}

gl::Camera& MapView3D::camera()
{
  return *m_camera;
}

vm::vec3d MapView3D::moveDirection(const vm::direction direction) const
{
  switch (direction)
  {
  case vm::direction::forward: {
    const auto plane = vm::plane3d{vm::vec3d{m_camera->position()}, vm::vec3d{0, 0, 1}};
    const auto projectedDirection =
      plane.project_vector(vm::vec3d{m_camera->direction()});
    if (vm::is_zero(projectedDirection, vm::Cd::almost_zero()))
    {
      // camera is looking straight down or up
      if (m_camera->direction().z() < 0.0f)
      {
        return vm::vec3d{vm::get_abs_max_component_axis(m_camera->up())};
      }
      else
      {
        return vm::vec3d{-vm::get_abs_max_component_axis(m_camera->up())};
      }
    }
    return vm::get_abs_max_component_axis(projectedDirection);
  }
  case vm::direction::backward:
    return -moveDirection(vm::direction::forward);
  case vm::direction::left:
    return -moveDirection(vm::direction::right);
  case vm::direction::right: {
    auto dir = vm::vec3d(vm::get_abs_max_component_axis(m_camera->right()));
    if (dir == moveDirection(vm::direction::forward))
    {
      dir = vm::cross(dir, vm::vec3d{0, 0, 1});
    }
    return dir;
  }
  case vm::direction::up:
    return vm::vec3d{0, 0, 1};
  case vm::direction::down:
    return vm::vec3d{0, 0, -1};
    switchDefault();
  }
}

size_t MapView3D::flipAxis(const vm::direction direction) const
{
  return vm::find_abs_max_component(moveDirection(direction));
}

vm::vec3d MapView3D::computePointEntityPosition(const vm::bbox3d& bounds) const
{
  using namespace mdl::HitFilters;

  const auto& map = m_document.map();

  const auto& grid = map.grid();
  const auto& worldBounds = map.worldBounds();

  const auto& hit = pickResult().first(type(mdl::BrushNode::BrushHitType));
  if (const auto faceHandle = mdl::hitToFaceHandle(hit))
  {
    const auto& face = faceHandle->face();
    return grid.moveDeltaForBounds(face.boundary(), bounds, worldBounds, pickRay());
  }
  else
  {
    const auto newPosition = gl::Camera::defaultPoint(pickRay());
    const auto defCenter = bounds.center();
    return grid.moveDeltaForPoint(defCenter, newPosition - defCenter);
  }
}

ActionContext::Type MapView3D::viewActionContext() const
{
  return ActionContext::View3D;
}
void MapView3D::preRender()
{
  const auto ignoreCameraUpdates = kdl::set_temp{m_ignoreCameraChangeEvents};
  if (m_playMode->active())
  {
    m_rawMouse->poll();
    m_playMode->update();
  }
  else
  {
    m_flyModeHelper->pollAndUpdate();
  }
}

render::RenderMode MapView3D::renderMode()
{
  return render::RenderMode::Render3D;
}

void MapView3D::renderMap(
  render::MapRenderer& renderer,
  render::RenderContext& renderContext,
  render::RenderBatch& renderBatch)
{
  renderer.render(renderContext, renderBatch);

  const auto& map = m_document.map();
  if (const auto& bounds = map.selectionBounds();
      bounds && renderContext.showSelectionGuide())
  {
    auto boundsRenderer = render::SelectionBoundsRenderer{*bounds};
    boundsRenderer.render(renderContext, renderBatch);

    auto* guideRenderer = new render::BoundsGuideRenderer{};
    guideRenderer->setColor(pref(Preferences::SelectionBoundsColor));
    guideRenderer->setBounds(*bounds);
    renderBatch.addOneShot(guideRenderer);
  }
}

void MapView3D::renderTools(
  MapViewToolBox& /* toolBox */,
  render::RenderContext& renderContext,
  render::RenderBatch& renderBatch)
{
  if (m_playMode->active())
  {
    m_playMode->render(renderContext, renderBatch);
    return;
  }

  ToolBoxConnector::renderTools(renderContext, renderBatch);
}

void MapView3D::beforePopupMenu()
{
  m_flyModeHelper->resetKeys();
}

void MapView3D::linkCamera(CameraLinkHelper& /* helper */) {}

} // namespace tb::ui
