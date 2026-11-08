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

#include "ui/EntityBrowserView.h"

#include "base/PreferenceManager.h"
#include "el/VariableStore.h"
#include "gl/ActiveShader.h"
#include "gl/FontDescriptor.h"
#include "gl/FontManager.h"
#include "gl/GlInterface.h"
#include "gl/GlUtils.h"
#include "gl/MaterialIndexRangeRenderer.h"
#include "gl/MaterialRenderFunc.h"
#include "gl/PrimType.h"
#include "gl/ResourceId.h"
#include "gl/Shaders.h"
#include "gl/TextureFont.h"
#include "gl/VertexArray.h"
#include "mdl/AssetUtils.h"
#include "mdl/EntityDefinition.h"
#include "mdl/EntityDefinitionGroup.h"
#include "mdl/EntityDefinitionManager.h"
#include "mdl/EntityDefinitionUtils.h"
#include "mdl/EntityModel.h"
#include "mdl/EntityModelManager.h"
#include "mdl/EntityProperties.h"
#include "mdl/Map.h"
#include "prefs/Preferences.h"
#include "render/Transformation.h"
#include "ui/InputEvent.h"
#include "ui/MapDocument.h"
#include "ui/Theme.h"

#include "kd/contracts.h"
#include "kd/string_compare.h"
#include "kd/string_utils.h"

#include "vm/mat.h"
#include "vm/mat_ext.h"
#include "vm/quat.h"
#include "vm/vec.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tb::ui
{
EntityBrowserView::EntityBrowserView(
  AppController& appController, QScrollBar* scrollBar, MapDocument& document)
  : CellView{appController, scrollBar}
  , m_document{document}
  , m_sortOrder{mdl::EntityDefinitionSortOrder::Name}
{
  const auto hRotation = vm::quatf{vm::vec3f{0, 0, 1}, vm::to_radians(-30.0f)};
  const auto vRotation = vm::quatf{vm::vec3f{0, 1, 0}, vm::to_radians(20.0f)};
  m_rotation = vRotation * hRotation;

  m_notifierConnection += m_document.resourcesWereProcessedNotifier.connect(
    this, &EntityBrowserView::resourcesWereProcessed);
}

EntityBrowserView::~EntityBrowserView()
{
  clear();
}

void EntityBrowserView::setDefaultModelScaleExpression(
  std::optional<el::ExpressionNode> defaultScaleExpression)
{
  m_defaultScaleModelExpression = std::move(defaultScaleExpression);
}

void EntityBrowserView::setSortOrder(const mdl::EntityDefinitionSortOrder sortOrder)
{
  if (sortOrder != m_sortOrder)
  {
    m_sortOrder = sortOrder;
    refreshDefinitions();
  }
}

void EntityBrowserView::setGroup(const bool group)
{
  if (group != m_group)
  {
    m_group = group;
    refreshDefinitions();
  }
}

void EntityBrowserView::setHideUnused(const bool hideUnused)
{
  if (hideUnused != m_hideUnused)
  {
    m_hideUnused = hideUnused;
    refreshDefinitions();
  }
}

void EntityBrowserView::setFilterText(const std::string& filterText)
{
  if (filterText != m_filterText)
  {
    m_filterText = filterText;
    refreshDefinitions();
  }
}

const std::string& EntityBrowserView::filterText() const
{
  return m_filterText;
}
mdl::EntityDefinitionSortOrder EntityBrowserView::sortOrder() const
{
  return m_sortOrder;
}
bool EntityBrowserView::group() const
{
  return m_group;
}
bool EntityBrowserView::hideUnused() const
{
  return m_hideUnused;
}
void EntityBrowserView::setMapActionsEnabled(const bool enabled)
{
  m_mapActionsEnabled = enabled;
}
const std::string& EntityBrowserView::selectedDefinitionName() const
{
  return m_selectedDefinitionName;
}

std::vector<const mdl::EntityDefinition*> EntityBrowserView::filteredDefinitions() const
{
  auto result = std::vector<const mdl::EntityDefinition*>{};
  const auto words = kdl::str_split(m_filterText, " ");
  for (const auto& definition : m_document.map().entityDefinitionManager().definitions())
  {
    const auto point = definition.pointEntityDefinition.has_value();
    if (
      definition.name == mdl::EntityPropertyValues::WorldspawnClassname || !point
      || (m_hideUnused && definition.usageCount() == 0))
      continue;
    const auto text = definition.name + " " + definition.description;
    if (std::ranges::all_of(
          words, [&](const auto& word) { return kdl::ci::str_contains(text, word); }))
      result.push_back(&definition);
  }
  std::ranges::sort(result, [this](const auto* lhs, const auto* rhs) {
    if (
      m_sortOrder == mdl::EntityDefinitionSortOrder::Usage
      && lhs->usageCount() != rhs->usageCount())
      return lhs->usageCount() > rhs->usageCount();
    return kdl::ci::string_less_natural{}(lhs->name, rhs->name);
  });
  return result;
}

void EntityBrowserView::refreshDefinitions()
{
  const auto definitions = filteredDefinitions();
  if (
    !m_selectedDefinitionName.empty()
    && !std::ranges::any_of(definitions, [this](const auto* definition) {
         return definition->name == m_selectedDefinitionName;
       }))
    m_selectedDefinitionName.clear();
  invalidate();
  update();
  emit definitionsChanged();
}

bool EntityBrowserView::selectDefinition(const std::string& name)
{
  const auto definitions = filteredDefinitions();
  if (!std::ranges::any_of(
        definitions, [&](const auto* definition) { return definition->name == name; }))
    return false;
  m_selectedDefinitionName = name;
  update();
  emit definitionSelected();
  return true;
}

QString EntityBrowserView::dragPayload(const std::string& name) const
{
  const auto* definition = m_document.map().entityDefinitionManager().definition(name);
  return m_mapActionsEnabled && definition && definition->pointEntityDefinition
           ? "entity:" + QString::fromStdString(name)
           : QString{};
}

void EntityBrowserView::doLeftClick(Layout& layout, const float x, const float y)
{
  if (const auto* cell = layout.cellAt(x, y))
    selectDefinition(cellData(*cell).entityDefinition.name);
}
void EntityBrowserView::doDoubleClick(Layout& layout, const float x, const float y)
{
  if (const auto* cell = layout.cellAt(x, y))
    if (cellData(*cell).entityDefinition.pointEntityDefinition)
    {
      selectDefinition(cellData(*cell).entityDefinition.name);
      emit definitionActivated();
    }
}
bool EntityBrowserView::isCellSelected(const Cell& cell) const
{
  return cellData(cell).entityDefinition.name == m_selectedDefinitionName;
}
void EntityBrowserView::processEvent(const KeyEvent& event)
{
  if (event.type == KeyEvent::Type::Down && event.modifiers == Qt::NoModifier)
  {
    const auto definitions = filteredDefinitions();
    if (event.key == Qt::Key_Return || event.key == Qt::Key_Enter)
    {
      if (!m_selectedDefinitionName.empty())
        emit definitionActivated();
      return;
    }
    if (event.key == Qt::Key_Left || event.key == Qt::Key_Right)
    {
      const auto selected =
        std::ranges::find_if(definitions, [this](const auto* definition) {
          return definition->name == m_selectedDefinitionName;
        });
      const auto next = selected == definitions.end()
                          ? std::ptrdiff_t{0}
                          : std::distance(definitions.begin(), selected)
                              + (event.key == Qt::Key_Right ? 1 : -1);
      if (next >= 0 && next < std::ptrdiff_t(definitions.size()))
      {
        selectDefinition(definitions[size_t(next)]->name);
        scrollToCell([this](const auto& cell) { return isCellSelected(cell); });
      }
      return;
    }
  }
  CellView::processEvent(event);
}

void EntityBrowserView::doInitLayout(Layout& layout)
{
  const auto cellSize =
    93.0f * std::clamp(pref(Preferences::AssetBrowserIconSize), 0.5f, 3.0f);
  layout.setOuterMargin(10.0f);
  layout.setGroupMargin(0.0f);
  layout.setRowMargin(10.0f);
  layout.setCellMargin(10.0f);
  layout.setTitleMargin(4.0f);
  layout.setCellWidth(cellSize, cellSize);
  layout.setCellHeight(cellSize, cellSize);
  layout.setMaxUpScale(1.5f);
}

void EntityBrowserView::doReloadLayout(Layout& layout)
{
  const auto& fontPath = pref(Preferences::RendererFontPath);
  const auto fontSize = pref(Preferences::BrowserFontSize);
  contract_assert(fontSize > 0);

  const auto& entityDefinitionManager = m_document.map().entityDefinitionManager();
  const auto font = gl::FontDescriptor{fontPath, static_cast<size_t>(fontSize)};

  const auto definitions = filteredDefinitions();
  if (m_group)
  {
    auto grouped =
      std::unordered_map<std::string_view, std::vector<const mdl::EntityDefinition*>>{};
    for (const auto* definition : definitions)
      grouped[mdl::getGroupName(*definition)].push_back(definition);
    for (const auto& group : entityDefinitionManager.groups())
      if (const auto it = grouped.find(group.name); it != grouped.end())
      {
        layout.addGroup(displayName(group), static_cast<float>(fontSize) + 2.0f);
        addEntitiesToLayout(layout, it->second, font);
      }
  }
  else
    addEntitiesToLayout(layout, definitions, font);
}

bool EntityBrowserView::dndEnabled()
{
  return m_mapActionsEnabled;
}

QString EntityBrowserView::dndData(const Cell& cell)
{
  return dragPayload(cellData(cell).entityDefinition.name);
}

void EntityBrowserView::resourcesWereProcessed(const std::vector<gl::ResourceId>&)
{
  invalidate();
}

void EntityBrowserView::addEntitiesToLayout(
  Layout& layout,
  const std::vector<const mdl::EntityDefinition*>& definitions,
  const gl::FontDescriptor& font)
{
  for (const auto* definition : definitions)
  {
    addEntityToLayout(layout, *definition, font);
  }
}

void EntityBrowserView::addEntityToLayout(
  Layout& layout, const mdl::EntityDefinition& definition, const gl::FontDescriptor& font)
{
  auto& map = m_document.map();
  const auto name =
    m_group ? std::string{mdl::getShortName(definition)} : definition.name;

  const auto* pointEntityDefinition =
    definition.pointEntityDefinition ? &*definition.pointEntityDefinition : nullptr;

  const auto maxCellWidth = layout.maxCellWidth();
  const auto actualFont = fontManager().selectFontSize(font, name, maxCellWidth, 5);
  const auto actualSize = fontManager().font(actualFont).measure(name);
  const auto spec =
    pointEntityDefinition
      ? mdl::safeGetModelSpecification(
          map.logger(),
          definition.name,
          [&]() {
            return pointEntityDefinition->modelDefinition.defaultModelSpecification();
          })
      : mdl::ModelSpecification{};

  const auto modelScale = pointEntityDefinition
                            ? vm::vec3f{mdl::safeGetModelScale(
                                pointEntityDefinition->modelDefinition,
                                el::NullVariableStore{},
                                m_defaultScaleModelExpression)}
                            : vm::vec3f::fill(1.0f);

  auto* modelRenderer = static_cast<gl::MaterialRenderer*>(nullptr);
  auto bounds = vm::bbox3f{};
  auto transform = vm::mat4x4f{};
  auto modelOrientation = mdl::Orientation::Oriented;

  const auto& entityModelManager = map.entityModelManager();
  const auto* model =
    pointEntityDefinition ? entityModelManager.model(spec.path) : nullptr;
  const auto* modelData = model ? model->data() : nullptr;
  const auto* modelFrame = modelData ? modelData->frame(spec.frameIndex) : nullptr;
  if (modelFrame)
  {
    modelRenderer = entityModelManager.renderer(spec);
    modelOrientation = modelData->orientation();

    bounds = modelFrame->bounds();

    const auto scalingMatrix = vm::scaling_matrix(modelScale);
    const auto center = bounds.center();
    const auto scaledCenter = scalingMatrix * center;
    transform = vm::translation_matrix(scaledCenter) * vm::rotation_matrix(m_rotation)
                * scalingMatrix * vm::translation_matrix(-center);
  }
  else
  {
    bounds = pointEntityDefinition ? vm::bbox3f{pointEntityDefinition->bounds}
                                   : vm::bbox3f{16.0f};

    const auto center = bounds.center();
    transform = vm::translation_matrix(-center) * vm::rotation_matrix(m_rotation)
                * vm::translation_matrix(center);
  }

  const auto rotatedBounds = bounds.transform(transform);
  const auto rotatedBoundsSize = rotatedBounds.size();

  layout.addItem(
    EntityCellData{
      definition,
      modelRenderer,
      modelOrientation,
      actualFont,
      bounds,
      transform,
      modelScale},
    name,
    rotatedBoundsSize.y(),
    rotatedBoundsSize.z(),
    actualSize.x(),
    static_cast<float>(font.size()) + 2.0f);
}

void EntityBrowserView::doClear() {}

void EntityBrowserView::doRender(
  gl::Gl& gl, Layout& layout, const float y, const float height)
{
  const auto viewLeft = static_cast<float>(0);
  const auto viewTop = static_cast<float>(size().height());
  const auto viewRight = static_cast<float>(size().width());
  const auto viewBottom = static_cast<float>(0);

  const auto projection =
    vm::ortho_matrix(-1024.0f, 1024.0f, viewLeft, viewTop, viewRight, viewBottom);
  const auto view =
    vm::view_matrix(CameraDirection, CameraUp) * vm::translation_matrix(CameraPosition);
  auto transformation = render::Transformation{gl, projection, view};

  renderBounds(gl, layout, y, height);
  renderModels(gl, layout, y, height, transformation);
}

QString EntityBrowserView::emptyMessage() const
{
  return !m_filterText.empty() || m_hideUnused ? tr("No matching entities")
                                               : tr("No entity definitions available");
}

bool EntityBrowserView::shouldRenderFocusIndicator() const
{
  return false;
}

Color EntityBrowserView::getBackgroundColor()
{
  return browserBackgroundColor(palette());
}

void EntityBrowserView::renderBounds(
  gl::Gl& gl, Layout& layout, const float y, const float height)
{
  using BoundsVertex = gl::VertexTypes::P3C4::Vertex;
  auto vertices = std::vector<BoundsVertex>{};

  for (const auto& group : layout.groups())
  {
    if (group.intersectsY(y, height))
    {
      for (const auto& row : group.rows())
      {
        if (row.intersectsY(y, height))
        {
          for (const auto& cell : row.cells())
          {
            const auto& definition = cellData(cell).entityDefinition;
            auto* modelRenderer = cellData(cell).modelRenderer;

            if (modelRenderer == nullptr)
            {
              const auto itemTrans = itemTransformation(cell, y, height);
              const auto& color = definition.color;
              cellData(cell).bounds.for_each_edge(
                [&](const vm::vec3f& v1, const vm::vec3f& v2) {
                  vertices.emplace_back(itemTrans * v1, color.to<RgbaF>().toVec());
                  vertices.emplace_back(itemTrans * v2, color.to<RgbaF>().toVec());
                });
            }
          }
        }
      }
    }
  }

  auto shader = gl::ActiveShader{gl, shaderManager(), gl::Shaders::VaryingPCShader};
  auto vertexArray = gl::VertexArray::move(std::move(vertices));

  vertexArray.prepare(gl, vboManager());
  if (vertexArray.setup(gl, shader.program()))
  {
    vertexArray.render(gl, gl::PrimType::Lines);
    vertexArray.cleanup(gl, shader.program());
  }
}

void EntityBrowserView::renderModels(
  gl::Gl& gl,
  Layout& layout,
  const float y,
  const float height,
  render::Transformation& transformation)
{
  gl.frontFace(GL_CW);

  auto& entityModelManager = m_document.map().entityModelManager();
  entityModelManager.prepare(gl, vboManager());

  auto shader = gl::ActiveShader{gl, shaderManager(), gl::Shaders::EntityModelShader};
  shader.set("ApplyTinting", false);
  shader.set("Brightness", pref(Preferences::Brightness));
  shader.set("GrayScale", false);

  shader.set("CameraPosition", CameraPosition);
  shader.set("CameraDirection", CameraDirection);
  shader.set("CameraRight", vm::cross(CameraDirection, CameraUp));
  shader.set("CameraUp", CameraUp);
  shader.set("ViewMatrix", transformation.viewMatrix());

  for (const auto& group : layout.groups())
  {
    if (group.intersectsY(y, height))
    {
      for (const auto& row : group.rows())
      {
        if (row.intersectsY(y, height))
        {
          for (const auto& cell : row.cells())
          {
            if (auto* modelRenderer = cellData(cell).modelRenderer)
            {
              shader.set(
                "Orientation", static_cast<int>(cellData(cell).modelOrientation));

              const auto itemTrans = itemTransformation(cell, y, height);
              shader.set("ModelMatrix", itemTrans);

              const auto multMatrix =
                render::MultiplyModelMatrix{transformation, itemTrans};

              auto renderFunc = gl::AlphaTestedMaterialRenderFunc{
                shader,
                pref(Preferences::TextureMinFilter),
                pref(Preferences::TextureMagFilter)};
              modelRenderer->render(gl, shader.program(), renderFunc);
            }
          }
        }
      }
    }
  }
}

vm::mat4x4f EntityBrowserView::itemTransformation(
  const Cell& cell, const float y, const float height) const
{
  const auto& cellData = this->cellData(cell);

  const auto offset =
    vm::vec3f{0.0f, cell.itemBounds().left(), height - (cell.itemBounds().bottom() - y)};
  const auto scaling = cell.scale();
  const auto& rotatedBounds = cellData.bounds.transform(cellData.transform);
  const auto rotationOffset =
    vm::vec3f{0.0f, -rotatedBounds.min.y(), -rotatedBounds.min.z()};

  return vm::translation_matrix(offset) * vm::scaling_matrix(vm::vec3f::fill(scaling))
         * vm::translation_matrix(rotationOffset) * cellData.transform;
}

QString EntityBrowserView::tooltip(const Cell& cell)
{
  const auto& definition = cellData(cell).entityDefinition;
  return QString::fromStdString(definition.name + "\n" + definition.description);
}

const EntityCellData& EntityBrowserView::cellData(const Cell& cell) const
{
  return cell.itemAs<EntityCellData>();
}

} // namespace tb::ui
