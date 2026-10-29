/*
  Copyright (C) 2026 XiangXtreme
  This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/AssetLibraryWindow.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "ui/AssetLibraryWatcher.h"
#include "ui/ProjectTextureImport.h"
#include "ui/QPathUtils.h"

#include <fmt/format.h>

namespace tb::ui
{
namespace
{
constexpr auto LastFolderKey = "AssetLibrary/LastFolder";

QString sizeText(const std::uint64_t bytes)
{
  if (bytes >= 1024 * 1024)
    return QString{"%1 MB"}.arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
  if (bytes >= 1024)
    return QString{"%1 KB"}.arg(double(bytes) / 1024.0, 0, 'f', 1);
  return QString{"%1 B"}.arg(bytes);
}
} // namespace

AssetLibraryWindow::AssetLibraryWindow(QWidget* parent)
  : QDialog{parent}
{
  setWindowTitle(tr("Asset Library"));
  resize(900, 520);

  auto* layout = new QVBoxLayout{this};

  m_path = new QLineEdit{this};
  m_path->setReadOnly(true);
  m_path->setPlaceholderText(tr("No asset library open"));
  auto* browse = new QPushButton{tr("Open Folder…"), this};
  auto* rescanButton = new QPushButton{tr("Rescan"), this};
  auto* convertButton = new QPushButton{tr("Convert Images…"), this};
  auto* row = new QHBoxLayout{};
  row->addWidget(m_path, 1);
  row->addWidget(browse);
  row->addWidget(rescanButton);
  row->addWidget(convertButton);
  layout->addLayout(row);

  m_summary = new QLabel{this};
  m_summary->setWordWrap(true);
  layout->addWidget(m_summary);
  m_watcher = new AssetLibraryWatcher{this};
  connect(m_watcher, &AssetLibraryWatcher::foldersChanged, this, [this]() {
    m_summary->setText(
      tr("Library files changed. Click Rescan to review the current files."));
  });

  m_assets = new QTreeWidget{this};
  m_assets->setRootIsDecorated(false);
  m_assets->setHeaderLabels(
    {tr("Name"), tr("Type"), tr("Path"), tr("Revision"), tr("Size"), tr("State")});
  m_assets->header()->setSectionResizeMode(2, QHeaderView::Stretch);
  layout->addWidget(m_assets, 1);

  auto* buttons = new QDialogButtonBox{QDialogButtonBox::Close, this};
  layout->addWidget(buttons);

  connect(browse, &QPushButton::clicked, this, [this]() { chooseFolder(); });
  connect(rescanButton, &QPushButton::clicked, this, [this]() { rescan(); });
  connect(convertButton, &QPushButton::clicked, this, [this]() { convertImages(); });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  // Reopening the window continues with the library used last; the path is only a
  // convenience, the library identity lives in the catalog itself.
  const auto last = QSettings{}.value(LastFolderKey).toString();
  if (!last.isEmpty() && QFileInfo{last}.isDir())
    openFolder(pathFromQString(last), false);
  else
    reload();
}

void AssetLibraryWindow::chooseFolder()
{
  const auto folder = QFileDialog::getExistingDirectory(
    this, tr("Asset Library"), {}, QFileDialog::ShowDirsOnly);
  if (folder.isEmpty())
    return;
  const auto path = pathFromQString(folder);
  const auto create = !project::AssetLibrary::isLibraryFolder(path)
                      && QMessageBox::question(
                           this,
                           tr("Create Asset Library"),
                           tr("'%1' is not an asset library yet.\n\nCreate a catalog "
                              "inside it and import the resources it contains?")
                             .arg(QFileInfo{folder}.fileName()))
                           == QMessageBox::Yes;
  openFolder(path, create);
}

void AssetLibraryWindow::openFolder(
  const std::filesystem::path& folder, const bool createIfMissing)
{
  m_library.reset();
  if (project::AssetLibrary::isLibraryFolder(folder))
  {
    auto opened = project::AssetLibrary::open(folder);
    if (opened.is_error())
    {
      QMessageBox::critical(
        this,
        tr("Asset Library"),
        QString::fromStdString(std::get<Error>(opened.error()).msg));
      reload();
      return;
    }
    m_library = std::move(opened).value();
  }
  else if (createIfMissing)
  {
    auto name = folder.filename().string();
    auto created = project::AssetLibrary::create(
      folder, name.empty() ? std::string{"Asset Library"} : name);
    if (created.is_error())
    {
      QMessageBox::critical(
        this,
        tr("Asset Library"),
        QString::fromStdString(std::get<Error>(created.error()).msg));
      reload();
      return;
    }
    m_library = std::move(created).value();
    if (const auto scan = m_library->scan(); scan.is_success())
    {
      if (!scan.value().warning.empty())
        QMessageBox::warning(
          this, tr("Asset Library"), QString::fromStdString(scan.value().warning));
    }
  }
  else
  {
    // A folder without a catalog is shown as "nothing here" instead of silently becoming
    // a library.
    reload();
    return;
  }
  QSettings{}.setValue(LastFolderKey, pathAsQString(folder));
  reload();
}

void AssetLibraryWindow::rescan()
{
  if (!m_library)
    return;
  const auto scanned = m_library->scan();
  if (scanned.is_error())
  {
    QMessageBox::critical(
      this,
      tr("Asset Library"),
      QString::fromStdString(std::get<Error>(scanned.error()).msg));
    return;
  }
  if (!scanned.value().warning.empty())
    QMessageBox::warning(
      this, tr("Asset Library"), QString::fromStdString(scanned.value().warning));
  reload();
}

void AssetLibraryWindow::convertImages()
{
  if (!m_library)
  {
    QMessageBox::information(
      this, tr("Convert Images"), tr("Open an asset library first."));
    return;
  }

  const auto assets = m_library->assets(project::AssetType::Texture);
  if (assets.is_error())
  {
    QMessageBox::critical(
      this,
      tr("Convert Images"),
      QString::fromStdString(std::get<Error>(assets.error()).msg));
    return;
  }

  auto images = std::vector<project::LibraryAsset>{};
  for (const auto& asset : assets.value())
    if (asset.nativeFormat == "image")
      images.push_back(asset);
  if (images.empty())
  {
    QMessageBox::information(
      this,
      tr("Convert Images"),
      tr("Every image in this library is already converted."));
    return;
  }

  if (
    QMessageBox::question(
      this,
      tr("Convert Images"),
      tr("Convert %1 image(s) to GoldSrc textures?\n\nThe original images stay in the "
         "library and each converted texture is stored as a new revision.")
        .arg(images.size()))
    != QMessageBox::Yes)
    return;

  auto converted = 0;
  auto failed = 0;
  auto cancelled = false;
  for (const auto& image : images)
  {
    auto original = m_library->readOriginal(image.id);
    if (original.is_error())
    {
      ++failed;
      continue;
    }

    auto import = project::AssetImport{};
    import.type = project::AssetType::Texture;
    import.displayName = image.displayName;
    import.virtualPath = image.virtualPath;
    import.sourceName = image.virtualPath;
    import.original = std::move(original).value();

    const auto prepared = prepareProjectTexture(this, import, std::nullopt, true);
    if (prepared.is_error())
    {
      ++failed;
      continue;
    }
    if (!prepared.value())
    {
      cancelled = true;
      break;
    }

    if (m_library
          ->publishCompatibleContent(image.id, import.compatible, import.conversionRecipe)
          .is_error())
      ++failed;
    else
      ++converted;
  }

  reload();
  auto message = tr("Converted %1 image(s).").arg(converted);
  if (failed > 0)
    message += " " + tr("%1 image(s) failed.").arg(failed);
  if (cancelled)
    message += " " + tr("Conversion cancelled.");
  m_summary->setText(message);
}

void AssetLibraryWindow::reload()
{
  m_assets->clear();
  m_watcher->setFolders(
    m_library ? QStringList{pathAsQString(m_library->folder())} : QStringList{});
  m_watcher->acknowledgeChanges();
  if (!m_library)
  {
    m_path->clear();
    m_summary->setText(
      tr("Open a folder to create or inspect an asset library. A library is a plain "
         "folder with its own catalog, independent of any project."));
    return;
  }

  const auto& info = m_library->info();
  m_path->setText(pathAsQString(std::filesystem::path{info.path}));
  m_path->setToolTip(QString::fromStdString(info.id));

  const auto assets = m_library->assets();
  if (assets.is_error())
  {
    m_summary->setText(QString::fromStdString(std::get<Error>(assets.error()).msg));
    return;
  }
  const auto groups = m_library->groups();
  auto missing = 0;
  for (const auto& asset : assets.value())
  {
    auto* item = new QTreeWidgetItem{
      m_assets,
      {QString::fromStdString(asset.displayName),
       QString::fromLatin1(project::assetTypeName(asset.type)),
       QString::fromStdString(asset.virtualPath),
       QString::number(asset.revision),
       sizeText(asset.size),
       asset.missing
         ? tr("Missing")
         : (asset.nativeFormat == "goldsrc-wad" ? tr("Converted") : QString{})}};
    item->setToolTip(0, QString::fromStdString(asset.id));
    if (asset.missing)
      ++missing;
  }
  m_summary->setText(tr("%1 resources in %2 groups%3.")
                       .arg(assets.value().size())
                       .arg(groups.is_success() ? groups.value().size() : std::size_t{0})
                       .arg(missing > 0 ? tr(", %1 missing").arg(missing) : QString{}));
}
} // namespace tb::ui
