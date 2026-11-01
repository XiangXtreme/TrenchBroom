#pragma once

#include "ui/SmartPropertyEditor.h"

#include <filesystem>
#include <optional>
#include <string>

class QPushButton;
class QWidget;

namespace tb::mdl
{
class EntityNodeBase;
}

namespace tb::ui
{
class MapDocument;

std::optional<std::string> modelPathForSmartModelEditor(
  const std::filesystem::path& absModelPath, const std::filesystem::path& gamePath);

class SmartModelEditor : public SmartPropertyEditor
{
  Q_OBJECT
private:
  QPushButton* m_browseButton = nullptr;
  QPushButton* m_assetButton = nullptr;

public:
  explicit SmartModelEditor(MapDocument& document, QWidget* parent = nullptr);

private:
  void createGui();
  void doUpdateVisual(const std::vector<mdl::EntityNodeBase*>& nodes) override;

  void browseFile();
};

} // namespace tb::ui
