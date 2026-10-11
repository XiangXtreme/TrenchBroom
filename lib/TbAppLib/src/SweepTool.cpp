/*
 Copyright (C) 2026 Jackson Palmer
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

#include "ui/SweepTool.h"

#include "base/PreferenceManager.h"
#include "gl/Camera.h"
#include "mdl/Brush.h"
#include "mdl/BrushFace.h" // IWYU pragma: keep
#include "mdl/BrushNode.h" // IWYU pragma: keep
#include "mdl/Grid.h"
#include "mdl/Hit.h"
#include "mdl/Map.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/Node.h"
#include "mdl/Transaction.h"
#include "prefs/Preferences.h"
#include "render/BrushRenderer.h"
#include "render/RenderBatch.h"
#include "render/RenderContext.h"
#include "render/RenderService.h"
#include "ui/HandleDragTracker.h"
#include "ui/MapDocument.h"

#include "kd/contracts.h"
#include "kd/flat_map.h"
#include "kd/ranges/concat_view.h"
#include "kd/ranges/contains.h"
#include "kd/ranges/to.h"

#include "vm/bbox.h"
#include "vm/line.h"
#include "vm/mat.h"
#include "vm/polygon_io.h" // IWYU pragma: keep
#include "vm/quat.h"
#include "vm/quat_io.h" // IWYU pragma: keep
#include "vm/vec.h"
#include "vm/vec_io.h" // IWYU pragma: keep

#include <array>
#include <map>
#include <numeric>
#include <optional>
#include <ranges>

namespace tb::ui
{
namespace
{

vm::vec3d clampScale(const vm::vec3d& factors)
{
  // a factor at zero collapses the profile, a negative one turns it inside-out
  return vm::max(factors, vm::vec3d::fill(SweepTool::MinScaleFactor));
}

SweepFaceAttributes captureFaceAttributes(const mdl::BrushFace& face)
{
  return SweepFaceAttributes{
    face.materialName(),
    face.uvAttributes(),
    face.surfaceAttributes(),
    face.takeUvCoordSystemSnapshot(),
    face.boundary(),
  };
}

bool containsVertex(const mdl::BrushFace& face, const vm::vec3d& vertex)
{
  return std::ranges::any_of(face.vertexPositions(), [&](const auto& candidate) {
    return vm::is_equal(candidate, vertex, vm::Cd::almost_zero());
  });
}

bool facesShareEdge(const mdl::BrushFaceHandle& lhs, const mdl::BrushFaceHandle& rhs)
{
  const auto lhsVertices = lhs.face().vertexPositions();
  const auto rhsVertices = rhs.face().vertexPositions();
  for (size_t lhsIndex = 0u; lhsIndex < lhsVertices.size(); ++lhsIndex)
  {
    const auto& lhsStart = lhsVertices[lhsIndex];
    const auto& lhsEnd = lhsVertices[(lhsIndex + 1u) % lhsVertices.size()];
    for (size_t rhsIndex = 0u; rhsIndex < rhsVertices.size(); ++rhsIndex)
    {
      const auto& rhsStart = rhsVertices[rhsIndex];
      const auto& rhsEnd = rhsVertices[(rhsIndex + 1u) % rhsVertices.size()];
      if (
        (vm::is_equal(lhsStart, rhsStart, vm::Cd::almost_zero())
         && vm::is_equal(lhsEnd, rhsEnd, vm::Cd::almost_zero()))
        || (vm::is_equal(lhsStart, rhsEnd, vm::Cd::almost_zero()) && vm::is_equal(lhsEnd, rhsStart, vm::Cd::almost_zero())))
      {
        return true;
      }
    }
  }
  return false;
}

std::vector<std::vector<mdl::BrushFaceHandle>> connectedFaceComponents(
  const std::vector<mdl::BrushFaceHandle>& faces)
{
  auto result = std::vector<std::vector<mdl::BrushFaceHandle>>{};
  auto assigned = std::vector<bool>(faces.size(), false);
  for (size_t seed = 0u; seed < faces.size(); ++seed)
  {
    if (assigned[seed])
    {
      continue;
    }

    auto indices = std::vector<size_t>{seed};
    assigned[seed] = true;
    for (size_t cursor = 0u; cursor < indices.size(); ++cursor)
    {
      for (size_t candidate = 0u; candidate < faces.size(); ++candidate)
      {
        if (
          !assigned[candidate]
          && facesShareEdge(faces[indices[cursor]], faces[candidate]))
        {
          assigned[candidate] = true;
          indices.push_back(candidate);
        }
      }
    }

    result.push_back(
      indices | std::views::transform([&](const auto index) { return faces[index]; })
      | kdl::ranges::to<std::vector>());
  }
  return result;
}

bool componentsUseDistinctBrushes(
  const std::vector<mdl::BrushFaceHandle>& lhs,
  const std::vector<mdl::BrushFaceHandle>& rhs)
{
  return std::ranges::none_of(lhs, [&](const auto& lhsFace) {
    return std::ranges::any_of(
      rhs, [&](const auto& rhsFace) { return lhsFace.node() == rhsFace.node(); });
  });
}

std::optional<SweepFaceAttributes> adjacentFaceAttributes(
  const mdl::Brush& brush,
  const size_t selectedFaceIndex,
  const vm::vec3d& edgeStart,
  const vm::vec3d& edgeEnd)
{
  for (size_t i = 0; i < brush.faceCount(); ++i)
  {
    if (i == selectedFaceIndex)
    {
      continue;
    }

    const auto& face = brush.face(i);
    if (containsVertex(face, edgeStart) && containsVertex(face, edgeEnd))
    {
      return captureFaceAttributes(face);
    }
  }

  return std::nullopt;
}

auto initializeFaces(const auto& faces)
{
  return faces | std::views::transform([](const auto& faceHandle) {
           const auto& face = faceHandle.face();
           auto polygon = face.polygon();
           const auto& vertices = polygon.vertices();
           const auto& brush = faceHandle.node()->brush();

           auto sideAttributes = std::vector<std::optional<SweepFaceAttributes>>{};
           sideAttributes.reserve(vertices.size());
           for (size_t i = 0; i < vertices.size(); ++i)
           {
             sideAttributes.push_back(adjacentFaceAttributes(
               brush,
               faceHandle.faceIndex(),
               vertices[i],
               vertices[(i + 1) % vertices.size()]));
           }

           return SweepFace{
             std::move(polygon),
             faceHandle.node()->parent(),
             captureFaceAttributes(face),
             std::move(sideAttributes),
           };
         })
         | kdl::ranges::to<std::vector>();
}

auto initializeCenter(const auto& faces)
{
  contract_pre(!faces.empty());

  return vm::bbox3d::build(faces | std::views::transform([](const auto& faceHandle) {
                             return faceHandle.face().bounds();
                           }))
    ->center();
}

auto initializeNormal(const auto& faces)
{
  const auto normals = faces | std::views::transform([](const auto& faceHandle) {
                         const auto& face = faceHandle.face();
                         return face.normal();
                       });

  const auto sumOfAllNormals =
    std::accumulate(normals.begin(), normals.end(), vm::vec3d{0, 0, 0});

  // cancelling normals leave no forward direction; S-bend falls back to straight
  return vm::squared_length(sumOfAllNormals) > vm::Cd::almost_zero()
           ? vm::normalize(sumOfAllNormals)
           : vm::vec3d{0, 0, 0};
}

vm::vec3d initializeScaleBaseVector(
  const std::vector<SweepFace>& sourceFaces, const vm::vec3d& center)
{
  if (sourceFaces.empty())
  {
    return vm::vec3d{0, 0, 0};
  }

  const auto getVertices = [](const auto& sourceFace) {
    return sourceFace.polygon.vertices();
  };

  const auto getScaleBaseVector = [&](const auto& vertex) { return vertex - center; };

  const auto scaleBaseVectors =
    sourceFaces | std::views::transform(getVertices) | std::views::join
    | std::views::transform(getScaleBaseVector) | kdl::ranges::to<std::vector>();

  const auto iLongestScaleBaseVector =
    std::ranges::max_element(scaleBaseVectors, std::less<double>{}, [](const auto& arm) {
      return vm::squared_length(arm);
    });

  contract_assert(iLongestScaleBaseVector != scaleBaseVectors.end());
  return *iLongestScaleBaseVector;
}

auto initializeSweepSource(const auto& faces)
{
  auto sourceFaces = initializeFaces(faces);
  const auto center = initializeCenter(faces);
  const auto normal = initializeNormal(faces);
  const auto scaleBaseVector = initializeScaleBaseVector(sourceFaces, center);

  return SweepSource{
    std::move(sourceFaces),
    center,
    normal,
    scaleBaseVector,
  };
}

SweepTarget initializeSweepTarget(const auto& faces)
{
  return SweepTarget{
    initializeFaces(faces),
    initializeCenter(faces),
    initializeNormal(faces),
  };
}

vm::vec3d scaleArmAtCap(const vm::vec3d& scaleBase, const SweepTransform& transform)
{
  return transform.effectiveRotation() * scaleBase;
}

class SweepRingDragTracker : public RingDragTracker
{
private:
  SweepTool& m_tool;
  vm::quatd m_initialRotation;

public:
  explicit SweepRingDragTracker(SweepTool& tool)
    : m_tool{tool}
    , m_initialRotation{tool.transform().rotation}
  {
  }

  void apply(const vm::vec3d&, const vm::vec3d& axis, const double angle) override
  {
    auto transform = m_tool.transform();
    transform.rotation = vm::quatd{vm::normalize(axis), angle} * m_initialRotation;
    m_tool.setTransform(transform);
  }

  void end() override {}

  void cancel() override
  {
    auto transform = m_tool.transform();
    transform.rotation = m_initialRotation;
    m_tool.setTransform(transform);
  }
};

} // namespace

const mdl::HitType::Type SweepTool::ScaleHitType = mdl::HitType::freeType();

SweepTool::SweepTool(MapDocument& document)
  : Tool{false}
  , m_document{document}
  , m_brushRenderer{std::make_unique<render::BrushRenderer>()}
{
}

SweepTool::~SweepTool() = default;

bool SweepTool::ownsSelection() const
{
  return true;
}

bool SweepTool::doActivate()
{
  auto& map = m_document.map();
  const auto& faces = map.selection().brushFaces;
  if (faces.empty())
  {
    return false;
  }

  m_source = initializeSweepSource(faces);
  m_bridgeSource.reset();
  m_bridgeTarget.reset();
  m_bridgeAlternateSource.reset();
  m_bridgeAlternateTarget.reset();
  m_bridgeFacesAreDistinct = false;
  const auto bridgeComponents = connectedFaceComponents(faces);
  if (bridgeComponents.size() == 2u)
  {
    const auto& sourceFaces = bridgeComponents.front();
    const auto& targetFaces = bridgeComponents.back();
    m_bridgeSource = initializeSweepSource(sourceFaces);
    m_bridgeTarget = initializeSweepTarget(targetFaces);
    m_bridgeAlternateSource = initializeSweepSource(targetFaces);
    m_bridgeAlternateTarget = initializeSweepTarget(sourceFaces);
    m_bridgeFacesAreDistinct = componentsUseDistinctBrushes(sourceFaces, targetFaces);
  }
  if (m_constructionMode == SweepConstructionMode::Bridge && !bridgeAvailable())
  {
    m_constructionMode = SweepConstructionMode::Sweep;
  }

  connectObservers();
  reset();

  return true;
}

bool SweepTool::doDeactivate()
{
  m_notifierConnection.disconnect();
  m_source = SweepSource{};
  m_bridgeSource.reset();
  m_bridgeTarget.reset();
  m_bridgeAlternateSource.reset();
  m_bridgeAlternateTarget.reset();
  m_bridgeFacesAreDistinct = false;

  m_previewBrushes.clear();
  m_sweepIssues.clear();
  m_brushRenderer->clear();
  sweepResultDidChangeNotifier();

  refreshViews();

  return true;
}

bool SweepTool::applies() const
{
  return m_document.map().selection().hasBrushFaces();
}

const SweepTransform& SweepTool::transform() const
{
  return m_transform;
}

void SweepTool::setTransform(const SweepTransform& transform)
{
  m_transform = transform;
  m_handle.setPosition(destinationCenter());
  updateBrushes();
}

const SweepParameters& SweepTool::parameters() const
{
  return m_parameters;
}

void SweepTool::setParameters(const SweepParameters& parameters)
{
  m_parameters = parameters;
  updateBrushes();
}

SweepConstructionMode SweepTool::constructionMode() const
{
  return m_constructionMode;
}

void SweepTool::setConstructionMode(const SweepConstructionMode mode)
{
  if (m_constructionMode != mode)
  {
    m_constructionMode = mode;
    m_handle.setPosition(destinationCenter());
    updateBrushes();
  }
}

bool SweepTool::bridgeAvailable() const
{
  return m_bridgeFacesAreDistinct && m_bridgeSource && m_bridgeTarget
         && m_bridgeSource->faces.size() == m_bridgeTarget->faces.size();
}

void SweepTool::swapBridgeEnds()
{
  if (
    m_bridgeSource && m_bridgeTarget && m_bridgeAlternateSource
    && m_bridgeAlternateTarget)
  {
    std::swap(m_bridgeSource, m_bridgeAlternateSource);
    std::swap(m_bridgeTarget, m_bridgeAlternateTarget);
    m_handle.setPosition(destinationCenter());
    updateBrushes();
  }
}

bool SweepTool::destinationEditable() const
{
  return m_constructionMode == SweepConstructionMode::Sweep;
}

vm::vec3d SweepTool::destinationCenter() const
{
  if (m_constructionMode == SweepConstructionMode::Bridge && m_bridgeTarget)
  {
    return m_bridgeTarget->center;
  }
  return m_transform.destinationCenter(m_source);
}

void SweepTool::setDestinationCenter(const vm::vec3d& position)
{
  if (!destinationEditable())
  {
    return;
  }
  auto transform = m_transform;
  transform.translation = position - m_source.center;
  setTransform(transform);
}

void SweepTool::rotateDestinationCap(const vm::vec3d& axis, const double angle)
{
  if (!destinationEditable())
  {
    return;
  }
  auto transform = m_transform;
  transform.rotation = vm::quatd{vm::normalize(axis), angle} * transform.rotation;
  setTransform(transform);
}

void SweepTool::reset()
{
  if (destinationEditable())
  {
    setTransform(SweepTransform{});
  }
  else
  {
    updateBrushes();
  }
}

bool SweepTool::cancel()
{
  if (!destinationEditable())
  {
    return false;
  }
  if (!m_transform.isNoOp())
  {
    reset();
    return true;
  }

  return false;
}

double SweepTool::minorHandleRadius(const gl::Camera& camera) const
{
  return m_handle.minorHandleRadius(camera);
}

void SweepTool::updateBrushes()
{
  if (active())
  {
    m_previewBrushes.clear();
    m_sweepIssues.clear();
    m_brushRenderer->clear();

    if (m_constructionMode == SweepConstructionMode::Bridge && m_parameters.segments > 0)
    {
      if (bridgeAvailable())
      {
        auto parameters = m_parameters;
        parameters.iterations = 1u;
        auto result = generateBridgeBrushes(
          m_document.map(), *m_bridgeSource, *m_bridgeTarget, parameters);
        m_previewBrushes = std::move(result.brushes);
        m_sweepIssues = std::move(result.issues);
      }
      else
      {
        m_sweepIssues.push_back(SweepIssue{
          0,
          0,
          0,
          "Bridge requires two disconnected, equal-sized face components on different "
          "brushes"});
      }
    }
    else if (
      !m_source.faces.empty() && m_parameters.segments > 0 && !m_transform.isNoOp())
    {
      auto result =
        generateSweepBrushes(m_document.map(), m_source, m_transform, m_parameters);
      m_previewBrushes = std::move(result.brushes);
      m_sweepIssues = std::move(result.issues);
    }

    for (const auto& [parent, brushNodes] : m_previewBrushes)
    {
      for (auto& brushNode : brushNodes)
      {
        m_brushRenderer->addBrush(*brushNode);
      }
    }

    sweepResultDidChangeNotifier();
    refreshViews();
  }
}

void SweepTool::commitSweep()
{
  if (canCommitSweep())
  {
    auto nodesToAdd = kdl::flat_map<mdl::Node*, std::vector<mdl::Node*>>{};
    for (auto&& [parent, childrenPtrs] : m_previewBrushes)
    {
      auto childrenRaw = childrenPtrs | std::views::transform([](auto& childPtr) {
                           return static_cast<mdl::Node*>(childPtr.release());
                         })
                         | kdl::ranges::to<std::vector>();
      nodesToAdd.insert({parent, std::move(childrenRaw)});
    }

    m_previewBrushes.clear();
    m_brushRenderer->clear();

    auto& map = m_document.map();
    auto transaction = mdl::Transaction{
      map,
      m_constructionMode == SweepConstructionMode::Bridge ? "Bridge Faces" : "Sweep"};
    const auto addedNodes = mdl::addNodes(map, nodesToAdd);
    mdl::deselectAll(map);
    mdl::selectNodes(map, addedNodes);
    transaction.commit();
  }

  refreshViews();
}

const std::vector<SweepIssue>& SweepTool::sweepIssues() const
{
  return m_sweepIssues;
}

bool SweepTool::canCommitSweep() const
{
  return !m_previewBrushes.empty() && m_sweepIssues.empty();
}

void SweepTool::renderDestinationGhost(
  render::RenderContext& renderContext, render::RenderBatch& renderBatch) const
{
  if (m_source.faces.empty())
  {
    return;
  }

  auto renderService = render::RenderService{renderContext, renderBatch};
  renderService.setLineWidth(2.0f);

  if (m_constructionMode == SweepConstructionMode::Bridge && m_bridgeTarget)
  {
    renderService.setForegroundColor(pref(Preferences::HandleColor));
    for (const auto& targetFace : m_bridgeTarget->faces)
    {
      const auto& vertices = targetFace.polygon.vertices();
      const auto loop =
        kdl::views::concat(vertices, vertices | std::views::take(1))
        | std::views::transform([](const auto& v) { return vm::vec3f{v}; })
        | kdl::ranges::to<std::vector>();
      renderService.renderLineStrip(loop);
    }
    return;
  }

  const auto renderCaps = [&](const vm::mat4x4d& transform) {
    for (const auto& sourceFace : m_source.faces)
    {
      const auto& vertices = sourceFace.polygon.vertices();
      if (vertices.size() < 2)
      {
        continue;
      }

      const auto loop =
        kdl::views::concat(vertices, vertices | std::views::take(1))
        | std::views::transform([&](const auto& v) { return vm::vec3f{transform * v}; })
        | kdl::ranges::to<std::vector>();

      renderService.renderLineStrip(loop);
    }
  };

  // later iterations' caps are drawn as fainter echoes
  const auto capTransform = stationTransform(
    m_source, m_transform, m_parameters, 1.0, m_transform.effectiveRotation());

  auto transform = capTransform;
  for (size_t r = 0; r < m_parameters.iterations; ++r)
  {
    renderService.setForegroundColor(
      r == 0 ? pref(Preferences::HandleColor)
             : RgbaF{pref(Preferences::HandleColor).to<RgbF>(), 0.35f});
    renderCaps(transform);
    transform = transform * capTransform;
  }
}

void SweepTool::renderPreview(
  render::RenderContext& renderContext, render::RenderBatch& renderBatch) const
{
  m_brushRenderer->setFaceColor(pref(Preferences::FaceColor));
  m_brushRenderer->setEdgeColor(pref(Preferences::SelectedEdgeColor));
  m_brushRenderer->setShowEdges(true);
  m_brushRenderer->setShowOccludedEdges(true);
  m_brushRenderer->setOccludedEdgeColor(RgbaF{
    pref(Preferences::SelectedEdgeColor).to<RgbF>(),
    pref(Preferences::OccludedSelectedEdgeAlpha)});
  m_brushRenderer->setTint(true);
  m_brushRenderer->setTintColor(pref(Preferences::SelectedFaceColor));
  m_brushRenderer->render(renderContext, renderBatch);
}

bool SweepTool::hasScaleHandle() const
{
  return destinationEditable() && !m_source.faces.empty()
         && vm::squared_length(m_source.scaleBaseVector) > vm::Cd::almost_zero();
}

vm::vec3d SweepTool::scaleHandlePosition() const
{
  const auto scaledArm =
    m_transform.effectiveRotation() * (m_transform.scale * m_source.scaleBaseVector);
  return destinationCenter() + scaledArm;
}

void SweepTool::dragScaleHandleTo(const vm::vec3d& position)
{
  const auto arm = scaleArmAtCap(m_source.scaleBaseVector, m_transform);
  const auto armLengthSquared = vm::squared_length(arm);
  if (armLengthSquared < vm::Cd::almost_zero())
  {
    return;
  }

  // project the dragged position onto the arm to read off a uniform factor
  const auto factor = vm::dot(position - destinationCenter(), arm) / armLengthSquared;
  m_transform.scale = clampScale(vm::vec3d{factor, factor, factor});
  updateBrushes();
}

void SweepTool::moveScaleHandle(const double distance)
{
  const auto center = destinationCenter();
  const auto arm = scaleHandlePosition() - center;
  const auto armLength = vm::length(arm);
  if (armLength < vm::Cd::almost_zero())
  {
    return;
  }

  // move the arm's largest component by the given distance, like a snapped drag
  const auto direction = arm / armLength;
  const auto step = distance / vm::abs(vm::get_abs_max_component(direction));
  const auto target = center + direction * (armLength + step);
  dragScaleHandleTo(grid().snap(target, vm::line3d{center, direction}));
}

mdl::Hit SweepTool::pickScaleHandle(
  const vm::ray3d& pickRay, const gl::Camera& camera) const
{
  if (hasScaleHandle())
  {
    if (
      const auto distance = camera.pickPointHandle(
        pickRay, scaleHandlePosition(), double(pref(Preferences::HandleRadius))))
    {
      return {ScaleHitType, *distance, vm::point_at_distance(pickRay, *distance), 0};
    }
  }

  return mdl::Hit::NoHit;
}

void SweepTool::renderScaleHandle(
  render::RenderContext& renderContext, render::RenderBatch& renderBatch) const
{
  if (hasScaleHandle())
  {
    const auto center = destinationCenter();
    const auto position = scaleHandlePosition();

    auto renderService = render::RenderService{renderContext, renderBatch};
    // green like the Scale tool's handles
    renderService.setForegroundColor(pref(Preferences::ScaleHandleColor));
    renderService.renderLine(vm::vec3f{center}, vm::vec3f{position});
    renderService.renderHandle(vm::vec3f{position});
  }
}

void SweepTool::renderScaleHighlight(
  render::RenderContext& renderContext, render::RenderBatch& renderBatch) const
{
  if (hasScaleHandle())
  {
    auto renderService = render::RenderService{renderContext, renderBatch};
    renderService.setForegroundColor(pref(Preferences::SelectedHandleColor));
    renderService.renderHandleHighlight(vm::vec3f{scaleHandlePosition()});
  }
}

Tool& SweepTool::tool()
{
  return *this;
}

const Tool& SweepTool::tool() const
{
  return *this;
}

const mdl::Grid& SweepTool::grid() const
{
  return m_document.map().grid();
}

RotateHandle& SweepTool::handle()
{
  return m_handle;
}

double SweepTool::handleSnapAngle() const
{
  return grid().angle();
}

vm::vec3d SweepTool::handleCenter() const
{
  return destinationCenter();
}

void SweepTool::setHandleCenter(const vm::vec3d& position)
{
  setDestinationCenter(position);
}

std::unique_ptr<RingDragTracker> SweepTool::beginRingDrag()
{
  return std::make_unique<SweepRingDragTracker>(*this);
}

vm::vec3d SweepTool::handlePosition() const
{
  return scaleHandlePosition();
}

void SweepTool::setHandlePosition(const vm::vec3d& position)
{
  dragScaleHandleTo(position);
}

DragHandleSnapper SweepTool::makeDragHandleSnapper(const SnapMode snapMode) const
{
  const auto center = destinationCenter();
  const auto arm = scaleHandlePosition() - center;
  const auto armLength = vm::length(arm);
  if (armLength < vm::Cd::almost_zero())
  {
    return PointHandleDelegate::makeDragHandleSnapper(snapMode);
  }

  // snap along the arm, otherwise off-arm drags land the cap between grid lines
  return makeAbsoluteLineHandleSnapper(grid(), vm::line3d{center, arm / armLength});
}

void SweepTool::renderHighlight(
  render::RenderContext& renderContext, render::RenderBatch& renderBatch) const
{
  renderScaleHighlight(renderContext, renderBatch);
}

void SweepTool::connectObservers()
{
  m_notifierConnection += m_document.nodesWereRemovedNotifier.connect(
    [&](const auto& nodes) { nodesWereRemoved(nodes); });
}

void SweepTool::nodesWereRemoved(const std::vector<mdl::Node*>& nodes)
{
  // a source face's parent may be deleted while the tool is active; null it so the commit
  // falls back to the default parent
  const auto clearRemovedParents = [&](auto& source) {
    auto changed = false;
    for (auto& sourceFace : source.faces)
    {
      if (
        sourceFace.parent
        && (kdl::ranges::contains(nodes, sourceFace.parent) || sourceFace.parent->isDescendantOf(nodes)))
      {
        sourceFace.parent = nullptr;
        changed = true;
      }
    }
    return changed;
  };

  auto mustRebuild = clearRemovedParents(m_source);
  if (m_bridgeSource)
  {
    mustRebuild = clearRemovedParents(*m_bridgeSource) || mustRebuild;
  }
  if (m_bridgeAlternateSource)
  {
    mustRebuild = clearRemovedParents(*m_bridgeAlternateSource) || mustRebuild;
  }
  if (mustRebuild)
  {
    updateBrushes();
  }
}

} // namespace tb::ui
