/*
 Copyright (C) 2020 MaxED

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "ui/ColorModel.h"

#include <QColorDialog>
#include <QCoreApplication>

#include "base/Macros.h"
#include "base/PreferenceManager.h"
#include "prefs/Preferences.h"
#include "ui/QColorUtils.h"
#include "ui/QPathUtils.h"

#include "kd/contracts.h"

#include <algorithm>


namespace tb::ui
{
namespace
{
QString colorGroup(const Preference<Color>& preference)
{
  const auto path = pathAsGenericQString(preference.path);
  const auto name = pathAsQString(preference.path.filename());
  if (path.startsWith("Browser/") || path.startsWith("Texture Browser/"))
    return QCoreApplication::translate("tb::ui::ColorModel", "Asset browsers");
  if (name.contains("overlay", Qt::CaseInsensitive))
    return QCoreApplication::translate("tb::ui::ColorModel", "Text overlays");
  if (
    name.contains("face", Qt::CaseInsensitive)
    || name.contains("edge", Qt::CaseInsensitive)
    || name.contains("entity", Qt::CaseInsensitive)
    || name.contains("group", Qt::CaseInsensitive) || name == "Selection bounds")
    return QCoreApplication::translate("tb::ui::ColorModel", "Map geometry");
  if (
    name.contains("handle", Qt::CaseInsensitive)
    || name.contains("scale", Qt::CaseInsensitive)
    || name.contains("shear", Qt::CaseInsensitive)
    || name.contains("move", Qt::CaseInsensitive)
    || name.contains("indicator", Qt::CaseInsensitive) || name == "Texture seam")
    return QCoreApplication::translate("tb::ui::ColorModel", "Editing tools");
  return QCoreApplication::translate("tb::ui::ColorModel", "Map views and guides");
}

QString colorName(const Preference<Color>& preference)
{
  // Mark the persisted captions for translation while leaving preference keys intact.
  static const auto captions = {
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Soft map bounds color"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Background"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "X axis"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Y axis"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Z axis"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Point file"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Portal file border"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Portal file fill"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Compass background"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Compass background outline"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Compass axis outline"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Camera frustum"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Groups"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Linked Groups"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Tutorial overlay text"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Tutorial overlay background"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Faces"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Selected faces"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Locked faces"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Edges"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Selected edges"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Locked edges"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Undefined entity"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Selection bounds"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Info overlay text"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Group info overlay text"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Info overlay background"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Selected info overlay text"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Selected info overlay background"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Locked info overlay text"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Locked info overlay background"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Handle"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Occluded handle"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Selected handle"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Occluded selected handle"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Clip handle"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Clip face"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Resize handle"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Rotate handle"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Scale handle"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Scale fill"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Scale outline"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Shear fill"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Shear outline"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Move trace"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Move indicator outline"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Move indicator fill"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Angle indicator"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Texture seam"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Color2D"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Text color"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Sub text color"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Group background color"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Background color"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Default color"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Selected color"),
    QT_TRANSLATE_NOOP("tb::ui::ColorModel", "Used color"),
  };
  const auto name = pathAsQString(preference.path.filename());
  for (const auto* caption : captions)
    if (name == QString::fromUtf8(caption))
      return QCoreApplication::translate("tb::ui::ColorModel", caption);
  return name;
}
} // namespace

ColorModel::ColorModel(QObject* parent)
  : QAbstractTableModel{parent}
{
  initialize();
}

void ColorModel::initialize()
{
  for (auto* pref : Preferences::colorPreferences())
  {
    if (pref->persistencePolicy != PreferencePersistencePolicy::ReadOnly)
    {
      m_colors.emplace_back(pref);
    }
  }

  std::stable_sort(
    m_colors.begin(), m_colors.end(), [](const auto* lhs, const auto* rhs) {
      return QString::localeAwareCompare(colorGroup(*lhs), colorGroup(*rhs)) < 0;
    });
  m_colorsCount = static_cast<int>(m_colors.size());
}

// Reset to default values
void ColorModel::reset()
{
  auto& prefs = PreferenceManager::instance();
  for (auto* pref : m_colors)
  {
    prefs.resetToDefault(*pref);
  }

  emit dataChanged(createIndex(0, 0), createIndex(m_colorsCount - 1, 2));
}

int ColorModel::rowCount(const QModelIndex& /* parent */) const
{
  return m_colorsCount;
}

int ColorModel::columnCount(const QModelIndex& /* parent */) const
{
  return 3; // Color, Context, Description
}

QVariant ColorModel::headerData(
  const int section, const Qt::Orientation orientation, const int role) const
{
  if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
  {
    switch (section)
    {
    case 0:
      return QString{tr("Color")};
    case 1:
      return QString{tr("Group")};
    case 2:
      return QString{tr("Description")};
      switchDefault();
    }
  }

  return {};
}

QVariant ColorModel::data(const QModelIndex& index, const int role) const
{
  if (!checkIndex(index))
  {
    return {};
  }

  if (role == Qt::DisplayRole)
  {
    const auto& colorPreference = getColorPreference(index.row());

    switch (index.column())
    {
    case 0:
      return {}; // Leave first cell empty
    case 1:
      return colorGroup(colorPreference);
    case 2:
      return colorName(colorPreference);
      switchDefault();
    }
  }

  if (role == Qt::ToolTipRole)
    return pathAsGenericQString(getColorPreference(index.row()).path);

  // Use decoration instead of the cell background so application QSS cannot cover the
  // swatch.
  if (role == Qt::DecorationRole && index.column() == 0)
  {
    auto& prefs = PreferenceManager::instance();

    const auto& colorPreference = getColorPreference(index.row());
    auto color = toQColor(prefs.getPendingValue(colorPreference));
    color.setAlpha(255); // Ignore alpha

    return color;
  }

  return {};
}

bool ColorModel::setData(
  const QModelIndex& index, const QVariant& value, const int /*role*/)
{
  if (!checkIndex(index) || index.column() != 0)
  {
    return false;
  }

  auto& prefs = PreferenceManager::instance();

  const auto& colorPreference = getColorPreference(index.row());
  const auto color = toQColor(prefs.getPendingValue(colorPreference));

  auto newColor = value.value<QColor>();
  newColor.setAlpha(color.alpha()); // Keep initial alpha...

  prefs.set(colorPreference, fromQColor(newColor));

  emit dataChanged(index, index);
  return true;
}

Qt::ItemFlags ColorModel::flags(const QModelIndex& index) const
{
  if (checkIndex(index) && index.column() == 0)
  {
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
  }

  return Qt::ItemIsEnabled;
}

void ColorModel::pickColor(const QModelIndex& mi)
{
  if (checkIndex(mi))
  {
    auto& prefs = PreferenceManager::instance();

    // Get current color
    const auto& colorPreference = getColorPreference(mi.row());
    auto color = toQColor(prefs.getPendingValue(colorPreference));

    // Show dialog
    auto newColor = QColorDialog::getColor(
      color, nullptr, "Select new color", QColorDialog::DontUseNativeDialog);

    // Apply color (QColorDialog::getColor() returns an invalid color if the user cancels
    // the dialog)
    if (newColor.isValid())
    {
      // pickColor() can be called for column 1 or 2 if the user double-clicks those
      // columns, but we always edit column 0 (where the color is displayed)
      setData(index(mi.row(), 0), newColor, Qt::EditRole);
    }
  }
}

const Preference<Color>& ColorModel::getColorPreference(const int index) const
{
  contract_pre(index < m_colorsCount);
  return *m_colors[static_cast<size_t>(index)];
}

bool ColorModel::checkIndex(const QModelIndex& index) const
{
  return index.isValid() && index.column() < 3 && index.row() < m_colorsCount;
}

} // namespace tb::ui
