#include "ui/BoxSelectionTool.h"

#include "base/Color.h"
#include "gl/Camera.h"
#include "mdl/EditorContext.h"
#include "mdl/Map.h"
#include "mdl/Map_Groups.h"
#include "mdl/Map_Selection.h"
#include "mdl/ModelUtils.h"
#include "mdl/Node.h"
#include "mdl/Transaction.h"
#include "mdl/WorldNode.h"
#include "render/RenderBatch.h"
#include "render/RenderContext.h"
#include "render/RenderService.h"
#include "ui/HandleDragTracker.h"
#include "ui/InputState.h"
#include "ui/Tool.h"

#include "vm/intersection.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace tb::ui
{
namespace
{
vm::bbox2d projectBounds(const vm::bbox3d& bounds, const vm::axis::type viewAxis)
{
  switch (viewAxis)
  {
  case vm::axis::x:
    return vm::bbox2d{
      vm::vec2d{bounds.min.y(), bounds.min.z()},
      vm::vec2d{bounds.max.y(), bounds.max.z()}};
  case vm::axis::y:
    return vm::bbox2d{
      vm::vec2d{bounds.min.x(), bounds.min.z()},
      vm::vec2d{bounds.max.x(), bounds.max.z()}};
  case vm::axis::z:
    return vm::bbox2d{
      vm::vec2d{bounds.min.x(), bounds.min.y()},
      vm::vec2d{bounds.max.x(), bounds.max.y()}};
  default:
    throw std::logic_error{"Invalid view axis"};
  }
}

std::vector<mdl::Node*> collectCandidateNodes(mdl::Map& map)
{
  return mdl::collectSelectableNodes(
    mdl::currentGroupOrWorld(map)->children(), map.editorContext());
}

bool containsAncestor(const std::vector<mdl::Node*>& nodes, const mdl::Node& node)
{
  return std::ranges::any_of(nodes, [&](const auto* other) {
    return other != &node && other->isAncestorOf(node);
  });
}

std::vector<mdl::Node*> selectNodesForBounds(
  mdl::Map& map, const vm::bbox3d& bounds, const vm::axis::type viewAxis)
{
  const auto selectionBounds2D = projectBounds(bounds, viewAxis);
  const auto& editorContext = map.editorContext();

  auto visited = std::unordered_set<mdl::Node*>{};
  auto selectedNodes = std::vector<mdl::Node*>{};
  for (auto* node : collectCandidateNodes(map))
  {
    if (
      node && visited.insert(node).second && editorContext.selectable(*node)
      && projectBounds(node->logicalBounds(), viewAxis).intersects(selectionBounds2D))
    {
      selectedNodes.push_back(node);
    }
  }

  selectedNodes.erase(
    std::ranges::remove_if(
      selectedNodes,
      [&](const auto* node) { return containsAncestor(selectedNodes, *node); })
      .begin(),
    selectedNodes.end());
  return selectedNodes;
}
} // namespace

SelectionBoxRenderer::SelectionBoxRenderer()
  : m_valid(false)
{
}

void SelectionBoxRenderer::setSelectionBounds(const vm::bbox3d& bounds)
{
  m_selectionBounds = bounds;
  m_valid = true;
}

void SelectionBoxRenderer::clear()
{
  m_valid = false;
}

void SelectionBoxRenderer::render(
  render::RenderContext& renderContext, render::RenderBatch& renderBatch) const
{
  if (!m_valid)
  {
    return;
  }

  auto renderService = render::RenderService{renderContext, renderBatch};

  const auto color = Color{RgbaB{51, 102, 255, 153}};
  renderService.setForegroundColor(color);
  renderService.setLineWidth(2.0f);

  const auto& camera = renderContext.camera();
  const auto direction = camera.direction();

  const auto min = m_selectionBounds.min;
  const auto max = m_selectionBounds.max;

  const auto majorAxis = vm::find_abs_max_component(direction);

  if (majorAxis == vm::axis::z)
  {
    renderService.renderLine(
      vm::vec3f(min.x(), min.y(), min.z()), vm::vec3f(max.x(), min.y(), min.z()));
    renderService.renderLine(
      vm::vec3f(max.x(), min.y(), min.z()), vm::vec3f(max.x(), max.y(), min.z()));
    renderService.renderLine(
      vm::vec3f(max.x(), max.y(), min.z()), vm::vec3f(min.x(), max.y(), min.z()));
    renderService.renderLine(
      vm::vec3f(min.x(), max.y(), min.z()), vm::vec3f(min.x(), min.y(), min.z()));
  }
  else if (majorAxis == vm::axis::y)
  {
    renderService.renderLine(
      vm::vec3f(min.x(), min.y(), min.z()), vm::vec3f(max.x(), min.y(), min.z()));
    renderService.renderLine(
      vm::vec3f(max.x(), min.y(), min.z()), vm::vec3f(max.x(), min.y(), max.z()));
    renderService.renderLine(
      vm::vec3f(max.x(), min.y(), max.z()), vm::vec3f(min.x(), min.y(), max.z()));
    renderService.renderLine(
      vm::vec3f(min.x(), min.y(), max.z()), vm::vec3f(min.x(), min.y(), min.z()));
  }
  else
  {
    renderService.renderLine(
      vm::vec3f(min.x(), min.y(), min.z()), vm::vec3f(min.x(), max.y(), min.z()));
    renderService.renderLine(
      vm::vec3f(min.x(), max.y(), min.z()), vm::vec3f(min.x(), max.y(), max.z()));
    renderService.renderLine(
      vm::vec3f(min.x(), max.y(), max.z()), vm::vec3f(min.x(), min.y(), max.z()));
    renderService.renderLine(
      vm::vec3f(min.x(), min.y(), max.z()), vm::vec3f(min.x(), min.y(), min.z()));
  }
}

BoxSelectionDragDelegate::BoxSelectionDragDelegate(
  Tool& tool, mdl::Map& map, const BoxSelectionMode mode)
  : m_tool{tool}
  , m_map{map}
  , m_mode{mode}
{
}

HandlePositionProposer BoxSelectionDragDelegate::start(
  const InputState& inputState,
  const vm::vec3d& initialHandlePosition,
  const vm::vec3d& handleOffset)
{
  m_selectionBounds = vm::bbox3d{initialHandlePosition, initialHandlePosition};
  m_renderer.setSelectionBounds(m_selectionBounds);
  m_tool.refreshViews();

  const auto& camera = inputState.camera();
  const auto plane = vm::plane3d{
    initialHandlePosition, vm::vec3d{vm::get_abs_max_component_axis(camera.direction())}};

  return makeHandlePositionProposer(
    makePlaneHandlePicker(plane, handleOffset), makeIdentityHandleSnapper());
}

DragStatus BoxSelectionDragDelegate::update(
  const InputState&, const DragState& dragState, const vm::vec3d& proposedHandlePosition)
{
  auto min = vm::vec3d{
    std::min(dragState.initialHandlePosition.x(), proposedHandlePosition.x()),
    std::min(dragState.initialHandlePosition.y(), proposedHandlePosition.y()),
    std::min(dragState.initialHandlePosition.z(), proposedHandlePosition.z())};

  auto max = vm::vec3d{
    std::max(dragState.initialHandlePosition.x(), proposedHandlePosition.x()),
    std::max(dragState.initialHandlePosition.y(), proposedHandlePosition.y()),
    std::max(dragState.initialHandlePosition.z(), proposedHandlePosition.z())};

  const auto minSize = 0.1;

  if (max.x() - min.x() < minSize)
  {
    max = vm::vec3d(min.x() + minSize, max.y(), max.z());
  }
  if (max.y() - min.y() < minSize)
  {
    max = vm::vec3d(max.x(), min.y() + minSize, max.z());
  }
  if (max.z() - min.z() < minSize)
  {
    max = vm::vec3d(max.x(), max.y(), min.z() + minSize);
  }

  m_selectionBounds = vm::bbox3d{min, max};
  m_renderer.setSelectionBounds(m_selectionBounds);
  m_tool.refreshViews();
  return DragStatus::Continue;
}

void BoxSelectionDragDelegate::end(const InputState& inputState, const DragState&)
{
  if (inputState.mouseButtonsDown(MouseButtons::Right))
  {
    m_renderer.clear();
    return;
  }

  if (!m_selectionBounds.is_empty())
  {
    const auto& camera = inputState.camera();
    const auto viewAxis = vm::find_abs_max_component(camera.direction());
    const auto selectedNodes = selectNodesForBounds(m_map, m_selectionBounds, viewAxis);

    auto transaction = mdl::Transaction{m_map, "Box Select"};
    if (m_mode == BoxSelectionMode::Remove)
    {
      mdl::deselectNodes(m_map, selectedNodes);
    }
    else
    {
      if (m_mode == BoxSelectionMode::Replace || m_map.selection().hasBrushFaces())
      {
        mdl::deselectAll(m_map);
      }
      if (!selectedNodes.empty())
      {
        mdl::selectNodes(m_map, selectedNodes);
      }
    }
    transaction.commit();
    m_renderer.clear();
  }
}

void BoxSelectionDragDelegate::cancel(const DragState&)
{
  m_renderer.clear();
}

void BoxSelectionDragDelegate::render(
  const InputState&,
  const DragState&,
  render::RenderContext& renderContext,
  render::RenderBatch& renderBatch) const
{
  m_renderer.render(renderContext, renderBatch);
}

} // namespace tb::ui
