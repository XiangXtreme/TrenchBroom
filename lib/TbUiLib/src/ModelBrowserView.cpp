/*
 Copyright (C) 2026 XiangXtreme

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

#include "ui/ModelBrowserView.h"

#include <QAudioOutput>
#include <QContextMenuEvent>
#include <QFile>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QMenu>
#include <QMessageBox>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QUrl>

#include "base/Logger.h"
#include "base/Macros.h"
#include "base/PreferenceManager.h"
#include "fs/DiskIO.h"
#include "fs/PathInfo.h"
#include "gl/ActiveShader.h"
#include "gl/FontDescriptor.h"
#include "gl/FontManager.h"
#include "gl/GlInterface.h"
#include "gl/GlManager.h"
#include "gl/GlUtils.h"
#include "gl/MaterialIndexRangeRenderer.h"
#include "gl/MaterialRenderFunc.h"
#include "gl/PrimType.h"
#include "gl/ResourceManager.h"
#include "gl/Shaders.h"
#include "gl/TextureFont.h"
#include "gl/VertexArray.h"
#include "gl/VertexType.h"
#include "mdl/BasicShapes.h"
#include "mdl/EntityModelDataResource.h"
#include "mdl/EntityModelManager.h"
#include "mdl/GameFileSystem.h"
#include "mdl/GoldSrcMdlScaler.h"
#include "mdl/Map.h"
#include "prefs/Preferences.h"
#include "render/Transformation.h"
#include "ui/AppController.h"
#include "ui/CatalogAssetDrop.h"
#include "ui/CatalogModelPreview.h"
#include "ui/ImageUtils.h"
#include "ui/MapDocument.h"
#include "ui/ProjectSession.h"
#include "ui/QPathUtils.h"
#include "ui/Theme.h"

#include "kd/path_utils.h"
#include "kd/ranges/repeat_view.h"
#include "kd/ranges/stride_view.h"
#include "kd/ranges/zip_view.h"
#include "kd/vector_utils.h"

#include "vm/mat.h"
#include "vm/mat_ext.h"
#include "vm/quat.h"
#include "vm/vec.h"

#include <algorithm>
#include <memory>
#include <ranges>
#include <set>
#include <variant>
#include <vector>

namespace tb::ui
{
namespace
{

constexpr auto SoundPreviewButtonSize = 24.0f;
constexpr auto SoundPreviewButtonMargin = 8.0f;
constexpr auto DefaultAssetCellSize = 93.0f;

float assetCellSize()
{
  return DefaultAssetCellSize
         * std::clamp(pref(Preferences::AssetBrowserIconSize), 0.5f, 3.0f);
}

std::optional<RgbaF> assetPlaceholderColor(
  const BrowserCellType type, const QPalette& palette)
{
  const auto lightBackground = palette.color(QPalette::Base).lightnessF() > 0.5f;
  switch (type)
  {
  case BrowserCellType::Sprite:
    return RgbaF{0.78f, 0.12f, 0.16f, lightBackground ? 0.12f : 0.22f};
  case BrowserCellType::Sound:
    return RgbaF{0.48f, 0.35f, 0.82f, lightBackground ? 0.14f : 0.28f};
  case BrowserCellType::Prefab:
    return RgbaF{0.12f, 0.48f, 0.34f, lightBackground ? 0.12f : 0.24f};
  case BrowserCellType::Map:
    return RgbaF{0.15f, 0.45f, 0.75f, lightBackground ? 0.12f : 0.24f};
  case BrowserCellType::Texture:
    return RgbaF{0.65f, 0.42f, 0.15f, lightBackground ? 0.12f : 0.24f};
  case BrowserCellType::Folder:
  case BrowserCellType::Model:
    return std::nullopt;
  }

  return std::nullopt;
}

void appendRoundedRectVertices(
  std::vector<gl::VertexTypes::P2C4::Vertex>& vertices,
  const float left,
  const float top,
  const float right,
  const float bottom,
  const float y,
  const float height,
  const vm::vec4f& color,
  const float cornerRadius = 4.0f)
{
  const auto width = std::max(0.0f, right - left);
  const auto rectHeight = std::max(0.0f, bottom - top);
  if (width <= 0.0f || rectHeight <= 0.0f)
  {
    return;
  }

  const auto radius = std::min(cornerRadius, std::min(width, rectHeight) / 2.0f);
  const auto center =
    vm::vec2f{left + width / 2.0f, height - (top + rectHeight / 2.0f - y)};
  for (const auto& vertex : mdl::roundedRect2D(width, rectHeight, radius, 3))
  {
    vertices.emplace_back(center + vertex, color);
  }
}

bool shouldRenderErrorText(const BrowserCellType type, const AssetPreviewState* preview)
{
  switch (type)
  {
  case BrowserCellType::Sprite:
  case BrowserCellType::Sound:
  case BrowserCellType::Texture:
    return preview && preview->status != AssetPreviewStatus::Ready;
  case BrowserCellType::Folder:
  case BrowserCellType::Model:
  case BrowserCellType::Prefab:
  case BrowserCellType::Map:
    return false;
  }

  return false;
}

bool canRenderImagePreview(const BrowserCellType type)
{
  return type == BrowserCellType::Sprite || type == BrowserCellType::Prefab
         || type == BrowserCellType::Texture;
}

bool canLoadPreview(const BrowserCellType type)
{
  return type == BrowserCellType::Sprite || type == BrowserCellType::Prefab
         || type == BrowserCellType::Sound || type == BrowserCellType::Texture;
}

} // namespace

LayoutBounds soundPreviewButtonBounds(const LayoutBounds& itemBounds)
{
  const auto size = std::min(
    SoundPreviewButtonSize,
    std::max(0.0f, std::min(itemBounds.width, itemBounds.height) - 2.0f));
  return LayoutBounds{
    itemBounds.right() - SoundPreviewButtonMargin - size,
    itemBounds.bottom() - SoundPreviewButtonMargin - size,
    size,
    size};
}

bool soundPreviewButtonHitTest(
  const LayoutBounds& itemBounds, const float x, const float y)
{
  const auto bounds = soundPreviewButtonBounds(itemBounds);
  return bounds.width > 0.0f && bounds.height > 0.0f && bounds.containsPoint(x, y);
}

ModelBrowserView::ModelBrowserView(
  AppController& appController, QScrollBar* scrollBar, MapDocument& document)
  : CellView{appController, scrollBar}
  , m_appController{appController}
  , m_document{document}
  , m_scrollBar{scrollBar}
{
  setMouseTracking(true);

  const auto hRotation = vm::quatf{vm::vec3f{0, 0, 1}, vm::to_radians(-30.0f)};
  const auto vRotation = vm::quatf{vm::vec3f{0, 1, 0}, vm::to_radians(20.0f)};
  m_rotation = vRotation * hRotation;

  if (scrollBar)
  {
    connect(
      scrollBar, &QAbstractSlider::valueChanged, this, [this](const int) { update(); });
  }

  const auto pixmap = loadSVGPixmap(std::filesystem::path{"Map_folder.svg"});
  if (!pixmap.isNull())
  {
    m_folderIconImage = pixmap.toImage().convertToFormat(QImage::Format_RGBA8888);
  }
}

ModelBrowserView::~ModelBrowserView()
{
  stopSoundPreview();
  destroyFolderIconTexture();
  clear();
}

void ModelBrowserView::setAssets(
  std::filesystem::path rootFolderPath, std::vector<BrowserAsset> assets)
{
  m_rootFolderPath = std::move(rootFolderPath);
  for (auto it = m_catalogModels.begin(); it != m_catalogModels.end();)
  {
    const auto previous =
      std::ranges::find_if(m_assets, [&](const auto& a) { return a.path == it->first; });
    const auto current =
      std::ranges::find_if(assets, [&](const auto& a) { return a.path == it->first; });
    if (
      previous == m_assets.end() || current == assets.end()
      || !sameBrowserAssetContent(*previous, *current))
    {
      m_retiredCatalogModels.push_back(std::move(it->second));
      it = m_catalogModels.erase(it);
    }
    else
      ++it;
  }
  // A newer library revision keeps the same catalog path, so its stale preview has to be
  // dropped explicitly instead of waiting for the path to disappear.
  for (const auto& previous : m_assets)
  {
    const auto it = std::ranges::find_if(
      assets, [&](const auto& asset) { return asset.path == previous.path; });
    if (it != assets.end() && !sameBrowserAssetContent(*it, previous))
    {
      m_assetPreviews.erase(previous.path);
    }
  }
  removeStalePreviews(assets);
  m_assets = std::move(assets);
  m_currentFolderPath.clear();
  m_hasSelection = false;
  stopSoundPreview();
  invalidate();
  update();
}

void ModelBrowserView::setPreviewOnly(const bool previewOnly)
{
  m_previewOnly = previewOnly;
  invalidate();
  update();
}

void ModelBrowserView::setIncludeDefaultRoots(const bool includeDefaultRoots)
{
  if (m_includeDefaultRoots == includeDefaultRoots)
    return;
  m_includeDefaultRoots = includeDefaultRoots;
  invalidate();
  update();
}

void ModelBrowserView::setPreviewPathResolver(AssetPreviewPathResolver resolver)
{
  m_previewPathResolver = std::move(resolver);
}

void ModelBrowserView::setCurrentFolderPath(std::filesystem::path currentFolderPath)
{
  currentFolderPath = currentFolderPath.lexically_normal();
  if (currentFolderPath == std::filesystem::path{"."})
  {
    currentFolderPath.clear();
  }

  if (m_currentFolderPath != currentFolderPath)
  {
    m_currentFolderPath = std::move(currentFolderPath);
    m_hasSelection = false;
    stopSoundPreview();
    invalidate();
    update();
  }
}

void ModelBrowserView::setSearchText(QString searchText)
{
  searchText = searchText.trimmed();
  if (m_searchText == searchText)
  {
    return;
  }

  m_searchText = std::move(searchText);
  invalidate();
  update();
}

void ModelBrowserView::resourcesWereProcessed(const std::vector<mdl::ResourceId>&)
{
  stopSoundPreview();
  invalidate();
  update();
}

void ModelBrowserView::doInitLayout(Layout& layout)
{
  const auto cellSize = assetCellSize();
  layout.setOuterMargin(5.0f);
  layout.setGroupMargin(0.0f);
  layout.setRowMargin(5.0f);
  layout.setCellMargin(5.0f);
  layout.setTitleMargin(2.0f);
  layout.setCellWidth(cellSize, cellSize);
  layout.setCellHeight(cellSize, cellSize);
  layout.setMaxUpScale(1.5f);
}

void ModelBrowserView::doReloadLayout(Layout& layout)
{
  const auto& fontPath = pref(Preferences::RendererFontPath);
  const auto fontSize = pref(Preferences::BrowserFontSize);
  const auto font = gl::FontDescriptor{fontPath, size_t(fontSize)};
  const auto maxCellWidth = layout.maxCellWidth();
  const auto cellSize = assetCellSize();
  for (const auto& entry : modelBrowserEntries(
         m_rootFolderPath,
         m_assets,
         m_currentFolderPath,
         m_searchText,
         m_includeDefaultRoots))
  {
    if (m_previewOnly && entry.cellData.type == BrowserCellType::Folder)
      continue;
    const auto titleHeight = fontManager().font(font).measure(entry.title).y();

    layout.addItem(
      entry.cellData, entry.title, cellSize, cellSize, maxCellWidth, titleHeight + 4.0f);
  }
}

void ModelBrowserView::doClear() {}

void ModelBrowserView::doRender(
  gl::Gl& gl, Layout& layout, const float y, const float height)
{
  m_retiredCatalogModels.clear();
  deletePendingSpritePreviewTextures(gl);
  loadMissingVisiblePreviews(layout, y, height);

  const auto viewLeft = float(0);
  const auto viewTop = float(size().height());
  const auto viewRight = float(size().width());
  const auto viewBottom = float(0);

  auto uiTransformation = render::Transformation{
    gl,
    vm::ortho_matrix(-1.0f, 1.0f, viewLeft, viewTop, viewRight, viewBottom),
    vm::view_matrix(vm::vec3f{0, 0, -1}, vm::vec3f{0, 1, 0})
      * vm::translation_matrix(vm::vec3f{0.0f, 0.0f, 0.1f})};
  renderFolders(gl, layout, y, height);
  renderAssetPlaceholders(gl, layout, y, height);
  renderSoundPreviewButtons(gl, layout, y, height);
  renderSpritePreviews(gl, layout, y, height);

  const auto projection =
    vm::ortho_matrix(-1024.0f, 1024.0f, viewLeft, viewTop, viewRight, viewBottom);
  const auto view =
    vm::view_matrix(CameraDirection, CameraUp) * vm::translation_matrix(CameraPosition);
  auto transformation = render::Transformation{gl, projection, view};
  renderModels(gl, layout, y, height, transformation);
}

void ModelBrowserView::ensureFolderIconTexture(gl::Gl& gl)
{
  if (m_folderIconTextureId != 0)
  {
    return;
  }

  if (m_folderIconImage.isNull())
  {
    return;
  }

  gl.genTextures(1, &m_folderIconTextureId);
  gl.bindTexture(GL_TEXTURE_2D, m_folderIconTextureId);
  gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  gl.pixelStorei(GL_UNPACK_ALIGNMENT, 1);
  gl.texImage2D(
    GL_TEXTURE_2D,
    0,
    GL_RGBA,
    m_folderIconImage.width(),
    m_folderIconImage.height(),
    0,
    GL_RGBA,
    GL_UNSIGNED_BYTE,
    m_folderIconImage.constBits());

  gl.bindTexture(GL_TEXTURE_2D, 0);
}

void ModelBrowserView::destroyFolderIconTexture()
{
  if (m_folderIconTextureId == 0)
  {
    return;
  }

  m_folderIconTextureId = 0;
}

const AssetPreviewState* ModelBrowserView::assetPreview(
  const std::filesystem::path& path) const
{
  const auto it = m_assetPreviews.find(path);
  return it != std::end(m_assetPreviews) ? &it->second : nullptr;
}

void ModelBrowserView::removeStalePreviews(const std::vector<BrowserAsset>& assets)
{
  auto currentAssets = std::map<std::filesystem::path, BrowserAsset>{};
  for (const auto& asset : assets)
  {
    currentAssets.emplace(asset.path, asset);
  }

  for (auto it = std::begin(m_assetPreviews); it != std::end(m_assetPreviews);)
  {
    const auto assetIt = currentAssets.find(it->first);
    if (assetIt == std::end(currentAssets))
    {
      if (m_spritePreviewTextures.contains(it->first))
      {
        invalidateSpritePreviewTextures();
      }
      it = m_assetPreviews.erase(it);
      continue;
    }

    const auto oldAssetIt = std::ranges::find(
      m_assets, it->first, [](const auto& asset) { return asset.path; });
    if (
      oldAssetIt == std::end(m_assets)
      || !sameBrowserAssetContent(*oldAssetIt, assetIt->second))
    {
      if (m_spritePreviewTextures.contains(it->first))
      {
        invalidateSpritePreviewTextures();
      }
      it = m_assetPreviews.erase(it);
      continue;
    }

    ++it;
  }
}

void ModelBrowserView::loadMissingVisiblePreviews(
  Layout& layout, const float y, const float height)
{
  auto paths = std::set<std::filesystem::path>{};
  for (const auto& group : layout.groups())
  {
    if (!group.intersectsY(y, height))
    {
      continue;
    }

    for (const auto& row : group.rows())
    {
      if (!row.intersectsY(y, height))
      {
        continue;
      }

      for (const auto& cell : row.cells())
      {
        const auto& item = cellData(cell);
        const auto* asset = assetAt(item.path);
        if (asset && asset->skybox)
        {
          // A skybox group is a manifest of six faces, not an image to decode.
          continue;
        }
        if (canLoadPreview(item.type) && !m_assetPreviews.contains(item.path))
        {
          paths.insert(item.path);
        }
      }
    }
  }

  if (paths.empty())
  {
    return;
  }

  auto previews = loadAssetPreviews(
    AssetPreviewProvider{m_document.map().gameFileSystem()},
    m_assets,
    paths,
    m_previewPathResolver);
  m_assetPreviews.merge(previews);
}

void ModelBrowserView::invalidateSpritePreviewTextures()
{
  for (const auto& [path, texture] : m_spritePreviewTextures)
  {
    unused(path);
    if (texture.textureId != 0)
    {
      m_pendingDeletedSpriteTextures.push_back(texture.textureId);
    }
  }
  m_spritePreviewTextures.clear();
}

void ModelBrowserView::deletePendingSpritePreviewTextures(gl::Gl& gl)
{
  if (m_pendingDeletedSpriteTextures.empty())
  {
    return;
  }

  gl.deleteTextures(
    GLsizei(m_pendingDeletedSpriteTextures.size()),
    m_pendingDeletedSpriteTextures.data());
  m_pendingDeletedSpriteTextures.clear();
}

const ModelBrowserView::SpritePreviewTexture* ModelBrowserView::
  ensureSpritePreviewTexture(
    gl::Gl& gl, const std::filesystem::path& path, const GoldSrcSpritePreview& preview)
{
  if (preview.rgba.empty())
  {
    return nullptr;
  }

  if (auto it = m_spritePreviewTextures.find(path);
      it != std::end(m_spritePreviewTextures))
  {
    if (it->second.width == preview.width && it->second.height == preview.height)
    {
      return &it->second;
    }

    if (it->second.textureId != 0)
    {
      gl.deleteTextures(1, &it->second.textureId);
    }
    m_spritePreviewTextures.erase(it);
  }

  auto textureId = GLuint{0};
  gl.genTextures(1, &textureId);
  gl.bindTexture(GL_TEXTURE_2D, textureId);
  gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl.texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl.pixelStorei(GL_UNPACK_ALIGNMENT, 1);
  gl.texImage2D(
    GL_TEXTURE_2D,
    0,
    GL_RGBA,
    GLsizei(preview.width),
    GLsizei(preview.height),
    0,
    GL_RGBA,
    GL_UNSIGNED_BYTE,
    preview.rgba.data());
  gl.bindTexture(GL_TEXTURE_2D, 0);

  const auto [it, inserted] = m_spritePreviewTextures.emplace(
    path, SpritePreviewTexture{textureId, preview.width, preview.height});
  unused(inserted);
  return &it->second;
}

bool ModelBrowserView::shouldRenderFocusIndicator() const
{
  return false;
}

Color ModelBrowserView::getBackgroundColor()
{
  return browserBackgroundColor(palette());
}

bool ModelBrowserView::isCellSelected(const Cell& cell) const
{
  if (!m_hasSelection)
  {
    return false;
  }

  const auto& item = cellData(cell);
  return item.type == m_selectedType && item.path == m_selectedPath;
}

void ModelBrowserView::doLeftClick(Layout& layout, const float x, const float y)
{
  if (const auto* cell = layout.cellAt(x, y))
  {
    if (soundPreviewButtonHitTest(*cell, x, y))
    {
      toggleSoundPreview(cellData(*cell).path);
      return;
    }

    const auto& item = cellData(*cell);
    if (
      m_hasSelection && (m_selectedType != item.type || m_selectedPath != item.path)
      && m_selectedType == BrowserCellType::Sound)
    {
      stopSoundPreview();
    }

    m_selectedType = item.type;
    m_selectedPath = item.path;
    m_hasSelection = true;
    update();
  }
}

void ModelBrowserView::doDoubleClick(Layout& layout, const float x, const float y)
{
  if (m_previewOnly)
    return;
  if (const auto* cell = layout.cellAt(x, y))
  {
    const auto& item = cellData(*cell);
    if (item.type == BrowserCellType::Folder)
    {
      emit folderActivated(QString::fromStdString(item.path.generic_string()));
    }
    else if (item.type == BrowserCellType::Map)
    {
      emit mapActivated(pathAsGenericQString(item.path));
    }
    else
    {
      // A skybox group is a manifest of six faces, so there is no single image to show.
      const auto* asset = assetAt(item.path);
      if (asset && asset->skybox)
        return;
      emit assetPreviewRequested(pathAsGenericQString(item.path));
    }
  }
}

void ModelBrowserView::doContextMenu(
  Layout& layout, const float x, const float y, QContextMenuEvent* event)
{
  if (m_previewOnly)
    return;
  const auto* cell = layout.cellAt(x, y);
  if (!cell)
  {
    return;
  }

  const auto& item = cellData(*cell);
  if (const auto* asset = assetAt(item.path); asset && isLibraryAsset(*asset))
  {
    auto menu = QMenu{this};
    const auto libraryId = QString::fromStdString(asset->libraryId);
    const auto libraryAssetId = QString::fromStdString(asset->libraryAssetId);
    auto* capture = menu.addAction(
      asset->projectAssetId.empty() ? tr("Add to Project")
                                    : tr("Use Current Library Version"));
    connect(capture, &QAction::triggered, this, [=, this]() {
      emit libraryAssetCaptureRequested(libraryId, libraryAssetId);
    });
    if (!asset->projectAssetId.empty())
    {
      const auto projectAssetId = QString::fromStdString(asset->projectAssetId);
      auto* update = menu.addAction(tr("Update Project Resource"));
      update->setEnabled(asset->projectUpdateAvailable);
      connect(update, &QAction::triggered, this, [=, this]() {
        emit libraryAssetUpdateRequested(projectAssetId);
      });
      auto* uses = menu.addAction(tr("Find Uses in Project"));
      connect(uses, &QAction::triggered, this, [=, this]() {
        emit libraryAssetFindUsesRequested(projectAssetId);
      });
    }
    menu.exec(event->globalPos());
    return;
  }

  if (item.type == BrowserCellType::Prefab)
  {
    auto menu = QMenu{this};
    menu.addAction(tr("Rename..."), this, [this, prefabPath = item.path]() {
      emit renamePrefabRequested(pathAsGenericQString(prefabPath));
    });
    menu.addAction(tr("Delete"), this, [this, prefabPath = item.path]() {
      emit deletePrefabRequested(pathAsGenericQString(prefabPath));
    });
    menu.exec(event->globalPos());
    return;
  }

  if (item.type != BrowserCellType::Model)
  {
    return;
  }

  if (kdl::path_to_lower(item.path.extension()) != ".mdl")
  {
    return;
  }

  auto menu = QMenu{this};
  menu.addAction(tr("Scale MDL..."), this, [this, modelPath = item.path]() {
    auto ok = false;
    const auto scale = QInputDialog::getDouble(
      this, tr("Scale MDL"), tr("Scale factor"), 1.0, 0.001, 1000.0, 3, &ok);
    if (!ok)
    {
      return;
    }

    if (scale == 1.0)
    {
      return;
    }

    auto absPath = std::filesystem::path{};
    if (modelPath.is_absolute())
    {
      absPath = modelPath;
    }
    else
    {
      const auto absPathResult =
        m_document.map().gameFileSystem().makeAbsolute(modelPath);
      if (!absPathResult.is_error())
      {
        absPath = absPathResult.value();
      }
    }

    if (absPath.empty() || fs::Disk::pathInfo(absPath) != fs::PathInfo::File)
    {
      QMessageBox::warning(
        this, tr("Error"), tr("Cannot locate a writable MDL file path."));
      return;
    }

    const auto project = m_appController.projectFor(m_document);
    auto temporary = QTemporaryDir{};
    auto scaledPath = absPath;
    auto projectAsset = std::optional<project::Asset>{};
    if (project)
    {
      const auto assets = project->store().assets(project::AssetType::Model);
      if (assets.is_error())
      {
        QMessageBox::warning(
          this, tr("Error"), QString::fromStdString(std::get<Error>(assets.error()).msg));
        return;
      }
      for (const auto& asset : assets.value())
        if (pathFromQString(QString::fromStdString(asset.publishPath)) == modelPath)
          projectAsset = asset;
      if (!projectAsset || !temporary.isValid())
      {
        QMessageBox::warning(
          this, tr("Error"), tr("Cannot prepare the project model for scaling."));
        return;
      }
      scaledPath = pathFromQString(temporary.path()) / "scaled.mdl";
      if (!QFile::copy(pathAsQString(absPath), pathAsQString(scaledPath)))
      {
        QMessageBox::warning(this, tr("Error"), tr("Cannot copy the model for scaling."));
        return;
      }
    }
    auto result = io::scaleGoldSrcMdlFile(scaledPath, float(scale));
    if (result.is_success() && projectAsset)
    {
      auto file = QFile{pathAsQString(scaledPath)};
      if (!file.open(QIODevice::ReadOnly))
        result = Error{file.errorString().toStdString()};
      else
      {
        const auto bytes = file.readAll();
        auto input = project::AssetImport{};
        input.type = project::AssetType::Model;
        input.sourceName = projectAsset->sourceName;
        auto recipe = QJsonDocument::fromJson(
                        QByteArray::fromStdString(projectAsset->conversionRecipe))
                        .object();
        auto operations = recipe.value("operations").toArray();
        operations.append(
          QJsonObject{{"operation", "scale-mdl"}, {"version", 1}, {"factor", scale}});
        recipe.insert("operations", operations);
        input.conversionRecipe =
          QJsonDocument{recipe}.toJson(QJsonDocument::Compact).toStdString();
        const auto original =
          project->store().readAsset(projectAsset->id, project::ContentKind::Original);
        if (file.error() != QFileDevice::NoError)
          result = Error{file.errorString().toStdString()};
        else if (original.is_error())
          result = std::get<Error>(original.error());
        else
        {
          input.original = original.value();
          input.compatible.assign(bytes.begin(), bytes.end());
          result =
            project->replaceAsset(projectAsset->id, input) | kdl::transform([](auto) {});
        }
      }
    }
    if (result.is_error())
    {
      const auto err = result.error();
      const auto& e = std::get<tb::Error>(err);
      QMessageBox::warning(this, tr("Error"), QString::fromStdString(e.msg));
      return;
    }

    m_document.map().reloadEntityModels(modelPath);
    invalidate();
    update();
  });

  menu.exec(event->globalPos());
}

void ModelBrowserView::ensureSoundPlayer()
{
  if (m_soundPlayer)
  {
    return;
  }

  m_soundAudioOutput = new QAudioOutput{this};
  m_soundPlayer = new QMediaPlayer{this};
  m_soundPlayer->setAudioOutput(m_soundAudioOutput);
  m_soundAudioOutput->setVolume(1.0f);

  connect(
    m_soundPlayer, &QMediaPlayer::playbackStateChanged, this, [this](const auto state) {
      if (state == QMediaPlayer::StoppedState && !m_playingSoundPath.empty())
      {
        m_playingSoundPath.clear();
        update();
      }
    });

  connect(
    m_soundPlayer,
    &QMediaPlayer::errorOccurred,
    this,
    [this](auto, const QString& error) {
      m_document.logger().error()
        << "Could not preview sound "
        << m_soundPlayer->source().toDisplayString().toStdString() << ": "
        << error.toStdString();
      m_playingSoundPath.clear();
      update();
    });
}

void ModelBrowserView::stopSoundPreview()
{
  if (m_soundPlayer)
  {
    m_soundPlayer->stop();
  }
  m_playingSoundPath.clear();
}

void ModelBrowserView::toggleSoundPreview(const std::filesystem::path& path)
{
  const auto* preview = assetPreview(path);
  if (
    !preview || preview->status != AssetPreviewStatus::Ready
    || preview->soundPath.empty())
  {
    stopSoundPreview();
    update();
    return;
  }

  if (m_playingSoundPath == path)
  {
    stopSoundPreview();
    update();
    return;
  }

  stopSoundPreview();
  ensureSoundPlayer();
  if (!m_soundPlayer->isAvailable())
  {
    m_document.logger().error()
      << "Could not preview sound: no Qt multimedia backend is available. "
         "Check that the multimedia plugins and their dependencies are installed.";
    update();
    return;
  }
  m_playingSoundPath = path;
  m_soundPlayer->setSource(
    QUrl::fromLocalFile(pathAsQString(preview->soundPath.lexically_normal())));
  m_soundPlayer->play();
  update();
}

bool ModelBrowserView::soundPreviewButtonHitTest(
  const Cell& cell, const float x, const float y) const
{
  const auto& item = cellData(cell);
  return item.type == BrowserCellType::Sound && m_hasSelection
         && m_selectedType == BrowserCellType::Sound && m_selectedPath == item.path
         && canPreviewSound(item.path)
         && tb::ui::soundPreviewButtonHitTest(cell.itemBounds(), x, y);
}

bool ModelBrowserView::canPreviewSound(const std::filesystem::path& path) const
{
  const auto* preview = assetPreview(path);
  return preview && preview->status == AssetPreviewStatus::Ready
         && !preview->soundPath.empty();
}

bool ModelBrowserView::isPreviewingSound(const std::filesystem::path& path) const
{
  return !m_playingSoundPath.empty() && m_playingSoundPath == path && m_soundPlayer
         && m_soundPlayer->playbackState() == QMediaPlayer::PlayingState;
}

void ModelBrowserView::renderAssetPlaceholders(
  gl::Gl& gl, Layout& layout, const float y, const float height)
{
  using Vertex = gl::VertexTypes::P2C4::Vertex;
  using TextVertex = gl::VertexTypes::P2Uv2C4::Vertex;
  auto backgroundVertices = std::vector<Vertex>{};
  auto iconVertices = std::vector<Vertex>{};
  auto errorTextVertices = std::vector<TextVertex>{};
  auto errorFont = gl::FontDescriptor{
    pref(Preferences::RendererFontPath),
    size_t(std::max(8, pref(Preferences::BrowserFontSize) - 1))};

  const auto addQuad = [&](
                         auto& vertices,
                         const float left,
                         const float top,
                         const float right,
                         const float bottom,
                         const vm::vec4f& color) {
    vertices.emplace_back(vm::vec2f{left, height - (top - y)}, color);
    vertices.emplace_back(vm::vec2f{left, height - (bottom - y)}, color);
    vertices.emplace_back(vm::vec2f{right, height - (bottom - y)}, color);
    vertices.emplace_back(vm::vec2f{right, height - (top - y)}, color);
  };

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
            const auto& item = cellData(cell);
            const auto* preview =
              canRenderImagePreview(item.type) || item.type == BrowserCellType::Sound
                ? assetPreview(item.path)
                : nullptr;
            if (
              preview && preview->status == AssetPreviewStatus::Ready && preview->sprite
              && !preview->sprite->rgba.empty())
            {
              continue;
            }

            const auto color = assetPlaceholderColor(item.type, palette());
            if (!color)
            {
              continue;
            }

            const auto& bounds = cell.itemBounds();
            const auto colorVec = color->toVec();
            appendRoundedRectVertices(
              backgroundVertices,
              bounds.left() + 6.0f,
              bounds.top() + 6.0f,
              bounds.right() - 6.0f,
              bounds.bottom() - 6.0f,
              y,
              height,
              colorVec);

            if (item.type == BrowserCellType::Sound)
            {
              const auto iconColor =
                RgbaF{browserTextColor(palette()).to<RgbF>(), 0.88f}.toVec();
              const auto iconSize = std::min(bounds.width, bounds.height) * 0.48f;
              const auto left = bounds.left() + (bounds.width - iconSize) / 2.0f;
              const auto top = bounds.top() + (bounds.height - iconSize) / 2.0f;
              const auto unit = iconSize / 16.0f;
              const auto cy = top + iconSize / 2.0f;

              addQuad(
                iconVertices,
                left + 1.0f * unit,
                cy - 3.0f * unit,
                left + 4.0f * unit,
                cy + 3.0f * unit,
                iconColor);
              addQuad(
                iconVertices,
                left + 4.0f * unit,
                cy - 5.0f * unit,
                left + 7.0f * unit,
                cy + 5.0f * unit,
                iconColor);
              addQuad(
                iconVertices,
                left + 9.0f * unit,
                cy - 3.0f * unit,
                left + 10.4f * unit,
                cy + 3.0f * unit,
                iconColor);
              addQuad(
                iconVertices,
                left + 11.6f * unit,
                cy - 4.6f * unit,
                left + 13.0f * unit,
                cy + 4.6f * unit,
                iconColor);
              addQuad(
                iconVertices,
                left + 14.2f * unit,
                cy - 6.2f * unit,
                left + 15.6f * unit,
                cy + 6.2f * unit,
                iconColor);
            }
            if (
              item.type == BrowserCellType::Map
              || shouldRenderErrorText(item.type, preview))
            {
              const auto errorText =
                std::string{item.type == BrowserCellType::Map ? "MAP" : "ERROR"};
              auto& font = fontManager().font(errorFont);
              const auto textSize = font.measure(errorText);
              const auto textOffset = vm::vec2f{
                bounds.left() + std::max((bounds.width - textSize.x()) / 2.0f, 0.0f),
                height - (bounds.top() - y) - (bounds.height - textSize.y()) / 2.0f};
              const auto textColor = item.type == BrowserCellType::Map
                                       ? browserTextColor(palette()).to<RgbaF>().toVec()
                                       : browserErrorColor(palette()).to<RgbaF>().toVec();
              const auto textQuads = font.quads(errorText, false, textOffset);
              const auto textVertices = TextVertex::toList(kdl::views::zip(
                textQuads | kdl::views::stride(2),
                textQuads | std::views::drop(1) | kdl::views::stride(2),
                kdl::views::repeat(textColor)));
              kdl::vec_append(errorTextVertices, textVertices);
            }
          }
        }
      }
    }
  }

  if (backgroundVertices.empty())
  {
    return;
  }

  auto shader =
    gl::ActiveShader{gl, shaderManager(), gl::Shaders::MaterialBrowserBorderShader};

  auto vertexArray = gl::VertexArray::move(std::move(backgroundVertices));
  vertexArray.prepare(gl, vboManager());
  gl.disable(GL_CULL_FACE);
  if (vertexArray.setup(gl, shader.program()))
  {
    vertexArray.render(gl, gl::PrimType::Triangles);
    vertexArray.cleanup(gl, shader.program());
  }
  gl.enable(GL_CULL_FACE);

  auto iconVertexArray = gl::VertexArray::move(std::move(iconVertices));
  iconVertexArray.prepare(gl, vboManager());
  if (iconVertexArray.setup(gl, shader.program()))
  {
    iconVertexArray.render(gl, gl::PrimType::Quads);
    iconVertexArray.cleanup(gl, shader.program());
  }

  if (!errorTextVertices.empty())
  {
    auto textVertexArray = gl::VertexArray::ref(errorTextVertices);
    textVertexArray.prepare(gl, vboManager());

    auto textShader =
      gl::ActiveShader{gl, shaderManager(), gl::Shaders::ColoredTextShader};
    textShader.set("Texture", 0);
    if (textVertexArray.setup(gl, textShader.program()))
    {
      fontManager().font(errorFont).activate(gl);
      textVertexArray.render(gl, gl::PrimType::Quads);
      textVertexArray.cleanup(gl, textShader.program());
      fontManager().font(errorFont).deactivate(gl);
    }
  }
}

void ModelBrowserView::renderSoundPreviewButtons(
  gl::Gl& gl, Layout& layout, const float y, const float height)
{
  if (
    !m_hasSelection || m_selectedType != BrowserCellType::Sound
    || !canPreviewSound(m_selectedPath))
  {
    return;
  }

  using Vertex = gl::VertexTypes::P2C4::Vertex;
  auto backgroundVertices = std::vector<Vertex>{};
  auto iconVertices = std::vector<Vertex>{};

  const auto addQuad = [&](
                         auto& vertices,
                         const float left,
                         const float top,
                         const float right,
                         const float bottom,
                         const vm::vec4f& color) {
    vertices.emplace_back(vm::vec2f{left, height - (top - y)}, color);
    vertices.emplace_back(vm::vec2f{left, height - (bottom - y)}, color);
    vertices.emplace_back(vm::vec2f{right, height - (bottom - y)}, color);
    vertices.emplace_back(vm::vec2f{right, height - (top - y)}, color);
  };
  const auto addTriangle = [&](
                             auto& vertices,
                             const vm::vec2f& a,
                             const vm::vec2f& b,
                             const vm::vec2f& c,
                             const vm::vec4f& color) {
    vertices.emplace_back(vm::vec2f{a.x(), height - (a.y() - y)}, color);
    vertices.emplace_back(vm::vec2f{b.x(), height - (b.y() - y)}, color);
    vertices.emplace_back(vm::vec2f{c.x(), height - (c.y() - y)}, color);
  };

  for (const auto& group : layout.groups())
  {
    if (!group.intersectsY(y, height))
    {
      continue;
    }

    for (const auto& row : group.rows())
    {
      if (!row.intersectsY(y, height))
      {
        continue;
      }

      for (const auto& cell : row.cells())
      {
        const auto& item = cellData(cell);
        if (
          item.type != BrowserCellType::Sound || item.path != m_selectedPath
          || !canPreviewSound(item.path))
        {
          continue;
        }

        const auto bounds = soundPreviewButtonBounds(cell.itemBounds());
        const auto backgroundColor = browserCellHoverColor(palette()).to<RgbaF>().toVec();
        const auto iconColor =
          RgbaF{browserTextColor(palette()).to<RgbF>(), 0.96f}.toVec();
        appendRoundedRectVertices(
          backgroundVertices,
          bounds.left(),
          bounds.top(),
          bounds.right(),
          bounds.bottom(),
          y,
          height,
          backgroundColor,
          5.0f);

        const auto pad = bounds.width * 0.28f;
        if (isPreviewingSound(item.path))
        {
          const auto barWidth = bounds.width * 0.16f;
          addQuad(
            iconVertices,
            bounds.left() + pad,
            bounds.top() + pad,
            bounds.left() + pad + barWidth,
            bounds.bottom() - pad,
            iconColor);
          addQuad(
            iconVertices,
            bounds.right() - pad - barWidth,
            bounds.top() + pad,
            bounds.right() - pad,
            bounds.bottom() - pad,
            iconColor);
        }
        else
        {
          addTriangle(
            iconVertices,
            vm::vec2f{bounds.left() + pad, bounds.top() + pad},
            vm::vec2f{bounds.left() + pad, bounds.bottom() - pad},
            vm::vec2f{bounds.right() - pad, bounds.top() + bounds.height / 2.0f},
            iconColor);
        }
      }
    }
  }

  if (backgroundVertices.empty())
  {
    return;
  }

  auto shader =
    gl::ActiveShader{gl, shaderManager(), gl::Shaders::MaterialBrowserBorderShader};

  auto backgroundVertexArray = gl::VertexArray::move(std::move(backgroundVertices));
  backgroundVertexArray.prepare(gl, vboManager());
  gl.disable(GL_CULL_FACE);
  if (backgroundVertexArray.setup(gl, shader.program()))
  {
    backgroundVertexArray.render(gl, gl::PrimType::Triangles);
    backgroundVertexArray.cleanup(gl, shader.program());
  }
  gl.enable(GL_CULL_FACE);

  auto iconVertexArray = gl::VertexArray::move(std::move(iconVertices));
  iconVertexArray.prepare(gl, vboManager());
  if (iconVertexArray.setup(gl, shader.program()))
  {
    iconVertexArray.render(
      gl,
      isPreviewingSound(m_selectedPath) ? gl::PrimType::Quads : gl::PrimType::Triangles);
    iconVertexArray.cleanup(gl, shader.program());
  }
}

void ModelBrowserView::renderSpritePreviews(
  gl::Gl& gl, Layout& layout, const float y, const float height)
{
  using Vertex = gl::VertexTypes::P2Uv2::Vertex;

  auto shader = gl::ActiveShader{gl, shaderManager(), gl::Shaders::MaterialBrowserShader};
  shader.set("ApplyTinting", false);
  shader.set("Material", 0);
  shader.set("Brightness", 1.0f);

  for (const auto& group : layout.groups())
  {
    if (!group.intersectsY(y, height))
    {
      continue;
    }

    for (const auto& row : group.rows())
    {
      if (!row.intersectsY(y, height))
      {
        continue;
      }

      for (const auto& cell : row.cells())
      {
        const auto& item = cellData(cell);
        if (!canRenderImagePreview(item.type))
        {
          continue;
        }

        const auto* previewState = assetPreview(item.path);
        if (
          !previewState || previewState->status != AssetPreviewStatus::Ready
          || !previewState->sprite || previewState->sprite->rgba.empty())
        {
          continue;
        }
        const auto& preview = *previewState->sprite;

        const auto& bounds = cell.itemBounds();
        const auto maxWidth = bounds.width - 16.0f;
        const auto maxHeight = bounds.height - 16.0f;
        if (maxWidth <= 0.0f || maxHeight <= 0.0f)
        {
          continue;
        }

        const auto previewWidth = float(preview.width);
        const auto previewHeight = float(preview.height);
        const auto scale = std::min(maxWidth / previewWidth, maxHeight / previewHeight);
        const auto imageWidth = previewWidth * scale;
        const auto imageHeight = previewHeight * scale;
        const auto left = bounds.left() + (bounds.width - imageWidth) / 2.0f;
        const auto top = bounds.top() + (bounds.height - imageHeight) / 2.0f;
        const auto right = left + imageWidth;
        const auto bottom = top + imageHeight;

        const auto* texture = ensureSpritePreviewTexture(gl, item.path, preview);
        if (!texture || texture->textureId == 0)
        {
          continue;
        }

        auto vertices = std::vector<Vertex>{
          Vertex{vm::vec2f{left, height - (top - y)}, vm::vec2f{0, 0}},
          Vertex{vm::vec2f{left, height - (bottom - y)}, vm::vec2f{0, 1}},
          Vertex{vm::vec2f{right, height - (bottom - y)}, vm::vec2f{1, 1}},
          Vertex{vm::vec2f{right, height - (top - y)}, vm::vec2f{1, 0}},
        };

        gl.bindTexture(GL_TEXTURE_2D, texture->textureId);
        auto vertexArray = gl::VertexArray::move(std::move(vertices));
        vertexArray.prepare(gl, vboManager());
        if (vertexArray.setup(gl, shader.program()))
        {
          vertexArray.render(gl, gl::PrimType::Quads);
          vertexArray.cleanup(gl, shader.program());
        }

        gl.bindTexture(GL_TEXTURE_2D, 0);
      }
    }
  }
}

void ModelBrowserView::renderFolders(
  gl::Gl& gl, Layout& layout, const float y, const float height)
{
  ensureFolderIconTexture(gl);
  if (m_folderIconTextureId == 0)
  {
    return;
  }

  using IconVertex = gl::VertexTypes::P2Uv2::Vertex;
  auto iconVertices = std::vector<IconVertex>{};

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
            const auto& item = cellData(cell);
            if (item.type != BrowserCellType::Folder)
            {
              continue;
            }

            const auto& bounds = cell.itemBounds();
            const auto iconSize = std::min(bounds.width, bounds.height) * 0.6f;
            const auto left = bounds.left() + (bounds.width - iconSize) / 2.0f;
            const auto top = bounds.top() + (bounds.height - iconSize) / 2.0f;
            const auto right = left + iconSize;
            const auto bottom = top + iconSize;

            iconVertices.emplace_back(
              vm::vec2f{left, height - (top - y)}, vm::vec2f{0, 0});
            iconVertices.emplace_back(
              vm::vec2f{left, height - (bottom - y)}, vm::vec2f{0, 1});
            iconVertices.emplace_back(
              vm::vec2f{right, height - (bottom - y)}, vm::vec2f{1, 1});
            iconVertices.emplace_back(
              vm::vec2f{right, height - (top - y)}, vm::vec2f{1, 0});
          }
        }
      }
    }
  }

  if (iconVertices.empty())
  {
    return;
  }

  auto iconShader =
    gl::ActiveShader{gl, shaderManager(), gl::Shaders::MaterialBrowserShader};
  iconShader.set("ApplyTinting", false);
  iconShader.set("Material", 0);
  iconShader.set("Brightness", 0.8f);

  gl.activeTexture(GL_TEXTURE0);
  gl.bindTexture(GL_TEXTURE_2D, m_folderIconTextureId);

  auto iconVertexArray = gl::VertexArray::move(std::move(iconVertices));
  iconVertexArray.prepare(gl, vboManager());
  if (iconVertexArray.setup(gl, iconShader.program()))
  {
    iconVertexArray.render(gl, gl::PrimType::Quads);
    iconVertexArray.cleanup(gl, iconShader.program());
  }

  gl.bindTexture(GL_TEXTURE_2D, 0);
}

void ModelBrowserView::renderModels(
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
            const auto& item = cellData(cell);
            if (item.type != BrowserCellType::Model)
            {
              continue;
            }
            auto* manager = &entityModelManager;
            auto spec = mdl::ModelSpecification{item.path, 0, 0};
            if (const auto* asset = assetAt(item.path);
                asset && asset->source != BrowserSource::GameFiles)
            {
              if (!asset->absolutePath.is_absolute())
              {
                m_assetPreviews[item.path] = {
                  AssetPreviewStatus::Error,
                  std::nullopt,
                  {},
                  "The catalog model preview file is unavailable."};
                continue;
              }
              auto& preview = m_catalogModels[item.path];
              if (!preview)
                preview = std::make_unique<CatalogModelPreview>(
                  asset->absolutePath,
                  m_document.map().gameInfo(),
                  [this](auto loader) {
                    auto resource = gl::createResourceSync(std::move(loader));
                    m_appController.glManager().resourceManager().addResource(resource);
                    return resource;
                  },
                  m_document.logger(),
                  asset->path);
              manager = &preview->models();
              spec = preview->specification();
            }
            manager->prepare(gl, vboManager());
            auto* modelRenderer = manager->renderer(spec);
            if (!modelRenderer)
            {
              if (manager != &entityModelManager)
                m_assetPreviews[item.path] = {
                  AssetPreviewStatus::Error,
                  std::nullopt,
                  {},
                  "The catalog model could not be decoded."};
              continue;
            }
            if (manager != &entityModelManager)
              m_assetPreviews[item.path] = {AssetPreviewStatus::Ready};
            modelRenderer->prepare(gl, vboManager());

            shader.set("Orientation", int(mdl::Orientation::Oriented));

            auto bounds = vm::bbox3f{8.0f};
            if (const auto* frame = manager->frame(spec))
            {
              bounds = frame->bounds();
            }

            const auto center = bounds.center();
            const auto transform = vm::translation_matrix(center)
                                   * vm::rotation_matrix(m_rotation)
                                   * vm::translation_matrix(-center);

            const auto itemTrans = itemTransformation(cell, y, height, bounds, transform);
            shader.set("ModelMatrix", itemTrans);

            const auto multMatrix =
              render::MultiplyModelMatrix{transformation, itemTrans};

            auto renderFunc = gl::DefaultMaterialRenderFunc{
              pref(Preferences::TextureMinFilter), pref(Preferences::TextureMagFilter)};
            modelRenderer->render(gl, shader.program(), renderFunc);
          }
        }
      }
    }
  }
}

vm::mat4x4f ModelBrowserView::itemTransformation(
  const Cell& cell,
  const float y,
  const float height,
  const vm::bbox3f& bounds,
  const vm::mat4x4f& transform) const
{
  const auto offset =
    vm::vec3f{0.0f, cell.itemBounds().left(), height - (cell.itemBounds().bottom() - y)};

  const auto rotatedBounds = bounds.transform(transform);
  const auto rotationOffset =
    vm::vec3f{0.0f, -rotatedBounds.min.y(), -rotatedBounds.min.z()};

  const auto rotatedBoundsSize = rotatedBounds.size();
  const auto targetWidth = cell.itemBounds().width;
  const auto targetHeight = cell.itemBounds().height;

  const auto widthScale =
    rotatedBoundsSize.y() > 0.0f ? (targetWidth / rotatedBoundsSize.y()) : 1.0f;
  const auto heightScale =
    rotatedBoundsSize.z() > 0.0f ? (targetHeight / rotatedBoundsSize.z()) : 1.0f;
  const auto scaling = std::min(widthScale, heightScale);

  return vm::translation_matrix(offset) * vm::scaling_matrix(vm::vec3f::fill(scaling))
         * vm::translation_matrix(rotationOffset) * transform;
}

bool ModelBrowserView::dndEnabled()
{
  return !m_previewOnly;
}

QString ModelBrowserView::dndData(const Cell& cell)
{
  const auto& item = cellData(cell);
  if (const auto* asset = assetAt(item.path);
      asset && asset->source != BrowserSource::GameFiles)
  {
    if (
      item.type == BrowserCellType::Model || item.type == BrowserCellType::Sprite
      || item.type == BrowserCellType::Sound || item.type == BrowserCellType::Texture)
    {
      const auto session = m_appController.projectFor(m_document);
      return session ? catalogAssetDragData(*asset, session->store().info().id)
                     : QString{};
    }
    if (isLibraryAsset(*asset))
      return {};
  }

  switch (item.type)
  {
  case BrowserCellType::Model:
    return QString{"model:"} + pathAsGenericQString(item.path);
  case BrowserCellType::Sprite:
    return QString{"sprite:"} + pathAsGenericQString(item.path);
  case BrowserCellType::Sound:
    return QString{"sound:"} + pathAsGenericQString(item.path);
  case BrowserCellType::Prefab:
    return QString{"prefab:"} + pathAsGenericQString(item.path);
  case BrowserCellType::Folder:
  case BrowserCellType::Map:
  case BrowserCellType::Texture:
    return "";
  }

  return "";
}

QString ModelBrowserView::tooltip(const Cell& cell)
{
  const auto& item = cellData(cell);
  if (item.type == BrowserCellType::Folder)
  {
    const auto folderAbs =
      item.path.empty() ? m_rootFolderPath : (m_rootFolderPath / item.path);
    return QString::fromStdString(folderAbs.generic_string());
  }
  if (const auto* asset = assetAt(item.path))
  {
    auto status = browserAssetStatus(*asset);
    if (const auto* preview = assetPreview(item.path); preview)
    {
      if (!preview->error.empty())
        status = tr("Preview failed: %1").arg(QString::fromStdString(preview->error));
      else if (preview->status == AssetPreviewStatus::Error)
        status = tr("Preview failed: the resource could not be decoded.");
      else if (preview->status == AssetPreviewStatus::Missing)
        status = tr("Preview file is missing.");
    }
    if (!status.isEmpty())
    {
      return QString{"%1\n%2 — %3"}
        .arg(QString::fromStdString(item.path.generic_string()))
        .arg(QString::fromLatin1(browserSourceName(asset->source)))
        .arg(status);
    }
  }
  return QString::fromStdString(item.path.generic_string());
}

const BrowserAsset* ModelBrowserView::assetAt(const std::filesystem::path& path) const
{
  const auto it =
    std::ranges::find_if(m_assets, [&](const auto& asset) { return asset.path == path; });
  return it == m_assets.end() ? nullptr : &*it;
}

const BrowserCellData& ModelBrowserView::cellData(const Cell& cell) const
{
  return cell.itemAs<BrowserCellData>();
}

} // namespace tb::ui
