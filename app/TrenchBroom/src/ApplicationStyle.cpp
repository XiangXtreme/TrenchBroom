/*
 Copyright (C) 2026 Kristian Duske

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

#include "ApplicationStyle.h"

#include <QApplication>
#include <QComboBox>
#include <QDebug>
#include <QFile>
#include <QPainter>
#include <QPainterPath>
#include <QProxyStyle>
#include <QStyleHints>
#include <QStyleOption>
#include <QTextStream>

#include "base/PreferenceManager.h"
#include "prefs/Preferences.h"
#include "ui/QPathUtils.h"
#include "ui/SystemPaths.h"
#include "ui/Theme.h"
#include "ui/ThemeRegistry.h"

#include <algorithm>
#include <cmath>

namespace tb::ui
{
namespace
{

QColor blendColor(const QColor& background, const QColor& foreground, const double amount)
{
  const auto channel = [amount](const int backgroundValue, const int foregroundValue) {
    return int(std::lround(
      double(backgroundValue) * (1.0 - amount) + double(foregroundValue) * amount));
  };

  return {
    channel(background.red(), foreground.red()),
    channel(background.green(), foreground.green()),
    channel(background.blue(), foreground.blue())};
}

class TrenchBroomProxyStyle : public QProxyStyle
{
private:
  ThemeTokens m_themeTokens;

  void drawCheckBoxIndicator(const QStyleOption* option, QPainter* painter) const
  {
    const auto enabled = option->state.testFlag(QStyle::State_Enabled);
    const auto hovered = option->state.testFlag(QStyle::State_MouseOver);
    const auto pressed = option->state.testFlag(QStyle::State_Sunken);
    const auto focused = option->state.testFlag(QStyle::State_HasFocus);
    const auto selected = option->state.testFlag(QStyle::State_Selected);
    const auto checked = option->state.testFlag(QStyle::State_On);
    const auto mixed = option->state.testFlag(QStyle::State_NoChange);

    const auto side = std::min(option->rect.width(), option->rect.height());
    // QRect::center() rounds even-sized rectangles toward the top-left, which puts
    // half of the 1 px border outside the indicator's paint clip.
    const auto center = QRectF{option->rect}.center();
    const auto indicatorRect = QRectF{
      center.x() - side / 2.0 + 0.5,
      center.y() - side / 2.0 + 0.5,
      side - 1.0,
      side - 1.0};
    const auto scale = side / 18.0;

    const auto background = !enabled  ? m_themeTokens.windowBackground
                            : pressed ? m_themeTokens.pressedBackground
                            : hovered ? m_themeTokens.hoverBackground
                                      : m_themeTokens.inputBackground;
    const auto border =
      !enabled ? m_themeTokens.disabledText
      : focused
        ? m_themeTokens.focusBorder
        : blendColor(background, m_themeTokens.text, hovered || pressed ? 0.60 : 0.45);
    const auto foreground = !enabled   ? m_themeTokens.disabledText
                            : selected ? m_themeTokens.inverseText
                                       : m_themeTokens.text;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen{border, 1.0});
    painter->setBrush(background);
    painter->drawRoundedRect(indicatorRect, 3.0 * scale, 3.0 * scale);

    if (checked || mixed)
    {
      auto mark = QPainterPath{};
      if (mixed)
      {
        mark.moveTo(indicatorRect.left() + 4.0 * scale, indicatorRect.center().y());
        mark.lineTo(indicatorRect.right() - 4.0 * scale, indicatorRect.center().y());
      }
      else
      {
        mark.moveTo(
          indicatorRect.left() + 4.0 * scale, indicatorRect.top() + 9.0 * scale);
        mark.lineTo(
          indicatorRect.left() + 7.0 * scale, indicatorRect.top() + 12.0 * scale);
        mark.lineTo(
          indicatorRect.left() + 14.0 * scale, indicatorRect.top() + 5.0 * scale);
      }

      auto markPen = QPen{foreground, std::max(1.5, 2.0 * scale)};
      markPen.setCapStyle(Qt::RoundCap);
      markPen.setJoinStyle(Qt::RoundJoin);
      painter->setPen(markPen);
      painter->setBrush(Qt::NoBrush);
      painter->drawPath(mark);
    }
    painter->restore();
  }

  void drawRadioButtonIndicator(const QStyleOption* option, QPainter* painter) const
  {
    const auto enabled = option->state.testFlag(QStyle::State_Enabled);
    const auto hovered = option->state.testFlag(QStyle::State_MouseOver);
    const auto pressed = option->state.testFlag(QStyle::State_Sunken);
    const auto focused = option->state.testFlag(QStyle::State_HasFocus);
    const auto checked = option->state.testFlag(QStyle::State_On);

    const auto side = std::min(option->rect.width(), option->rect.height());
    const auto center = QRectF{option->rect}.center();
    const auto indicatorRect = QRectF{
      center.x() - side / 2.0 + 0.5,
      center.y() - side / 2.0 + 0.5,
      side - 1.0,
      side - 1.0};
    const auto scale = side / 18.0;

    const auto background = !enabled  ? m_themeTokens.windowBackground
                            : pressed ? m_themeTokens.pressedBackground
                            : hovered ? m_themeTokens.hoverBackground
                                      : m_themeTokens.inputBackground;
    const auto border =
      !enabled ? m_themeTokens.disabledText
      : focused
        ? m_themeTokens.focusBorder
        : blendColor(background, m_themeTokens.text, hovered || pressed ? 0.60 : 0.45);
    const auto foreground = enabled ? m_themeTokens.text : m_themeTokens.disabledText;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen{border, 1.0});
    painter->setBrush(background);
    painter->drawEllipse(indicatorRect);

    if (checked)
    {
      const auto dotRect =
        indicatorRect.adjusted(5.0 * scale, 5.0 * scale, -5.0 * scale, -5.0 * scale);
      painter->setPen(Qt::NoPen);
      painter->setBrush(foreground);
      painter->drawEllipse(dotRect);
    }
    painter->restore();
  }

public:
  TrenchBroomProxyStyle(const QString& key, const ThemeTokens& themeTokens)
    : QProxyStyle{key}
    , m_themeTokens{themeTokens}
  {
  }

  explicit TrenchBroomProxyStyle(const ThemeTokens& themeTokens, QStyle* style = nullptr)
    : QProxyStyle{style}
    , m_themeTokens{themeTokens}
  {
  }

  void setThemeTokens(const ThemeTokens& themeTokens) { m_themeTokens = themeTokens; }

  void drawPrimitive(
    const PrimitiveElement element,
    const QStyleOption* option,
    QPainter* painter,
    const QWidget* widget = nullptr) const override
  {
    if (
      element == QStyle::PE_IndicatorCheckBox
      || element == QStyle::PE_IndicatorItemViewItemCheck)
    {
      drawCheckBoxIndicator(option, painter);
      return;
    }
    if (element == QStyle::PE_IndicatorRadioButton)
    {
      drawRadioButtonIndicator(option, painter);
      return;
    }

    QProxyStyle::drawPrimitive(element, option, painter, widget);
  }

  int styleHint(
    StyleHint hint,
    const QStyleOption* option = nullptr,
    const QWidget* widget = nullptr,
    QStyleHintReturn* returnData = nullptr) const override
  {
    switch (hint)
    {
    case QStyle::SH_MenuBar_AltKeyNavigation:
      return 0;
    case QStyle::SH_ComboBox_Popup:
      // Use the compact list popup on every platform so maxVisibleItems is respected.
      return 0;
    default:
      return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
  }

  int pixelMetric(
    PixelMetric metric,
    const QStyleOption* option = nullptr,
    const QWidget* widget = nullptr) const override
  {
    switch (metric)
    {
    case QStyle::PM_IndicatorWidth:
    case QStyle::PM_IndicatorHeight:
    case QStyle::PM_ExclusiveIndicatorWidth:
    case QStyle::PM_ExclusiveIndicatorHeight:
      return 18;
    case QStyle::PM_SmallIconSize:
    case QStyle::PM_ButtonIconSize:
    case QStyle::PM_TabBarIconSize:
      return 16;
    case QStyle::PM_ToolBarIconSize:
      return 20;
    case QStyle::PM_ToolBarItemSpacing:
    case QStyle::PM_ToolBarItemMargin:
      return 2;
    case QStyle::PM_FocusFrameHMargin:
    case QStyle::PM_FocusFrameVMargin:
      return 1;
    default:
      return QProxyStyle::pixelMetric(metric, option, widget);
    }
  }

#if defined(Q_OS_MACOS)
  QRect subElementRect(
    SubElement element,
    const QStyleOption* option,
    const QWidget* widget = nullptr) const override
  {
    auto rect = QProxyStyle::subElementRect(element, option, widget);

    // The macOS style draws small non-editable combo boxes further down than their
    // layout item rect suggests, so their text ends up below the text of the other
    // widgets in a row. Move the layout item rect so that it is vertically centered
    // on the text again.
    if (element == QStyle::SE_ComboBoxLayoutItem && rect.isValid())
    {
      if (const auto* comboBox = qobject_cast<const QComboBox*>(widget);
          comboBox && !comboBox->isEditable())
      {
        auto comboBoxOption = QStyleOptionComboBox{};
        comboBoxOption.initFrom(comboBox);
        comboBoxOption.rect = option->rect;

        const auto textRect = subControlRect(
          QStyle::CC_ComboBox, &comboBoxOption, QStyle::SC_ComboBoxEditField, widget);
        rect.translate(0, textRect.center().y() - rect.center().y());
      }
    }

    // Since macOS 26, the macOS style also draws the text of push buttons further down
    // than their layout item rect suggests; before that, the offset determined here is
    // zero. Where the text is drawn depends on the height of the button, which isn't
    // final yet when the layout item rect is requested, so determine the offset for a
    // button of the preferred height.
    if (element == QStyle::SE_PushButtonLayoutItem && rect.isValid())
    {
      if (const auto* button = qstyleoption_cast<const QStyleOptionButton*>(option))
      {
        const auto textSize = QSize{0, button->fontMetrics.height()};
        const auto size =
          sizeFromContents(QStyle::CT_PushButton, button, textSize, widget);

        auto sizedButton = *button;
        sizedButton.rect.setHeight(size.height());

        const auto textRect =
          subElementRect(QStyle::SE_PushButtonContents, &sizedButton, widget);
        const auto itemRect =
          QProxyStyle::subElementRect(element, &sizedButton, widget);

        // subtract the doubled centers so that the offset is rounded only once
        const auto offset =
          (textRect.top() + textRect.bottom() - itemRect.top() - itemRect.bottom()) / 2;
        rect.translate(0, offset);
      }
    }

    return rect;
  }
#endif
};

ThemeTokens installProxyStyle(
  QApplication& app, const std::optional<QString>& themeOverride)
{
  // Alt is part of TrenchBroom's fundamental fly-navigation controls. The proxy style
  // prevents an Alt press and release from unexpectedly focusing the menu bar.
  const auto& registry = ThemeRegistry::instance();
  for (const auto& diagnostic : registry.diagnostics())
  {
    qWarning().noquote() << "Theme" << diagnostic.source << diagnostic.message;
  }

  const auto storedTheme = QString::fromStdString(pref(Preferences::Theme));
  const auto requestedTheme = themeOverride.value_or(storedTheme);
  const auto canonicalTheme = registry.canonicalThemeId(requestedTheme);
  const auto* selectedTheme = registry.findTheme(canonicalTheme);
  if (selectedTheme == nullptr)
  {
    qWarning().noquote() << "Theme unavailable, falling back to builtin.system:"
                         << requestedTheme;
  }
  const auto& theme = registry.resolveTheme(canonicalTheme);

  if (!themeOverride && canonicalTheme != storedTheme && selectedTheme != nullptr)
  {
    setPref(Preferences::Theme, canonicalTheme.toStdString());
  }

  if (theme.appearance == ThemeAppearance::System)
  {
    auto* systemStyle = new TrenchBroomProxyStyle{theme.tokens};
    app.setStyle(systemStyle);
    const auto systemTokens = makeSystemThemeTokens(app.palette());
    systemStyle->setThemeTokens(systemTokens);
    return systemTokens;
  }

  // Explicit themes use Fusion for deterministic cross-platform rendering.
  app.setStyle(new TrenchBroomProxyStyle{"Fusion", theme.tokens});
  app.setPalette(makeThemePalette(theme.tokens));
  app.styleHints()->setColorScheme(
    theme.appearance == ThemeAppearance::Dark ? Qt::ColorScheme::Dark
                                              : Qt::ColorScheme::Light);
  return theme.tokens;
}

bool loadStyleSheet(QApplication& app, const ThemeTokens& themeTokens, QString* error)
{
  const auto path = SystemPaths::findResourceFile("stylesheets/base.qss");
  if (auto file = QFile{pathAsQPath(path)}; file.exists())
  {
    if (!file.open(QFile::ReadOnly | QFile::Text))
    {
      if (error != nullptr)
      {
        *error = file.errorString();
      }
      return false;
    }

    auto styleSheet = QTextStream{&file}.readAll();
    if (!expandThemeStyleSheet(styleSheet, themeTokens, error))
    {
      return false;
    }

    app.setStyleSheet(styleSheet);
    if (error != nullptr)
    {
      error->clear();
    }
    return true;
  }

  if (error != nullptr)
  {
    *error = QStringLiteral("Could not find stylesheet: %1").arg(pathAsQString(path));
  }
  return false;
}

} // namespace

bool installApplicationStyle(
  QApplication& app, const std::optional<QString>& themeOverride, QString* error)
{
  return loadStyleSheet(app, installProxyStyle(app, themeOverride), error);
}

} // namespace tb::ui
