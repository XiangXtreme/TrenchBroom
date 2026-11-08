/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/MaterialPreview.h"

#include "base/PreferenceManager.h"
#include "gl/ActiveShader.h"
#include "gl/Material.h"
#include "gl/MaterialManager.h"
#include "gl/MaterialRenderFunc.h"
#include "gl/PrimType.h"
#include "gl/Shaders.h"
#include "gl/Texture.h"
#include "gl/VertexArray.h"
#include "gl/VertexType.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushFaceHandle.h"
#include "mdl/Map.h"
#include "prefs/Preferences.h"
#include "render/Transformation.h"
#include "ui/MapDocument.h"
#include "ui/Theme.h"

#include "vm/mat_ext.h"

#include <algorithm>
namespace tb::ui
{
MaterialPreview::MaterialPreview(
  AppController& appController, MapDocument& document, QWidget* parent)
  : RenderView{appController, parent}
  , m_document{document}
{
  setObjectName("MaterialPreview");
  setFixedSize(48, 48);
  setFocusPolicy(Qt::NoFocus);
  setAttribute(Qt::WA_TransparentForMouseEvents);
  const auto refresh = [this](const auto&...) { update(); };
  m_connection += document.selectionDidChangeNotifier.connect(refresh);
  m_connection += document.documentDidChangeNotifier.connect(refresh);
  m_connection += document.currentMaterialNameDidChangeNotifier.connect(refresh);
  m_connection += document.resourcesWereProcessedNotifier.connect(refresh);
}
const gl::Material* MaterialPreview::material() const
{
  const auto& map = m_document.map();
  const auto faces = map.selection().allBrushFaces();
  if (faces.empty())
    return map.materialManager().material(map.currentMaterialName());
  const auto* first = faces.front().face().material();
  return std::ranges::all_of(
           faces, [first](const auto& face) { return face.face().material() == first; })
           ? first
           : nullptr;
}
bool MaterialPreview::shouldRenderFocusIndicator() const
{
  return false;
}
Color MaterialPreview::getBackgroundColor()
{
  return browserBackgroundColor(palette());
}
void MaterialPreview::renderContents(gl::Gl& gl)
{
  gl.viewport(
    0, 0, int(width() * devicePixelRatioF()), int(height() * devicePixelRatioF()));
  gl.disable(GL_DEPTH_TEST);
  gl.disable(GL_CULL_FACE);
  gl.enable(GL_BLEND);
  gl.blendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  const auto* current = material();
  const auto* texture = getTexture(current);
  if (!texture || !texture->isReady())
    return;
  const auto aspect = float(texture->width()) / float(texture->height());
  const auto x = std::min(1.0f, aspect);
  const auto y = std::min(1.0f, 1.0f / aspect);
  const auto transformation =
    render::Transformation{gl, vm::mat4x4f::identity(), vm::mat4x4f::identity()};
  auto shader = gl::ActiveShader{gl, shaderManager(), gl::Shaders::MaterialBrowserShader};
  shader.set("ApplyTinting", false);
  shader.set("Material", 0);
  shader.set("Brightness", pref(Preferences::Brightness));
  gl::setAlphaFuncUniforms(shader, current);
  current->activate(
    gl, pref(Preferences::TextureMinFilter), pref(Preferences::TextureMagFilter));
  using Vertex = gl::VertexTypes::P2Uv2::Vertex;
  auto quad = gl::VertexArray::move(std::vector<Vertex>{
    Vertex{{-x, y}, {0, 0}},
    Vertex{{-x, -y}, {0, 1}},
    Vertex{{x, -y}, {1, 1}},
    Vertex{{x, y}, {1, 0}}});
  quad.prepare(gl, vboManager());
  if (quad.setup(gl, shader.program()))
  {
    quad.render(gl, gl::PrimType::Quads);
    quad.cleanup(gl, shader.program());
  }
  current->deactivate(gl);
}
} // namespace tb::ui
