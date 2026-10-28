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

#include "ui/ModelBrowser.h"

#include <QComboBox>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "base/PreferenceManager.h"
#include "fs/DiskIO.h"
#include "fs/FileSystem.h"
#include "fs/PathInfo.h"
#include "fs/PathMatcher.h"
#include "fs/TraversalMode.h"
#include "mdl/BrushNode.h"
#include "mdl/EntityModelManager.h"
#include "mdl/EntityNode.h"
#include "mdl/GameConfig.h"
#include "mdl/GameFileSystem.h"
#include "mdl/GameInfo.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/Map_Assets.h"
#include "mdl/Map_CopyPaste.h"
#include "mdl/Map_Selection.h"
#include "mdl/Map_World.h"
#include "mdl/PatchNode.h"
#include "mdl/SetVisibilityCommand.h"
#include "mdl/Transaction.h"
#include "mdl/WadPropertyUtils.h"
#include "mdl/WorldNode.h"
#include "prefs/Preferences.h"
#include "ui/AppController.h"
#include "ui/AssetBrowserModel.h"
#include "ui/BitmapButton.h"
#include "ui/ImageUtils.h"
#include "ui/MapDocument.h"
#include "ui/MapView3D.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/ModelBrowserView.h"
#include "ui/PrefabAsset.h"
#include "ui/ProjectSession.h"
#include "ui/QPathUtils.h"
#include "ui/QWidgetUtils.h"
#include "ui/SearchBox.h"

#include "kd/overload.h"
#include "kd/path_utils.h"
#include "kd/ranges/to.h"

#include <algorithm>
#include <optional>
#include <ranges>

namespace tb::ui
{
namespace
{

const auto AssetRootPath = std::filesystem::path{};
const auto DocumentLoadAssetRefreshDelay = 1500;
constexpr auto PrefabThumbnailMaxSize = 512;

std::optional<float> assetIconSizeForComboIndex(QComboBox* combo, const int index)
{
  if (!combo || index < 0)
  {
    return std::nullopt;
  }

  auto ok = false;
  const auto value = combo->itemData(index).toFloat(&ok);
  return ok ? std::optional<float>{value} : std::nullopt;
}

std::filesystem::path userPrefabAssetPath(const std::filesystem::path& path)
{
  const auto prefix = std::filesystem::path{"prefabs"};
  if (path.empty() || !kdl::path_has_prefix(path, prefix))
  {
    return {};
  }

  return configuredPrefabDirectory() / path.lexically_relative(prefix);
}

std::filesystem::path userPrefabBrowserPath(const std::filesystem::path& path)
{
  return std::filesystem::path{"prefabs"}
         / path.lexically_relative(configuredPrefabDirectory());
}

bool isolateSelectionForThumbnail(mdl::Map& map)
{
  auto selectedNodes = std::vector<mdl::Node*>{};
  auto unselectedNodes = std::vector<mdl::Node*>{};

  const auto collectNode = [&](auto& node) {
    if (node.transitivelySelected() || node.descendantSelected())
    {
      selectedNodes.push_back(&node);
    }
    else
    {
      unselectedNodes.push_back(&node);
    }
  };

  map.worldNode().accept(kdl::overload(
    [](auto&& thisLambda, mdl::WorldNode& worldNode) {
      worldNode.visitChildren(thisLambda);
    },
    [](auto&& thisLambda, mdl::LayerNode& layerNode) {
      layerNode.visitChildren(thisLambda);
    },
    [&](auto&& thisLambda, mdl::GroupNode& groupNode) {
      collectNode(groupNode);
      groupNode.visitChildren(thisLambda);
    },
    [&](auto&& thisLambda, mdl::EntityNode& entityNode) {
      collectNode(entityNode);
      entityNode.visitChildren(thisLambda);
    },
    [&](mdl::BrushNode& brushNode) { collectNode(brushNode); },
    [&](mdl::PatchNode& patchNode) { collectNode(patchNode); }));

  return map.executeAndStore(mdl::SetVisibilityCommand::hide(unselectedNodes))
         && map.executeAndStore(mdl::SetVisibilityCommand::show(selectedNodes));
}

MapWindow* parentMapWindow(QWidget& widget)
{
  return dynamic_cast<MapWindow*>(widget.window());
}

Result<void> savePrefabThumbnail(
  MapWindow& mapWindow, mdl::Map& map, const std::filesystem::path& prefabPath)
{
  auto* view = mapWindow.currentOrFirstVisible3DMapView();
  if (!view)
  {
    return Error{"No visible 3D view available for prefab thumbnail"};
  }

  auto transaction = mdl::Transaction{map, "Capture Prefab Thumbnail"};
  const auto selectedNodes = map.selection().nodes;
  if (!isolateSelectionForThumbnail(map))
  {
    transaction.cancel();
    return Error{"Failed to isolate selection for prefab thumbnail"};
  }
  mdl::deselectAll(map);

  auto image =
    cropPrefabThumbnailImage(view->grabPrefabThumbnailFramebuffer(selectedNodes));
  transaction.cancel();

  if (image.isNull())
  {
    return Error{"Failed to capture prefab thumbnail"};
  }

  if (std::max(image.width(), image.height()) > PrefabThumbnailMaxSize)
  {
    image = image.scaled(
      PrefabThumbnailMaxSize,
      PrefabThumbnailMaxSize,
      Qt::KeepAspectRatio,
      Qt::SmoothTransformation);
  }

  if (!image.save(pathAsQString(prefabThumbnailPath(prefabPath)), "PNG"))
  {
    return Error{"Failed to write prefab thumbnail"};
  }

  return kdl::void_success;
}

std::vector<std::string> currentPrefabWadPaths(const mdl::Map& map)
{
  if (const auto wadPropertyKey = map.gameInfo().gameConfig.materialConfig.property)
  {
    if (const auto* wadProperty = map.worldNode().entity().property(*wadPropertyKey))
    {
      return mdl::splitWadProperty(*wadProperty);
    }
  }
  return {};
}

} // namespace

ModelBrowser::ModelBrowser(
  AppController& appController, MapDocument& document, QWidget* parent)
  : QWidget{parent}
  , m_document{document}
  , m_project{appController.projectFor(document)}
{
  createGui(appController);
  bindEvents(appController);
  connectObservers();
  updateFolderEdit();
  if (m_project)
    connect(m_project.get(), &ProjectSession::contentsChanged, this, [this]() {
      markAssetsDirty();
      scheduleAssetRefresh();
    });
}

ModelBrowser::~ModelBrowser()
{
  if (m_fileSystemWatcher)
  {
    delete std::exchange(m_fileSystemWatcher, nullptr);
  }
}

void ModelBrowser::createGui(AppController& appController)
{
  m_pathStack = new QStackedWidget{};
  m_pathStack->setObjectName("ModelBrowser_PathStack");

  m_breadcrumbBar = new QWidget{};
  m_breadcrumbBar->setObjectName("ModelBrowser_BreadcrumbBar");
  m_breadcrumbLayout = new QHBoxLayout{};
  m_breadcrumbLayout->setContentsMargins(6, 0, 0, 0);
  m_breadcrumbLayout->setSpacing(4);
  m_breadcrumbBar->setLayout(m_breadcrumbLayout);
  m_breadcrumbBar->installEventFilter(this);

  m_folderEdit = new QLineEdit{};
  m_folderEdit->setObjectName("ModelBrowser_FolderEdit");
  m_folderEdit->installEventFilter(this);

  m_pathStack->addWidget(m_breadcrumbBar);
  m_pathStack->addWidget(m_folderEdit);
  m_pathStack->setCurrentWidget(m_breadcrumbBar);

  m_reloadButton = new QToolButton{};
  m_reloadButton->setObjectName("ModelBrowser_ReloadButton");
  m_reloadButton->setIcon(loadSVGIcon(std::filesystem::path{"Refresh.svg"}));
  m_reloadButton->setToolTip(tr("Reload assets"));
  m_reloadButton->setAutoRaise(true);
  m_reloadButton->setProperty("tbControlRole", "compact");
  m_reloadButton->setFixedSize(QSize{24, 24});

  m_savePrefabButton = createBitmapButton("Add.svg", tr("Save selection as prefab"));
  m_savePrefabButton->setObjectName("ModelBrowser_SavePrefabButton");
  m_savePrefabButton->setProperty("tbControlRole", "compact");
  m_savePrefabButton->setFixedSize(QSize{24, 24});
  m_savePrefabButton->setEnabled(false);

  m_iconSizeCombo = new QComboBox{};
  m_iconSizeCombo->setObjectName("ModelBrowser_IconSize");
  m_iconSizeCombo->setMinimumWidth(58);
  m_iconSizeCombo->setToolTip(tr("Asset icon size"));
  m_iconSizeCombo->addItem("50%", 0.5f);
  m_iconSizeCombo->addItem("75%", 0.75f);
  m_iconSizeCombo->addItem("100%", 1.0f);
  m_iconSizeCombo->addItem("150%", 1.5f);
  m_iconSizeCombo->addItem("200%", 2.0f);
  m_iconSizeCombo->addItem("250%", 2.5f);
  m_iconSizeCombo->addItem("300%", 3.0f);
  updateIconSizeCombo();

  m_searchBox = createSearchBox();
  m_searchBox->setObjectName("ModelBrowser_Search");

  auto* pathRowLayout = new QHBoxLayout{};
  pathRowLayout->setContentsMargins(0, 0, 0, 0);
  pathRowLayout->setSpacing(0);
  pathRowLayout->addWidget(m_pathStack, 1);
  pathRowLayout->addWidget(m_savePrefabButton, 0);
  pathRowLayout->addWidget(m_iconSizeCombo, 0);
  pathRowLayout->addWidget(m_reloadButton, 0);

  auto* controlsLayout = new QVBoxLayout{};
  controlsLayout->setContentsMargins(0, 0, 0, 0);
  controlsLayout->setSpacing(0);
  controlsLayout->addLayout(pathRowLayout, 0);
  controlsLayout->addWidget(m_searchBox, 0);

  auto* controls = new QWidget{};
  controls->setObjectName("ModelBrowser_Controls");
  controls->setAttribute(Qt::WA_StyledBackground);
  controls->setLayout(controlsLayout);

  m_folderTree = new QTreeWidget{};
  m_folderTree->setObjectName("ModelBrowser_FolderTree");
  m_folderTree->setHeaderHidden(true);
  m_folderTree->setUniformRowHeights(true);
  m_folderTree->setIconSize(QSize{16, 16});

  m_scrollBar = new QScrollBar{Qt::Vertical};
  m_view = new ModelBrowserView{appController, m_scrollBar, m_document};
  m_view->setSearchText(m_searchBox->text());

  auto* browserLayout = new QHBoxLayout{};
  browserLayout->setContentsMargins(0, 0, 0, 0);
  browserLayout->setSpacing(0);
  browserLayout->addWidget(m_view, 1);
  browserLayout->addWidget(m_scrollBar, 0);

  auto* browser = new QWidget{};
  browser->setObjectName("ModelBrowser_AssetView");
  browser->setLayout(browserLayout);

  auto* splitter = new QSplitter{Qt::Horizontal};
  splitter->setObjectName("ModelBrowser_Splitter");
  splitter->setChildrenCollapsible(false);
  splitter->addWidget(m_folderTree);
  splitter->addWidget(browser);
  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);

  auto* layout = new QVBoxLayout{};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(controls, 0);
  layout->addWidget(splitter, 1);
  setLayout(layout);

  m_fileSystemWatcher = new QFileSystemWatcher{this};
  m_rescanTimer = new QTimer{this};
  m_rescanTimer->setSingleShot(true);
}

void ModelBrowser::bindEvents(AppController& appController)
{
  connect(m_searchBox, &QLineEdit::textChanged, this, [&](const QString& text) {
    if (m_view)
    {
      m_view->setSearchText(text);
    }
  });

  connect(m_folderEdit, &QLineEdit::editingFinished, this, [&]() {
    if (!m_folderEdit->isModified())
    {
      showBreadcrumbBar();
      return;
    }

    const auto typedPath =
      std::filesystem::path{m_folderEdit->text().toStdString()}.lexically_normal();
    if (typedPath.empty() || typedPath == std::filesystem::path{"."})
    {
      setCurrentFolderPath(std::filesystem::path{});
      showBreadcrumbBar();
      return;
    }

    const auto rootPath = m_folderPath.lexically_normal();
    if (!rootPath.empty())
    {
      if (typedPath == rootPath)
      {
        setCurrentFolderPath(std::filesystem::path{});
        showBreadcrumbBar();
        return;
      }

      if (kdl::path_has_prefix(typedPath, rootPath))
      {
        setCurrentFolderPath(typedPath.lexically_relative(rootPath));
        showBreadcrumbBar();
        return;
      }
    }

    setFolderPath(typedPath);
    showBreadcrumbBar();
  });

  connect(m_fileSystemWatcher, &QFileSystemWatcher::directoryChanged, this, [&]() {
    scheduleRescan();
  });

  connect(m_rescanTimer, &QTimer::timeout, this, [&]() { rescanWatchedDirectory(); });

  connect(
    m_folderTree,
    &QTreeWidget::currentItemChanged,
    this,
    [&](QTreeWidgetItem* current, QTreeWidgetItem*) {
      if (!current)
      {
        return;
      }
      const auto rel = current->data(0, Qt::UserRole).toString();
      setCurrentFolderPath(std::filesystem::path{rel.toStdString()});
    });

  connect(
    m_view, &ModelBrowserView::folderActivated, this, [&](const QString& folderPath) {
      setCurrentFolderPath(std::filesystem::path{folderPath.toStdString()});
    });
  connect(
    m_view,
    &ModelBrowserView::mapActivated,
    this,
    [this, &appController](const QString& path) {
      if (!m_project)
        return;
      const auto asset =
        std::ranges::find(m_assets, pathFromQString(path), &BrowserAsset::path);
      if (asset == m_assets.end())
        return;
      const auto result =
        appController.mapWindowManager().loadProjectMap(m_project, asset->assetId);
      if (result.is_error())
        QMessageBox::critical(
          this,
          tr("Open Map"),
          QString::fromStdString(std::get<Error>(result.error()).msg));
    });
  connect(
    m_view, &ModelBrowserView::renamePrefabRequested, this, [&](const QString& path) {
      renamePrefab(pathFromQString(path));
    });
  connect(
    m_view, &ModelBrowserView::deletePrefabRequested, this, [&](const QString& path) {
      deletePrefab(pathFromQString(path));
    });

  connect(m_reloadButton, &QToolButton::clicked, this, [&]() {
    m_lastWriteTimes.clear();
    m_assetRefreshPending = false;
    rescanWatchedDirectory();
  });

  connect(
    m_savePrefabButton, &QToolButton::clicked, this, [&]() { saveSelectionAsPrefab(); });

  connect(
    m_iconSizeCombo,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    [&](const int index) {
      if (const auto value = assetIconSizeForComboIndex(m_iconSizeCombo, index))
      {
        setPref(Preferences::AssetBrowserIconSize, *value);
      }
    });
}

void ModelBrowser::connectObservers()
{
  m_notifierConnection +=
    m_document.documentWasLoadedNotifier.connect(this, &ModelBrowser::documentWasLoaded);
  m_notifierConnection +=
    PreferenceManager::instance().preferenceDidChangeNotifier.connect(
      [this](const auto& path) {
        if (path == Preferences::AssetBrowserIconSize.path)
        {
          updateIconSizeCombo();
          m_view->invalidate();
          m_view->update();
        }
      });
  connectMapObservers();
}

void ModelBrowser::connectMapObservers()
{
  m_mapNotifierConnection.disconnect();
  m_mapNotifierConnection +=
    m_document.map().modsDidChangeNotifier.connect(this, &ModelBrowser::modsDidChange);
  m_mapNotifierConnection += m_document.selectionDidChangeNotifier.connect(
    [this](const auto&) { updateSavePrefabButton(); });
  updateSavePrefabButton();
}

void ModelBrowser::documentWasLoaded()
{
  connectMapObservers();
  ++m_assetRefreshDeferralGeneration;
  m_assetRefreshPending = true;
  m_deferAssetRefreshAfterDocumentLoad = isVisible();
  scheduleDeferredAssetRefreshAfterDocumentLoad();
  updateSavePrefabButton();
}

void ModelBrowser::modsDidChange()
{
  if (m_folderPath != AssetRootPath)
  {
    setFolderPath(AssetRootPath);
    updateSavePrefabButton();
    return;
  }

  markAssetsDirty();
  updateSavePrefabButton();
}

void ModelBrowser::showEvent(QShowEvent* event)
{
  QWidget::showEvent(event);
  scheduleDeferredAssetRefreshAfterDocumentLoad();
  scheduleAssetRefresh();
}

void ModelBrowser::updateIconSizeCombo()
{
  if (!m_iconSizeCombo)
  {
    return;
  }

  const auto value = pref(Preferences::AssetBrowserIconSize);
  auto index = 2;
  for (auto i = 0; i < m_iconSizeCombo->count(); ++i)
  {
    if (assetIconSizeForComboIndex(m_iconSizeCombo, i) == value)
    {
      index = i;
      break;
    }
  }

  const auto blocker = QSignalBlocker{m_iconSizeCombo};
  m_iconSizeCombo->setCurrentIndex(index);
}

bool ModelBrowser::eventFilter(QObject* obj, QEvent* event)
{
  if (obj == m_breadcrumbBar && event->type() == QEvent::MouseButtonDblClick)
  {
    showPathEditor();
    return true;
  }

  if (obj == m_folderEdit && event->type() == QEvent::KeyPress)
  {
    const auto* keyEvent = static_cast<QKeyEvent*>(event);
    if (keyEvent->key() == Qt::Key_Escape)
    {
      showBreadcrumbBar();
      return true;
    }
  }

  return QWidget::eventFilter(obj, event);
}

void ModelBrowser::rebuildBreadcrumbBar()
{
  if (!m_breadcrumbLayout)
  {
    return;
  }

  while (auto* item = m_breadcrumbLayout->takeAt(0))
  {
    if (auto* w = item->widget())
    {
      w->deleteLater();
    }
    delete item;
  }

  const auto addSeparator = [&]() {
    auto* sep = new QLabel{QString::fromUtf8("›")};
    sep->setEnabled(false);
    m_breadcrumbLayout->addWidget(sep, 0);
  };

  const auto addCrumb = [&](
                          const QString& title, const std::filesystem::path& folderPath) {
    auto* button = new QToolButton{};
    button->setAutoRaise(true);
    button->setText(title);
    connect(button, &QToolButton::clicked, this, [this, folderPath]() {
      setCurrentFolderPath(folderPath);
    });
    m_breadcrumbLayout->addWidget(button, 0);
  };

  addCrumb(tr("All"), std::filesystem::path{});

  auto accumulated = std::filesystem::path{};
  for (const auto& part : m_currentFolderPath)
  {
    accumulated /= part;
    addSeparator();
    addCrumb(QString::fromStdString(part.generic_string()), accumulated);
  }

  m_breadcrumbLayout->addStretch(1);
}

void ModelBrowser::showPathEditor()
{
  if (!m_pathStack || !m_folderEdit)
  {
    return;
  }

  auto displayedPath = m_folderPath;
  if (!m_currentFolderPath.empty())
  {
    displayedPath /= m_currentFolderPath;
  }

  auto displayedText = displayedPath.generic_string();
  if (displayedText.empty())
  {
    displayedText = "assets";
  }

  m_folderEdit->setText(QString::fromStdString(displayedText));
  m_folderEdit->setModified(false);
  m_pathStack->setCurrentWidget(m_folderEdit);
  m_folderEdit->setFocus();
  m_folderEdit->selectAll();
}

void ModelBrowser::showBreadcrumbBar()
{
  if (!m_pathStack || !m_breadcrumbBar)
  {
    return;
  }

  updateFolderEdit();
  m_pathStack->setCurrentWidget(m_breadcrumbBar);
}

void ModelBrowser::updateFolderEdit()
{
  rebuildBreadcrumbBar();
  if (!m_folderEdit || !m_pathStack || m_pathStack->currentWidget() == m_folderEdit)
  {
    return;
  }
}

void ModelBrowser::updateSavePrefabButton()
{
  if (m_savePrefabButton)
  {
    m_savePrefabButton->setEnabled(m_document.map().selection().hasNodes());
  }
}

void ModelBrowser::saveSelectionAsPrefab()
{
  auto& map = m_document.map();
  if (!map.selection().hasNodes())
  {
    return;
  }

  auto ok = false;
  auto prefabName = QInputDialog::getText(
    this, tr("Save Prefab"), tr("Prefab name:"), QLineEdit::Normal, {}, &ok);
  if (!ok)
  {
    return;
  }

  prefabName = prefabName.trimmed();
  if (prefabName.isEmpty())
  {
    return;
  }

  if (m_project)
  {
    const auto text = mdl::serializeSelectedNodes(map);
    auto input = project::AssetImport{};
    input.type = project::AssetType::Prefab;
    input.displayName = prefabName.toStdString();
    input.virtualPath = "prefabs/" + input.displayName + ".tbprefab";
    input.original.assign(text.begin(), text.end());
    input.compatible = input.original;
    const auto result = m_project->importAsset(input);
    if (result.is_error())
      QMessageBox::warning(
        this,
        tr("Save Prefab"),
        QString::fromStdString(std::get<Error>(result.error()).msg));
    return;
  }

  const auto prefabDirectory = configuredPrefabDirectory();
  if (const auto result =
        checkPrefabNameAvailable(prefabDirectory, prefabName.toStdString());
      result.is_error())
  {
    const auto error = std::get<tb::Error>(result.error());
    QMessageBox::warning(this, tr("Save Prefab"), QString::fromStdString(error.msg));
    return;
  }

  const auto prefabText = appendPrefabWadPaths(
    appendPrefabMaterialCollections(
      mdl::serializeSelectedNodes(map), mdl::enabledMaterialCollections(map)),
    currentPrefabWadPaths(map));
  const auto filePath = prefabPathForName(prefabDirectory, prefabName.toStdString());
  const auto result = writePrefabAsset(filePath, prefabText);
  if (result.is_error())
  {
    const auto error = std::get<tb::Error>(result.error());
    QMessageBox::warning(this, tr("Save Prefab"), QString::fromStdString(error.msg));
    return;
  }

  if (auto* mapWindow = parentMapWindow(*this))
  {
    if (const auto thumbnailResult = savePrefabThumbnail(*mapWindow, map, filePath);
        thumbnailResult.is_error())
    {
      const auto error = std::get<tb::Error>(thumbnailResult.error());
      QMessageBox::warning(this, tr("Save Prefab"), QString::fromStdString(error.msg));
    }
  }
  else
  {
    QMessageBox::warning(
      this, tr("Save Prefab"), tr("Could not find map window for prefab thumbnail"));
  }

  m_lastWriteTimes.clear();
  m_assetRefreshPending = false;
  rescanWatchedDirectory();
}

void ModelBrowser::renamePrefab(std::filesystem::path prefabPath)
{
  if (m_project)
  {
    for (const auto& entry : m_assets)
    {
      if (entry.path != prefabPath || entry.assetId.empty())
        continue;
      bool ok = false;
      const auto name = QInputDialog::getText(
                          this,
                          tr("Rename Prefab"),
                          tr("Prefab name:"),
                          QLineEdit::Normal,
                          QString::fromStdString(entry.displayName),
                          &ok)
                          .trimmed();
      if (ok && !name.isEmpty())
      {
        const auto result = m_project->renameAsset(entry.assetId, name.toStdString());
        if (result.is_error())
          QMessageBox::warning(
            this,
            tr("Rename Prefab"),
            QString::fromStdString(std::get<Error>(result.error()).msg));
      }
      return;
    }
    return;
  }
  const auto absPath = userPrefabAssetPath(prefabPath);
  if (absPath.empty())
  {
    return;
  }

  auto ok = false;
  auto prefabName = QInputDialog::getText(
    this,
    tr("Rename Prefab"),
    tr("Prefab name:"),
    QLineEdit::Normal,
    pathAsQString(absPath.stem()),
    &ok);
  if (!ok)
  {
    return;
  }

  prefabName = prefabName.trimmed();
  if (prefabName.isEmpty())
  {
    return;
  }

  const auto result = renamePrefabAsset(absPath, prefabName.toStdString());
  if (result.is_error())
  {
    const auto error = std::get<tb::Error>(result.error());
    QMessageBox::warning(this, tr("Rename Prefab"), QString::fromStdString(error.msg));
    return;
  }

  m_lastWriteTimes.clear();
  m_assetRefreshPending = false;
  rescanWatchedDirectory();
}

void ModelBrowser::deletePrefab(std::filesystem::path prefabPath)
{
  if (m_project)
  {
    for (const auto& entry : m_assets)
    {
      if (entry.path != prefabPath || entry.assetId.empty())
        continue;
      if (
        QMessageBox::question(
          this,
          tr("Delete Prefab"),
          tr("Delete prefab \"%1\"?").arg(QString::fromStdString(entry.displayName)))
        == QMessageBox::Yes)
      {
        const auto id = entry.assetId;
        const auto result = m_project->removeAsset(id);
        if (result.is_error())
          QMessageBox::warning(
            this,
            tr("Delete Prefab"),
            QString::fromStdString(std::get<Error>(result.error()).msg));
      }
      return;
    }
    return;
  }
  const auto absPath = userPrefabAssetPath(prefabPath);
  if (absPath.empty())
  {
    return;
  }

  if (
    QMessageBox::question(
      this,
      tr("Delete Prefab"),
      tr("Delete prefab \"%1\"?").arg(pathAsQString(absPath.stem())))
    != QMessageBox::Yes)
  {
    return;
  }

  const auto result = deletePrefabAsset(absPath);
  if (result.is_error())
  {
    const auto error = std::get<tb::Error>(result.error());
    QMessageBox::warning(this, tr("Delete Prefab"), QString::fromStdString(error.msg));
    return;
  }

  removePrefabFromBrowser(prefabPath);
}

void ModelBrowser::removePrefabFromBrowser(const std::filesystem::path& prefabPath)
{
  const auto oldSize = m_assets.size();
  std::erase_if(m_assets, [&](const auto& asset) {
    return asset.type == BrowserCellType::Prefab && asset.path == prefabPath;
  });

  if (m_assets.size() == oldSize)
  {
    m_lastWriteTimes.clear();
    scheduleRescan();
    return;
  }

  m_lastWriteTimes = assetLastWriteTimes(m_assets);
  rebuildFolderTree();
  m_view->setAssets(m_folderPath, m_assets);
  m_view->setCurrentFolderPath(m_currentFolderPath);
}

void ModelBrowser::setFolderPath(std::filesystem::path folderPath)
{
  folderPath = folderPath.lexically_normal();
  if (m_folderPath == folderPath)
  {
    return;
  }

  m_folderPath = std::move(folderPath);
  m_currentFolderPath.clear();
  updateFolderEdit();

  markAssetsDirty();
}

void ModelBrowser::setCurrentFolderPath(std::filesystem::path currentFolderPath)
{
  currentFolderPath = currentFolderPath.lexically_normal();
  if (currentFolderPath == std::filesystem::path{"."})
  {
    currentFolderPath.clear();
  }

  if (m_currentFolderPath == currentFolderPath)
  {
    return;
  }

  m_currentFolderPath = std::move(currentFolderPath);
  if (m_view)
  {
    m_view->setCurrentFolderPath(m_currentFolderPath);
  }

  if (m_folderTree)
  {
    if (auto it = m_folderTreeItems.find(m_currentFolderPath);
        it != m_folderTreeItems.end())
    {
      if (m_folderTree->currentItem() != it->second)
      {
        m_folderTree->setCurrentItem(it->second);
      }
    }
  }

  updateFolderEdit();
}

void ModelBrowser::markAssetsDirty()
{
  m_deferAssetRefreshAfterDocumentLoad = false;
  ++m_assetRefreshDeferralGeneration;
  m_assetRefreshPending = true;
  scheduleAssetRefresh();
}

void ModelBrowser::ensureAssetsLoaded()
{
  m_assetRefreshQueued = false;
  if (!m_assetRefreshPending)
  {
    return;
  }

  m_assetRefreshPending = false;
  reloadModels();
  setWatchedDirectory();
}

void ModelBrowser::scheduleAssetRefresh()
{
  if (!isVisible() || m_assetRefreshQueued || m_deferAssetRefreshAfterDocumentLoad)
  {
    return;
  }

  m_assetRefreshQueued = true;
  QTimer::singleShot(0, this, [this]() { ensureAssetsLoaded(); });
}

void ModelBrowser::scheduleDeferredAssetRefreshAfterDocumentLoad()
{
  if (!isVisible() || !m_deferAssetRefreshAfterDocumentLoad || !m_assetRefreshPending)
  {
    return;
  }

  const auto generation = m_assetRefreshDeferralGeneration;
  QTimer::singleShot(DocumentLoadAssetRefreshDelay, this, [this, generation]() {
    if (generation != m_assetRefreshDeferralGeneration)
    {
      return;
    }

    m_deferAssetRefreshAfterDocumentLoad = false;
    scheduleAssetRefresh();
  });
}

std::optional<std::vector<BrowserAsset>> ModelBrowser::scanAssets() const
{
  if (m_project && !m_folderPath.is_absolute())
  {
    const auto catalog = m_project->store().assets();
    if (catalog.is_error())
      return std::nullopt;
    auto assets = std::vector<BrowserAsset>{};
    for (const auto& asset : catalog.value())
    {
      const auto path = pathFromQString(QString::fromStdString(asset.publishPath));
      const auto type = asset.type == project::AssetType::Map ? BrowserCellType::Map
                        : asset.type == project::AssetType::Texture
                          ? BrowserCellType::Texture
                          : assetTypeForExtension(path);
      if (type == BrowserCellType::Folder)
        continue;
      const auto file = m_project->assetFile(asset);
      if (file.is_error())
        continue;
      auto entry = BrowserAsset{type, path, file.value()};
      entry.projectId = m_project->store().info().id;
      entry.assetId = asset.id;
      entry.revision = asset.revision;
      entry.displayName = asset.displayName;
      entry.catalogPath = pathFromQString(QString::fromStdString(asset.virtualPath));
      assets.push_back(std::move(entry));
    }
    return assets;
  }
  if (m_folderPath.is_absolute())
  {
    return collectBrowserAssets(
      m_folderPath,
      {},
      [&](const auto& rootPath) {
        return fs::Disk::find(
          rootPath,
          fs::TraversalMode::Recursive,
          fs::makeExtensionPathMatcher(assetBrowserExtensions()));
      },
      [](const auto& path) { return Result<std::filesystem::path>{path}; },
      assetBrowserRoots());
  }

  const auto& map = m_document.map();
  const auto& fs = map.gameFileSystem();
  const auto enabledMods = mdl::enabledMods(map);
  if (enabledMods.empty())
  {
    return std::nullopt;
  }

  const auto modRoots =
    enabledMods | std::views::transform([&](const auto& mod) {
      return (map.gamePath() / std::filesystem::path{mod}).lexically_normal();
    })
    | kdl::ranges::to<std::vector>();

  return collectBrowserAssets(
    m_folderPath,
    modRoots,
    [&](const auto& rootPath) {
      if (rootPath == std::filesystem::path{"prefabs"})
      {
        return fs::Disk::find(
                 configuredPrefabDirectory(),
                 fs::TraversalMode::Recursive,
                 fs::makeExtensionPathMatcher(
                   std::vector<std::filesystem::path>{".tbprefab"}))
               | kdl::transform([](const auto& paths) {
                   return paths | std::views::transform(userPrefabBrowserPath)
                          | kdl::ranges::to<std::vector>();
                 });
      }

      return fs.find(
        rootPath,
        fs::TraversalMode::Recursive,
        fs::makeExtensionPathMatcher(assetBrowserExtensions()));
    },
    [&](const auto& path) {
      if (isPrefabAssetPath(path))
      {
        const auto absPath = userPrefabAssetPath(path);
        return absPath.empty()
                 ? Result<std::filesystem::path>{Error{"Invalid prefab path"}}
                 : Result<std::filesystem::path>{absPath};
      }
      return fs.makeAbsolute(path);
    },
    assetBrowserRoots());
}

void ModelBrowser::reloadModels()
{
  auto nextAssets = scanAssets();

  if (!nextAssets)
  {
    m_assets.clear();
    m_lastWriteTimes.clear();
    m_currentFolderPath.clear();
    updateFolderEdit();
    rebuildFolderTree();
    m_view->setAssets(m_folderPath, {});
    m_view->setCurrentFolderPath(m_currentFolderPath);
    return;
  }

  m_assets = std::move(*nextAssets);
  m_lastWriteTimes = assetLastWriteTimes(m_assets);

  rebuildFolderTree();
  m_view->setAssets(m_folderPath, m_assets);
  m_view->setCurrentFolderPath(m_currentFolderPath);
}

void ModelBrowser::rebuildFolderTree()
{
  if (!m_folderTree)
  {
    return;
  }

  auto blocker = QSignalBlocker{m_folderTree};

  m_folderTree->clear();
  m_folderTreeItems.clear();

  const auto folderIcon = loadSVGIcon(std::filesystem::path{"Map_folder.svg"});

  auto rootTitle = m_folderPath.filename().empty() ? std::filesystem::path{"assets"}
                                                   : m_folderPath.filename();
  auto rootTitleText = rootTitle.generic_string();
  if (rootTitleText.empty())
  {
    rootTitleText = ".";
  }

  auto* rootItem =
    new QTreeWidgetItem{m_folderTree, QStringList{QString::fromStdString(rootTitleText)}};
  rootItem->setData(0, Qt::UserRole, QString{});
  rootItem->setIcon(0, folderIcon);
  m_folderTreeItems.emplace(std::filesystem::path{}, rootItem);

  if (m_folderPath.empty())
  {
    for (const auto& [type, rootPath] : assetBrowserRoots())
    {
      unused(type);
      if (rootPath.empty() || m_folderTreeItems.contains(rootPath))
      {
        continue;
      }

      auto* item = new QTreeWidgetItem{
        rootItem, QStringList{QString::fromStdString(rootPath.generic_string())}};
      item->setData(0, Qt::UserRole, QString::fromStdString(rootPath.generic_string()));
      item->setIcon(0, folderIcon);
      m_folderTreeItems.emplace(rootPath, item);
    }
  }

  for (const auto& asset : m_assets)
  {
    const auto& assetPath = asset.catalogPath.empty() ? asset.path : asset.catalogPath;
    if (assetPath.empty())
    {
      continue;
    }

    if (!m_folderPath.empty() && !kdl::path_has_prefix(assetPath, m_folderPath))
    {
      continue;
    }

    const auto relAssetPath =
      m_folderPath.empty() ? assetPath : assetPath.lexically_relative(m_folderPath);
    const auto relFolderPath = relAssetPath.parent_path();

    auto* parentItem = rootItem;
    auto accumulatedPath = std::filesystem::path{};
    for (const auto& part : relFolderPath)
    {
      accumulatedPath /= part;
      if (m_folderTreeItems.contains(accumulatedPath))
      {
        parentItem = m_folderTreeItems[accumulatedPath];
        continue;
      }

      auto* item = new QTreeWidgetItem{
        parentItem, QStringList{QString::fromStdString(part.generic_string())}};
      item->setData(
        0, Qt::UserRole, QString::fromStdString(accumulatedPath.generic_string()));
      item->setIcon(0, folderIcon);
      parentItem = item;
      m_folderTreeItems.emplace(accumulatedPath, item);
    }
  }

  m_folderTree->expandItem(rootItem);

  if (auto it = m_folderTreeItems.find(m_currentFolderPath);
      it != m_folderTreeItems.end())
  {
    m_folderTree->setCurrentItem(it->second);
  }
  else
  {
    m_currentFolderPath.clear();
    m_folderTree->setCurrentItem(rootItem);
    if (m_view)
    {
      m_view->setCurrentFolderPath(m_currentFolderPath);
    }
    updateFolderEdit();
  }
}

void ModelBrowser::setWatchedDirectory()
{
  if (!m_fileSystemWatcher)
  {
    return;
  }

  if (!m_fileSystemWatcher->directories().isEmpty())
  {
    m_fileSystemWatcher->removePaths(m_fileSystemWatcher->directories());
  }

  if (m_folderPath.is_absolute())
  {
    if (fs::Disk::pathInfo(m_folderPath) == fs::PathInfo::Directory)
    {
      m_fileSystemWatcher->addPath(pathAsQString(m_folderPath));
    }
    return;
  }

  const auto& map = m_document.map();
  const auto& fs = map.gameFileSystem();
  if (mdl::enabledMods(map).empty())
  {
    return;
  }

  if (m_folderPath.empty())
  {
    for (const auto& [type, rootPath] : assetBrowserRoots())
    {
      unused(type);
      if (auto absPathResult = fs.makeAbsolute(rootPath); !absPathResult.is_error())
      {
        const auto& absPath = absPathResult.value();
        if (fs::Disk::pathInfo(absPath) == fs::PathInfo::Directory)
        {
          m_fileSystemWatcher->addPath(pathAsQString(absPath));
        }
      }
    }
    return;
  }

  if (auto absPathResult = fs.makeAbsolute(m_folderPath); !absPathResult.is_error())
  {
    const auto& absPath = absPathResult.value();
    if (fs::Disk::pathInfo(absPath) == fs::PathInfo::Directory)
    {
      m_fileSystemWatcher->addPath(pathAsQString(absPath));
    }
  }
}

void ModelBrowser::scheduleRescan()
{
  if (m_rescanTimer)
  {
    m_rescanTimer->start(200);
  }
}

void ModelBrowser::rescanWatchedDirectory()
{
  auto nextAssets = scanAssets();

  if (!nextAssets)
  {
    for (const auto& p : entityModelAssetPaths(m_assets))
    {
      m_document.map().entityModelManager().invalidateModel(p);
    }
    m_assets.clear();
    m_lastWriteTimes.clear();
    m_currentFolderPath.clear();
    updateFolderEdit();
    rebuildFolderTree();
    m_view->setAssets(m_folderPath, {});
    m_view->setCurrentFolderPath(m_currentFolderPath);
    return;
  }

  auto newLastWriteTimes = assetLastWriteTimes(*nextAssets);
  const auto changedPaths =
    changedAssetPaths(m_lastWriteTimes, newLastWriteTimes, m_assets, *nextAssets);
  if (changedPaths.empty() && *nextAssets == m_assets)
  {
    return;
  }

  m_document.map().reloadEntityModels(changedPaths);

  m_assets = std::move(*nextAssets);
  m_lastWriteTimes = std::move(newLastWriteTimes);
  rebuildFolderTree();
  m_view->setAssets(m_folderPath, m_assets);
  m_view->setCurrentFolderPath(m_currentFolderPath);
}

} // namespace tb::ui
