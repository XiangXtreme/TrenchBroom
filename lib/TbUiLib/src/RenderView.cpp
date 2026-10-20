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

#include "ui/RenderView.h"

#include <QDateTime>
#include <QOpenGLContext>
#include <QOpenGLFunctions_2_1>
#include <QPalette>
#include <QTimer>
#include <QWidget>

#include "base/PreferenceManager.h"
#include "gl/ActiveShader.h"
#include "gl/GlManager.h"
#include "gl/PrimType.h"
#include "gl/ResourceManager.h"
#include "gl/ShaderManager.h"
#include "gl/Shaders.h"
#include "gl/VboManager.h"
#include "gl/VertexArray.h"
#include "gl/VertexType.h"
#include "prefs/Preferences.h"
#include "render/RenderTiming.h"
#include "render/Transformation.h"
#include "ui/AppController.h"
#include "ui/GlFunctions.h"
#include "ui/GlQt.h"
#include "ui/InputEvent.h"
#include "ui/QColorUtils.h"
#include "ui/RenderProfiler.h"

#include "vm/mat.h"
#include "vm/mat_ext.h"

#include <fmt/format.h>

#include <algorithm>

namespace tb::ui
{

RenderView::RenderView(AppController& appController, QWidget* parent)
  : QOpenGLWidget{parent}
  , m_appController{appController}
  , m_renderProfiler{RenderProfiler::fromEnvironment()}
{
  connect(this, &QOpenGLWidget::aboutToCompose, this, [this]() {
    if (m_renderProfiler)
    {
      m_renderProfiler->aboutToCompose();
    }
  });
  connect(this, &QOpenGLWidget::frameSwapped, this, [this]() {
    if (m_renderProfiler)
    {
      m_renderProfiler->frameSwapped();
    }
  });
  auto pal = QPalette{};
  const auto color = pal.color(QPalette::Highlight);
  m_focusColor = fromQColor(color);

  m_frameTimer = new QTimer{this};
  m_frameTimer->setSingleShot(true);
  m_frameTimer->setTimerType(Qt::PreciseTimer);
  connect(m_frameTimer, &QTimer::timeout, this, [this]() { update(); });
  m_notifierConnection +=
    PreferenceManager::instance().preferenceDidChangeNotifier.connect(
      [this](const auto& path) {
        if (path == Preferences::RenderFpsLimit.path)
        {
          m_frameTimer->stop();
          m_frameStart.invalidate();
          update();
        }
      });

  // FPS counter
  auto* fpsCounter = new QTimer{this};

  connect(fpsCounter, &QTimer::timeout, [&]() {
    const int64_t currentTime = QDateTime::currentMSecsSinceEpoch();
    const int framesRenderedInPeriod = m_framesRendered;
    const int maxFrameTime = m_maxFrameTimeMsecs;
    const int64_t fpsCounterPeriod = currentTime - m_lastFPSCounterUpdate;
    const double avgFps =
      double(framesRenderedInPeriod) / (double(fpsCounterPeriod) / 1000.0);

    m_framesRendered = 0;
    m_maxFrameTimeMsecs = 0;
    m_lastFPSCounterUpdate = currentTime;

    m_currentFPS = fmt::format("FPS: {:.0f}  Frame: {} ms", avgFps, maxFrameTime);
  });

  fpsCounter->start(1000);

  setMouseTracking(true); // request mouse move events even when no button is held down
  setFocusPolicy(Qt::StrongFocus); // accept focus by clicking or tab

  // Update any render view when resources were processed to reflect any changes
  m_notifierConnection +=
    m_appController.glManager().resourceManager().resourcesWereProcessedNotifier.connect(
      [this](const auto&) { update(); });
}

RenderView::~RenderView()
{
  if (m_renderProfiler)
  {
    makeCurrent();
    m_renderProfiler->releaseGpuQueries();
    m_renderProfiler.reset();
    doneCurrent();
  }
}

void RenderView::paintEvent(QPaintEvent* event)
{
  const auto fpsLimit =
    std::clamp(pref(Preferences::RenderFpsLimit), 0, Preferences::MaxRenderFpsLimit);
  if (fpsLimit > 0 && m_frameStart.isValid())
  {
    const auto interval = (1000000000LL + fpsLimit - 1) / fpsLimit;
    const auto remaining = interval - m_frameStart.nsecsElapsed();
    if (remaining > 0)
    {
      if (!m_frameTimer->isActive())
      {
        m_frameTimer->start(static_cast<int>((remaining + 999999LL) / 1000000LL));
      }
      return;
    }
  }

  // Defer before Qt invalidates the OpenGL framebuffer, retaining the previous image.
  m_frameTimer->stop();
  QOpenGLWidget::paintEvent(event);
}

void RenderView::keyPressEvent(QKeyEvent* event)
{
  m_eventRecorder.recordEvent(*event);
  update();
}

void RenderView::keyReleaseEvent(QKeyEvent* event)
{
  m_eventRecorder.recordEvent(*event);
  update();
}

static auto mouseEventWithFullPrecisionLocalPos(
  const QWidget* widget, const QMouseEvent* event)
{
  // The localPos of a Qt mouse event is only in integer coordinates, but window pos
  // and screen pos have full precision. We can't directly map the windowPos because
  // mapTo takes QPoint, so we just map the origin and subtract that.
  const auto localPos =
    event->scenePosition() - QPointF{widget->mapTo(widget->window(), QPoint{0, 0})};
  return QMouseEvent{
    event->type(),
    localPos,
    event->scenePosition(),
    event->globalPosition(),
    event->button(),
    event->buttons(),
    event->modifiers(),
    event->source()};
}

void RenderView::mouseDoubleClickEvent(QMouseEvent* event)
{
  m_eventRecorder.recordEvent(mouseEventWithFullPrecisionLocalPos(this, event));
  update();
}

void RenderView::mouseMoveEvent(QMouseEvent* event)
{
  m_eventRecorder.recordEvent(mouseEventWithFullPrecisionLocalPos(this, event));
  update();
}

void RenderView::mousePressEvent(QMouseEvent* event)
{
  m_eventRecorder.recordEvent(mouseEventWithFullPrecisionLocalPos(this, event));
  update();
}

void RenderView::mouseReleaseEvent(QMouseEvent* event)
{
  m_eventRecorder.recordEvent(mouseEventWithFullPrecisionLocalPos(this, event));
  update();
}

void RenderView::wheelEvent(QWheelEvent* event)
{
  m_eventRecorder.recordEvent(*event);
  update();
}

bool RenderView::event(QEvent* event)
{
  if (event->type() == QEvent::Hide || event->type() == QEvent::Resize)
  {
    m_frameTimer->stop();
    m_frameStart.invalidate();
  }
  // Unfortunately, QWidget doesn't define a specialized handler for QNativeGestureEvent,
  // so we must override the main event handler to handle it.
  if (event->type() == QEvent::NativeGesture)
  {
    const auto gestureEvent = static_cast<QNativeGestureEvent*>(event);
    m_eventRecorder.recordEvent(*gestureEvent);
    update();
    return true;
  }

  // Let the base class handle all other events normally
  return QOpenGLWidget::event(event);
}

void RenderView::paintGL()
{
  m_frameStart.start();
  if (m_renderProfiler)
  {
    auto& timing = m_renderProfiler->beginFrame(
      *context(),
      QString::fromLatin1(metaObject()->className()),
      size() * devicePixelRatioF(),
      pref(Preferences::RenderFpsLimit));
    {
      const auto capture = render::RenderTiming::Capture{timing};
      const auto scope = render::RenderTimingScope{"cpu.frame"};
      render();
    }
    m_renderProfiler->endFrame();
  }
  else
  {
    render();
  }

  // Update stats
  m_framesRendered++;
  if (m_timeSinceLastFrame.isValid())
  {
    auto frameTime = int(m_timeSinceLastFrame.restart());
    if (frameTime > m_maxFrameTimeMsecs)
    {
      m_maxFrameTimeMsecs = frameTime;
    }
  }
  else
  {
    m_timeSinceLastFrame.start();
  }
}

gl::VboManager& RenderView::vboManager()
{
  return m_appController.glManager().vboManager();
}

gl::FontManager& RenderView::fontManager()
{
  return m_appController.glManager().fontManager();
}

gl::ShaderManager& RenderView::shaderManager()
{
  return m_appController.glManager().shaderManager();
}

int RenderView::depthBits() const
{
  const auto format = this->context()->format();
  return format.depthBufferSize();
}

bool RenderView::multisample() const
{
  const auto format = this->context()->format();
  return format.samples() != -1;
}

void RenderView::initializeGL()
{
  if (m_renderProfiler)
  {
    connect(
      context(),
      &QOpenGLContext::aboutToBeDestroyed,
      this,
      [this]() {
        if (m_renderProfiler)
        {
          makeCurrent();
          m_renderProfiler->releaseGpuQueries();
          doneCurrent();
        }
      },
      Qt::DirectConnection);
  }
  doInitializeGL();
}

void RenderView::resizeGL(int w, int h)
{
  // These are in points, not pixels
  updateViewport(0, 0, w, h);
}

void RenderView::render()
{
  auto gl = GlQt{glFunctions()};
  const auto workload = render::RenderGlScope{"gl.frame"};

  {
    const auto scope = render::RenderTimingScope{"cpu.input"};
    processInput();
  }
  {
    const auto scope = render::RenderTimingScope{"cpu.clear"};
    clearBackground(gl);
  }
  {
    const auto scope = render::RenderTimingScope{"cpu.contents"};
    renderContents(gl);
  }
  const auto overlays = render::RenderTimingScope{"cpu.focus_and_alpha"};
  renderFocusIndicator(gl);

  // Work around a Qt/Wayland bug (QTBUG-110014, QTBUG-132197, QTBUG-119214) where
  // non-opaque alpha values left in a QOpenGLWidget's framebuffer by translucent draws
  // cause the Wayland compositor to blend the whole window against the desktop behind it.
  // Force the framebuffer's alpha to fully opaque after rendering so this doesn't happen
  // regardless of the requested surface format.
  gl.pushAttrib(GL_COLOR_BUFFER_BIT);
  gl.colorMask(false, false, false, true);
  gl.clearColor(0.0f, 0.0f, 0.0f, 1.0f);
  gl.clear(GL_COLOR_BUFFER_BIT);
  gl.popAttrib();
}

void RenderView::processInput()
{
  m_eventRecorder.processEvents(*this);
}

void RenderView::clearBackground(gl::Gl& gl)
{
  const auto backgroundColor = getBackgroundColor().to<RgbaF>();

  gl.clearColor(
    backgroundColor.get<ColorChannel::r>(),
    backgroundColor.get<ColorChannel::g>(),
    backgroundColor.get<ColorChannel::b>(),
    backgroundColor.get<ColorChannel::a>());
  gl.clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

Color RenderView::getBackgroundColor()
{
  return pref(Preferences::BackgroundColor);
}

void RenderView::renderFocusIndicator(gl::Gl& gl)
{
  if (shouldRenderFocusIndicator() && hasFocus())
  {
    const auto outer = m_focusColor.to<RgbaF>();
    const auto inner = m_focusColor.to<RgbaF>();

    const auto r = devicePixelRatioF();
    const auto w = float(width() * r);
    const auto h = float(height() * r);
    gl.viewport(0, 0, int(w), int(h));

    const auto t = 1.0f;

    const auto projection = vm::ortho_matrix(-1.0f, 1.0f, 0.0f, 0.0f, float(w), float(h));
    auto transformation = render::Transformation{gl, projection, vm::mat4x4f::identity()};

    gl.disable(GL_DEPTH_TEST);

    using Vertex = gl::VertexTypes::P3C4::Vertex;
    auto array = gl::VertexArray::move(std::vector{
      // top
      Vertex{{0.0f, 0.0f, 0.0f}, outer.toVec()},
      Vertex{{w, 0.0f, 0.0f}, outer.toVec()},
      Vertex{{w - t, t, 0.0f}, inner.toVec()},
      Vertex{{t, t, 0.0f}, inner.toVec()},

      // right
      Vertex{{w, 0.0f, 0.0f}, outer.toVec()},
      Vertex{{w, h, 0.0f}, outer.toVec()},
      Vertex{{w - t, h - t, 0.0f}, inner.toVec()},
      Vertex{{w - t, t, 0.0f}, inner.toVec()},

      // bottom
      Vertex{{w, h, 0.0f}, outer.toVec()},
      Vertex{{0.0f, h, 0.0f}, outer.toVec()},
      Vertex{{t, h - t, 0.0f}, inner.toVec()},
      Vertex{{w - t, h - t, 0.0f}, inner.toVec()},

      // left
      Vertex{{0.0f, h, 0.0f}, outer.toVec()},
      Vertex{{0.0f, 0.0f, 0.0f}, outer.toVec()},
      Vertex{{t, t, 0.0f}, inner.toVec()},
      Vertex{{t, h - t, 0.0f}, inner.toVec()},
    });

    array.prepare(gl, vboManager());

    auto shader = gl::ActiveShader{gl, shaderManager(), gl::Shaders::VaryingPCShader};
    if (array.setup(gl, shader.program()))
    {
      array.render(gl, gl::PrimType::Quads);
      array.cleanup(gl, shader.program());
    }
    gl.enable(GL_DEPTH_TEST);
  }
}

QOpenGLFunctions_2_1& RenderView::glFunctions()
{
  return getGlFunctions("RenderView::glFunctions", context());
}

bool RenderView::doInitializeGL()
{
  auto gl = GlQt{glFunctions()};
  return m_appController.glManager().initialize(gl);
}

void RenderView::updateViewport(
  const int /* x */, const int /* y */, const int /* width */, const int /* height */)
{
}

} // namespace tb::ui
