/*
  Copyright (C) 2026 XiangXtreme
  This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/AssetLibraryWindow.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "base/PreferenceManager.h"
#include "prefs/Preferences.h"
#include "ui/AssetLibraryImport.h"
#include "ui/AssetLibraryWatcher.h"
#include "ui/QPathUtils.h"

#include <fmt/format.h>

#include <algorithm>
#include <vector>

namespace tb::ui
{
namespace
{
enum class ConflictMode
{
  Overwrite,
  KeepBoth,
  Skip,
};

// Finds a free name next to an existing target, e.g. "wall_1.png".
std::filesystem::path uniqueRelativePath(
  const std::filesystem::path& libraryFolder, const std::filesystem::path& relative)
{
  if (!std::filesystem::exists(libraryFolder / relative))
    return relative;
  const auto parent = relative.parent_path();
  const auto stem = relative.stem().string();
  const auto extension = relative.extension().string();
  for (auto counter = 1;; ++counter)
  {
    const auto candidate = parent / fmt::format("{}_{}{}", stem, counter, extension);
    if (!std::filesystem::exists(libraryFolder / candidate))
      return candidate;
  }
}

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
  auto* importButton = new QPushButton{tr("Import Files…"), this};
  auto* rescanButton = new QPushButton{tr("Rescan"), this};
  auto* convertButton = new QPushButton{tr("Convert Images…"), this};
  auto* row = new QHBoxLayout{};
  row->addWidget(m_path, 1);
  row->addWidget(browse);
  row->addWidget(importButton);
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
  connect(importButton, &QPushButton::clicked, this, [this]() { importFiles(); });
  connect(rescanButton, &QPushButton::clicked, this, [this]() { rescan(); });
  connect(convertButton, &QPushButton::clicked, this, [this]() { convertImages(); });
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  // The library folder is application-wide, so opening the manager always shows the
  // configured library. The catalog itself owns the library identity.
  const auto configured =
    PreferenceManager::instance().get(Preferences::AssetLibraryDirectory);
  if (!configured.empty() && QFileInfo{pathAsQString(configured)}.isDir())
    openFolder(configured, false);
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
  PreferenceManager::instance().set(Preferences::AssetLibraryDirectory, folder);
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

void AssetLibraryWindow::importFiles()
{
  if (!m_library)
  {
    QMessageBox::information(
      this,
      tr("Import Assets"),
      tr("Configure and open an asset library folder first."));
    return;
  }

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

  auto entries = std::vector<Entry>{};
  for (const auto& file : files)
  {
    const auto source = pathFromQString(file);
    const auto folder = librarySubfolderForFile(source);
    if (folder.empty())
      continue;
    const auto relative = folder / source.filename();
    entries.push_back(
      {source, relative, std::filesystem::exists(m_library->folder() / relative)});
  }
  if (entries.empty())
  {
    QMessageBox::information(
      this,
      tr("Import Assets"),
      tr("None of the selected files can be stored in an asset library."));
    return;
  }

  const auto conflicts = std::count_if(
    entries.begin(), entries.end(), [](const Entry& entry) { return entry.conflict; });
  auto mode = ConflictMode::Overwrite;
  if (conflicts > 0)
  {
    auto box = QMessageBox{
      QMessageBox::Question,
      tr("Import Assets"),
      tr("%1 file(s) already exist in the library.").arg(int(conflicts)),
      QMessageBox::NoButton,
      this};
    auto* overwrite = box.addButton(tr("Overwrite"), QMessageBox::AcceptRole);
    auto* keepBoth = box.addButton(tr("Keep Both"), QMessageBox::ActionRole);
    auto* skip = box.addButton(tr("Skip"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() == overwrite)
      mode = ConflictMode::Overwrite;
    else if (box.clickedButton() == keepBoth)
      mode = ConflictMode::KeepBoth;
    else if (box.clickedButton() == skip)
      mode = ConflictMode::Skip;
    else
      return;
  }

  auto imported = std::vector<std::string>{};
  auto problems = QStringList{};
  auto skipped = 0;
  for (auto& entry : entries)
  {
    if (entry.conflict && mode == ConflictMode::Skip)
    {
      ++skipped;
      continue;
    }
    if (entry.conflict && mode == ConflictMode::KeepBoth)
      entry.relative = uniqueRelativePath(m_library->folder(), entry.relative);
    if (entry.conflict && mode == ConflictMode::Overwrite)
      std::filesystem::remove(m_library->folder() / entry.relative);

    const auto added = importFileIntoLibrary(
      m_library->folder(), entry.source, entry.relative.generic_string());
    if (added.is_error())
      problems << QString::fromStdString(std::get<Error>(added.error()).msg);
    else
      imported.push_back(added.value());
  }

  auto converted = std::size_t{0};
  if (!imported.empty())
  {
    const auto scanned = m_library->scan();
    if (scanned.is_error())
      problems << QString::fromStdString(std::get<Error>(scanned.error()).msg);
    else
    {
      if (!scanned.value().warning.empty())
        problems << QString::fromStdString(scanned.value().warning);

      const auto assets = m_library->assets(project::AssetType::Texture);
      if (assets.is_error())
        problems << QString::fromStdString(std::get<Error>(assets.error()).msg);
      else
        for (const auto& asset : assets.value())
        {
          if (
            asset.nativeFormat != "image"
            || std::find(imported.begin(), imported.end(), asset.virtualPath)
                 == imported.end())
            continue;
          const auto result = convertLibraryImage(*m_library, asset.id);
          if (result.is_error())
            problems << QString::fromStdString(std::get<Error>(result.error()).msg);
          else if (result.value())
            ++converted;
        }
    }
  }

  reload();
  auto summary = tr("Imported %1 file(s); converted %2 texture(s).")
                   .arg(int(imported.size()))
                   .arg(int(converted));
  if (skipped > 0)
    summary += " " + tr("%1 skipped.").arg(skipped);
  m_summary->setText(summary);
  if (!problems.isEmpty())
    QMessageBox::warning(
      this,
      tr("Import Assets"),
      problems.mid(0, 10).join("\n") + (problems.size() > 10 ? "\n…" : QString{}));
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

  const auto pending = std::count_if(
    assets.value().begin(), assets.value().end(), [](const auto& asset) {
      return asset.nativeFormat == "image";
    });
  if (pending == 0)
  {
    QMessageBox::information(
      this, tr("Convert Images"), tr("This library contains no unconverted images."));
    return;
  }

  if (
    QMessageBox::question(
      this,
      tr("Convert Images"),
      tr("Convert %1 image(s) to GoldSrc textures?\n\nThe converted textures replace "
         "the current library content; the original images stay in the folder.")
        .arg(int(pending)))
    != QMessageBox::Yes)
    return;

  auto problems = QStringList{};
  const auto converted = convertPendingImages(problems);
  reload();
  auto message = tr("Converted %1 image(s).").arg(int(converted));
  if (!problems.isEmpty())
    message += " " + tr("%1 image(s) failed.").arg(problems.size());
  m_summary->setText(message);
}

std::size_t AssetLibraryWindow::convertPendingImages(QStringList& problems)
{
  const auto assets = m_library->assets(project::AssetType::Texture);
  if (assets.is_error())
  {
    problems << QString::fromStdString(std::get<Error>(assets.error()).msg);
    return 0;
  }

  auto converted = std::size_t{0};
  for (const auto& asset : assets.value())
  {
    if (asset.nativeFormat != "image")
      continue;
    const auto result = convertLibraryImage(*m_library, asset.id);
    if (result.is_error())
      problems << QString::fromStdString(std::get<Error>(result.error()).msg);
    else if (result.value())
      ++converted;
  }
  return converted;
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
       QCoreApplication::translate("QObject", project::assetTypeName(asset.type)),
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
