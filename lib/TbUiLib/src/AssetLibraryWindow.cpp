/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom, licensed under GPL version 3 or later.
*/
#include "ui/AssetLibraryWindow.h"

#include <QAbstractItemView>
#include <QColor>
#include <QComboBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
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
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "base/PreferenceManager.h"
#include "img/DecodeImage.h"
#include "prefs/Preferences.h"
#include "project/AssetLibrary.h"
#include "project/GoldSrcWad.h"
#include "ui/AssetLibraryImport.h"
#include "ui/AssetLibraryItemDelegate.h"
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

// Kept short: the card subtitle has roughly 110 pixels next to the type.
QString statusText(const project::LibraryAsset& asset)
{
  if (asset.missing)
    return QObject::tr("Missing");
  if (needsConversion(asset))
    return QObject::tr("Pending");
  return QObject::tr("Ready");
}

QString glyphFor(const project::AssetType type)
{
  switch (type)
  {
  case project::AssetType::Model:
    return QStringLiteral("MDL");
  case project::AssetType::Sprite:
    return QStringLiteral("SPR");
  case project::AssetType::Sound:
    return QStringLiteral("WAV");
  case project::AssetType::Skybox:
    return QStringLiteral("SKY");
  default:
    return QStringLiteral("TEX");
  }
}
} // namespace

AssetLibraryWindow::AssetLibraryWindow(QWidget* parent)
  : QDialog{parent}
{
  setWindowTitle(tr("Asset Library"));
  setObjectName(QStringLiteral("AssetLibraryWindow"));
  resize(1180, 760);

  auto* layout = new QVBoxLayout{this};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  auto* top = new QWidget{this};
  top->setObjectName(QStringLiteral("AssetLibraryTopBar"));
  top->setAttribute(Qt::WA_StyledBackground, true);
  auto* topLayout = new QHBoxLayout{top};
  topLayout->setContentsMargins(12, 8, 12, 8);
  topLayout->setSpacing(8);

  m_libraryLabel = new QLabel{top};
  m_libraryLabel->setObjectName(QStringLiteral("AssetLibraryLibraryLabel"));
  auto* change = new QPushButton{tr("Change Library…"), top};
  auto* import = new QPushButton{tr("Import Files…"), top};
  auto* rescan = new QPushButton{tr("Rescan"), top};
  auto* convert = new QPushButton{tr("Convert Pending"), top};
  m_searchEdit = new QLineEdit{top};
  m_searchEdit->setPlaceholderText(tr("Search assets…"));
  m_searchEdit->setClearButtonEnabled(true);
  m_searchEdit->setMaximumWidth(240);

  topLayout->addWidget(m_libraryLabel);
  topLayout->addWidget(change);
  topLayout->addWidget(import);
  topLayout->addWidget(rescan);
  topLayout->addWidget(convert);
  topLayout->addStretch(1);
  topLayout->addWidget(m_searchEdit);
  layout->addWidget(top);

  auto* splitter = new QSplitter{Qt::Horizontal, this};
  splitter->setChildrenCollapsible(false);

  auto* sidebar = new QWidget{splitter};
  sidebar->setObjectName(QStringLiteral("AssetLibrarySidebar"));
  sidebar->setAttribute(Qt::WA_StyledBackground, true);
  auto* sideLayout = new QVBoxLayout{sidebar};
  sideLayout->setContentsMargins(8, 10, 8, 10);
  auto* sideTitle = new QLabel{tr("LIBRARY"), sidebar};
  sideTitle->setObjectName(QStringLiteral("AssetLibrarySideTitle"));
  m_categories = new QListWidget{sidebar};
  m_categories->setObjectName(QStringLiteral("AssetLibraryCategories"));
  m_categories->setFrameShape(QFrame::NoFrame);
  sideLayout->addWidget(sideTitle);
  sideLayout->addWidget(m_categories, 1);
  splitter->addWidget(sidebar);

  auto* center = new QWidget{splitter};
  auto* centerLayout = new QVBoxLayout{center};
  centerLayout->setContentsMargins(10, 8, 10, 8);
  centerLayout->setSpacing(6);
  auto* tools = new QWidget{center};
  auto* toolsLayout = new QHBoxLayout{tools};
  toolsLayout->setContentsMargins(0, 0, 0, 0);
  auto* sizeLabel = new QLabel{tr("Size"), tools};
  m_sizeSlider = new QSlider{Qt::Horizontal, tools};
  m_sizeSlider->setRange(72, 192);
  m_sizeSlider->setValue(112);
  m_sizeSlider->setMaximumWidth(160);
  m_gridButton = new QToolButton{tools};
  m_gridButton->setText(QStringLiteral("▦"));
  m_gridButton->setCheckable(true);
  m_gridButton->setChecked(true);
  m_listButton = new QToolButton{tools};
  m_listButton->setText(QStringLiteral("☷"));
  m_listButton->setCheckable(true);
  toolsLayout->addWidget(sizeLabel);
  toolsLayout->addWidget(m_sizeSlider);
  toolsLayout->addStretch(1);
  toolsLayout->addWidget(m_gridButton);
  toolsLayout->addWidget(m_listButton);

  m_grid = new QListWidget{center};
  m_grid->setObjectName(QStringLiteral("AssetLibraryGrid"));
  m_grid->setViewMode(QListView::IconMode);
  m_grid->setGridSize(QSize{m_sizeSlider->value() + 26, m_sizeSlider->value() + 66});
  m_grid->setResizeMode(QListView::Adjust);
  m_grid->setMovement(QListView::Static);
  m_grid->setUniformItemSizes(true);
  m_grid->setWordWrap(true);
  m_grid->setSpacing(4);
  m_grid->setFrameShape(QFrame::NoFrame);
  m_grid->setSelectionMode(QAbstractItemView::SingleSelection);
  m_grid->setSpacing(0);
  m_grid->setMouseTracking(true);
  m_delegate = new AssetLibraryItemDelegate{m_grid};
  m_grid->setItemDelegate(m_delegate);

  m_empty = new QLabel{tr("No assets in this library yet. Use Import Files… to add some."),
                       center};
  m_empty->setAlignment(Qt::AlignCenter);
  m_empty->setObjectName(QStringLiteral("AssetLibraryEmpty"));
  m_empty->hide();

  centerLayout->addWidget(tools);
  centerLayout->addWidget(m_grid, 1);
  centerLayout->addWidget(m_empty, 1);
  splitter->addWidget(center);

  auto* inspector = new QWidget{splitter};
  inspector->setObjectName(QStringLiteral("AssetLibraryInspector"));
  inspector->setAttribute(Qt::WA_StyledBackground, true);
  auto* inspectorLayout = new QVBoxLayout{inspector};
  inspectorLayout->setContentsMargins(10, 10, 10, 10);
  inspectorLayout->setSpacing(8);
  auto* inspectorTitle = new QLabel{tr("INSPECTOR"), inspector};
  inspectorTitle->setObjectName(QStringLiteral("AssetLibrarySideTitle"));
  m_preview = new QLabel{inspector};
  m_preview->setObjectName(QStringLiteral("AssetLibraryPreview"));
  m_preview->setAlignment(Qt::AlignCenter);
  m_preview->setMinimumHeight(180);
  m_preview->setScaledContents(false);
  m_previewMode = new QComboBox{inspector};
  m_previewMode->addItem(tr("Source"));
  m_previewMode->addItem(tr("Converted"));
  m_name = new QLabel{inspector};
  m_name->setObjectName(QStringLiteral("AssetLibraryName"));
  m_name->setWordWrap(true);
  m_status = new QLabel{inspector};
  m_status->setObjectName(QStringLiteral("AssetLibraryStatus"));
  auto* propertiesHost = new QWidget{inspector};
  m_properties = new QFormLayout{propertiesHost};
  m_properties->setContentsMargins(0, 0, 0, 0);
  m_properties->setLabelAlignment(Qt::AlignLeft);
  m_properties->setHorizontalSpacing(10);
  m_properties->setVerticalSpacing(4);
  auto* dependenciesTitle = new QLabel{tr("DEPENDENCIES"), inspector};
  dependenciesTitle->setObjectName(QStringLiteral("AssetLibrarySideTitle"));
  m_dependencies = new QListWidget{inspector};
  m_dependencies->setObjectName(QStringLiteral("AssetLibraryDependencies"));
  m_dependencies->setFrameShape(QFrame::NoFrame);
  m_dependencies->setMaximumHeight(140);

  inspectorLayout->addWidget(inspectorTitle);
  inspectorLayout->addWidget(m_preview);
  inspectorLayout->addWidget(m_previewMode);
  inspectorLayout->addWidget(m_name);
  inspectorLayout->addWidget(m_status);
  inspectorLayout->addWidget(propertiesHost);
  inspectorLayout->addWidget(dependenciesTitle);
  inspectorLayout->addWidget(m_dependencies);
  inspectorLayout->addStretch(1);
  splitter->addWidget(inspector);

  splitter->setStretchFactor(0, 0);
  splitter->setStretchFactor(1, 1);
  splitter->setStretchFactor(2, 0);
  splitter->setSizes({190, 640, 300});
  layout->addWidget(splitter, 1);

  m_footer = new QLabel{this};
  m_footer->setObjectName(QStringLiteral("AssetLibraryFooter"));
  m_footer->setContentsMargins(10, 4, 10, 4);
  layout->addWidget(m_footer);

  connect(change, &QPushButton::clicked, this, &AssetLibraryWindow::chooseLibrary);
  connect(import, &QPushButton::clicked, this, &AssetLibraryWindow::runImport);
  connect(rescan, &QPushButton::clicked, this, &AssetLibraryWindow::runRescan);
  connect(convert, &QPushButton::clicked, this, &AssetLibraryWindow::runConvertPending);
  connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
    m_search = text;
    refreshGrid();
  });
  connect(m_categories, &QListWidget::currentRowChanged, this, [this](int) {
    if (const auto* item = m_categories->currentItem())
      m_filter = item->data(Qt::UserRole).toString();
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
  connect(m_sizeSlider, &QSlider::valueChanged, this, [this](int value) {
    m_grid->setGridSize(QSize{value + 26, value + 66});
  });
  connect(m_gridButton, &QToolButton::clicked, this, [this]() {
    m_delegate->setListMode(false);
    m_grid->setViewMode(QListView::IconMode);
    m_grid->setGridSize(QSize{m_sizeSlider->value() + 26, m_sizeSlider->value() + 66});
    m_gridButton->setChecked(true);
    m_listButton->setChecked(false);
  });
  connect(m_listButton, &QToolButton::clicked, this, [this]() {
    m_delegate->setListMode(true);
    m_grid->setViewMode(QListView::ListMode);
    m_grid->setGridSize(QSize{});
    m_grid->setSpacing(0);
    m_gridButton->setChecked(false);
    m_listButton->setChecked(true);
  });
  connect(m_previewMode, &QComboBox::currentIndexChanged, this, [this](int) {
    updatePreview();
  });

  const auto folder =
    PreferenceManager::instance().get(Preferences::AssetLibraryDirectory);
  if (!folder.empty() && project::AssetLibrary::isLibraryFolder(folder))
    openLibrary(pathAsQString(folder));
  else
    reload();
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

void AssetLibraryWindow::openLibrary(const QString& folder)
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
  PreferenceManager::instance().set(Preferences::AssetLibraryDirectory, path);
  reload();
}

void AssetLibraryWindow::reload()
{
  m_assets.clear();
  m_selectedId.reset();
  if (!m_source)
  {
    m_libraryLabel->setText(tr("No asset library"));
    m_empty->show();
    m_grid->hide();
    clearInspector();
    updateStatus();
    refreshCategories();
    return;
  }
  m_libraryLabel->setText(
    QStringLiteral("%1 — %2").arg(m_source->name(), m_source->location()));
  m_libraryLabel->setToolTip(m_source->location());

  const auto assets = m_source->assets();
  if (assets.is_error())
  {
    m_empty->setText(QString::fromStdString(std::get<Error>(assets.error()).msg));
    m_empty->show();
    m_grid->hide();
    updateStatus();
    return;
  }
  m_assets = assets.value();
  refreshCategories();
  refreshGrid();
  updateStatus();
}

void AssetLibraryWindow::refreshCategories()
{
  const auto current = m_filter;
  const auto blockers = QSignalBlocker{m_categories};
  m_categories->clear();
  const auto pending = pendingCount();
  const auto missing = missingCount();

  const auto add = [this](const QString& key, const QString& label, const int count) {
    auto* item = new QListWidgetItem{m_categories};
    item->setText(count >= 0 ? QStringLiteral("%1  %2").arg(label).arg(count) : label);
    item->setData(Qt::UserRole, key);
  };
  add(QStringLiteral("all"), tr("All assets"), int(m_assets.size()));
  add(QStringLiteral("texture"), tr("Textures"),
      int(std::count_if(m_assets.begin(), m_assets.end(), [](const auto& asset) {
        return asset.type == project::AssetType::Texture;
      })));
  add(QStringLiteral("model"), tr("Models"),
      int(std::count_if(m_assets.begin(), m_assets.end(), [](const auto& asset) {
        return asset.type == project::AssetType::Model;
      })));
  add(QStringLiteral("sprite"), tr("Sprites"),
      int(std::count_if(m_assets.begin(), m_assets.end(), [](const auto& asset) {
        return asset.type == project::AssetType::Sprite;
      })));
  add(QStringLiteral("sound"), tr("Sounds"),
      int(std::count_if(m_assets.begin(), m_assets.end(), [](const auto& asset) {
        return asset.type == project::AssetType::Sound;
      })));
  add(QStringLiteral("skybox"), tr("Skyboxes"),
      int(std::count_if(m_assets.begin(), m_assets.end(), [](const auto& asset) {
        return asset.type == project::AssetType::Skybox;
      })));
  add(QStringLiteral("pending"), tr("Pending conversion"), pending);
  add(QStringLiteral("missing"), tr("Missing sources"), missing);

  for (auto row = 0; row < m_categories->count(); ++row)
  {
    if (m_categories->item(row)->data(Qt::UserRole).toString() == current)
    {
      m_categories->setCurrentRow(row);
      return;
    }
  }
  m_filter = QStringLiteral("all");
  m_categories->setCurrentRow(0);
}

void AssetLibraryWindow::refreshGrid()
{
  m_grid->clear();
  if (!m_source)
  {
    m_empty->show();
    m_grid->hide();
    return;
  }

  const auto search = m_search.trimmed();
  auto shown = 0;
  for (const auto& asset : m_assets)
  {
    const auto matchesType =
      m_filter == QStringLiteral("all")
      || (m_filter == QStringLiteral("texture") && asset.type == project::AssetType::Texture)
      || (m_filter == QStringLiteral("model") && asset.type == project::AssetType::Model)
      || (m_filter == QStringLiteral("sprite") && asset.type == project::AssetType::Sprite)
      || (m_filter == QStringLiteral("sound") && asset.type == project::AssetType::Sound)
      || (m_filter == QStringLiteral("skybox") && asset.type == project::AssetType::Skybox)
      || (m_filter == QStringLiteral("pending") && needsConversion(asset))
      || (m_filter == QStringLiteral("missing") && asset.missing);
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
    item->setData(
      AssetLibraryItemDelegate::SubtitleRole,
      QStringLiteral("%1 · %2").arg(typeText(asset.type), statusText(asset)));
    item->setData(
      AssetLibraryItemDelegate::StatusRole,
      asset.missing ? int(AssetLibraryItemDelegate::Missing)
      : needsConversion(asset)
        ? int(AssetLibraryItemDelegate::Pending)
        : int(AssetLibraryItemDelegate::Ready));
    item->setData(AssetLibraryItemDelegate::GlyphRole, glyphFor(asset.type));
    item->setToolTip(
      QStringLiteral("%1\n%2").arg(QString::fromStdString(asset.virtualPath), typeText(asset.type)));

    auto image = QImage{};
    if (isConverted(asset) && asset.type == project::AssetType::Texture)
    {
      if (const auto content = m_source->readContent(asset.id); content.is_success())
        image = decodeMiptexWad(content.value());
    }
    if (image.isNull())
    {
      if (const auto original = m_source->readOriginal(asset.id); original.is_success())
        image = decodeEncodedImage(original.value());
    }
    if (!image.isNull())
    {
      item->setData(
        AssetLibraryItemDelegate::ThumbnailRole,
        QPixmap::fromImage(
          image.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    }
    ++shown;
  }

  m_empty->setText(
    m_assets.empty()
      ? tr("This library is empty. Use Import Files… to add assets.")
      : tr("No assets match the current filter."));
  m_empty->setVisible(shown == 0);
  m_grid->setVisible(shown > 0);

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
  m_name->setText(QString::fromStdString(asset->displayName));
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

  m_previewMode->setEnabled(
    asset->type == project::AssetType::Texture && isConverted(*asset));
  updatePreview();
}

void AssetLibraryWindow::clearInspector()
{
  m_name->setText(tr("No asset selected"));
  m_status->clear();
  m_preview->setPixmap({});
  m_preview->setText(tr("Select an asset to preview it"));
  m_dependencies->clear();
  while (m_properties->rowCount() > 0)
    m_properties->removeRow(0);
}

void AssetLibraryWindow::updatePreview()
{
  const auto* asset = selectedAsset();
  if (!asset || !m_source)
  {
    clearInspector();
    return;
  }

  auto image = QImage{};
  if (m_previewMode->currentIndex() == 1 && isConverted(*asset)
      && asset->type == project::AssetType::Texture)
  {
    if (const auto content = m_source->readContent(asset->id); content.is_success())
      image = decodeMiptexWad(content.value());
  }
  else if (const auto original = m_source->readOriginal(asset->id); original.is_success())
  {
    image = decodeEncodedImage(original.value());
  }

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
  const auto total = m_assets.size();
  const auto pending = pendingCount();
  const auto missing = missingCount();
  auto text = tr("%1 assets · %2 pending conversion · %3 missing").arg(total).arg(pending).arg(missing);
  if (m_source)
    text += tr(" · %1").arg(m_source->isRemote() ? tr("Remote library") : tr("Local library"));
  m_footer->setText(text);
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
}

void AssetLibraryWindow::runConvertPending()
{
  if (!m_source || !m_source->canConvert())
    return;
  auto converted = 0;
  auto failed = 0;
  for (const auto& asset : m_assets)
  {
    if (!needsConversion(asset))
      continue;
    const auto result = m_source->convertAsset(asset.id);
    if (result.is_error() || !result.value())
      ++failed;
    else
      ++converted;
  }
  reload();
  m_footer->setText(
    tr("Converted %1 asset(s)%2.").arg(converted).arg(failed > 0 ? tr(", %1 failed").arg(failed)
                                                                : QString{}));
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

void AssetLibraryWindow::revealSelected()
{
  if (!m_source || !m_source->canReveal())
    return;
  QDesktopServices::openUrl(QUrl::fromLocalFile(m_source->location()));
}
} // namespace tb::ui
