#include "ui/SmartSkyboxEditor.h"

#include <QAbstractButton>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>

#include "fs/File.h"
#include "fs/PathInfo.h"
#include "fs/TraversalMode.h"
#include "gl/Texture.h"
#include "mdl/EntityNodeBase.h"
#include "mdl/GameFileSystem.h"
#include "mdl/LoadImageTexture.h"
#include "mdl/Map.h"
#include "ui/BitmapButton.h"
#include "ui/MapDocument.h"
#include "ui/PanelLayout.h"
#include "ui/QPathUtils.h"

#include "kd/string_compare.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <optional>
#include <unordered_map>

namespace tb::ui
{
namespace
{
constexpr auto SkySuffixes = std::array{"rt", "bk", "lf", "ft", "up", "dn"};
constexpr auto SkyExtensions =
  std::array{".tga", ".bmp", ".png", ".jpg", ".jpeg", ".dds"};
constexpr auto SkyboxItemSize = QSize{172, 52};
constexpr auto SkyboxPreviewSize = QSize{70, 36};

class SkyboxItemDelegate : public QStyledItemDelegate
{
public:
  explicit SkyboxItemDelegate(QObject* parent)
    : QStyledItemDelegate{parent}
  {
  }

  QSize sizeHint(const QStyleOptionViewItem& /* option */, const QModelIndex& /* index */)
    const override
  {
    return SkyboxItemSize;
  }

  void paint(
    QPainter* painter,
    const QStyleOptionViewItem& option,
    const QModelIndex& index) const override
  {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const auto tileRect = option.rect.adjusted(2, 2, -2, -2);
    const auto selected = option.state.testFlag(QStyle::State_Selected);
    const auto hovered = option.state.testFlag(QStyle::State_MouseOver);

    if (selected || hovered)
    {
      auto background = option.palette.color(QPalette::Highlight);
      if (!selected)
      {
        background.setAlpha(36);
      }
      painter->setPen(Qt::NoPen);
      painter->setBrush(background);
      painter->drawRoundedRect(tileRect, 3, 3);
    }

    const auto previewRect = QRect{
      tileRect.left() + 5,
      tileRect.top() + (tileRect.height() - SkyboxPreviewSize.height()) / 2,
      SkyboxPreviewSize.width(),
      SkyboxPreviewSize.height()};

    const auto icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
    if (!icon.isNull())
    {
      icon.paint(painter, previewRect, Qt::AlignCenter);
    }
    else
    {
      auto previewBackground = option.palette.color(QPalette::Base);
      previewBackground = previewBackground.darker(115);
      auto previewBorder = option.palette.color(QPalette::Mid);
      previewBorder.setAlpha(100);
      painter->setPen(previewBorder);
      painter->setBrush(previewBackground);
      painter->drawRoundedRect(previewRect, 2, 2);

      painter->setFont(option.font);
      painter->setPen(option.palette.color(QPalette::PlaceholderText));
      painter->drawText(previewRect, Qt::AlignCenter, tr("No preview"));
    }

    const auto textRect = QRect{
      previewRect.right() + 7,
      tileRect.top() + 2,
      tileRect.right() - previewRect.right() - 11,
      tileRect.height() - 4};
    const auto name = index.data(Qt::DisplayRole).toString();
    const auto elidedName =
      option.fontMetrics.elidedText(name, Qt::ElideRight, textRect.width());
    painter->setFont(option.font);
    painter->setPen(
      option.palette.color(selected ? QPalette::HighlightedText : QPalette::Text));
    painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, elidedName);

    painter->restore();
  }
};

bool hasSkyboxExtension(const std::filesystem::path& path)
{
  const auto extension = path.extension().generic_string();
  return std::ranges::any_of(SkyExtensions, [&](const auto* candidate) {
    return kdl::ci::str_is_equal(extension, candidate);
  });
}

bool endsWithIgnoreCase(const std::string& str, const char* suffix)
{
  const auto suffixLength = std::strlen(suffix);
  return str.size() >= suffixLength
         && kdl::ci::str_is_equal(str.substr(str.size() - suffixLength), suffix);
}

std::unordered_map<std::string, std::vector<SmartSkyboxItem>>& skyboxCache()
{
  static auto cache = std::unordered_map<std::string, std::vector<SmartSkyboxItem>>{};
  return cache;
}

std::unordered_map<std::string, QIcon>& skyboxIconCache()
{
  static auto cache = std::unordered_map<std::string, QIcon>{};
  return cache;
}

} // namespace

std::optional<std::pair<std::string, std::string>> skyboxBaseAndSuffix(
  const std::filesystem::path& path)
{
  if (!hasSkyboxExtension(path))
  {
    return std::nullopt;
  }

  const auto stem = path.stem().generic_string();
  for (const auto* suffix : SkySuffixes)
  {
    if (stem.size() > std::strlen(suffix) && endsWithIgnoreCase(stem, suffix))
    {
      return std::pair{stem.substr(0, stem.size() - std::strlen(suffix)), suffix};
    }
  }

  return std::nullopt;
}

std::vector<SmartSkyboxItem> findSmartSkyboxes(mdl::GameFileSystem& gameFileSystem)
{
  auto result = std::vector<SmartSkyboxItem>{};
  const auto envPath = std::filesystem::path{"gfx"} / "env";
  if (gameFileSystem.pathInfo(envPath) != fs::PathInfo::Directory)
  {
    return result;
  }

  const auto pathsResult = gameFileSystem.find(envPath, fs::TraversalMode::Flat);
  if (pathsResult.is_error())
  {
    return result;
  }

  struct SkyboxCandidate
  {
    std::map<std::string, std::filesystem::path, kdl::ci::string_less> faces;
  };

  auto candidates = std::map<std::string, SkyboxCandidate, kdl::ci::string_less>{};
  for (const auto& path : pathsResult.value())
  {
    if (gameFileSystem.pathInfo(path) != fs::PathInfo::File)
    {
      continue;
    }

    const auto baseAndSuffix = skyboxBaseAndSuffix(path);
    if (!baseAndSuffix)
    {
      continue;
    }

    const auto& [baseName, suffix] = *baseAndSuffix;
    candidates[baseName].faces.emplace(suffix, path);
  }

  for (const auto& [baseName, candidate] : candidates)
  {
    const auto complete = std::ranges::all_of(
      SkySuffixes, [&](const auto* suffix) { return candidate.faces.contains(suffix); });
    if (!complete)
    {
      continue;
    }

    result.push_back(SmartSkyboxItem{baseName, candidate.faces.at("rt")});
  }

  std::ranges::sort(result, {}, &SmartSkyboxItem::name);
  return result;
}

SmartSkyboxEditor::SmartSkyboxEditor(MapDocument& document, QWidget* parent)
  : SmartPropertyEditor{document, parent}
{
  createGui();
}

void SmartSkyboxEditor::createGui()
{
  auto* layout = new QVBoxLayout{this};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(4);

  auto* header = new QWidget{this};
  header->setObjectName("smartSkyboxHeader");
  auto* headerLayout = new QHBoxLayout{header};
  headerLayout->setContentsMargins(2, 0, 2, 0);
  headerLayout->setSpacing(4);

  auto* title = new QLabel{tr("Skyboxes"), header};
  title->setObjectName("smartSkyboxTitle");

  m_refreshButton = createBitmapButton("Refresh.svg", tr("Refresh skyboxes"), header);
  m_refreshButton->setObjectName("smartSkyboxRefreshButton");
  m_refreshButton->setProperty("tbControlRole", "compact");
  m_refreshButton->setFixedSize(QSize{24, 24});

  headerLayout->addWidget(title);
  headerLayout->addStretch(1);
  headerLayout->addWidget(m_refreshButton);

  m_listWidget = new PanelItemList{2, 3, this};
  m_listWidget->setObjectName("smartSkyboxList");
  m_listWidget->setViewMode(QListView::IconMode);
  m_listWidget->setMovement(QListView::Static);
  m_listWidget->setResizeMode(QListView::Adjust);
  m_listWidget->setFlow(QListView::LeftToRight);
  m_listWidget->setWrapping(true);
  m_listWidget->setUniformItemSizes(true);
  m_listWidget->setWordWrap(false);
  m_listWidget->setTextElideMode(Qt::ElideRight);
  m_listWidget->setIconSize(SkyboxPreviewSize);
  m_listWidget->setGridSize(SkyboxItemSize);
  m_listWidget->setSpacing(2);
  m_listWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  m_listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_listWidget->setItemDelegate(new SkyboxItemDelegate{m_listWidget});

  layout->addWidget(header);
  layout->addWidget(m_listWidget, 1);

  connect(
    m_refreshButton, &QAbstractButton::clicked, this, [this]() { reloadSkyboxes(true); });
  connect(m_listWidget, &QListWidget::itemClicked, this, &SmartSkyboxEditor::applySkybox);
}

void SmartSkyboxEditor::doUpdateVisual(const std::vector<mdl::EntityNodeBase*>& nodes)
{
  if (m_skyboxes.empty())
  {
    reloadSkyboxes();
  }
  selectCurrentSkybox(nodes);
}

void SmartSkyboxEditor::reloadSkyboxes(const bool force)
{
  const auto currentRow = m_listWidget->currentRow();
  const auto cacheKey = document().map().gamePath().generic_string();
  auto& cache = skyboxCache();
  if (force || !cache.contains(cacheKey))
  {
    cache[cacheKey] = findSmartSkyboxes(document().map().gameFileSystem());
    if (force)
    {
      skyboxIconCache().clear();
    }
  }
  m_skyboxes = cache[cacheKey];

  m_listWidget->clear();
  for (const auto& skybox : m_skyboxes)
  {
    auto* item =
      new QListWidgetItem{iconForSkybox(skybox), QString::fromStdString(skybox.name)};
    item->setToolTip(
      QString::fromStdString(skybox.name) + "\n" + pathAsQPath(skybox.previewPath));
    m_listWidget->addItem(item);
  }

  if (currentRow >= 0 && currentRow < m_listWidget->count())
  {
    m_listWidget->setCurrentRow(currentRow);
  }
}

void SmartSkyboxEditor::selectCurrentSkybox(
  const std::vector<mdl::EntityNodeBase*>& nodes)
{
  m_listWidget->clearSelection();
  m_listWidget->setCurrentRow(-1);
  if (nodes.size() != 1)
  {
    return;
  }

  const auto* value = nodes.front()->entity().property(propertyKey());
  if (!value)
  {
    return;
  }

  const auto it = std::ranges::find_if(m_skyboxes, [&](const auto& skybox) {
    return kdl::ci::str_is_equal(skybox.name, *value);
  });
  if (it == m_skyboxes.end())
  {
    return;
  }

  const auto row = int(std::distance(m_skyboxes.begin(), it));
  m_listWidget->setCurrentRow(row, QItemSelectionModel::ClearAndSelect);
  m_listWidget->scrollToItem(m_listWidget->item(row), QAbstractItemView::EnsureVisible);
}

void SmartSkyboxEditor::applySkybox(QListWidgetItem* item)
{
  const auto row = m_listWidget->row(item);
  if (row < 0 || row >= static_cast<int>(m_skyboxes.size()))
  {
    return;
  }

  addOrUpdateProperty(m_skyboxes[size_t(row)].name);
  emit skyboxApplied();
}

QIcon SmartSkyboxEditor::iconForSkybox(const SmartSkyboxItem& skybox)
{
  const auto cacheKey = document().map().gamePath().generic_string() + "\n"
                        + skybox.previewPath.generic_string();
  auto& cache = skyboxIconCache();
  if (cache.contains(cacheKey))
  {
    return cache.at(cacheKey);
  }

  auto fileResult = document().map().gameFileSystem().openFile(skybox.previewPath);
  if (fileResult.is_error())
  {
    return {};
  }

  auto reader = fileResult.value()->reader().buffer();
  auto textureResult = mdl::loadImageTexture(reader);
  if (textureResult.is_error())
  {
    return {};
  }

  auto texture = std::move(textureResult).value();
  const auto& buffers = texture.buffersIfLoaded();
  if (
    buffers.empty() || texture.width() == 0 || texture.height() == 0
    || (texture.format() != GL_RGBA && texture.format() != GL_BGRA))
  {
    return {};
  }

  const auto& pixels = buffers.front();
  auto image = QImage{
    pixels.data(),
    int(texture.width()),
    int(texture.height()),
    qsizetype(texture.width() * 4u),
    QImage::Format_RGBA8888};
  image = texture.format() == GL_BGRA ? image.rgbSwapped() : image.copy();

  const auto scaledImage = image.scaled(
    SkyboxPreviewSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
  const auto previewImage = scaledImage.copy(
    (scaledImage.width() - SkyboxPreviewSize.width()) / 2,
    (scaledImage.height() - SkyboxPreviewSize.height()) / 2,
    SkyboxPreviewSize.width(),
    SkyboxPreviewSize.height());

  auto icon = QIcon{QPixmap::fromImage(previewImage)};
  cache.emplace(cacheKey, icon);
  return icon;
}

} // namespace tb::ui
