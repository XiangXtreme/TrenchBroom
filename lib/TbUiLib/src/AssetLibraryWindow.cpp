/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom, licensed under GPL version 3 or later.
*/
#include "ui/AssetLibraryWindow.h"

#include <QAbstractItemView>
#include <QAction>
#include <QColor>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSize>
#include <QSlider>
#include <QSplitter>
#include <QStyle>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include "base/PreferenceManager.h"
#include "img/DecodeImage.h"
#include "img/GoldSrcTexture.h"
#include "prefs/Preferences.h"
#include "project/AssetLibrary.h"
#include "project/GoldSrcWad.h"
#include "project/ProjectStore.h"
#include "ui/AssetBrowserModel.h"
#include "ui/AssetInspector.h"
#include "ui/AssetLibraryGlyphs.h"
#include "ui/AssetLibraryImport.h"
#include "ui/AssetLibraryItemDelegate.h"
#include "ui/AssetLibrarySource.h"
#include "ui/CreateTerrainMaterialDialog.h"
#include "ui/GoldSrcSpritePreview.h"
#include "ui/ModelThumbnail.h"
#include "ui/ProjectCaptureConflictDialog.h"
#include "ui/ProjectResourceAudit.h"
#include "ui/ProjectSession.h"
#include "ui/QPathUtils.h"
#include "ui/QStyleUtils.h"
#include "ui/RecentDocuments.h"
#include "ui/TerrainMaterial.h"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <vector>

namespace tb::ui
{
namespace
{
constexpr auto DefaultThumbSide = 112;
constexpr auto ThumbMinSide = 72;
constexpr auto ThumbMaxSide = 192;

// A scan owns an atomic database transaction and cannot be cancelled partway through.
class LibraryScanProgress : public QProgressDialog
{
public:
  using QProgressDialog::QProgressDialog;
  void reject() override {}
};

/** Resolves the thumbnail role on demand, so offscreen cards do not decode assets. */
class AssetLibraryItem : public QListWidgetItem
{
  std::function<QImage()> m_thumbnail;

public:
  AssetLibraryItem(QListWidget* parent, std::function<QImage()> thumbnail)
    : QListWidgetItem{parent}
    , m_thumbnail{std::move(thumbnail)}
  {
  }

  QVariant data(const int role) const override
  {
    if (role == AssetLibraryItemDelegate::ThumbnailRole)
      return QPixmap::fromImage(m_thumbnail());
    return QListWidgetItem::data(role);
  }
};

QString sizeText(const std::uint64_t bytes)
{
  if (bytes >= 1024 * 1024)
    return QStringLiteral("%1 MB").arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
  if (bytes >= 1024)
    return QStringLiteral("%1 KB").arg(double(bytes) / 1024.0, 0, 'f', 1);
  return QStringLiteral("%1 B").arg(bytes);
}

QString typeText(const project::AssetType type)
{
  switch (type)
  {
  case project::AssetType::Map:
    return QObject::tr("Map");
  case project::AssetType::Texture:
    return QObject::tr("Texture");
  case project::AssetType::Material:
    return QObject::tr("Material");
  case project::AssetType::Model:
    return QObject::tr("Model");
  case project::AssetType::Sprite:
    return QObject::tr("Sprite");
  case project::AssetType::Sound:
    return QObject::tr("Sound");
  case project::AssetType::Skybox:
    return QObject::tr("Skybox");
  case project::AssetType::Prefab:
    return QObject::tr("Prefab");
  }
  return QObject::tr("Unknown");
}

QString libraryFolder(const project::AssetType assetType, const std::string& group)
{
  auto type = BrowserCellType::Folder;
  switch (assetType)
  {
  case project::AssetType::Model:
    type = BrowserCellType::Model;
    break;
  case project::AssetType::Sprite:
    type = BrowserCellType::Sprite;
    break;
  case project::AssetType::Sound:
    type = BrowserCellType::Sound;
    break;
  case project::AssetType::Prefab:
    type = BrowserCellType::Prefab;
    break;
  case project::AssetType::Map:
    type = BrowserCellType::Map;
    break;
  case project::AssetType::Texture:
  case project::AssetType::Skybox:
    type = BrowserCellType::Texture;
    break;
  case project::AssetType::Material:
    type = BrowserCellType::Material;
    break;
  }
  return pathAsGenericQString(browserPathUnderType(
    type,
    assetType == project::AssetType::Skybox,
    pathFromQString(QString::fromStdString(group))));
}

QString libraryFolder(const project::LibraryAsset& asset)
{
  return libraryFolder(asset.type, asset.group);
}

bool folderContains(const QString& folder, const QString& path)
{
  return path == folder || path.startsWith(folder + QLatin1Char{'/'});
}

// Only raster textures go through the GoldSrc converter; models, sprites, sounds and
// skybox groups keep their native bytes.
bool needsConversion(const project::LibraryAsset& asset)
{
  return asset.type == project::AssetType::Texture && asset.nativeFormat == "image";
}

int statusFor(const project::LibraryAsset& asset)
{
  if (asset.missing)
    return int(AssetLibraryItemDelegate::Missing);
  if (needsConversion(asset))
    return int(AssetLibraryItemDelegate::Pending);
  return int(AssetLibraryItemDelegate::Ready);
}

/** Category key to asset type; the sidebar, the filter and the counts share it. */
struct TypeFilter
{
  const char* key;
  project::AssetType type;
};

constexpr std::array kTypeFilters{
  TypeFilter{"texture", project::AssetType::Texture},
  TypeFilter{"material", project::AssetType::Material},
  TypeFilter{"model", project::AssetType::Model},
  TypeFilter{"sprite", project::AssetType::Sprite},
  TypeFilter{"sound", project::AssetType::Sound},
  TypeFilter{"skybox", project::AssetType::Skybox},
  TypeFilter{"prefab", project::AssetType::Prefab},
};

std::optional<project::AssetType> typeForFilter(const QString& filter)
{
  for (const auto& candidate : kTypeFilters)
  {
    if (filter == QLatin1String{candidate.key})
      return candidate.type;
  }
  return std::nullopt;
}

/** Card glyph, also used as the kind key for the tinted placeholder tile. */
AssetLibraryGlyph kindGlyph(const project::AssetType type)
{
  switch (type)
  {
  case project::AssetType::Texture:
  case project::AssetType::Material:
    return AssetLibraryGlyph::Texture;
  case project::AssetType::Model:
  case project::AssetType::Prefab:
    return AssetLibraryGlyph::Model;
  case project::AssetType::Sprite:
    return AssetLibraryGlyph::Sprite;
  case project::AssetType::Sound:
    return AssetLibraryGlyph::Sound;
  case project::AssetType::Skybox:
    return AssetLibraryGlyph::Skybox;
  case project::AssetType::Map:
    return AssetLibraryGlyph::Project;
  }
  return AssetLibraryGlyph::Texture;
}

QPushButton* createActionButton(
  QWidget* parent, const QString& text, const QString& tooltip)
{
  auto* button = new QPushButton{text, parent};
  // A dialog action must never become the default button, otherwise pressing Return in
  // the search field would trigger it.
  button->setAutoDefault(false);
  button->setDefault(false);
  button->setToolTip(tooltip);
  button->setCursor(Qt::PointingHandCursor);
  button->setIconSize(QSize{15, 15});
  return button;
}

QToolButton* createGlyphToolButton(
  QWidget* parent,
  const AssetLibraryGlyph glyph,
  const QString& tooltip,
  const QColor& color,
  const QString& role)
{
  auto* button = new QToolButton{parent};
  button->setAutoRaise(false);
  button->setToolButtonStyle(Qt::ToolButtonIconOnly);
  button->setToolTip(tooltip);
  button->setCursor(Qt::PointingHandCursor);
  button->setIcon(assetLibraryGlyphIcon(glyph, color));
  button->setIconSize(QSize{16, 16});
  button->setProperty("tbControlRole", role);
  return button;
}

QColor accentColor(const QWidget& widget)
{
  auto accent = widget.palette().color(QPalette::Active, QPalette::Accent);
  if (!accent.isValid())
    accent = widget.palette().color(QPalette::Active, QPalette::Highlight);
  return accent;
}
} // namespace

AssetLibraryWindow::AssetLibraryWindow(QWidget* parent)
  : QDialog{parent}
{
  buildUi();
  setAcceptDrops(true);

  const auto folder =
    PreferenceManager::instance().get(Preferences::AssetLibraryDirectory);
  if (!folder.empty() && project::AssetLibrary::isLibraryFolder(folder))
    openLibrary(pathAsQString(folder));
  else
    reload();
}

AssetLibraryWindow::AssetLibraryWindow(
  const QString& folder, const bool remember, QWidget* parent)
  : QDialog{parent}
{
  buildUi();
  setAcceptDrops(true);
  openLibrary(folder, remember);
}

void AssetLibraryWindow::buildUi()
{
  setWindowTitle(tr("Asset Library"));
  setObjectName(QStringLiteral("AssetLibraryWindow"));
  setWindowIconTB(this);
  resize(1180, 760);
  m_reloadTimer = new QTimer{this};
  m_reloadTimer->setSingleShot(true);
  connect(m_reloadTimer, &QTimer::timeout, this, &AssetLibraryWindow::reload);
  m_usageTimer = new QTimer{this};
  m_usageTimer->setSingleShot(true);
  m_usageTimer->setInterval(150);
  connect(m_usageTimer, &QTimer::timeout, this, &AssetLibraryWindow::refreshUsage);
  connect(
    &AssetLibraryChanges::instance(),
    &AssetLibraryChanges::changed,
    this,
    [this](const QString& id) {
      if (
        m_source && id.toStdString() == m_source->libraryId()
        && !m_convertTimer->isActive())
        m_reloadTimer->start(0);
    });

  const auto textColor = palette().color(QPalette::Active, QPalette::Text);
  const auto dimColor = palette().color(QPalette::Active, QPalette::PlaceholderText);

  auto* layout = new QVBoxLayout{this};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  // ---- header bar: library identity, status chip and the library level actions --------
  auto* header = new QWidget{this};
  header->setObjectName(QStringLiteral("AssetLibraryHeader"));
  header->setAttribute(Qt::WA_StyledBackground, true);
  auto* headerLayout = new QHBoxLayout{header};
  headerLayout->setContentsMargins(14, 10, 14, 10);
  headerLayout->setSpacing(10);

  m_libraryBadge = new QLabel{header};
  m_libraryBadge->setObjectName(QStringLiteral("AssetLibraryBadge"));
  m_libraryBadge->setFixedSize(38, 38);
  m_libraryBadge->setAlignment(Qt::AlignCenter);

  auto* identity = new QWidget{header};
  auto* identityLayout = new QVBoxLayout{identity};
  identityLayout->setContentsMargins(0, 0, 0, 0);
  identityLayout->setSpacing(1);
  m_libraryLabel = new QLabel{tr("No asset library"), identity};
  m_libraryLabel->setObjectName(QStringLiteral("AssetLibraryHeaderTitle"));
  m_libraryPath = new QLabel{identity};
  m_libraryPath->setObjectName(QStringLiteral("AssetLibraryHeaderPath"));
  identityLayout->addWidget(m_libraryLabel);
  identityLayout->addWidget(m_libraryPath);

  m_libraryChip = new QLabel{tr("No library"), header};
  m_libraryChip->setObjectName(QStringLiteral("AssetLibraryChip"));

  headerLayout->addWidget(m_libraryBadge);
  headerLayout->addWidget(identity);
  headerLayout->addSpacing(2);
  headerLayout->addWidget(m_libraryChip, 0, Qt::AlignVCenter);
  headerLayout->addStretch(1);

  m_importButton =
    createActionButton(header, tr("Import Files…"), tr("Copy assets into this library"));
  m_createMaterialButton = createActionButton(
    header, tr("New Material…"), tr("Create a composite terrain material"));
  m_createMaterialButton->setObjectName(QStringLiteral("AssetLibraryCreateMaterial"));
  m_rescanButton =
    createActionButton(header, tr("Rescan"), tr("Re-read the library contents"));
  m_convertButton = createActionButton(
    header, tr("Convert Pending"), tr("Convert source textures to GoldSrc miptex"));
  m_convertButton->setObjectName("AssetLibraryConvert");
  m_revealButton =
    createActionButton(header, tr("Reveal"), tr("Show the library folder"));
  m_changeButton =
    createActionButton(header, tr("Change Library…"), tr("Open another asset library"));

  m_importButton->setIcon(
    assetLibraryGlyphIcon(AssetLibraryGlyph::Import, textColor, 15));
  m_createMaterialButton->setIcon(
    assetLibraryGlyphIcon(AssetLibraryGlyph::Texture, textColor, 15));
  m_rescanButton->setIcon(
    assetLibraryGlyphIcon(AssetLibraryGlyph::Rescan, textColor, 15));
  m_convertButton->setIcon(
    assetLibraryGlyphIcon(AssetLibraryGlyph::Convert, textColor, 15));
  m_revealButton->setIcon(
    assetLibraryGlyphIcon(AssetLibraryGlyph::Reveal, textColor, 15));
  m_changeButton->setIcon(
    assetLibraryGlyphIcon(AssetLibraryGlyph::Library, textColor, 15));

  auto* separator = new QFrame{header};
  separator->setObjectName(QStringLiteral("AssetLibrarySeparator"));
  separator->setFrameShape(QFrame::VLine);
  separator->setFixedWidth(1);

  m_searchEdit = new QLineEdit{header};
  m_searchEdit->setObjectName(QStringLiteral("AssetLibrarySearch"));
  m_searchEdit->setPlaceholderText(tr("Search assets"));
  m_searchEdit->setClearButtonEnabled(true);
  m_searchEdit->setFixedWidth(210);
  m_searchEdit->addAction(
    assetLibraryGlyphIcon(AssetLibraryGlyph::Search, dimColor, 14),
    QLineEdit::LeadingPosition);

  headerLayout->addWidget(m_importButton);
  headerLayout->addWidget(m_createMaterialButton);
  headerLayout->addWidget(m_rescanButton);
  headerLayout->addWidget(m_convertButton);
  headerLayout->addWidget(m_revealButton);
  headerLayout->addWidget(separator);
  headerLayout->addWidget(m_changeButton);
  headerLayout->addWidget(m_searchEdit);
  layout->addWidget(header);

  // ---- body: categories, grid and inspector ------------------------------------------
  auto* splitter = new QSplitter{Qt::Horizontal, this};
  splitter->setObjectName(QStringLiteral("AssetLibrarySplitter"));
  splitter->setChildrenCollapsible(false);
  splitter->setHandleWidth(1);

  auto* sidebar = new QWidget{splitter};
  sidebar->setObjectName(QStringLiteral("AssetLibrarySidebar"));
  sidebar->setAttribute(Qt::WA_StyledBackground, true);
  auto* sideLayout = new QVBoxLayout{sidebar};
  sideLayout->setContentsMargins(8, 10, 8, 10);
  sideLayout->setSpacing(4);
  m_categories = new AssetLibraryListView{sidebar};
  m_categories->setObjectName(QStringLiteral("AssetLibraryCategories"));
  m_categories->setFrameShape(QFrame::NoFrame);
  m_categories->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_categories->setItemDelegate(new AssetLibraryCategoryDelegate{m_categories});
  auto* sideSplitter = new QSplitter{Qt::Vertical, sidebar};
  sideSplitter->addWidget(m_categories);
  auto* foldersPanel = new QWidget{sideSplitter};
  auto* foldersLayout = new QVBoxLayout{foldersPanel};
  foldersLayout->setContentsMargins(0, 0, 0, 0);
  foldersLayout->addWidget(new QLabel{tr("Folders"), foldersPanel});
  m_folders = new QTreeWidget{foldersPanel};
  m_folders->setObjectName(QStringLiteral("AssetLibraryFolders"));
  m_folders->setHeaderHidden(true);
  m_folders->setUniformRowHeights(true);
  foldersLayout->addWidget(m_folders, 1);
  sideSplitter->addWidget(foldersPanel);
  sideSplitter->setStretchFactor(0, 3);
  sideSplitter->setStretchFactor(1, 2);
  sideLayout->addWidget(sideSplitter, 1);
  connect(m_folders, &QTreeWidget::currentItemChanged, this, [this](auto* item, auto*) {
    m_folderPath = item ? item->data(0, Qt::UserRole).toString() : QString{};
    if (const auto type = typeForFilter(m_filter);
        type && !m_folderPath.isEmpty()
        && !folderContains(libraryFolder(*type, {}), m_folderPath))
    {
      // A type filter cannot match a different type's folder. Honour the newly
      // selected folder instead of leaving the grid unexpectedly empty.
      m_filter = QStringLiteral("all");
      const auto blocker = QSignalBlocker{m_categories};
      for (auto row = 0; row < m_categories->count(); ++row)
      {
        if (
          m_categories->item(row)
            ->data(AssetLibraryCategoryDelegate::FilterRole)
            .toString()
          == m_filter)
        {
          m_categories->setCurrentRow(row);
          break;
        }
      }
    }
    if (const auto* asset = selectedAsset(); asset && !m_folderPath.isEmpty())
    {
      const auto group = libraryFolder(*asset);
      if (!folderContains(m_folderPath, group))
        m_selectedId.reset();
    }
    refreshGrid();
  });
  splitter->addWidget(sidebar);

  auto* center = new QWidget{splitter};
  center->setObjectName(QStringLiteral("AssetLibraryCenter"));
  auto* centerLayout = new QVBoxLayout{center};
  centerLayout->setContentsMargins(10, 8, 10, 8);
  centerLayout->setSpacing(8);

  auto* tools = new QWidget{center};
  tools->setObjectName(QStringLiteral("AssetLibraryToolBar"));
  auto* toolsLayout = new QHBoxLayout{tools};
  toolsLayout->setContentsMargins(2, 0, 2, 0);
  toolsLayout->setSpacing(6);

  m_countLabel = new QLabel{tools};
  m_countLabel->setObjectName(QStringLiteral("AssetLibraryCount"));

  auto* sizeLabel = new QLabel{tr("Size"), tools};
  sizeLabel->setObjectName(QStringLiteral("AssetLibrarySizeLabel"));
  m_sizeSlider = new QSlider{Qt::Horizontal, tools};
  m_sizeSlider->setRange(ThumbMinSide, ThumbMaxSide);
  m_sizeSlider->setValue(DefaultThumbSide);
  m_sizeSlider->setFixedWidth(120);
  m_sizeSlider->setProperty("tbControlRole", QStringLiteral("compact"));
  m_sizeValue = new QLabel{tools};
  m_sizeValue->setObjectName(QStringLiteral("AssetLibrarySizeValue"));
  m_sizeValue->setFixedWidth(28);
  m_sizeValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

  m_gridButton = createGlyphToolButton(
    tools,
    AssetLibraryGlyph::Grid,
    tr("Grid view"),
    textColor,
    QStringLiteral("compact"));
  m_listButton = createGlyphToolButton(
    tools,
    AssetLibraryGlyph::List,
    tr("List view"),
    textColor,
    QStringLiteral("compact"));
  m_gridButton->setCheckable(true);
  m_listButton->setCheckable(true);
  m_gridButton->setChecked(true);

  m_convertProgress = new QProgressBar{tools};
  m_convertProgress->setMaximumWidth(180);
  m_convertProgress->setTextVisible(false);
  m_convertProgress->hide();
  m_convertCancel = new QPushButton{tr("Cancel"), tools};
  m_convertCancel->hide();
  m_retryButton = new QPushButton{tr("Retry failed"), tools};
  m_retryButton->setObjectName("AssetLibraryRetryFailed");
  m_retryButton->setProperty("tbControlRole", "compact");
  m_retryButton->setToolTip(
    tr("Re-run conversion only for the assets that failed in the last batch."));
  m_retryButton->hide();
  connect(
    m_retryButton,
    &QPushButton::clicked,
    this,
    &AssetLibraryWindow::retryFailedConversions);
  m_convertTimer = new QTimer{this};
  m_convertTimer->setObjectName("AssetLibraryConvertTimer");
  m_convertTimer->setInterval(0);
  connect(m_convertTimer, &QTimer::timeout, this, &AssetLibraryWindow::stepConvertQueue);
  connect(
    m_convertCancel,
    &QPushButton::clicked,
    this,
    &AssetLibraryWindow::cancelConvertQueue);

  toolsLayout->addWidget(m_countLabel);
  toolsLayout->addWidget(m_convertProgress);
  toolsLayout->addWidget(m_convertCancel);
  toolsLayout->addWidget(m_retryButton);
  toolsLayout->addStretch(1);
  toolsLayout->addWidget(sizeLabel);
  toolsLayout->addWidget(m_sizeSlider);
  toolsLayout->addWidget(m_sizeValue);
  toolsLayout->addSpacing(4);
  toolsLayout->addWidget(m_gridButton);
  toolsLayout->addWidget(m_listButton);

  m_grid = new AssetLibraryListView{center};
  m_grid->setObjectName(QStringLiteral("AssetLibraryGrid"));
  m_grid->setFrameShape(QFrame::NoFrame);
  m_grid->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_grid->setContextMenuPolicy(Qt::ActionsContextMenu);
  m_createMaterialFromSelection =
    new QAction{tr("Create Terrain Material from Selected Textures…"), m_grid};
  m_createMaterialFromSelection->setObjectName(
    QStringLiteral("AssetLibrary_CreateMaterialFromSelection"));
  m_createMaterialFromSelection->setEnabled(false);
  m_grid->addAction(m_createMaterialFromSelection);
  m_deleteAction = new QAction{tr("Delete…"), m_grid};
  m_deleteAction->setObjectName(QStringLiteral("AssetLibrary_DeleteSelected"));
  m_deleteAction->setShortcut(QKeySequence::Delete);
  m_deleteAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
  m_deleteAction->setEnabled(false);
  m_grid->addAction(m_deleteAction);
  m_grid->setViewMode(QListView::IconMode);
  m_grid->setResizeMode(QListView::Adjust);
  m_grid->setMovement(QListView::Static);
  m_grid->setUniformItemSizes(true);
  m_grid->setWordWrap(false);
  m_grid->setSpacing(0);
  m_grid->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_delegate = new AssetLibraryItemDelegate{m_grid};
  m_grid->setItemDelegate(m_delegate);

  m_empty = new QLabel{center};
  m_empty->setObjectName(QStringLiteral("AssetLibraryEmpty"));
  m_empty->setAlignment(Qt::AlignCenter);
  m_empty->setWordWrap(true);
  m_empty->hide();

  centerLayout->addWidget(tools);
  centerLayout->addWidget(m_grid, 1);
  centerLayout->addWidget(m_empty, 1);
  splitter->addWidget(center);

  auto* inspector = new QWidget{};
  inspector->setObjectName(QStringLiteral("AssetLibraryInspector"));
  inspector->setAttribute(Qt::WA_StyledBackground, true);
  auto* inspectorLayout = new QVBoxLayout{inspector};
  inspectorLayout->setContentsMargins(12, 10, 12, 10);
  inspectorLayout->setSpacing(7);

  auto* previewTitle = new QLabel{tr("Preview"), inspector};
  previewTitle->setObjectName(QStringLiteral("AssetLibrarySectionTitle"));
  m_inspector =
    new AssetInspector{inspector, AssetInspector::ScrollMode::SharedWithParent};
  auto* actionRow = new QWidget{inspector};
  auto* actionLayout = new QHBoxLayout{actionRow};
  actionLayout->setContentsMargins(0, 0, 0, 0);
  actionLayout->setSpacing(6);
  m_favoriteButton = new QPushButton{QStringLiteral("☆ Favourite"), actionRow};
  m_favoriteButton->setCheckable(true);
  m_favoriteButton->setObjectName(QStringLiteral("AssetLibraryFavoriteButton"));
  m_renameButton = new QPushButton{tr("Rename…"), actionRow};
  m_deleteButton = new QPushButton{tr("Delete…"), actionRow};
  m_deleteButton->setObjectName(QStringLiteral("AssetLibraryDeleteButton"));
  m_deleteButton->setToolTip(
    tr("Remove selected assets from the library. The source files stay on disk, and a "
       "later rescan will not add them back."));
  m_favoriteButton->setEnabled(false);
  m_renameButton->setEnabled(false);
  m_deleteButton->setEnabled(false);
  actionLayout->addWidget(m_favoriteButton, 1);
  actionLayout->addWidget(m_renameButton, 1);
  actionLayout->addWidget(m_deleteButton, 1);
  connect(
    m_favoriteButton, &QPushButton::clicked, this, &AssetLibraryWindow::toggleFavorite);
  connect(
    m_renameButton, &QPushButton::clicked, this, &AssetLibraryWindow::renameSelected);
  connect(
    m_deleteButton, &QPushButton::clicked, this, &AssetLibraryWindow::deleteSelected);
  connect(m_deleteAction, &QAction::triggered, this, &AssetLibraryWindow::deleteSelected);

  auto* tagsRow = new QWidget{inspector};
  auto* tagsLayout = new QHBoxLayout{tagsRow};
  tagsLayout->setContentsMargins(0, 0, 0, 0);
  tagsLayout->setSpacing(6);
  auto* tagsLabel = new QLabel{tr("Tags"), tagsRow};
  tagsLabel->setObjectName(QStringLiteral("AssetLibraryPropertyKey"));
  m_tagsEdit = new QLineEdit{tagsRow};
  m_tagsEdit->setPlaceholderText(tr("comma separated"));
  m_tagsEdit->setEnabled(false);
  tagsLayout->addWidget(tagsLabel);
  tagsLayout->addWidget(m_tagsEdit, 1);
  connect(m_tagsEdit, &QLineEdit::editingFinished, this, &AssetLibraryWindow::applyTags);

  m_name = new QLabel{inspector};
  m_name->setObjectName(QStringLiteral("AssetLibraryAssetName"));
  m_name->setWordWrap(true);
  m_status = new QLabel{inspector};
  m_status->setObjectName(QStringLiteral("AssetLibraryStatus"));

  auto* propertiesTitle = new QLabel{tr("Details"), inspector};
  propertiesTitle->setObjectName(QStringLiteral("AssetLibrarySectionTitle"));
  auto* propertiesHost = new QWidget{inspector};
  m_properties = new QFormLayout{propertiesHost};
  m_properties->setContentsMargins(0, 0, 0, 0);
  m_properties->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
  m_properties->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
  m_properties->setHorizontalSpacing(10);
  m_properties->setVerticalSpacing(5);

  auto* dependenciesTitle = new QLabel{tr("Dependencies"), inspector};
  dependenciesTitle->setObjectName(QStringLiteral("AssetLibrarySectionTitle"));
  m_dependencies = new QListWidget{inspector};
  m_dependencies->setObjectName(QStringLiteral("AssetLibraryDependencies"));
  m_dependencies->setFrameShape(QFrame::NoFrame);
  m_dependencies->setSelectionMode(QAbstractItemView::NoSelection);
  m_dependencies->setMaximumHeight(150);

  inspectorLayout->addWidget(previewTitle);
  inspectorLayout->addWidget(m_inspector, 1);
  inspectorLayout->addWidget(actionRow);
  inspectorLayout->addWidget(tagsRow);
  inspectorLayout->addWidget(m_name);
  inspectorLayout->addWidget(m_status);
  inspectorLayout->addWidget(propertiesTitle);
  inspectorLayout->addWidget(propertiesHost);
  auto* usageTitle = new QLabel{tr("Used by"), inspector};
  usageTitle->setObjectName(QStringLiteral("AssetLibrarySectionTitle"));
  m_usage = new QListWidget{inspector};
  m_usage->setObjectName(QStringLiteral("AssetLibraryUsage"));
  m_usage->setFrameShape(QFrame::NoFrame);
  m_usage->setSelectionMode(QAbstractItemView::SingleSelection);
  m_usage->setMaximumHeight(110);

  inspectorLayout->addWidget(dependenciesTitle);
  inspectorLayout->addWidget(m_dependencies);
  inspectorLayout->addWidget(usageTitle);
  m_projectScope =
    new QLabel{tr("Saved captures in up to 12 recent projects"), inspector};
  m_projectScope->setObjectName("AssetLibrary_ProjectScope");
  m_projectScope->setWordWrap(true);
  inspectorLayout->addWidget(m_projectScope);
  auto* refreshUsage = createActionButton(
    inspector,
    tr("Refresh usage"),
    tr("Query the bound project, including current unsaved map edits"));
  refreshUsage->setObjectName("AssetLibrary_RefreshUsage");
  connect(refreshUsage, &QPushButton::clicked, this, &AssetLibraryWindow::refreshUsage);
  inspectorLayout->addWidget(refreshUsage);
  inspectorLayout->addWidget(m_usage);
  auto* inspectorScroll = new QScrollArea{splitter};
  inspectorScroll->setObjectName("AssetLibrary_InspectorScroll");
  inspectorScroll->setWidgetResizable(true);
  inspectorScroll->setFrameShape(QFrame::NoFrame);
  inspectorScroll->setWidget(inspector);
  splitter->addWidget(inspectorScroll);

  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  splitter->setStretchFactor(2, 0);
  // The inspector holds the shared preview; give it room for a usable 3D render.
  splitter->setSizes({196, 620, 400});
  layout->addWidget(splitter, 1);

  m_footer = new QLabel{this};
  m_footer->setObjectName(QStringLiteral("AssetLibraryFooter"));
  m_footer->setContentsMargins(14, 5, 14, 5);
  layout->addWidget(m_footer);

  connect(
    m_changeButton, &QPushButton::clicked, this, &AssetLibraryWindow::chooseLibrary);
  connect(m_importButton, &QPushButton::clicked, this, &AssetLibraryWindow::runImport);
  connect(
    m_createMaterialButton,
    &QPushButton::clicked,
    this,
    &AssetLibraryWindow::runCreateTerrainMaterial);
  connect(
    m_createMaterialFromSelection,
    &QAction::triggered,
    this,
    &AssetLibraryWindow::runCreateTerrainMaterial);
  connect(
    m_grid,
    &QListWidget::itemSelectionChanged,
    this,
    &AssetLibraryWindow::updateSelectionActions);
  connect(m_rescanButton, &QPushButton::clicked, this, &AssetLibraryWindow::runRescan);
  connect(
    m_convertButton, &QPushButton::clicked, this, &AssetLibraryWindow::runConvertPending);
  connect(
    m_revealButton, &QPushButton::clicked, this, &AssetLibraryWindow::revealSelected);
  connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
    m_search = text;
    refreshGrid();
  });
  m_tagCombo = new QComboBox{};
  m_tagCombo->setObjectName("AssetLibrary_Tag");
  m_tagCombo->addItem(tr("All tags"), QString{});
  m_tagCombo->setProperty("tbControlRole", "compact");
  m_tagCombo->setMaximumWidth(180);
  toolsLayout->insertWidget(0, m_tagCombo);
  m_soundCombo = new QComboBox{};
  m_soundCombo->setObjectName("AssetLibrary_SoundKind");
  m_soundCombo->setProperty("tbControlRole", "compact");
  m_soundCombo->addItem(tr("All sounds"), int(inspection::WaveKind::All));
  m_soundCombo->addItem(tr("Looping"), int(inspection::WaveKind::Looping));
  m_soundCombo->addItem(tr("One-shot"), int(inspection::WaveKind::OneShot));
  m_soundCombo->setMaximumWidth(128);
  m_soundCombo->setToolTip(
    tr("Filter sounds by their WAV sampler loop chunk. Waveforms are read on demand and "
       "cached until the library revision changes."));
  toolsLayout->insertWidget(1, m_soundCombo);
  connect(m_tagCombo, &QComboBox::currentIndexChanged, this, [this]() { refreshGrid(); });
  connect(m_soundCombo, &QComboBox::currentIndexChanged, this, [this]() {
    m_waveKind = inspection::WaveKind(m_soundCombo->currentData().toInt());
    refreshGrid();
  });
  connect(m_usage, &QListWidget::itemDoubleClicked, this, [this](const auto* item) {
    const auto mapId = item->data(Qt::UserRole).toString();
    if (const auto project = m_project.lock(); project && !mapId.isEmpty())
    {
      emit project->browserOpenMapRequested(mapId);
      hide();
    }
  });
  connect(m_inspector, &AssetInspector::showSnapshotRequested, this, [this]() {
    if (const auto project = m_project.lock(); project && m_projectAssetId)
    {
      emit project->browserRevealAssetRequested(
        QString::fromStdString(*m_projectAssetId));
      hide();
    }
  });
  connect(m_inspector, &AssetInspector::findUsesRequested, this, [this]() {
    if (const auto project = m_project.lock(); project && m_projectAssetId)
      emit project->browserFindUsesRequested(QString::fromStdString(*m_projectAssetId));
  });
  connect(
    m_inspector,
    &AssetInspector::captureRequested,
    this,
    &AssetLibraryWindow::captureSelected);
  connect(
    m_inspector,
    &AssetInspector::updateRequested,
    this,
    &AssetLibraryWindow::updateSelected);
  connect(m_grid, &QListWidget::itemDoubleClicked, this, [this]() {
    if (!m_inspector->asset())
      return;
    auto dialog = QDialog{this};
    dialog.setWindowTitle(tr("Asset preview"));
    dialog.resize(820, 720);
    auto* layout = new QVBoxLayout{&dialog};
    auto* inspector = new AssetInspector{&dialog};
    inspector->setAsset(*m_inspector->asset());
    layout->addWidget(inspector);
    auto* buttons = new QDialogButtonBox{QDialogButtonBox::Close};
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
  });
  connect(m_categories, &QListWidget::currentRowChanged, this, [this](int) {
    if (const auto* item = m_categories->currentItem())
    {
      if (item->data(AssetLibraryCategoryDelegate::SectionRole).toBool())
        return;
      m_filter = item->data(AssetLibraryCategoryDelegate::FilterRole).toString();
    }
    if (const auto type = typeForFilter(m_filter);
        type && !m_folderPath.isEmpty()
        && !folderContains(libraryFolder(*type, {}), m_folderPath))
    {
      const auto target = libraryFolder(*type, {});
      auto* folderItem = m_folders->topLevelItem(0);
      for (auto it = QTreeWidgetItemIterator{m_folders}; *it; ++it)
      {
        if ((*it)->data(0, Qt::UserRole).toString() == target)
        {
          folderItem = *it;
          break;
        }
      }
      if (folderItem != m_folders->currentItem())
      {
        m_folders->setCurrentItem(folderItem);
        return; // The folder selection refreshes the grid.
      }
    }
    refreshGrid();
  });
  connect(m_grid, &QListWidget::currentItemChanged, this, [this](auto* current, auto*) {
    if (!current)
    {
      m_selectedId.reset();
      clearInspector();
      return;
    }
    m_selectedId =
      current->data(AssetLibraryItemDelegate::AssetIdRole).toString().toStdString();
    showAsset(selectedAsset());
  });
  connect(m_sizeSlider, &QSlider::valueChanged, this, [this](int) { applyViewMode(); });
  connect(m_gridButton, &QToolButton::clicked, this, [this]() {
    m_listMode = false;
    applyViewMode();
  });
  connect(m_listButton, &QToolButton::clicked, this, [this]() {
    m_listMode = true;
    applyViewMode();
  });


  applyViewMode();
  clearInspector();
}

AssetLibraryWindow::~AssetLibraryWindow() = default;

void AssetLibraryWindow::chooseLibrary()
{
  const auto folder = QFileDialog::getExistingDirectory(
    this, tr("Choose Asset Library"), {}, QFileDialog::ShowDirsOnly);
  if (folder.isEmpty())
    return;
  openLibrary(folder);
}

void AssetLibraryWindow::openLibrary(const QString& folder, const bool remember)
{
  const auto path = pathFromQString(folder);
  if (!project::AssetLibrary::isLibraryFolder(path))
  {
    const auto create =
      QMessageBox::question(
        this,
        tr("Create Asset Library"),
        tr("'%1' is not an asset library yet.\n\nCreate a catalog inside it?")
          .arg(QFileInfo{folder}.fileName()))
      == QMessageBox::Yes;
    if (!create)
      return;
    auto created = project::AssetLibrary::create(
      path,
      path.filename().empty() ? std::string{"Asset Library"} : path.filename().string());
    if (created.is_error())
    {
      QMessageBox::critical(
        this,
        tr("Asset Library"),
        QString::fromStdString(std::get<Error>(created.error()).msg));
      return;
    }
    if (const auto scanned = created.value()->scan(); scanned.is_error())
    {
      QMessageBox::critical(
        this,
        tr("Asset Library"),
        QString::fromStdString(std::get<Error>(scanned.error()).msg));
      return;
    }
  }

  QString error;
  auto source = openLocalAssetLibrary(path, &error);
  if (!source)
  {
    QMessageBox::critical(this, tr("Asset Library"), error);
    return;
  }
  resetConvertQueue();
  m_reloadTimer->stop();
  m_thumbnailCache.clear();
  m_waveInfoCache.clear();
  m_source = std::move(source);
  m_selectedId.reset();
  m_filter = QStringLiteral("all");
  m_folderPath.clear();
  if (remember)
    PreferenceManager::instance().set(Preferences::AssetLibraryDirectory, path);
  reload();
}

void AssetLibraryWindow::reload()
{
  m_assets.clear();
  m_thumbnailKeys.clear();
  if (!m_source)
  {
    m_empty->setText(tr("No library is open.\nUse Change Library… to choose one."));
    m_empty->show();
    m_grid->hide();
    clearInspector();
    updateHeader();
    refreshCategories();
    refreshFolders();
    updateStatus();
    return;
  }

  const auto assets = m_source->assets();
  if (assets.is_error())
  {
    m_empty->setText(QString::fromStdString(std::get<Error>(assets.error()).msg));
    m_empty->show();
    m_grid->hide();
    updateHeader();
    refreshCategories();
    refreshFolders();
    updateStatus();
    return;
  }
  m_assets = assets.value();
  const auto keys = assetPreviewKeys(*m_source);
  if (keys.is_success())
    m_thumbnailKeys = keys.value();
  else
  {
    m_thumbnailCache.clear();
    for (const auto& asset : m_assets)
      m_thumbnailKeys.emplace(asset.id, QString::fromStdString(asset.id));
  }
  m_favorites.clear();
  m_metadata.clear();
  const auto tag = m_tagCombo->currentData().toString();
  const auto tagBlocker = QSignalBlocker{m_tagCombo};
  m_tagCombo->clear();
  m_tagCombo->addItem(tr("All tags"), QString{});
  auto tags = QStringList{};
  if (const auto metadata = m_source->allMetadata(); metadata.is_success())
    m_metadata = metadata.value();
  else
    for (const auto& asset : m_assets)
      if (const auto meta = m_source->metadata(asset.id); meta.is_success())
        m_metadata.emplace(asset.id, meta.value());
  for (const auto& asset : m_assets)
  {
    const auto meta = m_metadata.find(asset.id);
    if (meta == m_metadata.end())
      continue;
    if (meta->second.favorite)
      m_favorites.insert(asset.id);
    for (const auto& value : meta->second.tags)
      if (!tags.contains(QString::fromStdString(value), Qt::CaseInsensitive))
        tags.push_back(QString::fromStdString(value));
  }
  tags.sort(Qt::CaseInsensitive);
  for (const auto& value : tags)
    m_tagCombo->addItem(value, value);
  if (!tag.isEmpty() && m_tagCombo->findData(tag) < 0)
    m_tagCombo->addItem(tag, tag);
  m_tagCombo->setCurrentIndex(std::max(0, m_tagCombo->findData(tag)));
  updateHeader();
  refreshCategories();
  refreshFolders();
  refreshGrid();
  updateStatus();
}

void AssetLibraryWindow::updateHeader()
{
  const auto accent = accentColor(*this);
  m_libraryBadge->setPixmap(
    assetLibraryGlyphPixmap(AssetLibraryGlyph::Library, accent, 20, devicePixelRatioF()));

  if (!m_source)
  {
    m_libraryLabel->setText(tr("No asset library"));
    m_libraryPath->setText(tr("Choose a folder to browse as an asset library."));
    m_libraryChip->setText(tr("No library"));
    m_libraryChip->setProperty("libraryState", QStringLiteral("none"));
  }
  else
  {
    m_libraryLabel->setText(m_source->name());
    m_libraryPath->setText(m_source->location());
    m_libraryPath->setToolTip(m_source->location());
    const auto remote = m_source->isRemote();
    m_libraryChip->setText(remote ? tr("Remote") : tr("Local"));
    m_libraryChip->setProperty(
      "libraryState", remote ? QStringLiteral("remote") : QStringLiteral("local"));
  }
  // Re-evaluate the state property selectors above.
  m_libraryChip->style()->unpolish(m_libraryChip);
  m_libraryChip->style()->polish(m_libraryChip);
  updateActions();
}

void AssetLibraryWindow::updateActions()
{
  const auto hasSource = m_source != nullptr;
  m_importButton->setEnabled(hasSource && m_source->canImport());
  m_createMaterialButton->setEnabled(hasSource && m_source->canManage());
  m_rescanButton->setEnabled(hasSource);
  m_convertButton->setEnabled(
    hasSource && m_source->canConvert() && pendingCount() > 0
    && !m_convertTimer->isActive());
  m_revealButton->setEnabled(hasSource && m_source->canReveal());
  m_searchEdit->setEnabled(hasSource);
}

void AssetLibraryWindow::updateSelectionActions()
{
  m_createMaterialFromSelection->setEnabled(selectedTextures().has_value());
  const auto canDelete =
    m_source && m_source->canManage() && !m_grid->selectedItems().empty();
  m_deleteButton->setEnabled(canDelete);
  m_deleteAction->setEnabled(canDelete);
}

void AssetLibraryWindow::applyViewMode()
{
  const auto side = m_sizeSlider->value();
  if (m_sizeValue)
    m_sizeValue->setText(QString::number(side));

  m_gridButton->setChecked(!m_listMode);
  m_listButton->setChecked(m_listMode);
  m_delegate->setListMode(m_listMode);

  if (m_listMode)
  {
    m_grid->setViewMode(QListView::ListMode);
    m_grid->setUniformItemSizes(false);
    m_grid->setGridSize(QSize{});
  }
  else
  {
    const auto cell = QSize{side + 24, side + 24 + 46};
    m_delegate->setCellSize(cell);
    m_grid->setViewMode(QListView::IconMode);
    m_grid->setUniformItemSizes(true);
    m_grid->setGridSize(cell);
  }
  m_grid->doItemsLayout();
}

void AssetLibraryWindow::refreshCategories()
{
  const auto current = m_filter;
  const auto blockers = QSignalBlocker{m_categories};
  m_categories->clear();

  const auto addSection = [this](const QString& title) {
    auto* item = new QListWidgetItem{m_categories};
    item->setText(title);
    item->setData(AssetLibraryCategoryDelegate::SectionRole, true);
    item->setFlags(Qt::NoItemFlags);
  };
  const auto add = [this](const QString& key, const QString& label, const int count) {
    auto* item = new QListWidgetItem{m_categories};
    item->setText(label);
    item->setData(AssetLibraryCategoryDelegate::FilterRole, key);
    item->setData(AssetLibraryCategoryDelegate::CountRole, count);
  };

  addSection(tr("Library"));
  add(QStringLiteral("all"), tr("All assets"), int(m_assets.size()));
  add(QStringLiteral("favourites"), tr("Favourites"), int(m_favorites.size()));
  const auto countOf = [this](const project::AssetType type) {
    return int(std::count_if(m_assets.begin(), m_assets.end(), [type](const auto& asset) {
      return asset.type == type;
    }));
  };
  add(QStringLiteral("texture"), tr("Textures"), countOf(project::AssetType::Texture));
  add(QStringLiteral("material"), tr("Materials"), countOf(project::AssetType::Material));
  add(QStringLiteral("model"), tr("Models"), countOf(project::AssetType::Model));
  add(QStringLiteral("sprite"), tr("Sprites"), countOf(project::AssetType::Sprite));
  add(QStringLiteral("sound"), tr("Sounds"), countOf(project::AssetType::Sound));
  add(QStringLiteral("skybox"), tr("Skyboxes"), countOf(project::AssetType::Skybox));
  add(QStringLiteral("prefab"), tr("Prefabs"), countOf(project::AssetType::Prefab));

  addSection(tr("Attention"));
  add(QStringLiteral("pending"), tr("Pending"), pendingCount());
  add(QStringLiteral("missing"), tr("Missing sources"), missingCount());

  for (auto row = 0; row < m_categories->count(); ++row)
  {
    auto* item = m_categories->item(row);
    if (
      !item->data(AssetLibraryCategoryDelegate::SectionRole).toBool()
      && item->data(AssetLibraryCategoryDelegate::FilterRole).toString() == current)
    {
      m_categories->setCurrentRow(row);
      return;
    }
  }

  m_filter = QStringLiteral("all");
  for (auto row = 0; row < m_categories->count(); ++row)
  {
    auto* item = m_categories->item(row);
    if (
      !item->data(AssetLibraryCategoryDelegate::SectionRole).toBool()
      && item->data(AssetLibraryCategoryDelegate::FilterRole).toString()
           == QStringLiteral("all"))
    {
      m_categories->setCurrentRow(row);
      return;
    }
  }
}

void AssetLibraryWindow::refreshFolders()
{
  auto expanded = QSet<QString>{};
  for (auto it = QTreeWidgetItemIterator{m_folders}; *it; ++it)
    if ((*it)->isExpanded())
      expanded.insert((*it)->data(0, Qt::UserRole).toString());

  const auto blocker = QSignalBlocker{m_folders};
  m_folders->clear();
  auto* root = new QTreeWidgetItem{m_folders, QStringList{tr("All folders")}};
  root->setData(0, Qt::UserRole, QString{});
  auto items = QHash<QString, QTreeWidgetItem*>{{QString{}, root}};
  for (const auto& asset : m_assets)
  {
    auto path = QString{};
    auto* parent = root;
    for (const auto& part : libraryFolder(asset).split('/', Qt::SkipEmptyParts))
    {
      path += (path.isEmpty() ? QString{} : QStringLiteral("/")) + part;
      if (const auto found = items.constFind(path); found != items.cend())
        parent = found.value();
      else
      {
        const auto title =
          parent == root ? browserFolderDisplayName(pathFromQString(path)) : part;
        auto* item = new QTreeWidgetItem{parent, QStringList{title}};
        item->setData(0, Qt::UserRole, path);
        items.insert(path, item);
        parent = item;
      }
    }
  }
  for (auto it = items.cbegin(); it != items.cend(); ++it)
    it.value()->setExpanded(it.key().isEmpty() || expanded.contains(it.key()));
  auto* selected = items.value(m_folderPath, nullptr);
  if (!selected)
  {
    m_folderPath.clear();
    selected = root;
  }
  m_folders->setCurrentItem(selected);
  for (auto* parent = selected->parent(); parent; parent = parent->parent())
    parent->setExpanded(true);
  m_folders->scrollToItem(selected);
}

std::optional<inspection::WaveInfo> AssetLibraryWindow::waveInfo(
  const project::LibraryAsset& asset) const
{
  const auto key = fmt::format("{}/{}/{}", asset.id, asset.contentHash, asset.revision);
  if (const auto cached = m_waveInfoCache.find(key); cached != m_waveInfoCache.end())
    return cached->second;

  auto info = std::optional<inspection::WaveInfo>{};
  if (m_source)
  {
    const auto bytes = m_source->readContent(asset.id);
    if (bytes.is_success())
    {
      const auto wave = inspection::readWaveInfo(bytes.value());
      if (wave.is_success())
        info = wave.value();
    }
  }
  if (m_waveInfoCache.size() > 4096)
    m_waveInfoCache.clear();
  m_waveInfoCache[key] = info;
  return info;
}

void AssetLibraryWindow::refreshGrid()
{
  const auto selection = m_selectedId;
  const auto blocker = QSignalBlocker{m_grid};
  m_grid->clear();
  m_empty->hide();

  if (!m_source)
  {
    m_grid->hide();
    m_countLabel->clear();
    updateSelectionActions();
    return;
  }

  const auto search = m_search.trimmed();
  auto shown = 0;
  for (const auto& asset : m_assets)
  {
    if (!m_folderPath.isEmpty())
    {
      const auto group = libraryFolder(asset);
      if (!folderContains(m_folderPath, group))
        continue;
    }
    const auto typeFilter = typeForFilter(m_filter);
    const auto matchesType =
      m_filter == QStringLiteral("all") || (typeFilter && asset.type == *typeFilter)
      || (m_filter == QStringLiteral("pending") && needsConversion(asset))
      || (m_filter == QStringLiteral("missing") && asset.missing)
      || (m_filter == QStringLiteral("favourites") && m_favorites.contains(asset.id));
    if (!matchesType)
      continue;
    const auto found = m_metadata.find(asset.id);
    const auto metadata = found == m_metadata.end()
                            ? std::optional<BrowserAssetMetadata>{}
                            : std::optional{found->second};
    if (!assetMetadataMatches(
          metadata, m_filter == "favourites", m_tagCombo->currentData().toString()))
      continue;
    if (m_waveKind != inspection::WaveKind::All)
    {
      if (asset.type != project::AssetType::Sound)
        continue;
      const auto info = waveInfo(asset);
      if (!info || !inspection::matchesWaveKind(*info, m_waveKind))
        continue;
    }
    const auto displayName = QString::fromStdString(asset.displayName);
    if (!assetSearchMatches(
          displayName,
          QString::fromStdString(asset.virtualPath),
          metadata ? metadata->tags : std::vector<std::string>{},
          search))
      continue;

    auto* item =
      new AssetLibraryItem{m_grid, [this, asset]() { return thumbnail(asset); }};
    item->setText(displayName);
    item->setData(
      AssetLibraryItemDelegate::AssetIdRole, QString::fromStdString(asset.id));
    item->setData(AssetLibraryItemDelegate::TypeLabelRole, typeText(asset.type));
    item->setData(AssetLibraryItemDelegate::DetailRole, sizeText(asset.size));
    item->setData(AssetLibraryItemDelegate::StatusRole, statusFor(asset));
    item->setData(AssetLibraryItemDelegate::KindRole, int(kindGlyph(asset.type)));
    item->setData(
      AssetLibraryItemDelegate::PathRole, QString::fromStdString(asset.virtualPath));
    item->setToolTip(QStringLiteral("%1\n%2 · %3")
                       .arg(
                         QString::fromStdString(asset.virtualPath),
                         typeText(asset.type),
                         sizeText(asset.size)));

    ++shown;
  }

  m_countLabel->setText(
    shown == int(m_assets.size())
      ? tr("%n asset(s)", nullptr, shown)
      : tr("%1 of %2 assets").arg(shown).arg(int(m_assets.size())));

  if (shown == 0)
  {
    m_empty->setText(
      m_assets.empty() ? tr("This library is empty.\nUse Import Files… to add assets.")
                       : tr("No assets match the current filter."));
    m_empty->show();
    m_grid->hide();
  }
  else
  {
    m_grid->show();
  }

  if (selection)
  {
    for (auto row = 0; row < m_grid->count(); ++row)
    {
      if (
        m_grid->item(row)
          ->data(AssetLibraryItemDelegate::AssetIdRole)
          .toString()
          .toStdString()
        == *selection)
      {
        m_grid->setCurrentRow(row);
        break;
      }
    }
  }
  else if (m_grid->count() > 0)
  {
    // Open with something selected so the inspector already shows the first asset.
    m_grid->setCurrentRow(0);
  }
  if (const auto* current = m_grid->currentItem())
    m_selectedId =
      current->data(AssetLibraryItemDelegate::AssetIdRole).toString().toStdString();
  else
    m_selectedId.reset();
  updateSelectionActions();
  showAsset(selectedAsset());
}

void AssetLibraryWindow::showAsset(const project::LibraryAsset* asset)
{
  if (!asset)
  {
    clearInspector();
    return;
  }
  const auto manageable = m_source && m_source->canManage();
  m_favoriteButton->setEnabled(manageable);
  m_renameButton->setEnabled(manageable);
  auto favorite = false;
  auto tags = QString{};
  if (manageable)
  {
    if (const auto meta = m_source->metadata(asset->id); meta.is_success())
    {
      favorite = meta.value().favorite;
      for (const auto& tag : meta.value().tags)
        tags += (tags.isEmpty() ? QString{} : QStringLiteral(", "))
                + QString::fromStdString(tag);
    }
  }
  m_tagsEdit->setEnabled(manageable);
  m_tagsEdit->setText(tags);
  m_favoriteButton->setChecked(favorite);
  m_favoriteButton->setText(
    favorite ? QStringLiteral("★ Favourite") : QStringLiteral("☆ Favourite"));

  m_name->setText(QString::fromStdString(asset->displayName));
  m_name->setToolTip(QString::fromStdString(asset->virtualPath));
  m_status->setText(
    asset->missing                              ? tr("Missing source")
    : needsConversion(*asset)                   ? tr("Pending conversion")
    : asset->type == project::AssetType::Skybox ? tr("Skybox group")
                                                : tr("Ready"));

  while (m_properties->rowCount() > 0)
    m_properties->removeRow(0);
  const auto add = [this](const QString& key, const QString& value) {
    auto* keyLabel = new QLabel{key, m_properties->parentWidget()};
    keyLabel->setObjectName(QStringLiteral("AssetLibraryPropertyKey"));
    auto* valueLabel = new QLabel{value, m_properties->parentWidget()};
    valueLabel->setObjectName(QStringLiteral("AssetLibraryPropertyValue"));
    valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    valueLabel->setWordWrap(true);
    m_properties->addRow(keyLabel, valueLabel);
  };
  add(tr("Type"), typeText(asset->type));
  add(tr("Path"), QString::fromStdString(asset->virtualPath));
  add(tr("Size"), sizeText(asset->size));
  add(tr("Format"), QString::fromStdString(asset->nativeFormat));
  add(tr("Hash"), QString::fromStdString(asset->contentHash).left(12));

  m_dependencies->clear();
  if (const auto dependencies = m_source->dependencies(asset->id);
      dependencies.is_success())
  {
    for (const auto& dependency : dependencies.value())
    {
      auto name = QString::fromStdString(dependency.dependencyId);
      if (const auto it = std::find_if(
            m_assets.begin(),
            m_assets.end(),
            [&](const auto& candidate) {
              return candidate.id == dependency.dependencyId;
            });
          it != m_assets.end())
        name = QString::fromStdString(it->displayName);
      m_dependencies->addItem(
        QStringLiteral("%1  ·  %2").arg(name, QString::fromStdString(dependency.kind)));
    }
  }
  if (m_dependencies->count() == 0)
    m_dependencies->addItem(tr("No recorded dependencies"));

  refreshUsage();

  updatePreview();
}

void AssetLibraryWindow::clearInspector()
{
  m_favoriteButton->setEnabled(false);
  m_renameButton->setEnabled(false);
  m_projectAssetId.reset();
  m_usage->clear();
  m_usage->addItem(tr("No asset selected"));
  m_name->setText(tr("No asset selected"));
  m_name->setToolTip({});
  m_status->clear();
  m_inspector->clear();
  m_dependencies->clear();
  while (m_properties->rowCount() > 0)
    m_properties->removeRow(0);
}

QImage AssetLibraryWindow::thumbnail(const project::LibraryAsset& asset) const
{
  const auto found = m_thumbnailKeys.find(asset.id);
  if (found == m_thumbnailKeys.end())
    return {};
  const auto& key = found->second;
  if (const auto* cached = m_thumbnailCache.object(key))
    return *cached;
  auto image = loadThumbnail(asset);
  if (!image.isNull())
    image = image.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  const auto cost = std::max(qsizetype{1}, (image.sizeInBytes() + 1023) / 1024);
  m_thumbnailCache.insert(key, new QImage{image}, cost);
  return image;
}

QImage AssetLibraryWindow::loadThumbnail(const project::LibraryAsset& asset) const
{
  if (!m_source || asset.type == project::AssetType::Sound)
    return {};

  if (asset.type == project::AssetType::Skybox)
  {
    const auto inspected = inspection::readLibraryAsset(*m_source, asset);
    if (inspected.is_error())
      return {};
    return inspection::skyThumbnail(inspected.value());
  }

  if (asset.type == project::AssetType::Model)
    return renderModelThumbnail(
      pathFromQString(m_source->location()), asset.sourceContainer, 256);
  if (asset.type == project::AssetType::Material)
    return inspection::libraryTerrainMaterialPreview(*m_source, asset.id);
  const auto bytes = m_source->readContent(asset.id);
  if (bytes.is_error())
    return {};
  if (asset.type == project::AssetType::Texture)
  {
    if (const auto image = inspection::decodeImage(bytes.value(), asset.nativeFormat);
        image.is_success())
      return image.value();
    return {};
  }
  if (asset.type == project::AssetType::Sprite)
  {
    const auto sprite = loadGoldSrcSpritePreview(bytes.value());
    if (sprite)
      return QImage{
        sprite->rgba.data(),
        int(sprite->width),
        int(sprite->height),
        int(sprite->width * 4),
        QImage::Format_RGBA8888}
        .copy();
  }
  if (asset.type == project::AssetType::Prefab)
  {
    // An editor-saved thumbnail wins; otherwise draw the authored footprint so the card
    // shows the prefab's volume instead of a generic type icon.
    const auto location = pathFromQString(m_source->location());
    const auto saved = QImage{pathAsQString(
      prefabThumbnailPath(location / std::filesystem::path{asset.virtualPath}))};
    if (!saved.isNull())
      return saved;
    const auto& contentsBytes = bytes.value();
    if (const auto contents =
          readPrefabContents(std::string{contentsBytes.begin(), contentsBytes.end()});
        contents.is_success())
      return renderPrefabThumbnail(contents.value(), 256);
  }
  return {};
}

void AssetLibraryWindow::updatePreview()
{
  if (const auto* asset = selectedAsset(); asset && m_source)
  {
    const auto project = m_project.lock();
    auto inspected = inspection::readLibraryAsset(*m_source, *asset, project.get());
    if (inspected.is_success())
    {
      auto loaded = std::move(inspected).value();
      if (m_projectAssetId)
      {
        loaded.resource.projectAssetId = *m_projectAssetId;
        if (project)
        {
          const auto snapshot = project->store().findAsset(*m_projectAssetId);
          if (snapshot.is_success() && snapshot.value())
            loaded.resource.projectRevision = snapshot.value()->revision;
        }
      }
      m_inspector->setAsset(std::move(loaded));
    }
    else
      m_inspector->setAsset(std::move(inspected));
  }
  else
    m_inspector->clear();
  updateProjectActions();
}

void AssetLibraryWindow::updateStatus()
{
  const auto total = int(m_assets.size());
  const auto pending = pendingCount();
  const auto missing = missingCount();
  auto text = tr("%1 assets · %2 pending conversion · %3 missing")
                .arg(total)
                .arg(pending)
                .arg(missing);
  if (m_source)
  {
    text +=
      tr(" · %1").arg(m_source->isRemote() ? tr("Remote library") : tr("Local library"));
  }
  m_footer->setText(text);
  updateActions();
}

int AssetLibraryWindow::pendingCount() const
{
  return int(std::count_if(m_assets.begin(), m_assets.end(), [](const auto& asset) {
    return needsConversion(asset);
  }));
}

int AssetLibraryWindow::missingCount() const
{
  return int(std::count_if(
    m_assets.begin(), m_assets.end(), [](const auto& asset) { return asset.missing; }));
}

const project::LibraryAsset* AssetLibraryWindow::selectedAsset() const
{
  if (!m_selectedId)
    return nullptr;
  const auto it = std::find_if(m_assets.begin(), m_assets.end(), [&](const auto& asset) {
    return asset.id == *m_selectedId;
  });
  return it == m_assets.end() ? nullptr : &*it;
}

Result<void> AssetLibraryWindow::rescanLibrary()
{
  // Keep the source and dialog alive while the worker owns a separate catalog handle.
  // Application modality prevents concurrent imports, library switching and closing.
  const auto converting = m_convertTimer->isActive();
  m_convertTimer->stop();
  auto progress = LibraryScanProgress{tr("Rescan"), QString{}, 0, 0, this};
  progress.setCancelButton(nullptr);
  progress.setWindowFlag(Qt::WindowCloseButtonHint, false);
  progress.setWindowModality(Qt::ApplicationModal);
  auto watcher = QFutureWatcher<Result<void>>{};
  auto loop = QEventLoop{};
  connect(&watcher, &QFutureWatcher<Result<void>>::finished, &loop, &QEventLoop::quit);
  watcher.setFuture(QtConcurrent::run(m_source->rescanTask()));
  progress.show();
  loop.exec();
  // The event loop keeps UI painting and input processing active until completion.
  progress.close();
  const auto scanned = watcher.result();
  if (converting)
    m_convertTimer->start();
  if (scanned.is_success())
    emit AssetLibraryChanges::instance().changed(
      QString::fromStdString(m_source -> libraryId()));
  return scanned;
}

void AssetLibraryWindow::runRescan()
{
  if (!m_source)
    return;
  if (const auto scanned = rescanLibrary(); scanned.is_error())
  {
    QMessageBox::critical(
      this, tr("Rescan"), QString::fromStdString(std::get<Error>(scanned.error()).msg));
    return;
  }
  reload();
  m_footer->setText(tr("Rescanned %1 asset(s).").arg(int(m_assets.size())));
}

void AssetLibraryWindow::retryFailedConversions()
{
  if (
    !m_source || !m_source->canConvert() || m_retryIds.empty()
    || m_convertTimer->isActive())
    return;

  m_reloadTimer->stop();
  m_convertQueue = m_retryIds;
  m_retryIds.clear();
  m_retryButton->hide();
  m_convertDone = 0;
  m_convertFailed = 0;
  m_conversionReport.clear();
  m_failedConversions.clear();
  m_convertProgress->setRange(0, int(m_convertQueue.size()));
  m_convertProgress->setValue(0);
  m_convertProgress->show();
  m_convertCancel->show();
  m_convertTimer->start();
  updateActions();
}

void AssetLibraryWindow::runConvertPending()
{
  if (!m_source || !m_source->canConvert() || m_convertTimer->isActive())
    return;
  m_reloadTimer->stop();
  m_convertQueue.clear();
  m_retryIds.clear();
  m_retryButton->hide();
  for (const auto& asset : m_assets)
  {
    if (needsConversion(asset))
      m_convertQueue.push_back(asset.id);
  }
  if (m_convertQueue.empty())
    return;

  m_convertDone = 0;
  m_convertFailed = 0;
  m_conversionReport.clear();
  m_failedConversions.clear();
  m_convertProgress->setRange(0, int(m_convertQueue.size()));
  m_convertProgress->setValue(0);
  m_convertProgress->show();
  m_convertCancel->show();
  m_convertTimer->start();
  updateActions();
}

void AssetLibraryWindow::stepConvertQueue()
{
  if (!m_source || m_convertQueue.empty())
  {
    m_convertTimer->stop();
    return;
  }

  const auto id = m_convertQueue.front();
  m_convertQueue.erase(m_convertQueue.begin());
  const auto result = m_source->convertAsset(id);
  if (result.is_error() || !result.value())
  {
    ++m_convertFailed;
    m_failedConversions.push_back(id);
    m_conversionReport
      << QString::fromStdString(id) + ": "
           + (result.is_error() ? QString::fromStdString(std::get<Error>(result.error()).msg) : tr("No compatible content was produced."));
  }
  ++m_convertDone;
  m_convertProgress->setValue(m_convertDone);

  if (m_convertQueue.empty())
  {
    m_convertTimer->stop();
    m_convertProgress->hide();
    m_convertCancel->hide();
    reload();
    m_footer->setText(
      tr("Converted %1 asset(s)%2.")
        .arg(m_convertDone - m_convertFailed)
        .arg(m_convertFailed > 0 ? tr(", %1 failed").arg(m_convertFailed) : QString{}));
    if (!m_failedConversions.empty())
    {
      m_retryIds = m_failedConversions;
      m_retryButton->setText(tr("Retry failed (%1)").arg(qulonglong(m_retryIds.size())));
      m_retryButton->show();
      showOperationReport(m_conversionReport, {}, m_failedConversions);
    }
    else
    {
      m_retryIds.clear();
      m_retryButton->hide();
    }
  }
}

void AssetLibraryWindow::cancelConvertQueue()
{
  m_convertTimer->stop();
  const auto remaining = m_convertQueue.size();
  m_convertQueue.clear();
  m_convertProgress->hide();
  m_convertCancel->hide();
  reload();
  m_footer->setText(tr("Cancelled: %1 completed, %2 failed, %3 pending.")
                      .arg(m_convertDone)
                      .arg(m_convertFailed)
                      .arg(qulonglong(remaining)));
}

void AssetLibraryWindow::resetConvertQueue()
{
  m_convertTimer->stop();
  m_convertQueue.clear();
  m_retryIds.clear();
  m_failedConversions.clear();
  m_conversionReport.clear();
  m_convertDone = m_convertFailed = 0;
  m_convertProgress->hide();
  m_convertCancel->hide();
  m_retryButton->hide();
}

void AssetLibraryWindow::runImport()
{
  if (!m_source || !m_source->canImport())
    return;
  const auto files = QFileDialog::getOpenFileNames(
    this,
    tr("Import Assets"),
    {},
    tr("Supported assets (*.png *.jpg *.jpeg *.bmp *.tga *.wad *.mdl *.spr *.wav "
       "*.tbprefab);;"
       "All files (*)"));
  if (files.isEmpty())
    return;
  importFiles(files);
}

std::optional<std::array<std::string, 3>> AssetLibraryWindow::selectedTextures() const
{
  if (!m_source || !m_source->canManage())
    return std::nullopt;
  auto items = m_grid->selectedItems();
  if (items.size() != 3)
    return std::nullopt;
  std::ranges::sort(items, [this](const auto* left, const auto* right) {
    return m_grid->row(left) < m_grid->row(right);
  });
  auto result = std::array<std::string, 3>{};
  for (auto slot = size_t{0}; slot < result.size(); ++slot)
  {
    const auto id = items.at(qsizetype(slot))
                      ->data(AssetLibraryItemDelegate::AssetIdRole)
                      .toString()
                      .toStdString();
    const auto asset = std::ranges::find_if(
      m_assets, [&](const auto& candidate) { return candidate.id == id; });
    if (
      asset == m_assets.end() || asset->missing
      || asset->type != project::AssetType::Texture)
      return std::nullopt;
    result[slot] = id;
  }
  return result;
}

void AssetLibraryWindow::runCreateTerrainMaterial()
{
  if (!m_source || !m_source->canManage())
    return;
  auto textures = std::vector<project::LibraryAsset>{};
  for (const auto& asset : m_assets)
  {
    if (asset.type == project::AssetType::Texture)
      textures.push_back(asset);
  }
  auto dialog = CreateTerrainMaterialDialog{textures, this};
  if (const auto selected = selectedTextures())
    dialog.setSelectedTextures(*selected);
  auto def = project::AssetLibrary::TerrainMaterialDefinition{};
  auto package = std::vector<std::uint8_t>{};
  while (true)
  {
    if (dialog.exec() != QDialog::Accepted)
      return;
    def = dialog.definition();
    auto images = std::array<QImage, 3>{};
    auto input = std::array<std::vector<std::uint8_t>, 3>{};
    auto convert = std::array<bool, 3>{};
    auto problem = QString{};
    for (auto slot = size_t{0}; slot < images.size(); ++slot)
    {
      const auto& id = def.materialSlots[slot].texture;
      const auto asset =
        std::ranges::find_if(textures, [&](const auto& item) { return item.id == id; });
      if (asset == textures.end())
      {
        problem = tr("A selected texture is no longer available.");
        break;
      }
      auto bytes = m_source->readContent(id);
      if (bytes.is_error())
      {
        problem = QString::fromStdString(std::get<Error>(bytes.error()).msg);
        break;
      }
      auto image = inspection::decodeImage(bytes.value(), asset->nativeFormat);
      if (image.is_error())
      {
        problem = QString::fromStdString(std::get<Error>(image.error()).msg);
        break;
      }
      images[slot] = std::move(image).value();
      input[slot] = std::move(bytes).value();
      convert[slot] = asset->nativeFormat == "image";
    }
    if (problem.isEmpty())
    {
      const auto validation = normalizeTerrainMaterialImages(images);
      if (validation.is_error())
        problem = QString::fromStdString(std::get<Error>(validation.error()).msg);
    }
    // A project captures ordinary images as GoldSrc textures. Generate the package
    // from those same pixels, so a project can verify it against its own snapshots.
    for (auto slot = size_t{0}; problem.isEmpty() && slot < images.size(); ++slot)
    {
      if (!convert[slot])
        continue;
      const auto decoded = img::decodeImage(input[slot].data(), input[slot].size());
      if (decoded.is_error())
      {
        problem = QString::fromStdString(std::get<Error>(decoded.error()).msg);
        break;
      }
      const auto texture = img::convertGoldSrcTexture(decoded.value(), "terrain_source");
      if (texture.is_error())
      {
        problem = QString::fromStdString(std::get<Error>(texture.error()).msg);
        break;
      }
      auto image = inspection::decodeImage(texture.value().wad);
      if (image.is_error())
      {
        problem = QString::fromStdString(std::get<Error>(image.error()).msg);
        break;
      }
      images[slot] = std::move(image).value();
    }
    if (problem.isEmpty())
    {
      const auto material = prepareTerrainMaterial(std::move(images));
      if (material.is_error())
        problem = QString::fromStdString(std::get<Error>(material.error()).msg);
      else
        package = encodeTerrainMaterialPackage(material.value());
      if (problem.isEmpty() && package.empty())
        problem = tr("Cannot create the terrain material package.");
    }
    if (problem.isEmpty())
      break;
    QMessageBox::warning(this, tr("Create Material"), problem);
  }
  const auto created = m_source->createTerrainMaterial(def, std::move(package));
  if (created.is_error())
  {
    QMessageBox::warning(
      this,
      tr("Create Material"),
      QString::fromStdString(std::get<Error>(created.error()).msg));
    return;
  }
  reload();
  focusAsset(created.value().id, QStringLiteral("material"));
  updateStatus();
}

void AssetLibraryWindow::showOperationReport(
  const QStringList& lines,
  const QStringList& files,
  const std::vector<std::string>& conversions)
{
  auto dialog = QDialog{this};
  dialog.setWindowTitle(tr("Asset operation results"));
  dialog.resize(700, 420);
  auto* layout = new QVBoxLayout{&dialog};
  auto* report = new QTextEdit{};
  report->setReadOnly(true);
  report->setPlainText(lines.join("\n"));
  layout->addWidget(report);
  auto* buttons = new QDialogButtonBox{QDialogButtonBox::Close};
  if (!files.empty())
    connect(
      buttons->addButton(tr("Retry failed imports"), QDialogButtonBox::ActionRole),
      &QPushButton::clicked,
      &dialog,
      [&]() { dialog.done(1); });
  if (!conversions.empty())
    connect(
      buttons->addButton(tr("Retry failed conversions"), QDialogButtonBox::ActionRole),
      &QPushButton::clicked,
      &dialog,
      [&]() { dialog.done(2); });
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  const auto result = dialog.exec();
  if (result == 1)
    importFiles(files);
  else if (result == 2)
  {
    const auto retryConversions = conversions;
    m_convertQueue = retryConversions;
    m_convertDone = m_convertFailed = 0;
    m_conversionReport.clear();
    m_failedConversions.clear();
    m_convertProgress->setRange(0, int(retryConversions.size()));
    m_convertProgress->setValue(0);
    m_convertProgress->show();
    m_convertCancel->show();
    m_convertTimer->start();
    updateActions();
  }
}

void AssetLibraryWindow::importFiles(const QStringList& files)
{
  if (!m_source || !m_source->canImport())
    return;
  struct Entry
  {
    std::filesystem::path source;
    std::filesystem::path relative;
    bool conflict = false;
  };
  const auto folder = pathFromQString(m_source->location());
  auto entries = std::vector<Entry>{};
  auto report = QStringList{};
  auto retry = QStringList{};
  auto failedConversions = std::vector<std::string>{};
  for (const auto& file : files)
  {
    const auto source = pathFromQString(file);
    const auto subfolder = librarySubfolderForFile(source);
    if (subfolder.empty())
    {
      report << tr("%1: unsupported file type").arg(file);
      continue;
    }
    const auto relative = subfolder / source.filename();
    auto ec = std::error_code{};
    entries.push_back({source, relative, std::filesystem::exists(folder / relative, ec)});
  }
  auto overwrite = false;
  if (std::ranges::any_of(entries, [](const auto& entry) { return entry.conflict; }))
  {
    const auto answer = QMessageBox::question(
      this,
      tr("Import Assets"),
      tr("Some files already exist in the library. Overwrite them?\n\nChoosing No keeps "
         "both by adding a suffix."),
      QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    if (answer == QMessageBox::Cancel)
      return;
    overwrite = answer == QMessageBox::Yes;
  }
  auto progress = QProgressDialog{
    tr("Importing and converting assets"),
    tr("Cancel"),
    0,
    int(entries.size() * 2),
    this};
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(0);
  auto imported = std::vector<std::string>{};
  auto copied = 0;
  for (auto& entry : entries)
  {
    progress.setValue(copied);
    if (progress.wasCanceled())
      break;
    auto ec = std::error_code{};
    if (!overwrite)
    {
      const auto requested = entry.relative;
      for (auto counter = 1; std::filesystem::exists(folder / entry.relative, ec);
           ++counter)
        entry.relative = requested.parent_path() / (requested.stem().string() + "_" + std::to_string(counter) + requested.extension().string());
    }
    const auto added =
      overwrite ? m_source->replaceFile(entry.source, entry.relative.generic_string())
                : m_source->importFile(entry.source, entry.relative.generic_string());
    if (added.is_error())
    {
      report << tr("%1: import failed: %2")
                  .arg(
                    pathAsQString(entry.source),
                    QString::fromStdString(std::get<Error>(added.error()).msg));
      retry << pathAsQString(entry.source);
    }
    else
    {
      imported.push_back(added.value());
      report << tr("%1: imported as %2")
                  .arg(
                    pathAsQString(entry.source), QString::fromStdString(added.value()));
    }
    ++copied;
  }
  if (!imported.empty())
  {
    const auto scanned = rescanLibrary();
    if (scanned.is_error())
      report << tr("Index failed: %1")
                  .arg(QString::fromStdString(std::get<Error>(scanned.error()).msg));
  }
  reload();
  // Copy IDs before converting: each publication can invalidate a browser's listing.
  auto convert = std::vector<std::pair<std::string, std::string>>{};
  for (const auto& asset : m_assets)
    if (
      std::ranges::find(imported, asset.sourceContainer) != imported.end()
      || std::ranges::find(imported, asset.virtualPath) != imported.end())
    {
      if (needsConversion(asset))
        convert.emplace_back(asset.id, asset.virtualPath);
      else
        report << tr("%1: ready to use").arg(QString::fromStdString(asset.virtualPath));
    }
  auto converted = 0;
  for (const auto& [id, path] : convert)
  {
    if (progress.wasCanceled())
      break;
    progress.setValue(int(entries.size()) + converted);
    if (progress.wasCanceled())
      break;
    const auto result = m_source->convertAsset(id);
    if (result.is_error() || !result.value())
    {
      report << tr("%1: imported, conversion failed: %2")
                  .arg(
                    QString::fromStdString(path),
                    result.is_error()
                      ? QString::fromStdString(std::get<Error>(result.error()).msg)
                      : tr("No compatible content was produced."));
      failedConversions.push_back(id);
    }
    else
      report << tr("%1: converted and ready to use").arg(QString::fromStdString(path));
    ++converted;
  }
  const auto cancelled = progress.wasCanceled();
  progress.close();
  if (cancelled)
    report << tr(
                "Cancelled: %1 file(s) not imported; %2 conversion(s) pending. Completed "
                "imports were kept.")
                .arg(int(entries.size()) - copied)
                .arg(int(convert.size()) - converted);
  reload();
  m_footer->setText(tr("Imported %1 file(s); converted %2; failed %3.")
                      .arg(int(imported.size()))
                      .arg(converted - int(failedConversions.size()))
                      .arg(retry.size() + qsizetype(failedConversions.size())));
  showOperationReport(report, retry, failedConversions);
}

void AssetLibraryWindow::refreshUsage()
{
  m_usage->clear();
  m_projectAssetId.reset();
  const auto* asset = selectedAsset();
  if (!asset || !m_source)
  {
    m_usage->addItem(tr("No asset selected"));
    return;
  }

  const auto libraryId = m_source->libraryId();
  if (libraryId.empty())
  {
    m_usage->addItem(tr("This library has no catalog identity"));
    return;
  }
  if (const auto project = m_project.lock())
  {
    auto projectRevision = std::uint64_t{0};
    m_projectScope->setText(tr("Project: %1\n%2\nExplicit MAP references, including "
                               "unsaved edits. Double-click a reference to open its map.")
                              .arg(
                                QString::fromStdString(project->store().info().name),
                                pathAsQString(project->path())));
    const auto captured = project->projectAssetForLibraryAsset(libraryId, asset->id);
    if (captured.is_error())
      m_usage->addItem(
        tr("Snapshot lookup failed: %1")
          .arg(QString::fromStdString(std::get<Error>(captured.error()).msg)));
    else if (!captured.value())
      m_usage->addItem(tr("Not captured in this project"));
    else
    {
      m_projectAssetId = captured.value()->id;
      projectRevision = captured.value()->revision;
      const auto audit = auditGoldSrcProjectResources(*project);
      if (audit.is_error())
        m_usage->addItem(
          tr("Usage check failed: %1")
            .arg(QString::fromStdString(std::get<Error>(audit.error()).msg)));
      else
      {
        auto matches = 0;
        auto failures = 0;
        for (const auto& use : audit.value().uses)
        {
          const auto failed = use.status == ProjectResourceStatus::ScanError;
          if (
            !failed
            && std::ranges::find(use.assetIds, *m_projectAssetId) == use.assetIds.end())
            continue;
          auto* row = new QListWidgetItem{
            failed ? tr("Scan failed: %1 — %2")
                       .arg(
                         QString::fromStdString(use.mapPath),
                         QString::fromStdString(use.detail))
                   : tr("%1 : %2 · %3 · %4 · %5")
                       .arg(QString::fromStdString(use.mapPath))
                       .arg(qulonglong(use.line))
                       .arg(projectResourceStatusLabel(use.status))
                       .arg(
                         QString::fromStdString(use.location),
                         QString::fromStdString(use.reference)),
            m_usage};
          row->setData(Qt::UserRole, QString::fromStdString(use.mapId));
          row->setToolTip(row->text());
          if (failed)
            ++failures;
          else
            ++matches;
        }
        if (matches == 0)
          m_usage->addItem(
            failures
              ? tr("No matching references found; failed scans leave usage incomplete.")
              : tr("Captured, with no explicit references in the scanned project maps."));
      }
    }
    // Refresh capture annotations only when the snapshot changed, preserving ongoing
    // playback and the preview view while map edits merely refresh the usage list.
    if (m_inspector->asset()
        && (m_inspector->asset()->resource.projectAssetId != m_projectAssetId.value_or("")
            || m_inspector->asset()->resource.projectRevision != projectRevision))
      updatePreview();
    else
      updateProjectActions();
    return;
  }

  m_projectScope->setText(
    tr("Saved captures in up to 12 recent projects. Live map references are checked only "
       "when opened from a project."));
  auto used = 0;
  auto inaccessible = 0;
  for (const auto& path : loadRecentDocuments(12))
  {
    if (path.extension() != ".tbproj")
      continue;
    const auto store = project::ProjectStore::open(path);
    if (store.is_error())
    {
      auto* row =
        new QListWidgetItem{tr("Unavailable: %1").arg(pathAsQString(path)), m_usage};
      row->setToolTip(QString::fromStdString(std::get<Error>(store.error()).msg));
      ++inaccessible;
      continue;
    }
    const auto found = store.value()->findAssetByLibraryAsset(libraryId, asset->id);
    if (found.is_error())
    {
      m_usage->addItem(tr("Cannot check: %1 — %2")
                         .arg(
                           pathAsQString(path),
                           QString::fromStdString(std::get<Error>(found.error()).msg)));
      ++inaccessible;
      continue;
    }
    if (!found.value())
      continue;

    m_usage->addItem(tr("Captured: %1  ·  %2")
                       .arg(
                         QString::fromStdString(path.stem().string()),
                         QString::fromStdString(found.value().value().virtualPath)));
    ++used;
  }
  if (used == 0)
    m_usage->addItem(
      inaccessible ? tr("No captures found in readable recent projects; some projects "
                        "could not be checked.")
                   : tr("No saved captures found in the checked recent projects."));
  updateProjectActions();
}

void AssetLibraryWindow::setProjectContext(const std::shared_ptr<ProjectSession>& project)
{
  QObject::disconnect(m_projectConnection);
  QObject::disconnect(m_mapConnection);
  m_project = project;
  if (project)
  {
    m_projectConnection =
      connect(project.get(), &ProjectSession::stateChanged, this, [this]() {
        m_usageTimer->start();
      });
    m_mapConnection =
      connect(project.get(), &ProjectSession::mapContentsChanged, this, [this]() {
        m_usageTimer->start();
      });
  }
  refreshUsage();
  updatePreview();
}

std::string AssetLibraryWindow::libraryId() const
{
  return m_source ? m_source->libraryId() : std::string{};
}

Result<void> AssetLibraryWindow::revealAsset(
  const std::string& libraryId, const std::string& assetId)
{
  if (!m_source || m_source->libraryId() != libraryId)
    return Error{"The requested resource belongs to another asset library."};
  if (std::ranges::find(m_assets, assetId, &project::LibraryAsset::id) == m_assets.end())
  {
    reload();
    if (
      std::ranges::find(m_assets, assetId, &project::LibraryAsset::id) == m_assets.end())
      return Error{"The requested asset no longer exists in this library."};
  }
  focusAsset(assetId, QStringLiteral("all"));
  return {};
}

void AssetLibraryWindow::focusAsset(const std::string& assetId, const QString& category)
{
  m_filter = category;
  if (const auto found = std::ranges::find(m_assets, assetId, &project::LibraryAsset::id);
      found != m_assets.end())
    m_folderPath = libraryFolder(*found);
  const auto searchBlocker = QSignalBlocker{m_searchEdit};
  const auto tagBlocker = QSignalBlocker{m_tagCombo};
  const auto soundBlocker = QSignalBlocker{m_soundCombo};
  m_searchEdit->clear();
  m_search.clear();
  m_tagCombo->setCurrentIndex(0);
  m_soundCombo->setCurrentIndex(0);
  m_waveKind = inspection::WaveKind::All;
  m_selectedId = assetId;
  refreshCategories();
  refreshFolders();
  refreshGrid();
  if (const auto* current = m_grid->currentItem())
    m_grid->scrollToItem(current);
}

void AssetLibraryWindow::updateProjectActions()
{
  const auto project = m_project.lock();
  const auto* asset = selectedAsset();
  auto canCapture = false;
  auto canUpdate = false;
  if (project && asset && m_source && !asset->missing)
  {
    const auto libraries = project->libraries();
    canCapture =
      !m_projectAssetId && libraries.is_success()
      && std::ranges::any_of(libraries.value(), [&](const auto& connection) {
           return connection.info.id == m_source->libraryId() && connection.available;
         });
    if (m_projectAssetId)
    {
      const auto updates = project->checkLibraryUpdates();
      if (updates.is_success())
        for (const auto& update : updates.value())
          if (update.projectAssetId == *m_projectAssetId)
            canUpdate =
              update.state == project::LibraryUpdateState::NewVersion
              || update.state == project::LibraryUpdateState::DependenciesChanged;
    }
  }
  m_inspector->setActions(
    false,
    canCapture,
    canUpdate,
    bool(project && m_projectAssetId),
    project ? tr("Target project: %1\n%2")
                .arg(
                  QString::fromStdString(project->store().info().name),
                  pathAsQString(project->path()))
            : QString{});
  m_inspector->setNavigation(false, bool(project && m_projectAssetId));
}

void AssetLibraryWindow::captureSelected()
{
  const auto project = m_project.lock();
  const auto* asset = selectedAsset();
  if (!project || !asset || !m_source)
    return;
  const auto captured = project->captureLibraryAssetResolving(
    m_source->libraryId(), asset->id, [this](const CaptureConflict& conflict) {
      return confirmCaptureConflict(this, conflict) ? CaptureConflictChoice::Replace
                                                    : CaptureConflictChoice::Cancel;
    });
  if (captured.is_error())
    QMessageBox::warning(
      this,
      tr("Add to project"),
      QString::fromStdString(std::get<Error>(captured.error()).msg));
  refreshUsage();
  updatePreview();
}

void AssetLibraryWindow::updateSelected()
{
  const auto project = m_project.lock();
  if (!project || !m_projectAssetId)
    return;
  const auto updated = project->updateLibraryAssets({*m_projectAssetId});
  if (updated.is_error())
    QMessageBox::warning(
      this,
      tr("Update project resource"),
      QString::fromStdString(std::get<Error>(updated.error()).msg));
  refreshUsage();
  updatePreview();
}

void AssetLibraryWindow::applyTags()
{
  const auto* asset = selectedAsset();
  if (!asset || !m_source || !m_source->canManage())
    return;

  auto tags = std::vector<std::string>{};
  for (const auto& part : m_tagsEdit->text().split(',', Qt::SkipEmptyParts))
  {
    const auto tag = part.trimmed().toStdString();
    if (!tag.empty())
      tags.push_back(tag);
  }
  if (const auto result = m_source->setTags(asset->id, tags); result.is_error())
    QMessageBox::critical(
      this, tr("Tags"), QString::fromStdString(std::get<Error>(result.error()).msg));
}

void AssetLibraryWindow::toggleFavorite()
{
  const auto* asset = selectedAsset();
  if (!asset || !m_source || !m_source->canManage())
    return;
  const auto favorite = m_favoriteButton->isChecked();
  if (const auto result = m_source->setFavorite(asset->id, favorite); result.is_error())
  {
    QMessageBox::critical(
      this, tr("Favourite"), QString::fromStdString(std::get<Error>(result.error()).msg));
    m_favoriteButton->setChecked(!favorite);
    return;
  }
  m_favoriteButton->setText(
    favorite ? QStringLiteral("★ Favourite") : QStringLiteral("☆ Favourite"));
  if (favorite)
    m_favorites.insert(asset->id);
  else
    m_favorites.erase(asset->id);
  refreshCategories();
  refreshGrid();
}

void AssetLibraryWindow::renameSelected()
{
  const auto* asset = selectedAsset();
  if (!asset || !m_source || !m_source->canManage())
    return;

  auto accepted = false;
  const auto name = QInputDialog::getText(
    this,
    tr("Rename Asset"),
    tr("Catalog name"),
    QLineEdit::Normal,
    QString::fromStdString(asset->displayName),
    &accepted);
  if (!accepted || name.trimmed().isEmpty())
    return;

  if (const auto result = m_source->renameAsset(asset->id, name.trimmed().toStdString());
      result.is_error())
  {
    QMessageBox::critical(
      this,
      tr("Rename Asset"),
      QString::fromStdString(std::get<Error>(result.error()).msg));
    return;
  }
  reload();
}

void AssetLibraryWindow::deleteSelected()
{
  if (!m_source || !m_source->canManage())
    return;

  const auto selected = m_grid->selectedItems();
  if (selected.empty())
    return;
  auto assetIds = std::vector<std::string>{};
  assetIds.reserve(static_cast<size_t>(selected.size()));
  for (const auto* item : selected)
    assetIds.push_back(
      item->data(AssetLibraryItemDelegate::AssetIdRole).toString().toStdString());
  const auto libraryId = m_source->libraryId();
  const auto message = selected.size() == 1
                         ? tr(
                             "Remove '%1' from the library?\n\nThe source file is kept "
                             "and a later rescan will not add the asset back. Projects "
                             "keep their captured copies. Import the source again to "
                             "restore it.")
                             .arg(selected.front()->text())
                         : tr(
                             "Remove %1 selected assets from the library?\n\nThe source "
                             "files are kept and a later rescan will not add the assets "
                             "back. Projects keep their captured copies. Import the "
                             "sources again to restore them.")
                             .arg(selected.size());
  const auto answer = QMessageBox::question(
    this, tr("Delete Asset"), message, QMessageBox::Yes | QMessageBox::Cancel);
  if (answer != QMessageBox::Yes || !m_source || m_source->libraryId() != libraryId)
    return;

  if (const auto result = m_source->removeAssets(assetIds); result.is_error())
  {
    QMessageBox::critical(
      this,
      tr("Delete Asset"),
      QString::fromStdString(std::get<Error>(result.error()).msg));
    return;
  }
  m_reloadTimer->stop();
  m_selectedId.reset();
  reload();
}

void AssetLibraryWindow::revealSelected()
{
  if (!m_source || !m_source->canReveal())
    return;
  QDesktopServices::openUrl(QUrl::fromLocalFile(m_source->location()));
}

void AssetLibraryWindow::dragEnterEvent(QDragEnterEvent* event)
{
  if (!m_source || !m_source->canImport() || !event->mimeData()->hasUrls())
    return;
  const auto droppable = std::any_of(
    event->mimeData()->urls().begin(),
    event->mimeData()->urls().end(),
    [](const QUrl& url) {
      return url.isLocalFile()
             && !librarySubfolderForFile(pathFromQString(url.toLocalFile())).empty();
    });
  if (droppable)
    event->acceptProposedAction();
}

void AssetLibraryWindow::dropEvent(QDropEvent* event)
{
  if (!m_source || !m_source->canImport())
    return;
  auto files = QStringList{};
  for (const auto& url : event->mimeData()->urls())
  {
    if (!url.isLocalFile())
      continue;
    const auto path = url.toLocalFile();
    if (!librarySubfolderForFile(pathFromQString(path)).empty())
      files.push_back(path);
  }
  if (files.isEmpty())
    return;
  event->acceptProposedAction();
  importFiles(files);
}
} // namespace tb::ui
