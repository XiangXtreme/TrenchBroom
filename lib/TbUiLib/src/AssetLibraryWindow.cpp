/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom, licensed under GPL version 3 or later.
*/
#include "ui/AssetLibraryWindow.h"

#include <QAbstractItemView>
#include <QColor>
#include <QComboBox>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QInputDialog>
#include <QProgressBar>
#include <QTimer>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSize>
#include <QSlider>
#include <QSplitter>
#include <QStyle>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "base/PreferenceManager.h"
#include "img/DecodeImage.h"
#include "prefs/Preferences.h"
#include "project/AssetLibrary.h"
#include "project/GoldSrcWad.h"
#include "ui/AssetLibraryGlyphs.h"
#include "ui/AssetLibraryImport.h"
#include "ui/AssetLibraryItemDelegate.h"
#include "ui/GoldSrcSpritePreview.h"
#include "ui/ModelThumbnail.h"
#include "ui/RecentDocuments.h"

#include "project/ProjectStore.h"
#include "ui/AssetLibrarySource.h"
#include "ui/QPathUtils.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace tb::ui
{
namespace
{
constexpr auto DefaultThumbSide = 112;
constexpr auto ThumbMinSide = 72;
constexpr auto ThumbMaxSide = 192;

std::uint32_t readU32(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
  if (offset + 4 > bytes.size())
    return 0;
  return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8)
         | (std::uint32_t(bytes[offset + 2]) << 16)
         | (std::uint32_t(bytes[offset + 3]) << 24);
}

std::uint16_t readU16(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
  if (offset + 2 > bytes.size())
    return 0;
  return std::uint16_t(bytes[offset]) | (std::uint16_t(bytes[offset + 1]) << 8);
}

QImage decodeSpriteImage(const std::vector<std::uint8_t>& bytes)
{
  const auto preview = loadGoldSrcSpritePreview(bytes);
  if (!preview || preview->width == 0 || preview->height == 0 || preview->rgba.empty())
    return {};
  return QImage{
    preview->rgba.data(),
    int(preview->width),
    int(preview->height),
    int(preview->width * 4),
    QImage::Format_RGBA8888}
    .copy();
}

QImage decodeEncodedImage(const std::vector<std::uint8_t>& bytes)
{
  if (bytes.empty())
    return {};
  const auto decoded = img::decodeImage(bytes.data(), bytes.size());
  if (decoded.is_error())
    return {};
  const auto& image = decoded.value();
  if (image.width == 0 || image.height == 0)
    return {};
  return QImage{
    image.pixels.data(),
    int(image.width),
    int(image.height),
    int(image.width * 4),
    QImage::Format_RGBA8888}
    .copy();
}

// Decodes the mip 0 image of a single texture WAD3 archive. The palette follows the
// smallest mip level and stores BGR triples.
QImage decodeMiptexWad(const std::vector<std::uint8_t>& bytes)
{
  const auto wad = project::readGoldSrcWad(bytes);
  if (wad.is_error() || wad.value().textures.empty())
    return {};
  const auto& miptex = wad.value().textures.front().miptex;
  if (miptex.size() < 40)
    return {};

  const auto width = readU32(miptex, 16);
  const auto height = readU32(miptex, 20);
  if (width == 0 || height == 0 || width > 4096 || height > 4096)
    return {};
  const auto pixels = std::size_t{width} * height;
  const auto mip0 = std::size_t{readU32(miptex, 24)};
  const auto mip3 = std::size_t{readU32(miptex, 36)};
  if (mip0 + pixels > miptex.size())
    return {};

  const auto paletteOffset = mip3 + pixels / 64;
  const auto colors = readU16(miptex, paletteOffset);
  const auto palette = paletteOffset + 2;
  if (colors == 0 || palette + std::size_t{colors} * 3 > miptex.size())
    return {};

  auto image = QImage{int(width), int(height), QImage::Format_RGB888};
  for (auto y = 0u; y < height; ++y)
  {
    for (auto x = 0u; x < width; ++x)
    {
      const auto index = std::uint8_t(miptex[mip0 + std::size_t{y} * width + x]);
      const auto slot = std::size_t(index < colors ? index : 0u) * 3 + palette;
      image.setPixelColor(
        int(x),
        int(y),
        QColor(int(miptex[slot + 2]), int(miptex[slot + 1]), int(miptex[slot])));
    }
  }
  return image;
}

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

bool isConverted(const project::LibraryAsset& asset)
{
  return asset.nativeFormat == "goldsrc-wad" || asset.nativeFormat == "goldsrc-miptex";
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
  TypeFilter{"model", project::AssetType::Model},
  TypeFilter{"sprite", project::AssetType::Sprite},
  TypeFilter{"sound", project::AssetType::Sound},
  TypeFilter{"skybox", project::AssetType::Skybox},
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

AssetLibraryWindow::AssetLibraryWindow(const QString& folder, const bool remember, QWidget* parent)
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
  resize(1180, 760);

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
  m_rescanButton =
    createActionButton(header, tr("Rescan"), tr("Re-read the library contents"));
  m_convertButton = createActionButton(
    header, tr("Convert Pending"), tr("Convert source textures to GoldSrc miptex"));
  m_revealButton =
    createActionButton(header, tr("Reveal"), tr("Show the library folder"));
  m_changeButton =
    createActionButton(header, tr("Change Library…"), tr("Open another asset library"));

  m_importButton->setIcon(
    assetLibraryGlyphIcon(AssetLibraryGlyph::Import, textColor, 15));
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
    assetLibraryGlyphIcon(AssetLibraryGlyph::Search, dimColor, 14), QLineEdit::LeadingPosition);

  headerLayout->addWidget(m_importButton);
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
  sideLayout->addWidget(m_categories, 1);
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
    tools, AssetLibraryGlyph::Grid, tr("Grid view"), textColor, QStringLiteral("compact"));
  m_listButton = createGlyphToolButton(
    tools, AssetLibraryGlyph::List, tr("List view"), textColor, QStringLiteral("compact"));
  m_gridButton->setCheckable(true);
  m_listButton->setCheckable(true);
  m_gridButton->setChecked(true);

  m_convertProgress = new QProgressBar{tools};
  m_convertProgress->setMaximumWidth(180);
  m_convertProgress->setTextVisible(false);
  m_convertProgress->hide();
  m_convertCancel = new QPushButton{tr("Cancel"), tools};
  m_convertCancel->hide();
  m_convertTimer = new QTimer{this};
  m_convertTimer->setInterval(0);
  connect(
    m_convertTimer, &QTimer::timeout, this, &AssetLibraryWindow::stepConvertQueue);
  connect(
    m_convertCancel, &QPushButton::clicked, this, &AssetLibraryWindow::cancelConvertQueue);

  toolsLayout->addWidget(m_countLabel);
  toolsLayout->addWidget(m_convertProgress);
  toolsLayout->addWidget(m_convertCancel);
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
  m_grid->setSelectionMode(QAbstractItemView::SingleSelection);
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

  auto* inspector = new QWidget{splitter};
  inspector->setObjectName(QStringLiteral("AssetLibraryInspector"));
  inspector->setAttribute(Qt::WA_StyledBackground, true);
  auto* inspectorLayout = new QVBoxLayout{inspector};
  inspectorLayout->setContentsMargins(12, 10, 12, 10);
  inspectorLayout->setSpacing(7);

  auto* previewTitle = new QLabel{tr("Preview"), inspector};
  previewTitle->setObjectName(QStringLiteral("AssetLibrarySectionTitle"));
  m_preview = new QLabel{inspector};
  m_preview->setObjectName(QStringLiteral("AssetLibraryPreview"));
  m_preview->setAlignment(Qt::AlignCenter);
  m_preview->setMinimumHeight(180);
  m_preview->setScaledContents(false);
  m_previewMode = new QComboBox{inspector};
  m_previewMode->addItem(tr("Source"));
  m_previewMode->addItem(tr("Converted"));

  auto* actionRow = new QWidget{inspector};
  auto* actionLayout = new QHBoxLayout{actionRow};
  actionLayout->setContentsMargins(0, 0, 0, 0);
  actionLayout->setSpacing(6);
  m_favoriteButton = new QPushButton{QStringLiteral("☆ Favourite"), actionRow};
  m_favoriteButton->setCheckable(true);
  m_favoriteButton->setObjectName(QStringLiteral("AssetLibraryFavoriteButton"));
  m_renameButton = new QPushButton{tr("Rename…"), actionRow};
  m_deleteButton = new QPushButton{tr("Delete…"), actionRow};
  m_favoriteButton->setEnabled(false);
  m_renameButton->setEnabled(false);
  m_deleteButton->setEnabled(false);
  actionLayout->addWidget(m_favoriteButton, 1);
  actionLayout->addWidget(m_renameButton, 1);
  actionLayout->addWidget(m_deleteButton, 1);
  connect(m_favoriteButton, &QPushButton::clicked, this, &AssetLibraryWindow::toggleFavorite);
  connect(m_renameButton, &QPushButton::clicked, this, &AssetLibraryWindow::renameSelected);
  connect(m_deleteButton, &QPushButton::clicked, this, &AssetLibraryWindow::deleteSelected);

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
  inspectorLayout->addWidget(m_preview);
  inspectorLayout->addWidget(m_previewMode);
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
  m_usage->setSelectionMode(QAbstractItemView::NoSelection);
  m_usage->setMaximumHeight(110);

  inspectorLayout->addWidget(dependenciesTitle);
  inspectorLayout->addWidget(m_dependencies);
  inspectorLayout->addWidget(usageTitle);
  inspectorLayout->addWidget(m_usage);
  inspectorLayout->addStretch(1);
  splitter->addWidget(inspector);

  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  splitter->setStretchFactor(2, 0);
  splitter->setSizes({196, 660, 320});
  layout->addWidget(splitter, 1);

  m_footer = new QLabel{this};
  m_footer->setObjectName(QStringLiteral("AssetLibraryFooter"));
  m_footer->setContentsMargins(14, 5, 14, 5);
  layout->addWidget(m_footer);

  connect(m_changeButton, &QPushButton::clicked, this, &AssetLibraryWindow::chooseLibrary);
  connect(m_importButton, &QPushButton::clicked, this, &AssetLibraryWindow::runImport);
  connect(m_rescanButton, &QPushButton::clicked, this, &AssetLibraryWindow::runRescan);
  connect(
    m_convertButton, &QPushButton::clicked, this, &AssetLibraryWindow::runConvertPending);
  connect(m_revealButton, &QPushButton::clicked, this, &AssetLibraryWindow::revealSelected);
  connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
    m_search = text;
    refreshGrid();
  });
  connect(m_categories, &QListWidget::currentRowChanged, this, [this](int) {
    if (const auto* item = m_categories->currentItem())
    {
      if (item->data(AssetLibraryCategoryDelegate::SectionRole).toBool())
        return;
      m_filter = item->data(AssetLibraryCategoryDelegate::FilterRole).toString();
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
  connect(m_previewMode, &QComboBox::currentIndexChanged, this, [this](int) {
    updatePreview();
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
      path, path.filename().empty() ? std::string{"Asset Library"}
                                    : path.filename().string());
    if (created.is_error())
    {
      QMessageBox::critical(
        this, tr("Asset Library"), QString::fromStdString(std::get<Error>(created.error()).msg));
      return;
    }
    if (const auto scanned = created.value()->scan(); scanned.is_error())
    {
      QMessageBox::critical(
        this, tr("Asset Library"), QString::fromStdString(std::get<Error>(scanned.error()).msg));
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
  m_source = std::move(source);
  if (remember)
    PreferenceManager::instance().set(Preferences::AssetLibraryDirectory, path);
  reload();
}

void AssetLibraryWindow::reload()
{
  m_assets.clear();
  m_selectedId.reset();
  if (!m_source)
  {
    m_empty->setText(tr("No library is open.\nUse Change Library… to choose one."));
    m_empty->show();
    m_grid->hide();
    clearInspector();
    updateHeader();
    refreshCategories();
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
    updateStatus();
    return;
  }
  m_assets = assets.value();
  m_favorites.clear();
  if (m_source->canManage())
  {
    for (const auto& asset : m_assets)
    {
      if (const auto meta = m_source->metadata(asset.id);
          meta.is_success() && meta.value().favorite)
        m_favorites.insert(asset.id);
    }
  }
  updateHeader();
  refreshCategories();
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
  m_rescanButton->setEnabled(hasSource);
  m_convertButton->setEnabled(
    hasSource && m_source->canConvert() && pendingCount() > 0);
  m_revealButton->setEnabled(hasSource && m_source->canReveal());
  m_searchEdit->setEnabled(hasSource);
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
    return int(std::count_if(
      m_assets.begin(), m_assets.end(),
      [type](const auto& asset) { return asset.type == type; }));
  };
  add(QStringLiteral("texture"), tr("Textures"), countOf(project::AssetType::Texture));
  add(QStringLiteral("model"), tr("Models"), countOf(project::AssetType::Model));
  add(QStringLiteral("sprite"), tr("Sprites"), countOf(project::AssetType::Sprite));
  add(QStringLiteral("sound"), tr("Sounds"), countOf(project::AssetType::Sound));
  add(QStringLiteral("skybox"), tr("Skyboxes"), countOf(project::AssetType::Skybox));

  addSection(tr("Attention"));
  add(QStringLiteral("pending"), tr("Pending"), pendingCount());
  add(QStringLiteral("missing"), tr("Missing sources"), missingCount());

  for (auto row = 0; row < m_categories->count(); ++row)
  {
    auto* item = m_categories->item(row);
    if (!item->data(AssetLibraryCategoryDelegate::SectionRole).toBool()
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
    if (!item->data(AssetLibraryCategoryDelegate::SectionRole).toBool()
        && item->data(AssetLibraryCategoryDelegate::FilterRole).toString()
             == QStringLiteral("all"))
    {
      m_categories->setCurrentRow(row);
      return;
    }
  }
}

void AssetLibraryWindow::refreshGrid()
{
  m_grid->clear();
  m_empty->hide();

  if (!m_source)
  {
    m_grid->hide();
    m_countLabel->clear();
    return;
  }

  const auto search = m_search.trimmed();
  auto shown = 0;
  for (const auto& asset : m_assets)
  {
    const auto typeFilter = typeForFilter(m_filter);
    const auto matchesType =
      m_filter == QStringLiteral("all")
      || (typeFilter && asset.type == *typeFilter)
      || (m_filter == QStringLiteral("pending") && needsConversion(asset))
      || (m_filter == QStringLiteral("missing") && asset.missing)
      || (m_filter == QStringLiteral("favourites") && m_favorites.contains(asset.id));
    if (!matchesType)
      continue;
    const auto displayName = QString::fromStdString(asset.displayName);
    if (!search.isEmpty()
        && !displayName.contains(search, Qt::CaseInsensitive)
        && !QString::fromStdString(asset.virtualPath)
              .contains(search, Qt::CaseInsensitive))
      continue;

    auto* item = new QListWidgetItem{m_grid};
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

    const auto image = previewImage(asset, true, 256);
    if (!image.isNull())
    {
      item->setData(
        AssetLibraryItemDelegate::ThumbnailRole,
        QPixmap::fromImage(
          image.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    }
    ++shown;
  }

  m_countLabel->setText(
    shown == int(m_assets.size())
      ? tr("%n asset(s)", nullptr, shown)
      : tr("%1 of %2 assets").arg(shown).arg(int(m_assets.size())));

  if (shown == 0)
  {
    m_empty->setText(
      m_assets.empty()
        ? tr("This library is empty.\nUse Import Files… to add assets.")
        : tr("No assets match the current filter."));
    m_empty->show();
    m_grid->hide();
  }
  else
  {
    m_grid->show();
  }

  if (m_selectedId)
  {
    for (auto row = 0; row < m_grid->count(); ++row)
    {
      if (m_grid->item(row)->data(AssetLibraryItemDelegate::AssetIdRole).toString().toStdString()
          == *m_selectedId)
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
  m_deleteButton->setEnabled(manageable);
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
    asset->missing ? tr("Missing source")
    : needsConversion(*asset) ? tr("Pending conversion")
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
            m_assets.begin(), m_assets.end(), [&](const auto& candidate) {
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

  m_previewMode->setEnabled(
    asset->type == project::AssetType::Texture && isConverted(*asset));
  updatePreview();
}

void AssetLibraryWindow::clearInspector()
{
  m_name->setText(tr("No asset selected"));
  m_name->setToolTip({});
  m_status->clear();
  m_preview->setPixmap({});
  m_preview->setText(tr("Select an asset to preview it"));
  m_dependencies->clear();
  while (m_properties->rowCount() > 0)
    m_properties->removeRow(0);
}

QImage AssetLibraryWindow::previewImage(
  const project::LibraryAsset& asset, const bool converted, const int size) const
{
  if (!m_source)
    return {};

  if (converted && isConverted(asset) && asset.type == project::AssetType::Texture)
  {
    if (const auto content = m_source->readContent(asset.id); content.is_success())
    {
      if (auto image = decodeMiptexWad(content.value()); !image.isNull())
        return image;
    }
  }

  const auto original = m_source->readOriginal(asset.id);
  if (original.is_error())
    return {};

  if (auto image = decodeEncodedImage(original.value()); !image.isNull())
    return image;
  if (asset.type == project::AssetType::Sprite)
    return decodeSpriteImage(original.value());
  if (asset.type == project::AssetType::Model)
    return renderModelThumbnail(
      std::filesystem::path{pathFromQString(m_source->location())},
      std::filesystem::path{asset.sourceContainer},
      size);
  return {};
}

void AssetLibraryWindow::updatePreview()
{
  const auto* asset = selectedAsset();
  if (!asset || !m_source)
  {
    clearInspector();
    return;
  }

  const auto image = previewImage(*asset, m_previewMode->currentIndex() == 1, 512);

  if (image.isNull())
  {
    m_preview->setPixmap({});
    m_preview->setText(tr("No preview available"));
    return;
  }
  m_preview->setText({});
  m_preview->setPixmap(QPixmap::fromImage(image).scaled(
    m_preview->width() > 0 ? m_preview->width() - 8 : 220,
    m_preview->height() > 0 ? m_preview->height() - 8 : 170,
    Qt::KeepAspectRatio,
    Qt::SmoothTransformation));
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
    text += tr(" · %1")
              .arg(m_source->isRemote() ? tr("Remote library") : tr("Local library"));
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
  const auto it = std::find_if(
    m_assets.begin(), m_assets.end(),
    [&](const auto& asset) { return asset.id == *m_selectedId; });
  return it == m_assets.end() ? nullptr : &*it;
}

void AssetLibraryWindow::runRescan()
{
  if (!m_source)
    return;
  if (const auto scanned = m_source->rescan(); scanned.is_error())
  {
    QMessageBox::critical(
      this, tr("Rescan"), QString::fromStdString(std::get<Error>(scanned.error()).msg));
    return;
  }
  reload();
  m_footer->setText(tr("Rescanned %1 asset(s).").arg(int(m_assets.size())));
}

void AssetLibraryWindow::runConvertPending()
{
  if (!m_source || !m_source->canConvert())
    return;
  m_convertQueue.clear();
  for (const auto& asset : m_assets)
  {
    if (needsConversion(asset))
      m_convertQueue.push_back(asset.id);
  }
  if (m_convertQueue.empty())
    return;

  m_convertDone = 0;
  m_convertFailed = 0;
  m_convertProgress->setRange(0, int(m_convertQueue.size()));
  m_convertProgress->setValue(0);
  m_convertProgress->show();
  m_convertCancel->show();
  m_convertTimer->start();
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
    ++m_convertFailed;
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
        .arg(
          m_convertFailed > 0 ? tr(", %1 failed").arg(m_convertFailed) : QString{}));
  }
}

void AssetLibraryWindow::cancelConvertQueue()
{
  m_convertTimer->stop();
  m_convertQueue.clear();
  m_convertProgress->hide();
  m_convertCancel->hide();
  reload();
}

void AssetLibraryWindow::runImport()
{
  if (!m_source || !m_source->canImport())
    return;
  const auto files = QFileDialog::getOpenFileNames(
    this,
    tr("Import Assets"),
    {},
    tr("Supported assets (*.png *.jpg *.jpeg *.bmp *.tga *.wad *.mdl *.spr *.wav);;"
       "All files (*)"));
  if (files.isEmpty())
    return;
  importFiles(files);
}

void AssetLibraryWindow::importFiles(const QStringList& files)
{
  struct Entry
  {
    std::filesystem::path source;
    std::filesystem::path relative;
    bool conflict = false;
  };

  const auto folder = std::filesystem::path{pathFromQString(m_source->location())};
  auto entries = std::vector<Entry>{};
  for (const auto& file : files)
  {
    const auto source = pathFromQString(file);
    const auto subfolder = librarySubfolderForFile(source);
    if (subfolder.empty())
      continue;
    const auto relative = subfolder / source.filename();
    entries.push_back({source, relative, std::filesystem::exists(folder / relative)});
  }
  if (entries.empty())
  {
    QMessageBox::information(
      this, tr("Import Assets"),
      tr("None of the selected files can be stored in an asset library."));
    return;
  }

  auto overwrite = false;
  if (std::any_of(entries.begin(), entries.end(), [](const auto& entry) { return entry.conflict; }))
  {
    const auto answer = QMessageBox::question(
      this, tr("Import Assets"),
      tr("Some files already exist in the library. Overwrite them?\n\nChoosing No keeps both "
         "by adding a suffix."),
      QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    if (answer == QMessageBox::Cancel)
      return;
    overwrite = answer == QMessageBox::Yes;
  }

  auto imported = std::vector<std::string>{};
  for (auto& entry : entries)
  {
    if (entry.conflict)
    {
      if (overwrite)
        std::filesystem::remove(folder / entry.relative);
      else
      {
        const auto stem = entry.relative.stem().string();
        const auto extension = entry.relative.extension().string();
        for (auto counter = 1;; ++counter)
        {
          const auto candidate = entry.relative.parent_path()
                                 / (stem + "_" + std::to_string(counter) + extension);
          if (!std::filesystem::exists(folder / candidate))
          {
            entry.relative = candidate;
            break;
          }
        }
      }
    }
    const auto added = m_source->importFile(entry.source, entry.relative.generic_string());
    if (added.is_success())
      imported.push_back(added.value());
  }

  if (!imported.empty())
    (void)m_source->rescan();
  reload();

  for (const auto& asset : m_assets)
  {
    if (needsConversion(asset)
        && std::find(imported.begin(), imported.end(), asset.virtualPath) != imported.end())
      (void)m_source->convertAsset(asset.id);
  }
  reload();
  m_footer->setText(tr("Imported %1 file(s).").arg(int(imported.size())));
}

void AssetLibraryWindow::refreshUsage()
{
  m_usage->clear();
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

  // Recent projects are the only project list available without asking the user for a
  // search root; anything unreadable is skipped instead of interrupting the browser.
  auto used = 0;
  for (const auto& path : loadRecentDocuments(12))
  {
    if (path.extension() != ".tbproj")
      continue;
    const auto store = project::ProjectStore::open(path);
    if (store.is_error())
      continue;
    const auto found = store.value()->findAssetByLibraryAsset(libraryId, asset->id);
    if (found.is_error() || !found.value())
      continue;

    m_usage->addItem(
      QStringLiteral("%1  ·  %2")
        .arg(
          QString::fromStdString(path.stem().string()),
          QString::fromStdString(found.value().value().virtualPath)));
    ++used;
  }
  if (used == 0)
    m_usage->addItem(tr("Not used by known projects"));
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
      this, tr("Rename Asset"), QString::fromStdString(std::get<Error>(result.error()).msg));
    return;
  }
  reload();
}

void AssetLibraryWindow::deleteSelected()
{
  const auto* asset = selectedAsset();
  if (!asset || !m_source || !m_source->canManage())
    return;

  const auto answer = QMessageBox::question(
    this,
    tr("Delete Asset"),
    tr("Remove '%1' from the library catalog?\n\nThe source file is kept.")
      .arg(QString::fromStdString(asset->displayName)),
    QMessageBox::Yes | QMessageBox::Cancel);
  if (answer != QMessageBox::Yes)
    return;

  if (const auto result = m_source->removeAsset(asset->id); result.is_error())
  {
    QMessageBox::critical(
      this, tr("Delete Asset"), QString::fromStdString(std::get<Error>(result.error()).msg));
    return;
  }
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
    event->mimeData()->urls().begin(), event->mimeData()->urls().end(),
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
