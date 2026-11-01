/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/AssetInspector.h"

#include <QAudioOutput>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QLabel>
#include <QMediaPlayer>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "ui/CollapsibleTitledPanel.h"
#include "ui/GoldSrcSpritePreview.h"
#include "ui/ModelThumbnail.h"
#include "ui/QPathUtils.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <functional>

namespace tb::ui
{
/** Data canvas: checker colors follow the active palette; standard controls stay native.
 */
class AssetImageView : public QGraphicsView
{
  QGraphicsScene m_scene;
  QGraphicsPixmapItem* m_item = nullptr;
  bool m_fit = true;
  QPoint m_last;

public:
  std::function<void(QPoint)> orbit;
  explicit AssetImageView(QWidget* parent)
    : QGraphicsView{parent}
  {
    setObjectName("AssetInspector_Image");
    setScene(&m_scene);
    m_item = m_scene.addPixmap({});
    setMinimumSize(140, 140);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    // Fixed scrollbar policy keeps fitInView from causing resize recursion.
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  }
  void setImage(const QImage& image, const bool reset = true)
  {
    m_item->setPixmap(QPixmap::fromImage(image));
    m_scene.setSceneRect(m_item->boundingRect());
    if (reset || m_fit)
      fit();
  }
  void fit()
  {
    m_fit = true;
    if (!m_item->pixmap().isNull())
      fitInView(m_item, Qt::KeepAspectRatio);
  }
  void actualSize()
  {
    m_fit = false;
    resetTransform();
  }

protected:
  void mousePressEvent(QMouseEvent* event) override
  {
    m_last = event->position().toPoint();
    QGraphicsView::mousePressEvent(event);
  }
  void mouseMoveEvent(QMouseEvent* event) override
  {
    if (orbit && event->buttons().testFlag(Qt::LeftButton))
    {
      const auto current = event->position().toPoint();
      orbit(current - m_last);
      m_last = current;
      event->accept();
      return;
    }
    QGraphicsView::mouseMoveEvent(event);
  }
  void resizeEvent(QResizeEvent* event) override
  {
    QGraphicsView::resizeEvent(event);
    if (m_fit)
      fit();
  }
  void wheelEvent(QWheelEvent* event) override
  {
    const auto factor = std::pow(1.0015, event->angleDelta().y());
    const auto next = transform().m11() * factor;
    if (next >= 0.02 && next <= 64)
    {
      m_fit = false;
      scale(factor, factor);
    }
    event->accept();
  }
  void drawBackground(QPainter* painter, const QRectF& rect) override
  {
    painter->fillRect(rect, palette().brush(QPalette::Base));
    const auto left = int(std::floor(rect.left() / 16)) * 16;
    const auto top = int(std::floor(rect.top() / 16)) * 16;
    for (auto y = top; y < rect.bottom(); y += 16)
      for (auto x = left; x < rect.right(); x += 16)
        if (((x / 16) + (y / 16)) % 2 == 0)
          painter->fillRect(
            QRect{x, y, 16, 16}, palette().brush(QPalette::AlternateBase));
  }
};

AssetInspector::AssetInspector(QWidget* parent)
  : QWidget{parent}
{
  setObjectName("AssetInspector");
  auto* layout = new QVBoxLayout{this};
  layout->setContentsMargins(4, 4, 4, 4);
  m_name = new QLabel{};
  m_name->setWordWrap(true);
  m_name->setProperty("regionAnchor", true);
  m_status = new QLabel{};
  m_status->setObjectName("AssetInspector_Status");
  m_status->setWordWrap(true);
  m_mode = new QComboBox{};
  m_mode->setObjectName("AssetInspector_ContentKind");
  m_mode->addItems({tr("Compatible result"), tr("Original source")});
  m_mode->setProperty("tbControlRole", "compact");
  m_image = new AssetImageView{this};
  m_skyMode = new QComboBox{};
  m_skyMode->addItems({tr("Six-face cross"), tr("Look around (drag)")});
  m_skyMode->setObjectName("AssetInspector_SkyMode");
  connect(m_skyMode, &QComboBox::currentIndexChanged, this, [this]() { showSky(); });
  m_summary = new QLabel{};
  m_summary->setObjectName("AssetInspector_Summary");
  m_summary->setWordWrap(true);
  auto* imageActions = new QHBoxLayout{};
  auto* fit = new QPushButton{tr("Fit")};
  auto* actual = new QPushButton{tr("100%")};
  fit->setProperty("tbControlRole", "compact");
  actual->setProperty("tbControlRole", "compact");
  imageActions->addWidget(m_mode, 1);
  imageActions->addWidget(fit);
  imageActions->addWidget(actual);
  connect(fit, &QPushButton::clicked, this, [this]() { m_image->fit(); });
  connect(actual, &QPushButton::clicked, this, [this]() { m_image->actualSize(); });
  connect(m_mode, &QComboBox::currentIndexChanged, this, [this]() { refreshPreview(); });

  m_player = new QMediaPlayer{this};
  m_output = new QAudioOutput{this};
  m_output->setVolume(0.5f);
  m_player->setAudioOutput(m_output);
  m_audio = new QBuffer{this};
  m_audioControls = new QWidget{};
  auto* audioLayout = new QVBoxLayout{m_audioControls};
  audioLayout->setContentsMargins(0, 0, 0, 0);
  m_position = new QSlider{Qt::Horizontal};
  m_position->setObjectName("AssetInspector_Position");
  m_position->setAccessibleName(tr("Playback position"));
  auto* buttons = new QHBoxLayout{};
  m_play = new QPushButton{tr("Play")};
  auto* stop = new QPushButton{tr("Stop")};
  auto* volume = new QSlider{Qt::Horizontal};
  volume->setAccessibleName(tr("Preview volume"));
  volume->setRange(0, 100);
  volume->setValue(50);
  m_autoplay = new QCheckBox{tr("Auto-play")};
  m_autoplay->setObjectName("AssetInspector_Autoplay");
  m_autoplay->setAccessibleName(tr("Auto-play preview"));
  m_autoplay->setToolTip(
    tr("Start playing as soon as a sound is selected in this preview."));
  buttons->addWidget(m_play);
  buttons->addWidget(stop);
  buttons->addWidget(m_autoplay);
  buttons->addWidget(new QLabel{tr("Volume")});
  buttons->addWidget(volume, 1);
  audioLayout->addWidget(m_position);
  audioLayout->addLayout(buttons);
  connect(m_play, &QPushButton::clicked, this, [this]() {
    if (m_player->playbackState() == QMediaPlayer::PlayingState)
      m_player->pause();
    else
    {
      if (!m_player->sourceDevice())
        m_player->setSourceDevice(m_audio, QUrl{"preview.wav"});
      m_player->play();
    }
  });
  connect(stop, &QPushButton::clicked, m_player, &QMediaPlayer::stop);
  connect(volume, &QSlider::valueChanged, this, [this](const int value) {
    m_output->setVolume(float(value) / 100.0f);
  });
  connect(m_position, &QSlider::sliderMoved, this, [this](const int value) {
    m_player->setPosition(qint64(value));
  });
  connect(m_player, &QMediaPlayer::durationChanged, this, [this](const qint64 duration) {
    m_position->setRange(0, int(std::min(duration, qint64(INT_MAX))));
  });
  connect(m_player, &QMediaPlayer::positionChanged, this, [this](const qint64 position) {
    if (!m_position->isSliderDown())
      m_position->setValue(int(std::min(position, qint64(INT_MAX))));
  });
  connect(m_player, &QMediaPlayer::seekableChanged, m_position, &QSlider::setEnabled);
  connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this]() {
    // One audible preview across library, project and enlarged inspectors.
    static auto audible = QPointer<QMediaPlayer>{};
    if (m_player->playbackState() == QMediaPlayer::PlayingState)
    {
      if (audible && audible != m_player)
        audible->stop();
      audible = m_player;
    }
    m_play->setText(
      m_player->playbackState() == QMediaPlayer::PlayingState ? tr("Pause") : tr("Play"));
  });
  connect(m_player, &QMediaPlayer::errorOccurred, this, [this]() {
    m_status->setText(tr("Cannot preview: %1").arg(m_player->errorString()));
  });
  m_spriteControls = new QWidget{};
  auto* spriteLayout = new QVBoxLayout{m_spriteControls};
  spriteLayout->setContentsMargins(0, 0, 0, 0);
  m_spriteFrame = new QSlider{Qt::Horizontal};
  m_spriteFrame->setObjectName("AssetInspector_SpriteFrame");
  m_spriteFrame->setAccessibleName(tr("Sprite frame"));
  auto* spriteButtons = new QHBoxLayout{};
  m_spritePlay = new QPushButton{tr("Play animation")};
  m_spriteRate = new QDoubleSpinBox{};
  m_spriteRate->setRange(0.1, 120);
  m_spriteRate->setValue(10);
  m_spriteRate->setSuffix(tr(" fps (single frames)"));
  m_spriteRate->setToolTip(tr("Grouped frames retain their file timing."));
  spriteButtons->addWidget(m_spritePlay);
  spriteButtons->addWidget(m_spriteRate);
  spriteLayout->addWidget(m_spriteFrame);
  spriteLayout->addLayout(spriteButtons);
  m_spriteTimer = new QTimer{this};
  m_spriteTimer->setSingleShot(true);
  connect(m_spriteFrame, &QSlider::valueChanged, this, [this]() { showSpriteFrame(); });
  connect(m_spritePlay, &QPushButton::clicked, this, [this]() {
    if (m_spriteTimer->isActive())
    {
      m_spriteTimer->stop();
      m_spritePlay->setText(tr("Play animation"));
    }
    else if (m_sprite && m_sprite->frames.size() > 1)
    {
      m_spritePlay->setText(tr("Pause animation"));
      m_spriteTimer->start(1);
    }
  });
  connect(m_spriteTimer, &QTimer::timeout, this, [this]() {
    if (!m_sprite || !isVisible())
      return;
    m_spriteFrame->setValue((m_spriteFrame->value() + 1) % int(m_sprite->frames.size()));
    const auto seconds = m_sprite->durations[size_t(m_spriteFrame->value())];
    const auto delay =
      seconds > 0 ? double(seconds) * 1000.0 : 1000.0 / m_spriteRate->value();
    m_spriteTimer->start(int(std::clamp(delay, 8.0, 60000.0)));
  });
  m_modelControls = new QWidget{};
  auto* modelLayout = new QVBoxLayout{m_modelControls};
  modelLayout->setContentsMargins(0, 0, 0, 0);
  m_modelFrame = new QComboBox{};
  m_modelFrame->setAccessibleName(tr("Pose"));
  m_modelFrame->setObjectName("AssetInspector_ModelPose");
  m_modelBody = new QComboBox{};
  m_modelBody->setAccessibleName(tr("Body variant"));
  m_modelBody->setObjectName("AssetInspector_ModelBody");
  m_modelBody->hide();
  auto* modelPoseRow = new QHBoxLayout{};
  modelPoseRow->addWidget(m_modelFrame, 1);
  modelPoseRow->addWidget(m_modelBody, 1);
  auto* modelButtons = new QHBoxLayout{};
  m_modelBounds = new QCheckBox{tr("Bounds")};
  m_modelOrigin = new QCheckBox{tr("Origin")};
  auto* modelReset = new QPushButton{tr("Reset view")};
  modelButtons->addWidget(m_modelBounds);
  modelButtons->addWidget(m_modelOrigin);
  modelButtons->addWidget(modelReset);
  modelLayout->addLayout(modelPoseRow);
  modelLayout->addLayout(modelButtons);
  m_modelLimits = new QLabel{};
  m_modelLimits->setObjectName("AssetInspector_ModelLimits");
  m_modelLimits->setWordWrap(true);
  modelLayout->addWidget(m_modelLimits);
  connect(m_modelFrame, &QComboBox::currentIndexChanged, this, [this]() { showModel(); });
  connect(m_modelBody, &QComboBox::currentIndexChanged, this, [this]() {
    const auto index = size_t(m_modelBody->currentIndex());
    if (
      !m_studioModel || m_modelBody->currentIndex() < 0
      || index >= m_studioChoices.size())
      return;
    const auto [part, variant] = m_studioChoices[index];
    if (part < m_studioVariants.size())
      m_studioVariants[part] = variant;
    rebuildStudioPose();
  });
  connect(m_modelBounds, &QCheckBox::toggled, this, [this]() { showModel(); });
  connect(m_modelOrigin, &QCheckBox::toggled, this, [this]() { showModel(); });
  connect(modelReset, &QPushButton::clicked, this, [this]() {
    m_modelYaw = 0.55f;
    m_modelPitch = 0.38f;
    showModel();
    m_image->fit();
  });
  auto* actions = new QVBoxLayout{};
  const auto button = [&](const QString& text) {
    auto* result = new QPushButton{text};
    actions->addWidget(result);
    return result;
  };
  m_apply = button(tr("Apply to selection"));
  m_apply->setObjectName("AssetInspector_Apply");
  m_capture = button(tr("Add to project"));
  m_capture->setObjectName("AssetInspector_Capture");
  m_update = button(tr("Update project resource"));
  m_update->setObjectName("AssetInspector_Update");
  m_uses = button(tr("Find uses in project"));
  m_uses->setObjectName("AssetInspector_FindUses");
  m_showLibrary = button(tr("Show in asset library"));
  m_showLibrary->setObjectName("AssetInspector_ShowLibrary");
  m_showSnapshot = button(tr("Show project snapshot"));
  m_showSnapshot->setObjectName("AssetInspector_ShowSnapshot");
  connect(m_apply, &QPushButton::clicked, this, &AssetInspector::applyRequested);
  connect(m_capture, &QPushButton::clicked, this, &AssetInspector::captureRequested);
  connect(m_update, &QPushButton::clicked, this, &AssetInspector::updateRequested);
  connect(m_uses, &QPushButton::clicked, this, &AssetInspector::findUsesRequested);
  connect(
    m_showLibrary, &QPushButton::clicked, this, &AssetInspector::showLibraryRequested);
  connect(
    m_showSnapshot, &QPushButton::clicked, this, &AssetInspector::showSnapshotRequested);

  auto* details = new CollapsibleTitledPanel{tr("Technical details"), false};
  m_details = new QTreeWidget{};
  m_details->setObjectName("AssetInspector_Details");
  m_details->setHeaderLabels({tr("Property"), tr("Value")});
  m_details->setRootIsDecorated(false);
  m_details->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
  auto* detailsLayout = new QVBoxLayout{details->getPanel()};
  detailsLayout->setContentsMargins(0, 0, 0, 0);
  detailsLayout->addWidget(m_details);
  layout->addWidget(m_name);
  layout->addWidget(m_status);
  layout->addLayout(imageActions);
  layout->addWidget(m_skyMode);
  layout->addWidget(m_image, 1);
  layout->addWidget(m_audioControls);
  layout->addWidget(m_spriteControls);
  layout->addWidget(m_modelControls);
  layout->addWidget(m_summary);
  layout->addLayout(actions);
  layout->addWidget(details);
  clear();
}

AssetInspector::~AssetInspector()
{
  stopPreview();
}

void AssetInspector::stopPreview()
{
  m_player->stop();
  m_player->setSource({});
  m_audio->close();
  m_spriteTimer->stop();
  m_spritePlay->setText(tr("Play animation"));
  m_position->setValue(0);
}

void AssetInspector::clear()
{
  stopPreview();
  m_asset.reset();
  m_sprite.reset();
  m_model.reset();
  m_studioModel.reset();
  m_studioVariants.clear();
  m_studioChoices.clear();
  if (m_modelBody)
  {
    const auto blocker = QSignalBlocker{m_modelBody};
    m_modelBody->clear();
    m_modelBody->hide();
  }
  m_modelControls->hide();
  m_image->orbit = {};
  m_skyMode->hide();
  m_skyYaw = m_skyPitch = 0;
  m_image->setImage({});
  m_image->show();
  m_mode->setEnabled(false);
  m_name->setText(tr("Select an asset"));
  m_status->clear();
  m_summary->clear();
  m_details->clear();
  m_audioControls->hide();
  m_spriteControls->hide();
  setActions(false, false, false, false);
  setNavigation(false, false);
  emit assetChanged();
}

void AssetInspector::setAsset(Result<inspection::Asset> asset)
{
  clear();
  if (asset.is_error())
  {
    m_status->setText(QString::fromStdString(std::get<Error>(asset.error()).msg));
    return;
  }
  m_asset = std::move(asset).value();
  m_name->setText(m_asset->name);
  const auto status = browserAssetStatus(m_asset->resource);
  m_status->setText(m_asset->source + (status.isEmpty() ? QString{} : " · " + status));
  const auto add = [this](const QString& key, const QString& value) {
    auto* item = new QTreeWidgetItem{m_details, {key, value}};
    item->setToolTip(1, value);
  };
  add(tr("Identity"), m_asset->identity);
  add(tr("Path"), pathAsQString(m_asset->resource.path));
  add(tr("Conversion recipe"), m_asset->recipe);
  add(tr("Original bytes"), QString::number(qulonglong(m_asset->original.size())));
  add(tr("Compatible bytes"), QString::number(qulonglong(m_asset->compatible.size())));
  {
    const auto blocker = QSignalBlocker{m_mode};
    m_mode->setCurrentIndex(0);
  }
  m_mode->setEnabled(
    m_asset->resource.skybox
    || (m_asset->original != m_asset->compatible && m_asset->resource.type != BrowserCellType::Model));
  refreshPreview();
  emit assetChanged();
}

void AssetInspector::refreshPreview()
{
  if (!m_asset)
    return;
  stopPreview();
  const auto& bytes =
    m_mode->currentIndex() == 0 ? m_asset->compatible : m_asset->original;
  const auto type = m_asset->resource.type;
  m_audioControls->setVisible(type == BrowserCellType::Sound);
  m_image->setVisible(type != BrowserCellType::Sound && type != BrowserCellType::Prefab);
  m_spriteControls->setVisible(type == BrowserCellType::Sprite);
  m_modelControls->setVisible(type == BrowserCellType::Model);
  m_sprite.reset();
  m_studioModel.reset();
  m_studioVariants.clear();
  m_studioChoices.clear();
  {
    const auto blocker = QSignalBlocker{m_modelBody};
    m_modelBody->clear();
  }
  m_modelBody->hide();
  m_modelLimits->setText(
    tr("Geometry preview · drag to orbit, wheel to zoom.\nSkin, body variants and "
       "continuous animation depend on the resource format."));
  m_skyMode->setVisible(m_asset->resource.skybox);
  m_image->orbit = {};
  if (m_asset->resource.skybox)
  {
    showSky();
    return;
  }
  if (type == BrowserCellType::Prefab)
  {
    showPrefab();
    return;
  }
  if (type == BrowserCellType::Sound)
  {
    const auto info = inspection::readWaveInfo(bytes);
    if (info.is_success())
    {
      const auto& wav = info.value();
      m_summary->setText(
        tr("%1 s · %2 Hz · %3-bit · %4 channel(s)\n%5")
          .arg(wav.seconds, 0, 'f', 2)
          .arg(wav.sampleRate)
          .arg(wav.bits)
          .arg(wav.channels)
          .arg(
            wav.loop
              ? tr("Loop: samples %1–%2").arg(wav.loop->first).arg(wav.loop->second)
              : tr("Plays once")));
    }
    else
      m_summary->setText(QString::fromStdString(std::get<Error>(info.error()).msg));
    m_audio->setData(
      QByteArray{reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size())});
    m_audio->open(QIODevice::ReadOnly);
    if (m_autoplay->isChecked())
    {
      m_player->stop();
      m_player->setSourceDevice(m_audio, QUrl{"preview.wav"});
      m_player->play();
    }
    return;
  }
  auto image = QImage{};
  if (type == BrowserCellType::Sprite)
  {
    m_sprite = loadGoldSrcSpriteAnimation(bytes);
    if (m_sprite)
    {
      m_spriteFrame->setRange(0, int(m_sprite->frames.size()) - 1);
      m_spriteFrame->setValue(0);
      showSpriteFrame();
      return;
    }
  }
  else if (type == BrowserCellType::Model)
  {
    // GoldSrc studio models are read straight from the resource bytes: the entity model
    // loaders only understand Quake-era formats.
    if (auto studio = loadGoldSrcStudioPreviewScene(m_asset->compatible))
    {
      m_model = std::move(studio);
      m_studioModel = m_model->studioModel;
      m_studioVariants.assign(m_model->studioModel->bodyParts.size(), 0);
      configureStudioModel();
      const auto blocker = QSignalBlocker{m_modelFrame};
      m_modelFrame->clear();
      m_modelFrame->addItem(tr("Bind pose"));
      m_modelFrame->setCurrentIndex(0);
      m_modelYaw = 0.55f;
      m_modelPitch = 0.38f;
      if (!m_model->frames.empty())
      {
        showModel();
        return;
      }
      m_summary->setText(tr("The studio model has no readable bind pose."));
      return;
    }
    m_model = loadModelPreviewScene(
      m_asset->resource.absolutePath.parent_path(),
      m_asset->resource.absolutePath.filename());
    if (m_model)
    {
      const auto blocker = QSignalBlocker{m_modelFrame};
      m_modelFrame->clear();
      for (const auto& frame : m_model->frames)
        m_modelFrame->addItem(QString::fromStdString(frame.name));
      m_modelFrame->setCurrentIndex(0);
      m_modelYaw = 0.55f;
      m_modelPitch = 0.38f;
      showModel();
      return;
    }
  }
  else
  {
    const auto decoded = inspection::decodeImage(bytes);
    if (decoded.is_success())
      image = decoded.value();
    else
      m_summary->setText(QString::fromStdString(std::get<Error>(decoded.error()).msg));
  }
  m_image->setImage(image);
  if (!image.isNull())
    m_summary->setText(tr("%1 × %2 · %3")
                         .arg(image.width())
                         .arg(image.height())
                         .arg(image.hasAlphaChannel() ? tr("RGBA") : tr("RGB")));
  else if (type == BrowserCellType::Model || type == BrowserCellType::Sprite)
    m_summary->setText(tr("The resource could not be decoded."));
}

void AssetInspector::showModel()
{
  if (!m_model || m_modelFrame->currentIndex() < 0)
    return;
  m_image->orbit = [this](const QPoint delta) {
    m_modelYaw += float(delta.x()) * 0.01f;
    m_modelPitch = std::clamp(m_modelPitch + float(delta.y()) * 0.01f, -1.5f, 1.5f);
    showModel();
  };
  const auto& frame = m_model->frames[size_t(m_modelFrame->currentIndex())];
  m_image->setImage(
    renderModelPreview(
      frame,
      512,
      m_modelYaw,
      m_modelPitch,
      m_modelBounds->isChecked(),
      m_modelOrigin->isChecked()),
    false);
  m_summary->setText(
    m_studioModel ? tr("%1 vertices · %2 triangles · bind pose · %3 bone(s)")
                      .arg(frame.positions.size())
                      .arg(frame.triangles.size())
                      .arg(m_studioModel->bones)
                  : tr("%1 vertices · %2 triangles · %3 pose(s)")
                      .arg(frame.positions.size())
                      .arg(frame.triangles.size())
                      .arg(m_model->frames.size()));
}

void AssetInspector::showPrefab()
{
  if (!m_asset)
    return;
  if (!m_asset->prefab)
  {
    m_summary->setText(
      m_asset->prefabIssues.isEmpty() ? tr("The prefab could not be read.")
                                      : m_asset->prefabIssues.join(QStringLiteral("\n")));
    return;
  }

  const auto& prefab = *m_asset->prefab;
  const auto size = prefab.bounds.is_empty() ? vm::vec3d{} : prefab.bounds.size();
  auto summary = tr("%1 brushes · %2 entities · %3 faces\n%4 × %5 × %6 units")
                   .arg(qulonglong(prefab.brushes))
                   .arg(qulonglong(prefab.entities))
                   .arg(qulonglong(prefab.faces))
                   .arg(size.x(), 0, 'f', 1)
                   .arg(size.y(), 0, 'f', 1)
                   .arg(size.z(), 0, 'f', 1);
  if (!m_asset->prefabIssues.isEmpty())
    summary += "\n" + m_asset->prefabIssues.join(QStringLiteral("\n"));
  m_summary->setText(summary);

  const auto keys = std::array{
    tr("Brushes"),
    tr("Entities"),
    tr("Faces"),
    tr("Size"),
    tr("Materials"),
    tr("Models and sprites"),
    tr("Sounds"),
    tr("Sky"),
    tr("Reference check")};
  for (const auto& key : keys)
    for (auto* item : m_details->findItems(key, Qt::MatchExactly, 0))
      delete item;

  const auto add = [this](const QString& key, const QString& value) {
    auto* item = new QTreeWidgetItem{m_details, {key, value}};
    item->setToolTip(1, value);
  };
  const auto joined = [](const std::vector<std::string>& values) {
    auto list = QStringList{};
    for (const auto& value : values)
      list << QString::fromStdString(value);
    return list.isEmpty() ? QObject::tr("None") : list.join(QStringLiteral(", "));
  };
  add(tr("Brushes"), QString::number(qulonglong(prefab.brushes)));
  add(tr("Entities"), QString::number(qulonglong(prefab.entities)));
  add(tr("Faces"), QString::number(qulonglong(prefab.faces)));
  add(
    tr("Size"),
    prefab.bounds.is_empty() ? tr("No brush volume")
                             : tr("%1 × %2 × %3")
                                 .arg(size.x(), 0, 'f', 1)
                                 .arg(size.y(), 0, 'f', 1)
                                 .arg(size.z(), 0, 'f', 1));
  add(tr("Materials"), joined(prefab.materials));
  add(tr("Models and sprites"), joined(prefab.models));
  add(tr("Sounds"), joined(prefab.sounds));
  add(tr("Sky"), joined(prefab.skies));
  add(
    tr("Reference check"),
    m_asset->prefabIssues.isEmpty()
      ? tr("Referenced resources resolve in the current project")
      : m_asset->prefabIssues.join(QStringLiteral("\n")));
}

void AssetInspector::configureStudioModel()
{
  if (!m_studioModel || !m_model)
    return;
  const auto& studio = *m_studioModel;
  m_studioChoices.clear();
  {
    const auto blocker = QSignalBlocker{m_modelBody};
    m_modelBody->clear();
    for (auto part = std::size_t{0}; part < studio.bodyParts.size(); ++part)
    {
      const auto& bodyPart = studio.bodyParts[part];
      if (bodyPart.variants.size() < 2)
        continue;
      const auto partName = bodyPart.name.empty() ? tr("Body %1").arg(part + 1)
                                                  : QString::fromStdString(bodyPart.name);
      for (auto variant = std::size_t{0}; variant < bodyPart.variants.size(); ++variant)
      {
        m_modelBody->addItem(tr("%1 · %2").arg(
          partName, QString::fromStdString(bodyPart.variants[variant])));
        m_studioChoices.emplace_back(part, variant);
      }
    }
  }
  m_modelBody->setVisible(!m_studioChoices.empty());
  m_modelLimits->setText(
    tr("GoldSrc studio model · bind pose from bone rest transforms.\nDrag to orbit, "
       "wheel to zoom. Animation, skins and bone controllers are not decoded."));

  const auto keys = std::array{
    tr("Bones"),
    tr("Sequences"),
    tr("Body variants"),
    tr("Skin families"),
    tr("Embedded textures"),
    tr("Source bounds")};
  for (const auto& key : keys)
    for (auto* item : m_details->findItems(key, Qt::MatchExactly, 0))
      delete item;

  const auto add = [this](const QString& key, const QString& value) {
    auto* item = new QTreeWidgetItem{m_details, {key, value}};
    item->setToolTip(1, value);
  };
  auto variants = std::size_t{0};
  for (const auto& bodyPart : studio.bodyParts)
    variants += bodyPart.variants.size();
  add(tr("Bones"), QString::number(qulonglong(studio.bones)));
  add(
    tr("Sequences"),
    studio.sequenceGroups > 1 ? tr("%1 in %2 external groups")
                                  .arg(qulonglong(studio.sequences))
                                  .arg(qulonglong(studio.sequenceGroups))
                              : QString::number(qulonglong(studio.sequences)));
  add(
    tr("Body variants"),
    tr("%1 in %2 part(s)")
      .arg(qulonglong(variants))
      .arg(qulonglong(studio.bodyParts.size())));
  add(tr("Skin families"), QString::number(qulonglong(studio.skinFamilies)));
  auto textures = QStringList{};
  for (const auto& texture : studio.textures)
    textures << QString::fromStdString(texture);
  add(
    tr("Embedded textures"),
    textures.isEmpty() ? tr("None") : textures.join(QStringLiteral(", ")));
  const auto size = studio.bounds.size();
  add(
    tr("Source bounds"),
    tr("%1 × %2 × %3")
      .arg(size.x(), 0, 'f', 1)
      .arg(size.y(), 0, 'f', 1)
      .arg(size.z(), 0, 'f', 1));
}

void AssetInspector::rebuildStudioPose()
{
  if (!m_asset || !m_studioModel || !m_model)
    return;
  const auto mesh = mdl::buildGoldSrcStudioMesh(m_asset->compatible, m_studioVariants);
  if (mesh.is_error())
  {
    m_summary->setText(QString::fromStdString(std::get<Error>(mesh.error()).msg));
    return;
  }
  auto frame = ModelPreviewFrame{};
  frame.name = "Bind pose";
  frame.positions = mesh.value().positions;
  frame.triangles = mesh.value().triangles;
  m_model->frames.assign(1, std::move(frame));
  showModel();
}

void AssetInspector::showSky()
{
  if (!m_asset || !m_asset->resource.skybox)
    return;
  const auto& faces =
    m_mode->currentIndex() == 0 ? m_asset->skyCompatible : m_asset->skyOriginal;
  const auto cross = m_skyMode->currentIndex() == 0;
  m_image->orbit = cross ? std::function<void(QPoint)>{} : [this](const QPoint delta) {
    m_skyYaw -= delta.x() * 0.01;
    m_skyPitch = std::clamp(m_skyPitch + delta.y() * 0.01, -1.55, 1.55);
    showSky();
  };
  m_image->setImage(
    cross ? inspection::renderSkyCross(faces)
          : inspection::renderSkyView(faces, m_skyYaw, m_skyPitch),
    false);
  const auto faceSize = [](const std::array<QImage, 6>& faces) {
    for (const auto& face : faces)
      if (!face.isNull())
        return face.size();
    return QSize{};
  };
  const auto source = faceSize(m_asset->skyOriginal);
  const auto published = faceSize(m_asset->skyCompatible);
  auto summary = tr("%1 · source %2 × %3 · published %4 × %5 opaque RGB")
                   .arg(
                     m_mode->currentIndex() == 0 ? tr("Published") : tr("Source"),
                     QString::number(source.width()),
                     QString::number(source.height()),
                     QString::number(published.width()),
                     QString::number(published.height()));
  if (!m_asset->diagnostics.isEmpty())
    summary += "\n" + m_asset->diagnostics.join(QStringLiteral("\n"));
  m_summary->setText(summary);
}

void AssetInspector::showSpriteFrame()
{
  if (!m_sprite || m_sprite->frames.empty())
    return;
  const auto index = size_t(m_spriteFrame->value());
  const auto& frame = m_sprite->frames[index];
  m_image->setImage(
    QImage{
      frame.rgba.data(),
      int(frame.width),
      int(frame.height),
      int(frame.width * 4),
      QImage::Format_RGBA8888}
      .copy(),
    false);
  const auto alpha =
    QStringList{tr("Normal"), tr("Additive"), tr("Index alpha"), tr("Alpha test")};
  m_summary->setText(tr("%1 × %2 · Frame %3 / %4 · %5")
                       .arg(qulonglong(frame.width))
                       .arg(qulonglong(frame.height))
                       .arg(index + 1)
                       .arg(m_sprite->frames.size())
                       .arg(alpha.at(m_sprite->alphaType)));
}

void AssetInspector::setActions(
  const bool apply,
  const bool capture,
  const bool update,
  const bool uses,
  const QString& target)
{
  m_apply->setVisible(apply);
  m_capture->setVisible(capture);
  m_update->setVisible(update);
  m_uses->setVisible(uses);
  m_apply->setToolTip(target);
  m_capture->setToolTip(target);
  m_update->setToolTip(target);
  m_uses->setToolTip(target);
}

void AssetInspector::setNavigation(const bool library, const bool snapshot)
{
  m_showLibrary->setVisible(library);
  m_showSnapshot->setVisible(snapshot);
}

const std::optional<inspection::Asset>& AssetInspector::asset() const
{
  return m_asset;
}

void AssetInspector::hideEvent(QHideEvent* event)
{
  m_player->stop();
  m_spriteTimer->stop();
  m_spritePlay->setText(tr("Play animation"));
  QWidget::hideEvent(event);
}
} // namespace tb::ui
