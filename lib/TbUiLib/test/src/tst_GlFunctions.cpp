/*
 Copyright (C) 2026 TrenchBroom contributors

 This file is part of TrenchBroom, licensed under the GNU General Public License,
 version 3 or (at your option) any later version. See <http://www.gnu.org/licenses/>.
 */

#include <QScopeGuard>
#include <QSurfaceFormat>

#include "ui/GlFunctions.h"

#include <catch2/catch_test_macros.hpp>

namespace tb::ui
{

TEST_CASE("configureDefaultGlFormat")
{
  const auto previous = QSurfaceFormat::defaultFormat();
  const auto restore = qScopeGuard([&]() { QSurfaceFormat::setDefaultFormat(previous); });

  configureDefaultGlFormat();

  // Application startup and native benchmarks must request the same quality
  // without introducing another FPS cap below RenderView's user preference.
  const auto format = QSurfaceFormat::defaultFormat();
  CHECK(format.renderableType() == QSurfaceFormat::OpenGL);
  CHECK(format.majorVersion() == 2);
  CHECK(format.minorVersion() == 1);
  CHECK(format.profile() == QSurfaceFormat::CompatibilityProfile);
  CHECK(format.depthBufferSize() == 24);
  CHECK(format.alphaBufferSize() == 0);
  CHECK(format.samples() == 4);
  CHECK(format.swapInterval() == 0);
}

} // namespace tb::ui
