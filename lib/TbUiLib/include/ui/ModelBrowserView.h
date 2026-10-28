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

#pragma once

#include <QImage>
#include <QString>

#include "base/NotifierConnection.h"
#include "ui/AssetBrowserModel.h"
#include "ui/AssetPreviewProvider.h"
#include "ui/CellView.h"

#include "vm/bbox.h"
#include "vm/quat.h" // IWYU pragma: keep

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <vector>

class QContextMenuEvent;
class QAudioOutput;
class QMediaPlayer;
class QScrollBar;

namespace tb::mdl
{
class ResourceId;
} // namespace tb::mdl

namespace tb::render
{
class Transformation;
} // namespace tb::render

namespace tb::gl
{
class Gl;
} // namespace tb::gl

namespace tb::ui
{

class AppController;
class MapDocument;

LayoutBounds soundPreviewButtonBounds(const LayoutBounds& itemBounds);
bool soundPreviewButtonHitTest(const LayoutBounds& itemBounds, float x, float y);

class ModelBrowserView : public CellView
{
  Q_OBJECT
private:
  static constexpr auto CameraPosition = vm::vec3f{256.0f, 0.0f, 0.0f};
  static constexpr auto CameraDirection = vm::vec3f{-1, 0, 0};
  static constexpr auto CameraUp = vm::vec3f{0, 0, 1};

private:
  AppController& m_appController;
  MapDocument& m_document;
  vm::quatf m_rotation;
  QScrollBar* m_scrollBar = nullptr;
  std::filesystem::path m_rootFolderPath;
  std::vector<BrowserAsset> m_assets;
  std::filesystem::path m_currentFolderPath;
  QString m_searchText;

  NotifierConnection m_notifierConnection;
  QImage m_folderIconImage;
  GLuint m_folderIconTextureId = 0;
  AssetPreviewMap m_assetPreviews;
  struct SpritePreviewTexture
  {
    GLuint textureId = 0;
    size_t width = 0;
    size_t height = 0;
  };
  std::map<std::filesystem::path, SpritePreviewTexture> m_spritePreviewTextures;
  std::vector<GLuint> m_pendingDeletedSpriteTextures;
  QMediaPlayer* m_soundPlayer = nullptr;
  QAudioOutput* m_soundAudioOutput = nullptr;
  std::filesystem::path m_playingSoundPath;
  bool m_hasSelection = false;
  BrowserCellType m_selectedType = BrowserCellType::Folder;
  std::filesystem::path m_selectedPath;
  // Game files are browsed through fixed roots; catalog listings only show the folders
  // their own entries imply.
  bool m_includeDefaultRoots = true;
  std::function<std::filesystem::path(const BrowserAsset&)> m_previewPathResolver;

public:
  ModelBrowserView(
    AppController& appController, QScrollBar* scrollBar, MapDocument& document);
  ~ModelBrowserView() override;

  void setAssets(std::filesystem::path rootFolderPath, std::vector<BrowserAsset> assets);
  void setCurrentFolderPath(std::filesystem::path currentFolderPath);
  void setSearchText(QString searchText);
  void setIncludeDefaultRoots(bool includeDefaultRoots);
  /**
   * Resolves a previewable file for entries that have no file of their own.
   *
   * A library texture has no path until its compatible content is materialized, so the
   * browser resolves it on demand for the cells it is about to draw instead of
   * duplicating the whole library into the cache.
   */
  void setPreviewPathResolver(
    std::function<std::filesystem::path(const BrowserAsset&)> resolver);

private:
  void resourcesWereProcessed(const std::vector<mdl::ResourceId>& resources);

  void doInitLayout(Layout& layout) override;
  void doReloadLayout(Layout& layout) override;

  bool dndEnabled() override;
  QString dndData(const Cell& cell) override;

  void doClear() override;
  void doRender(gl::Gl& gl, Layout& layout, float y, float height) override;
  bool shouldRenderFocusIndicator() const override;
  Color getBackgroundColor() override;
  bool isCellSelected(const Cell& cell) const override;
  void doLeftClick(Layout& layout, float x, float y) override;
  void doDoubleClick(Layout& layout, float x, float y) override;
  void doContextMenu(Layout& layout, float x, float y, QContextMenuEvent* event) override;

  void ensureSoundPlayer();
  void stopSoundPreview();
  void toggleSoundPreview(const std::filesystem::path& path);
  bool soundPreviewButtonHitTest(const Cell& cell, float x, float y) const;
  bool canPreviewSound(const std::filesystem::path& path) const;
  bool isPreviewingSound(const std::filesystem::path& path) const;

  void ensureFolderIconTexture(gl::Gl& gl);
  void destroyFolderIconTexture();
  const AssetPreviewState* assetPreview(const std::filesystem::path& path) const;
  void removeStalePreviews(const std::vector<BrowserAsset>& assets);
  void loadMissingVisiblePreviews(Layout& layout, float y, float height);
  void invalidateSpritePreviewTextures();
  void deletePendingSpritePreviewTextures(gl::Gl& gl);
  const SpritePreviewTexture* ensureSpritePreviewTexture(
    gl::Gl& gl, const std::filesystem::path& path, const GoldSrcSpritePreview& preview);

  void renderFolders(gl::Gl& gl, Layout& layout, float y, float height);
  void renderModels(
    gl::Gl& gl,
    Layout& layout,
    float y,
    float height,
    render::Transformation& transformation);
  void renderAssetPlaceholders(gl::Gl& gl, Layout& layout, float y, float height);
  void renderSoundPreviewButtons(gl::Gl& gl, Layout& layout, float y, float height);
  void renderSpritePreviews(gl::Gl& gl, Layout& layout, float y, float height);

  vm::mat4x4f itemTransformation(
    const Cell& cell,
    float y,
    float height,
    const vm::bbox3f& bounds,
    const vm::mat4x4f& transform) const;

  QString tooltip(const Cell& cell) override;

  const BrowserCellData& cellData(const Cell& cell) const;
  const BrowserAsset* assetAt(const std::filesystem::path& path) const;

signals:
  void folderActivated(const QString& folderPath);
  void mapActivated(const QString& mapPath);
  void renamePrefabRequested(const QString& prefabPath);
  void deletePrefabRequested(const QString& prefabPath);
  /** Adds a library resource to the project so it becomes a real dependency. */
  void libraryAssetCaptureRequested(
    const QString& libraryId, const QString& libraryAssetId);
  /** Applies the current library revision to an existing project snapshot. */
  void libraryAssetUpdateRequested(const QString& projectAssetId);
  void libraryAssetFindUsesRequested(const QString& projectAssetId);
};

} // namespace tb::ui
