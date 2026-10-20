/*
 Copyright (C) 2026 Kristian Duske

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

#include "ui/GlFunctions.h"

#include <QOpenGLContext>
#include <QOpenGLFunctions_2_1>
#include <QOpenGLVersionFunctionsFactory>
#include <QSurfaceFormat>

#include "ui/FileLogger.h"

#include "kd/contracts.h"

namespace tb::ui
{

void configureDefaultGlFormat()
{
  auto format = QSurfaceFormat{};
  format.setRenderableType(QSurfaceFormat::OpenGL);
  format.setVersion(2, 1);
  format.setProfile(QSurfaceFormat::CompatibilityProfile);
  format.setDepthBufferSize(24);
  // Best-effort hint; not reliably honored by Qt's Wayland QPA plugin (see
  // RenderView::render() for the fix that doesn't depend on this).
  format.setAlphaBufferSize(0);
  format.setSamples(4);
  // RenderView applies the user's FPS limit. Avoid an additional refresh-rate
  // limit when it is set to Unlimited; the platform may override this request.
  format.setSwapInterval(0);
  QSurfaceFormat::setDefaultFormat(format);
}

namespace
{

std::string_view toString(const QSurfaceFormat::OpenGLContextProfile profile)
{
  switch (profile)
  {
  case QSurfaceFormat::NoProfile:
    return "NoProfile";
  case QSurfaceFormat::CoreProfile:
    return "CoreProfile";
  case QSurfaceFormat::CompatibilityProfile:
    return "CompatibilityProfile";
  }

  return "UnknownProfile";
}

std::string_view toString(const QSurfaceFormat::RenderableType renderableType)
{
  switch (renderableType)
  {
  case QSurfaceFormat::DefaultRenderableType:
    return "DefaultRenderableType";
  case QSurfaceFormat::OpenGL:
    return "OpenGL";
  case QSurfaceFormat::OpenGLES:
    return "OpenGLES";
  case QSurfaceFormat::OpenVG:
    return "OpenVG";
  }

  return "UnknownRenderableType";
}

void logGlFunctionFactoryFailure(std::string_view callSite, QOpenGLContext* context)
{
  auto& logger = FileLogger::instance();

  if (!context)
  {
    logger.error() << callSite << ": OpenGL context is null";
    return;
  }

  const auto format = context->format();
  const auto isCurrent = (QOpenGLContext::currentContext() == context);

  logger.error() << callSite << ": OpenGL factory failed";
  logger.error() << "  QOpenGLContext is current: " << isCurrent;
  logger.error() << "  QOpenGLContext is valid: " << context->isValid();
  logger.error() << "  QOpenGLContext is OpenGL ES: " << context->isOpenGLES();
  logger.error() << "  QSurfaceFormat version: " << format.majorVersion() << "."
                 << format.minorVersion();
  logger.error() << "  QSurfaceFormat profile: " << toString(format.profile());
  logger.error() << "  QSurfaceFormat renderable type: "
                 << toString(format.renderableType());
}

} // namespace

QOpenGLFunctions_2_1& getGlFunctions(std::string_view callSite, QOpenGLContext* context)
{
  if (
    auto* functions = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_2_1>(context))
  {
    return *functions;
  }

  logGlFunctionFactoryFailure(callSite, context);
  contract_assert(false);
}

} // namespace tb::ui
