/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/WorkspaceLayout.h"

#include <QDataStream>
#include <QSettings>
#include <QStringList>
#include <QVariantMap>

#include <cmath>

namespace tb::ui
{
namespace
{
QString key(const WorkspaceKind kind, const QString& region)
{
  return QString{"WorkspaceLayouts/%1/%2"}.arg(
    kind == WorkspaceKind::Project ? "Project" : "Map", region);
}

double ratio(const QVariant& value, const double fallback)
{
  auto ok = false;
  const auto result = value.toDouble(&ok);
  return ok && std::isfinite(result) && result >= 0.1 && result <= 0.8 ? result
                                                                       : fallback;
}

const auto InspectorPages =
  QStringList{"map", "entity", "face", "outliner", "plugin", "terrain"};
const auto InfoPages = QStringList{"output-log", "python-console", "issues", "assets"};

int pageIndex(const QStringList& pages, const QVariant& value)
{
  const auto index = pages.indexOf(value.toString());
  return index >= 0 ? static_cast<int>(index) : 0;
}

template <typename T>
T legacyValue(QSettings& settings, const QString& path, const T fallback)
{
  auto stream = QDataStream{settings.value(path).toByteArray()};
  auto value = fallback;
  stream >> value;
  return stream.status() == QDataStream::Ok ? value : fallback;
}
} // namespace

EditorLayout loadEditorLayout(QSettings& settings, const WorkspaceKind kind)
{
  auto result = EditorLayout{};
  const auto path = key(kind, "Editor");
  if (!settings.contains(path))
  {
    result.legacyHorizontal =
      settings.value("Windows/MapWindow_HorizontalSplitter/State").toByteArray();
    result.legacyVertical =
      settings.value("Windows/MapWindow_VerticalSplitterSplitter/State").toByteArray();
    result.inspectorVisible = legacyValue(settings, "Windows/Inspector/State", true);
    result.infoVisible = legacyValue(settings, "Windows/MapWindow_InfoPanel/State", true);
    const auto inspector = legacyValue(
      settings,
      "Windows/Inspector_TabBook/State",
      static_cast<int>(result.inspectorPage));
    if (inspector >= 0 && inspector < InspectorPages.size())
      result.inspectorPage = static_cast<InspectorPage>(inspector);
    const auto info = legacyValue(settings, "Windows/InfoPanel_TabBook/State", 0);
    if (info >= 0 && info < InfoPages.size())
      result.infoPage = static_cast<InfoPanelPage>(info);
    return result;
  }
  const auto values = settings.value(path).toMap();
  if (values.value("version").toInt() != 1)
    return result;
  result.inspectorRatio = ratio(values.value("inspectorRatio"), result.inspectorRatio);
  result.infoRatio = ratio(values.value("infoRatio"), result.infoRatio);
  result.inspectorVisible = values.value("inspectorVisible", true).toBool();
  result.infoVisible = values.value("infoVisible", true).toBool();
  result.inspectorPage =
    static_cast<InspectorPage>(pageIndex(InspectorPages, values.value("inspectorPage")));
  result.infoPage =
    static_cast<InfoPanelPage>(pageIndex(InfoPages, values.value("infoPage")));
  return result;
}

void saveEditorLayout(
  QSettings& settings, const WorkspaceKind kind, const EditorLayout& layout)
{
  settings.setValue(
    key(kind, "Editor"),
    QVariantMap{
      {"version", 1},
      {"inspectorRatio", ratio(layout.inspectorRatio, 0.33)},
      {"infoRatio", ratio(layout.infoRatio, 0.25)},
      {"inspectorVisible", layout.inspectorVisible},
      {"infoVisible", layout.infoVisible},
      {"inspectorPage", InspectorPages.at(static_cast<int>(layout.inspectorPage))},
      {"infoPage", InfoPages.at(static_cast<int>(layout.infoPage))}});
}

ToolLayout loadToolLayout(QSettings& settings, const WorkspaceKind kind)
{
  auto result = ToolLayout{};
  const auto path = key(kind, "Tools");
  if (!settings.contains(path))
  {
    if (kind == WorkspaceKind::Project)
      result.expanded = result.pinned =
        settings.value("ProjectTools/Pinned", false).toBool();
    return result;
  }
  const auto values = settings.value(path).toMap();
  if (values.value("version").toInt() != 1)
    return result;
  const auto tool = values.value("tool").toString();
  if (QStringList{"content-browser", "output-log", "python-console", "issues"}.contains(
        tool))
    result.tool = tool;
  result.pinned = values.value("pinned", false).toBool();
  result.expanded = values.value("expanded", false).toBool();
  result.keepOpen = values.value("keepOpen", false).toBool();
  result.heightRatio = ratio(values.value("heightRatio"), result.heightRatio);
  return result;
}

void saveToolLayout(
  QSettings& settings, const WorkspaceKind kind, const ToolLayout& layout)
{
  settings.setValue(
    key(kind, "Tools"),
    QVariantMap{
      {"version", 1},
      {"tool", layout.tool},
      {"expanded", layout.expanded},
      {"pinned", layout.pinned},
      {"keepOpen", layout.keepOpen},
      {"heightRatio", ratio(layout.heightRatio, 0.4)}});
}
} // namespace tb::ui
