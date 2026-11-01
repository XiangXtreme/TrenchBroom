#include "ui/SmartModelEditor.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QPushButton>

#include "mdl/EntityNodeBase.h"
#include "mdl/Map.h"
#include "ui/AssetBrowserModel.h"
#include "ui/FileDialogDefaultDir.h"
#include "ui/MapDocument.h"
#include "ui/QPathUtils.h"
#include "ui/QWidgetUtils.h"

#include <filesystem>
#include <optional>

namespace tb::ui
{

std::optional<std::string> modelPathForSmartModelEditor(
  const std::filesystem::path& absModelPath, const std::filesystem::path& gamePath)
{
  if (gamePath.empty())
  {
    return std::nullopt;
  }

  std::error_code ec;
  const auto relativeModelPathFull =
    std::filesystem::relative(absModelPath, gamePath, ec);
  if (ec)
  {
    return std::nullopt;
  }

  auto relativePathStr = relativeModelPathFull.string();
  auto modelsPos = relativePathStr.find("models/");
  if (modelsPos == std::string::npos)
  {
    modelsPos = relativePathStr.find("models\\");
  }

  auto finalModelPath = std::string{};
  if (modelsPos != std::string::npos)
  {
    finalModelPath = relativePathStr.substr(modelsPos);
  }
  else
  {
    finalModelPath = relativeModelPathFull.generic_string();
  }

  for (auto& ch : finalModelPath)
  {
    if (ch == '\\')
    {
      ch = '/';
    }
  }

  if (finalModelPath.empty())
  {
    return std::nullopt;
  }

  return finalModelPath;
}

SmartModelEditor::SmartModelEditor(MapDocument& document, QWidget* parent)
  : SmartPropertyEditor{document, parent}
{
  createGui();
}

void SmartModelEditor::createGui()
{
  auto* layout = new QHBoxLayout{this};
  layout->setContentsMargins(0, 0, 0, 0);

  m_browseButton = new QPushButton{this};
  m_browseButton->setText(tr("Load Model File"));

  layout->addWidget(m_browseButton);
  m_assetButton = new QPushButton{tr("Choose project asset")};
  layout->addWidget(m_assetButton);
  connect(m_assetButton, &QPushButton::clicked, this, [this]() {
    const auto selected = nodes();
    const auto sprite = !selected.empty() && (selected.front()->entity().classname() == "env_sprite" || selected.front()->entity().classname() == "env_glow");
    browseAsset(sprite ? BrowserCellType::Sprite : BrowserCellType::Model);
  });

  connect(m_browseButton, &QPushButton::clicked, this, &SmartModelEditor::browseFile);
}

void SmartModelEditor::doUpdateVisual(const std::vector<mdl::EntityNodeBase*>&)
{
  m_assetButton->setVisible(canBrowseProjectAssets());
}

void SmartModelEditor::browseFile()
{
  const auto caption = tr("Load Model File");
  const auto filter = tr("Model files (*.mdl);;All files (*.*)");

  const auto pathQStr = QFileDialog::getOpenFileName(
    nullptr, caption, fileDialogDefaultDirectory(FileDialogDir::GamePath), filter);
  if (pathQStr.isEmpty())
  {
    return;
  }

  updateFileDialogDefaultDirectoryWithFilename(FileDialogDir::GamePath, pathQStr);

  const auto absModelPath = pathFromQString(pathQStr);
  const auto gamePath = document().map().gamePath();
  const auto finalModelPath = modelPathForSmartModelEditor(absModelPath, gamePath);
  if (!finalModelPath)
  {
    return;
  }

  addOrUpdateProperty(*finalModelPath);
}

} // namespace tb::ui
