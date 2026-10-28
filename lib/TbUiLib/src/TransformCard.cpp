/*
 Copyright (C) 2026 TrenchBroom Authors

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

#include "ui/TransformCard.h"

#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QFocusEvent>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLineF>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "el/EvaluationContext.h"
#include "el/ParseExpression.h"
#include "el/Value.h"
#include "mdl/Entity.h"
#include "mdl/EntityNodeBase.h"
#include "mdl/EntityProperties.h"
#include "mdl/Grid.h"
#include "mdl/Map.h"
#include "mdl/Map_Entities.h"
#include "mdl/Map_Geometry.h"
#include "mdl/Transaction.h"
#include "mdl/TransactionScope.h"
#include "ui/BitmapButton.h"
#include "ui/ImageUtils.h"
#include "ui/MapDocument.h"
#include "ui/SpinControl.h"
#include "ui/ViewConstants.h"

#include "kd/string_utils.h"

#include "vm/mat_ext.h"
#include "vm/util.h"
#include "vm/vec_io.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <optional>
#include <vector>

namespace tb::ui
{

// See docs/transform-card-interaction.md for the Blender source audit and TB's
// gesture, text editing, transaction and focus contracts.
class VectorDragGroupController : public QObject
{
public:
  enum class Mode
  {
    Location,
    Rotation,
    Scale
  };

private:
  enum class State
  {
    Idle,
    Pending,
    VerticalSelect,
    Scrubbing,
    TextEditing,
    MultiEditing
  };
  std::vector<SpinControl*> m_controls;
  mdl::Map& m_map;
  Mode m_mode;
  std::function<void(size_t)> m_apply;
  std::function<void()> m_refresh;
  std::function<void()> m_finishEditing;
  State m_state = State::Idle;
  QWidget* m_grabbedWidget = nullptr;
  QPoint m_pressPos;
  QPoint m_lastPos;
  QPoint m_dragDirection;
  int m_verticalStartX = 0;
  size_t m_primary = 0;
  std::vector<size_t> m_selected;
  std::vector<double> m_startValues;
  std::vector<double> m_dragValues;
  bool m_transaction = false;
  bool m_cursorOverridden = false;
  bool m_dragChanged = false;
  bool m_consumeRelease = false;
  bool m_suppressContextMenu = false;

public:
  VectorDragGroupController(
    std::vector<SpinControl*> controls,
    mdl::Map& map,
    Mode mode,
    std::function<void(size_t)> apply,
    std::function<void()> refresh,
    std::function<void()> finishEditing,
    QObject* parent)
    : QObject{parent}
    , m_controls{std::move(controls)}
    , m_map{map}
    , m_mode{mode}
    , m_apply{std::move(apply)}
    , m_refresh{std::move(refresh)}
    , m_finishEditing{std::move(finishEditing)}
  {
    for (size_t i = 0; i < m_controls.size(); ++i)
    {
      auto* spin = m_controls[i];
      spin->installEventFilter(this);
      spin->findChild<QLineEdit*>()->installEventFilter(this);
      spin->findChild<QLineEdit*>()->setCursor(Qt::SizeHorCursor);
      connect(spin->findChild<QLineEdit*>(), &QLineEdit::textChanged, this, [this, i]() {
        if (m_state == State::MultiEditing && i == m_primary)
        {
          mirrorText();
        }
      });
    }
  }

  ~VectorDragGroupController() override
  {
    releaseMouse();
    if (m_transaction)
    {
      m_map.cancelTransaction();
    }
  }

  bool isInteracting() const { return m_state != State::Idle; }
  bool isMultiEditing() const { return m_state == State::MultiEditing; }
  bool hasMultipleChannels() const { return isInteracting() && m_selected.size() > 1; }

protected:
  bool eventFilter(QObject* watched, QEvent* event) override
  {
    const auto index = findControl(watched);
    switch (event->type())
    {
    case QEvent::ContextMenu:
      if (m_suppressContextMenu)
      {
        m_suppressContextMenu = false;
        return true;
      }
      break;
    case QEvent::Wheel:
      // Plain wheel events belong to the inspector, including while editing text.
      if (
        !static_cast<QWheelEvent*>(event)->modifiers().testFlag(Qt::ControlModifier)
        || isInteracting())
      {
        event->ignore();
        return true;
      }
      break;
    case QEvent::MouseButtonPress: {
      auto* mouse = static_cast<QMouseEvent*>(event);
      if (mouse->button() == Qt::RightButton && isInteracting())
      {
        finish(false);
        m_suppressContextMenu = true;
        exitTextEditing(index);
        return true;
      }
      if (mouse->button() != Qt::LeftButton)
      {
        break;
      }
      m_suppressContextMenu = false;
      if (isTextEditing())
      {
        if (index == m_primary)
        {
          return false;
        }
        commitEditing();
      }
      if (!m_controls[index]->isEnabled())
      {
        break;
      }
      m_primary = index;
      m_selected = {index};
      m_startValues.clear();
      for (auto* spin : m_controls)
      {
        m_startValues.push_back(spin->value());
      }
      m_dragValues = m_startValues;
      m_dragChanged = false;
      m_dragDirection = {};
      m_pressPos = mouse->globalPosition().toPoint();
      m_lastPos = m_pressPos;
      m_state = State::Pending;
      m_grabbedWidget = static_cast<QWidget*>(watched);
      m_controls[index]->setFocus(Qt::MouseFocusReason);
      m_controls[index]->findChild<QLineEdit*>()->deselect();
      if (mouse->modifiers().testFlag(Qt::ControlModifier))
      {
        m_grabbedWidget = nullptr;
        beginTextEditing();
        m_consumeRelease = true;
      }
      else
      {
        m_grabbedWidget->grabMouse();
      }
      return true;
    }
    case QEvent::MouseMove: {
      auto* mouse = static_cast<QMouseEvent*>(event);
      if (!(mouse->buttons() & Qt::LeftButton))
      {
        break;
      }
      const auto pos = mouse->globalPosition().toPoint();
      const auto delta = pos - m_pressPos;
      const auto motion = pos - m_lastPos;
      m_lastPos = pos;
      if (m_state == State::Pending)
      {
        m_dragDirection += QPoint{std::abs(motion.x()), std::abs(motion.y())};
        const auto distance = std::hypot(m_dragDirection.x(), m_dragDirection.y());
        if (distance <= gestureThreshold())
        {
          return true;
        }
        if (m_dragDirection.y() / distance > 0.75)
        {
          m_state = State::VerticalSelect;
          m_verticalStartX = pos.x();
        }
        else if (std::abs(delta.x()) >= gestureThreshold())
        {
          beginScrub();
          return true;
        }
        else
        {
          return true;
        }
      }
      if (m_state == State::VerticalSelect)
      {
        if (
          std::abs(pos.x() - m_verticalStartX) > gestureThreshold()
          && std::abs(motion.x()) > std::abs(motion.y()))
        {
          beginScrub();
        }
        else
        {
          selectChannels(pos);
        }
        return true;
      }
      if (m_state == State::Scrubbing)
      {
        scrub(motion.x(), mouse->modifiers());
        return true;
      }
      break;
    }
    case QEvent::MouseButtonRelease: {
      auto* mouse = static_cast<QMouseEvent*>(event);
      if (mouse->button() != Qt::LeftButton)
      {
        break;
      }
      if (m_consumeRelease)
      {
        m_consumeRelease = false;
        return true;
      }
      if (m_state == State::Scrubbing)
      {
        if (m_dragChanged)
        {
          finish(true);
          exitTextEditing(m_primary);
        }
        else
        {
          if (m_transaction)
          {
            m_transaction = false;
            m_map.cancelTransaction();
          }
          beginTextEditing();
        }
        return true;
      }
      if (m_state == State::VerticalSelect || m_state == State::Pending)
      {
        beginTextEditing();
        return true;
      }
      break;
    }
    case QEvent::KeyPress: {
      auto* key = static_cast<QKeyEvent*>(event);
      if (key->key() == Qt::Key_Escape && isInteracting())
      {
        finish(false);
        exitTextEditing(index);
        return true;
      }
      if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
      {
        if (m_state == State::Idle)
        {
          prepareTextEditing(index);
        }
        else
        {
          commitEditing();
          exitTextEditing(index);
        }
        return true;
      }
      if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab)
      {
        if (isTextEditing())
        {
          commitEditing();
        }
        m_controls[index]->findChild<QLineEdit*>()->setCursor(Qt::SizeHorCursor);
      }
      else if (m_state == State::Scrubbing)
      {
        if (key->key() == Qt::Key_Control || key->key() == Qt::Key_Shift)
        {
          scrub(0, key->modifiers());
        }
        return true;
      }
      else if (m_state == State::Pending || m_state == State::VerticalSelect)
      {
        return true;
      }
      else if (
        isTextEditing() && (key->key() == Qt::Key_Left || key->key() == Qt::Key_Right)
        && !key->modifiers().testFlag(Qt::ShiftModifier))
      {
        auto* edit = m_controls[index]->findChild<QLineEdit*>();
        if (edit->hasSelectedText())
        {
          edit->setCursorPosition(
            key->key() == Qt::Key_Left ? edit->selectionStart() : edit->selectionEnd());
          return true;
        }
      }
      else if (
        isTextEditing() && (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down))
      {
        auto cursorKey = QKeyEvent{
          QEvent::KeyPress,
          key->key() == Qt::Key_Up ? Qt::Key_Home : Qt::Key_End,
          key->modifiers()};
        QApplication::sendEvent(m_controls[index]->findChild<QLineEdit*>(), &cursorKey);
        return true;
      }
      else if (m_state == State::Idle && !key->text().isEmpty())
      {
        prepareTextEditing(index);
      }
      break;
    }
    case QEvent::KeyRelease:
      if (m_state == State::Scrubbing)
      {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Control || key->key() == Qt::Key_Shift)
        {
          scrub(0, key->modifiers());
        }
        return true;
      }
      break;
    case QEvent::FocusIn: {
      const auto reason = static_cast<QFocusEvent*>(event)->reason();
      // Qt can focus the spin box before delivering its mouse press. Only an
      // explicit keyboard navigation request should enter text editing here.
      if (
        m_state == State::Idle
        && (reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason || reason == Qt::ShortcutFocusReason))
      {
        prepareTextEditing(index);
      }
      break;
    }
    case QEvent::FocusOut:
      if (isTextEditing() && index == m_primary)
      {
        commitEditing();
      }
      m_controls[index]->findChild<QLineEdit*>()->setCursor(Qt::SizeHorCursor);
      break;
    case QEvent::Hide:
    case QEvent::WindowDeactivate:
      if (isInteracting())
      {
        finish(false);
      }
      break;
    default:
      break;
    }
    return QObject::eventFilter(watched, event);
  }

private:
  bool isTextEditing() const
  {
    return m_state == State::TextEditing || m_state == State::MultiEditing;
  }

  int gestureThreshold() const
  {
    return std::max(1, m_controls[m_primary]->height() / 4);
  }

  void prepareTextEditing(size_t index)
  {
    m_primary = index;
    m_selected = {index};
    m_startValues.clear();
    for (auto* spin : m_controls)
    {
      m_startValues.push_back(spin->value());
    }
    beginTextEditing();
  }

  void beginTextEditing()
  {
    releaseMouse();
    m_state = m_selected.size() > 1 ? State::MultiEditing : State::TextEditing;
    auto* spin = m_controls[m_primary];
    spin->setFocus(Qt::OtherFocusReason);
    auto* edit = spin->findChild<QLineEdit*>();
    edit->setCursor(Qt::IBeamCursor);
    spin->selectAll();
    // QAbstractSpinBox selects backwards, leaving its caret at the start. Blender
    // anchors the initial selection at the start and places the caret at the end.
    edit->setSelection(
      edit->selectionStart(), static_cast<int>(edit->selectedText().size()));
    if (m_state == State::MultiEditing)
    {
      mirrorText();
    }
  }

  void mirrorText()
  {
    // Blender's widget_draw_text uses the primary field's edit string for all
    // multi-edit fields. Keep incomplete numbers intact until confirmation.
    const auto text = m_controls[m_primary]->findChild<QLineEdit*>()->text();
    for (const auto i : m_selected)
    {
      if (i != m_primary)
      {
        auto* spin = m_controls[i];
        auto* edit = spin->findChild<QLineEdit*>();
        const QSignalBlocker spinBlocker{spin};
        const QSignalBlocker editBlocker{edit};
        edit->setText(text);
      }
    }
  }

  void commitEditing()
  {
    if (!m_controls[m_primary]->hasAcceptableInput())
    {
      finish(false);
      return;
    }
    if (m_state == State::MultiEditing)
    {
      commitText();
    }
    else
    {
      m_controls[m_primary]->interpretText();
      finish(true);
    }
  }

  void exitTextEditing(size_t index)
  {
    m_controls[index]->findChild<QLineEdit*>()->deselect();
    m_controls[index]->clearFocus();
    m_finishEditing();
  }

  size_t findControl(QObject* obj) const
  {
    for (size_t i = 0; i < m_controls.size(); ++i)
    {
      if (obj == m_controls[i] || obj == m_controls[i]->findChild<QLineEdit*>())
      {
        return i;
      }
    }
    return 0;
  }

  void selectChannels(const QPoint& pos)
  {
    m_selected.clear();
    const auto segment = QLineF{m_pressPos, pos};
    for (size_t i = 0; i < m_controls.size(); ++i)
    {
      auto* spin = m_controls[i];
      const auto rect =
        QRectF{QPointF{spin->mapToGlobal(QPoint{})}, QSizeF{spin->size()}};
      const auto intersects = [&]() {
        if (rect.contains(segment.p1()) || rect.contains(segment.p2()))
        {
          return true;
        }
        for (const auto& edge :
             {QLineF{rect.topLeft(), rect.topRight()},
              QLineF{rect.topRight(), rect.bottomRight()},
              QLineF{rect.bottomRight(), rect.bottomLeft()},
              QLineF{rect.bottomLeft(), rect.topLeft()}})
        {
          if (segment.intersects(edge) == QLineF::BoundedIntersection)
          {
            return true;
          }
        }
        return false;
      };
      if (spin->isEnabled() && (i == m_primary || intersects()))
      {
        m_selected.push_back(i);
      }
      highlight(spin, std::ranges::find(m_selected, i) != m_selected.end());
    }
  }

  void beginScrub()
  {
    m_state = State::Scrubbing;
    QGuiApplication::setOverrideCursor(Qt::SizeHorCursor);
    m_cursorOverridden = true;
    m_map.startTransaction("Edit Transform", mdl::TransactionScope::LongRunning);
    m_transaction = true;
  }

  void scrub(int dx, Qt::KeyboardModifiers modifiers)
  {
    const auto sensitivity = m_mode == Mode::Scale      ? 0.005
                             : m_mode == Mode::Rotation ? 0.5
                                                        : 1.0;
    const auto precision = modifiers.testFlag(Qt::ShiftModifier) ? 0.1 : 1.0;
    const auto snap = (m_mode == Mode::Scale      ? 0.1
                       : m_mode == Mode::Rotation ? 5.0
                                                  : m_map.grid().actualSize())
                      * precision;
    auto* primary = m_controls[m_primary];
    m_dragValues[m_primary] = std::clamp(
      m_dragValues[m_primary] + dx * sensitivity * precision,
      primary->minimum(),
      primary->maximum());
    auto primaryValue = m_dragValues[m_primary];
    if (modifiers.testFlag(Qt::ControlModifier))
    {
      primaryValue = std::round(primaryValue / snap) * snap;
    }
    primaryValue = std::clamp(primaryValue, primary->minimum(), primary->maximum());
    const auto delta = primaryValue - m_startValues[m_primary];
    for (const auto i : m_selected)
    {
      auto* spin = m_controls[i];
      if (!spin->isEnabled())
      {
        continue;
      }
      const auto value = i == m_primary ? primaryValue
                         : m_mode == Mode::Scale && m_startValues[m_primary] != 0.0
                           ? m_startValues[i] * (primaryValue / m_startValues[m_primary])
                           : m_startValues[i] + delta;
      const QSignalBlocker blocker{spin};
      m_dragChanged |=
        spin->value() != std::clamp(value, spin->minimum(), spin->maximum());
      spin->setValue(value);
    }
    m_apply(m_primary);
    // Uniform scaling can update the other channels; retain their displayed values.
    for (size_t i = 0; i < m_controls.size(); ++i)
    {
      if (std::ranges::find(m_selected, i) == m_selected.end())
      {
        m_dragValues[i] = m_controls[i]->value();
      }
    }
  }

  void commitText()
  {
    auto* primary = m_controls[m_primary];
    {
      const QSignalBlocker blocker{primary};
      primary->interpretText();
    }
    const auto value = primary->value();
    for (const auto i : m_selected)
    {
      const QSignalBlocker blocker{m_controls[i]};
      m_controls[i]->setValue(value);
    }
    m_state = State::Scrubbing; // Keep model notifications from replacing pending values.
    m_apply(m_primary);
    finish(true);
  }

  void releaseMouse()
  {
    if (m_grabbedWidget)
    {
      m_grabbedWidget->releaseMouse();
      m_grabbedWidget = nullptr;
    }
    if (m_cursorOverridden)
    {
      QGuiApplication::restoreOverrideCursor();
      m_cursorOverridden = false;
    }
  }

  void finish(bool commit)
  {
    releaseMouse();
    if (m_transaction)
    {
      m_transaction = false;
      if (commit)
      {
        m_map.commitTransaction();
      }
      else
      {
        m_map.cancelTransaction();
      }
    }
    if (!commit)
    {
      for (size_t i = 0; i < m_startValues.size(); ++i)
      {
        const QSignalBlocker blocker{m_controls[i]};
        m_controls[i]->setValue(m_startValues[i]);
      }
    }
    m_state = State::Idle;
    m_consumeRelease = false;
    for (auto* spin : m_controls)
    {
      highlight(spin, false);
      spin->findChild<QLineEdit*>()->setCursor(Qt::SizeHorCursor);
    }
    m_refresh();
  }

  static void highlight(SpinControl* spin, bool selected)
  {
    if (spin->property("multiSelected").toBool() == selected)
    {
      return;
    }
    spin->setProperty("multiSelected", selected);
    spin->style()->unpolish(spin);
    spin->style()->polish(spin);
    spin->update();
  }
};

// Keep the requested scale precision without changing other numeric editors.
class TransformSpinControl : public SpinControl
{
  bool m_fixedPrecision;

  std::optional<double> evaluateText(QString text) const
  {
    if (!prefix().isEmpty() && text.startsWith(prefix()))
    {
      text.remove(0, prefix().size());
    }
    if (!suffix().isEmpty() && text.endsWith(suffix()))
    {
      text.chop(suffix().size());
    }
    text = text.trimmed();
    if (text.isEmpty() || text.size() > 256)
    {
      return std::nullopt;
    }
    auto ok = false;
    const auto number = locale().toDouble(text, &ok);
    if (ok)
    {
      return std::isfinite(number) ? std::optional{number} : std::nullopt;
    }
    text.replace(locale().decimalPoint(), ".");
    // Reuse TB's expression parser for arithmetic, preserving incomplete input
    // until confirmation. Other EL constructs are not numeric field syntax.
    if (text.contains("//"))
    {
      return std::nullopt;
    }
    // EL accepts unfinished exponents and falls back to zero on literal overflow.
    // Check the numeric tokens before parsing so neither can change the geometry.
    static const auto tokens = QRegularExpression{
      QStringLiteral(R"(((?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)|[()+*/\s-])")};
    auto matches = tokens.globalMatch(text);
    auto end = qsizetype{0};
    while (matches.hasNext())
    {
      const auto match = matches.next();
      if (match.capturedStart() != end)
      {
        return std::nullopt;
      }
      end = match.capturedEnd();
      if (match.hasCaptured(1))
      {
        const auto literal = match.captured(1).toDouble(&ok);
        if (!ok || !std::isfinite(literal))
        {
          return std::nullopt;
        }
      }
    }
    if (end != text.size())
    {
      return std::nullopt;
    }
    const auto expression =
      el::parseExpression(el::ParseMode::Strict, text.toStdString());
    if (!expression)
    {
      return std::nullopt;
    }
    const auto result = el::withEvaluationContext([&](auto& context) {
      const auto value = expression.value().evaluate(context);
      return value.hasType(el::ValueType::Number) ? std::optional{value.numberValue()}
                                                  : std::nullopt;
    });
    if (!result || !result.value() || !std::isfinite(*result.value()))
    {
      return std::nullopt;
    }
    return result.value();
  }

public:
  TransformSpinControl(bool fixedPrecision, QWidget* parent)
    : SpinControl{parent}
    , m_fixedPrecision{fixedPrecision}
  {
    lineEdit()->setMaxLength(256);
  }

  QValidator::State validate(QString& text, int&) const override
  {
    // Intermediate lets users type parentheses, operators and temporarily invalid
    // expressions without QLineEdit silently dropping part of the formula.
    return evaluateText(text) ? QValidator::Acceptable : QValidator::Intermediate;
  }

  double valueFromText(const QString& text) const override
  {
    const auto result = evaluateText(text);
    return result ? std::clamp(*result, minimum(), maximum()) : value();
  }
  QString textFromValue(double value) const override
  {
    return m_fixedPrecision ? QDoubleSpinBox::textFromValue(value)
                            : SpinControl::textFromValue(value);
  }
};

TransformCard::TransformCard(MapDocument& document, QWidget* parent)
  : CollapsibleTitledPanel{tr("Transform"), true, parent}
  , m_document{document}
{
  setObjectName("TransformCard");
  setAttribute(Qt::WA_StyledBackground, true);

  createGui();
  connectObservers();
  updateFromSelection();
}

TransformCard::~TransformCard()
{
  // QAbstractSpinBox can interpret pending text while QWidget hides its children.
  // The card's members must no longer receive edits during base-class destruction.
  m_updatingFromModel = true;
  for (auto* spin : findChildren<SpinControl*>())
  {
    spin->blockSignals(true);
  }
  m_notifierConnection = {};
  m_locDragController.reset();
  m_rotDragController.reset();
  m_scaleDragController.reset();
}

TransformCard::RotationMode TransformCard::rotationMode() const
{
  return m_rotMode;
}

void TransformCard::setRotationMode(const RotationMode mode)
{
  if (m_rotMode != mode)
  {
    m_rotMode = mode;
    const QSignalBlocker blocker{m_rotModeCombo};
    m_rotModeCombo->setCurrentIndex(static_cast<int>(mode));
    updateFromSelection();
  }
}

bool TransformCard::uniformScale() const
{
  return m_uniformScale;
}

void TransformCard::setUniformScale(const bool uniform)
{
  if (m_uniformScale != uniform)
  {
    m_uniformScale = uniform;
    const QSignalBlocker blocker{m_uniformScaleButton};
    m_uniformScaleButton->setChecked(uniform);
    m_uniformScaleButton->setIcon(
      loadSVGIcon(uniform ? "ProportionalScale_on.svg" : "ProportionalScale_off.svg"));
  }
}

const vm::vec3d& TransformCard::currentLocation() const
{
  return m_currentLocation;
}

const vm::vec3d& TransformCard::currentRotation() const
{
  return m_currentRotation;
}

const vm::vec3d& TransformCard::currentScale() const
{
  return m_currentScale;
}

void TransformCard::createGui()
{
  auto* panel = getPanel();
  panel->setAttribute(Qt::WA_StyledBackground, true);

  auto* panelLayout = new QVBoxLayout{panel};
  panelLayout->setContentsMargins(12, 8, 10, 10);
  panelLayout->setSpacing(4);

  auto* grid = new QGridLayout{};
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(6);
  grid->setVerticalSpacing(0);

  auto createSpin = [this](
                      const double minVal,
                      const double maxVal,
                      const int minDigits,
                      const int maxDigits,
                      const double regInc,
                      const double shiftInc,
                      const double ctrlInc,
                      const QString& suffix = QString{}) {
    auto* spin = new TransformSpinControl{minDigits == 3, this};
    spin->setProperty("tbControlRole", "vector");
    spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spin->setAlignment(Qt::AlignCenter);
    spin->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    spin->setFocusPolicy(Qt::StrongFocus);
    spin->setRange(minVal, maxVal);
    spin->setDigits(minDigits, maxDigits);
    spin->setIncrements(regInc, shiftInc, ctrlInc);
    if (!suffix.isEmpty())
    {
      spin->setSuffix(suffix);
    }
    spin->setKeyboardTracking(false);
    spin->setToolTip(
      tr("Click to edit; drag horizontally to adjust.\n"
         "Drag vertically across axes to edit together.\n"
         "Enter arithmetic, for example (32 + 16) / 2.\n"
         "Shift: precision; Ctrl: snap; Esc or right-click: cancel."));
    return spin;
  };

  auto createLock = [this](const QString& tip, SpinControl* target) {
    auto* btn = createBitmapToggleButton("Lock_off.svg", tip, this);
    btn->setProperty("tbControlRole", "compact");
    btn->setIconSize(QSize{16, 16});
    btn->setAccessibleName(tip);
    connect(btn, &QAbstractButton::toggled, this, [btn, target](const bool locked) {
      btn->setIcon(loadSVGIcon(locked ? "Lock_on.svg" : "Lock_off.svg"));
      target->setEnabled(!locked);
      btn->setAccessibleDescription(locked ? tr("Locked in this panel") : tr("Unlocked"));
    });
    return btn;
  };

  auto createLabel = [this](const QString& text) {
    auto* label = new QLabel{text, this};
    label->setProperty("tbTextRole", "secondary");
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return label;
  };

  // Location
  m_posX = createSpin(-1000000.0, 1000000.0, 0, 3, 1.0, 8.0, 0.1);
  m_posX->setObjectName("TransformCard_PosX");
  m_posY = createSpin(-1000000.0, 1000000.0, 0, 3, 1.0, 8.0, 0.1);
  m_posY->setObjectName("TransformCard_PosY");
  m_posZ = createSpin(-1000000.0, 1000000.0, 0, 3, 1.0, 8.0, 0.1);
  m_posZ->setObjectName("TransformCard_PosZ");
  m_lockLocX = createLock(tr("Lock X Location"), m_posX);
  m_lockLocY = createLock(tr("Lock Y Location"), m_posY);
  m_lockLocZ = createLock(tr("Lock Z Location"), m_posZ);

  grid->addWidget(createLabel(tr("Location X")), 0, 0);
  grid->addWidget(m_posX, 0, 1);
  grid->addWidget(m_lockLocX, 0, 2);

  grid->addWidget(createLabel(tr("Y")), 1, 0);
  grid->addWidget(m_posY, 1, 1);
  grid->addWidget(m_lockLocY, 1, 2);

  grid->addWidget(createLabel(tr("Z")), 2, 0);
  grid->addWidget(m_posZ, 2, 1);
  grid->addWidget(m_lockLocZ, 2, 2);

  // Rotation
  m_rotX = createSpin(-3600.0, 3600.0, 0, 2, 15.0, 90.0, 1.0, QStringLiteral("°"));
  m_rotX->setObjectName("TransformCard_RotX");
  m_rotY = createSpin(-3600.0, 3600.0, 0, 2, 15.0, 90.0, 1.0, QStringLiteral("°"));
  m_rotY->setObjectName("TransformCard_RotY");
  m_rotZ = createSpin(-3600.0, 3600.0, 0, 2, 15.0, 90.0, 1.0, QStringLiteral("°"));
  m_rotZ->setObjectName("TransformCard_RotZ");
  m_lockRotX = createLock(tr("Lock X Rotation"), m_rotX);
  m_lockRotY = createLock(tr("Lock Y Rotation"), m_rotY);
  m_lockRotZ = createLock(tr("Lock Z Rotation"), m_rotZ);

  grid->addWidget(createLabel(tr("Rotation X")), 4, 0);
  grid->addWidget(m_rotX, 4, 1);
  grid->addWidget(m_lockRotX, 4, 2);

  grid->addWidget(createLabel(tr("Y")), 5, 0);
  grid->addWidget(m_rotY, 5, 1);
  grid->addWidget(m_lockRotY, 5, 2);

  grid->addWidget(createLabel(tr("Z")), 6, 0);
  grid->addWidget(m_rotZ, 6, 1);
  grid->addWidget(m_lockRotZ, 6, 2);

  // Rotation mode
  m_rotModeCombo = new QComboBox{this};
  m_rotModeCombo->addItem(tr("XYZ Euler"));
  m_rotModeCombo->addItem(tr("Quake (Pitch Yaw Roll)"));
  m_rotModeCombo->addItem(tr("Yaw Only (angle)"));
  m_rotModeCombo->setCurrentIndex(static_cast<int>(m_rotMode));

  m_rotModeCombo->setObjectName("TransformCard_RotationMode");
  m_rotModeCombo->setAccessibleName(tr("Rotation mode"));
  m_rotModeCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  m_rotModeCombo->setMinimumContentsLength(8);
  grid->addWidget(createLabel(tr("Mode")), 8, 0);
  grid->addWidget(m_rotModeCombo, 8, 1);

  // Scale
  m_scaleX = createSpin(0.001, 1000.0, 3, 3, 0.1, 1.0, 0.01);
  m_scaleX->setObjectName("TransformCard_ScaleX");
  m_scaleY = createSpin(0.001, 1000.0, 3, 3, 0.1, 1.0, 0.01);
  m_scaleY->setObjectName("TransformCard_ScaleY");
  m_scaleZ = createSpin(0.001, 1000.0, 3, 3, 0.1, 1.0, 0.01);
  m_scaleZ->setObjectName("TransformCard_ScaleZ");
  m_lockScaleX = createLock(tr("Lock X Scale"), m_scaleX);
  m_lockScaleY = createLock(tr("Lock Y Scale"), m_scaleY);
  m_lockScaleZ = createLock(tr("Lock Z Scale"), m_scaleZ);

  m_uniformScaleButton = createBitmapToggleButton(
    "ProportionalScale_off.svg",
    tr("Uniform scale: preserve proportions of unlocked axes"),
    this);
  m_uniformScaleButton->setProperty("tbControlRole", "compact");
  m_uniformScaleButton->setObjectName("TransformCard_UniformScale");
  m_uniformScaleButton->setAccessibleName(tr("Uniform scale"));
  m_uniformScaleButton->setIconSize(QSize{16, 16});
  m_uniformScaleButton->setChecked(m_uniformScale);

  connect(
    m_uniformScaleButton, &QAbstractButton::toggled, this, [this](const bool checked) {
      m_uniformScale = checked;
      m_uniformScaleButton->setIcon(
        loadSVGIcon(checked ? "ProportionalScale_on.svg" : "ProportionalScale_off.svg"));
    });

  auto* scaleLabel = new QWidget{this};
  auto* scaleLabelLayout = new QHBoxLayout{scaleLabel};
  scaleLabelLayout->setContentsMargins(0, 0, 0, 0);
  scaleLabelLayout->setSpacing(4);
  scaleLabelLayout->addStretch();
  scaleLabelLayout->addWidget(m_uniformScaleButton);
  scaleLabelLayout->addWidget(createLabel(tr("Scale X")));
  grid->addWidget(scaleLabel, 10, 0);
  grid->addWidget(m_scaleX, 10, 1);
  grid->addWidget(m_lockScaleX, 10, 2);

  grid->addWidget(createLabel(tr("Y")), 11, 0);
  grid->addWidget(m_scaleY, 11, 1);
  grid->addWidget(m_lockScaleY, 11, 2);

  grid->addWidget(createLabel(tr("Z")), 12, 0);
  grid->addWidget(m_scaleZ, 12, 1);
  grid->addWidget(m_lockScaleZ, 12, 2);

  grid->setRowMinimumHeight(3, 8);
  grid->setRowMinimumHeight(7, 4);
  grid->setRowMinimumHeight(9, 8);
  grid->setColumnMinimumWidth(0, 90);
  for (const auto controls :
       {std::array{m_posX, m_posY, m_posZ},
        std::array{m_rotX, m_rotY, m_rotZ},
        std::array{m_scaleX, m_scaleY, m_scaleZ}})
  {
    for (size_t i = 0; i < controls.size(); ++i)
    {
      controls[i]->setAccessibleName((controls[0] == m_posX   ? tr("Location %1")
                                      : controls[0] == m_rotX ? tr("Rotation %1")
                                                              : tr("Scale %1"))
                                       .arg(QString{"XYZ"}.at(static_cast<int>(i))));
      controls[i]->setProperty(
        "tbVectorPosition",
        i == 0   ? "first"
        : i == 2 ? "last"
                 : "middle");
    }
  }
  m_lockLocX->setObjectName("TransformCard_LockPosX");
  m_lockLocY->setObjectName("TransformCard_LockPosY");
  m_lockLocZ->setObjectName("TransformCard_LockPosZ");
  m_lockScaleX->setObjectName("TransformCard_LockScaleX");
  m_lockScaleY->setObjectName("TransformCard_LockScaleY");
  m_lockScaleZ->setObjectName("TransformCard_LockScaleZ");

  grid->setColumnStretch(0, 0);
  grid->setColumnStretch(1, 1);
  grid->setColumnStretch(2, 0);

  panelLayout->addLayout(grid);

  m_locDragController = std::make_unique<VectorDragGroupController>(
    std::vector<SpinControl*>{m_posX, m_posY, m_posZ},
    m_document.map(),
    VectorDragGroupController::Mode::Location,
    [this](size_t) { onLocationChanged(); },
    [this] { updateFromSelection(); },
    [this] { emit editingFinished(); },
    this);
  m_rotDragController = std::make_unique<VectorDragGroupController>(
    std::vector<SpinControl*>{m_rotX, m_rotY, m_rotZ},
    m_document.map(),
    VectorDragGroupController::Mode::Rotation,
    [this](size_t) { onRotationChanged(); },
    [this] { updateFromSelection(); },
    [this] { emit editingFinished(); },
    this);
  m_scaleDragController = std::make_unique<VectorDragGroupController>(
    std::vector<SpinControl*>{m_scaleX, m_scaleY, m_scaleZ},
    m_document.map(),
    VectorDragGroupController::Mode::Scale,
    [this](size_t axis) { onScaleChanged(axis); },
    [this] { updateFromSelection(); },
    [this] { emit editingFinished(); },
    this);

  // Wire normal text edit signals
  auto connectSpin = [this](SpinControl* spin, auto func) {
    connect(
      spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [func](double) {
        func();
      });
  };

  connectSpin(m_posX, [this]() { onLocationChanged(); });
  connectSpin(m_posY, [this]() { onLocationChanged(); });
  connectSpin(m_posZ, [this]() { onLocationChanged(); });

  connectSpin(m_rotX, [this]() { onRotationChanged(); });
  connectSpin(m_rotY, [this]() { onRotationChanged(); });
  connectSpin(m_rotZ, [this]() { onRotationChanged(); });

  connect(
    m_rotModeCombo,
    QOverload<int>::of(&QComboBox::currentIndexChanged),
    this,
    &TransformCard::onRotationModeChanged);

  connectSpin(m_scaleX, [this]() { onScaleChanged(0); });
  connectSpin(m_scaleY, [this]() { onScaleChanged(1); });
  connectSpin(m_scaleZ, [this]() { onScaleChanged(2); });
}

void TransformCard::connectObservers()
{
  m_notifierConnection += m_document.selectionDidChangeNotifier.connect(
    [this](const auto&) { updateFromSelection(); });
  m_notifierConnection += m_document.nodesDidChangeNotifier.connect(
    [this](const auto&) { updateFromSelection(); });
  m_notifierConnection +=
    m_document.documentDidChangeNotifier.connect([this]() { updateFromSelection(); });
  m_notifierConnection += m_document.transactionDoneNotifier.connect(
    [this](const auto&, bool, bool) { updateFromSelection(); });
  m_notifierConnection += m_document.transactionUndoneNotifier.connect(
    [this](const auto&, bool, bool) { updateFromSelection(); });
}

void TransformCard::updateFromSelection()
{
  if (
    m_updatingFromModel || (m_locDragController && m_locDragController->isInteracting())
    || (m_rotDragController && m_rotDragController->isInteracting())
    || (m_scaleDragController && m_scaleDragController->isInteracting()))
  {
    return;
  }


  m_updatingFromModel = true;

  auto& map = m_document.map();
  const auto& selection = map.selection();
  if (!selection.hasNodes())
  {
    setVisible(false);
    m_updatingFromModel = false;
    return;
  }

  const auto entityNodes = selection.allEntities();
  const auto isSinglePointEntity =
    (entityNodes.size() == 1 && entityNodes.front()->entity().pointEntity()
     && entityNodes.front()->entity().classname()
          != mdl::EntityPropertyValues::WorldspawnClassname);

  setVisible(true);

  if (isSinglePointEntity)
  {
    const auto* entityNode = entityNodes.front();
    const auto& entity = entityNode->entity();

    m_currentLocation = entity.origin();

    m_currentRotation = vm::vec3d{0, 0, 0};
    if (const auto* anglesValue = entity.property(mdl::EntityPropertyKeys::Angles))
    {
      const auto parsed = vm::parse<double, 3>(*anglesValue);
      if (parsed)
      {
        const auto pitch = parsed->x();
        const auto yaw = parsed->y();
        const auto roll = parsed->z();
        if (m_rotMode == RotationMode::XyzEuler)
        {
          m_currentRotation = vm::vec3d{roll, pitch, yaw};
        }
        else if (m_rotMode == RotationMode::QuakeEuler)
        {
          m_currentRotation = vm::vec3d{pitch, yaw, roll};
        }
        else
        {
          m_currentRotation = vm::vec3d{0, 0, yaw};
        }
      }
    }
    else if (const auto* angleValue = entity.property(mdl::EntityPropertyKeys::Angle))
    {
      const auto parsed = kdl::str_to_double(*angleValue);
      if (parsed)
      {
        m_currentRotation = m_rotMode == RotationMode::QuakeEuler
                              ? vm::vec3d{0, *parsed, 0}
                              : vm::vec3d{0, 0, *parsed};
      }
    }

    m_currentScale = vm::vec3d{1, 1, 1};
    if (const auto* scaleValue = entity.property("scale"))
    {
      const auto parsedVec = vm::parse<double, 3>(*scaleValue);
      if (parsedVec)
      {
        m_currentScale = *parsedVec;
      }
      else
      {
        const auto parsedFloat = kdl::str_to_double(*scaleValue);
        if (parsedFloat && *parsedFloat > 0.0)
        {
          m_currentScale = vm::vec3d{*parsedFloat, *parsedFloat, *parsedFloat};
        }
      }
    }
  }
  else
  {
    m_currentLocation = map.selectionBounds().value_or(vm::bbox3d{}).center();
    m_currentRotation = vm::vec3d{0, 0, 0};
    m_currentScale = vm::vec3d{1, 1, 1};
  }

  {
    const QSignalBlocker bX{m_posX};
    const QSignalBlocker bY{m_posY};
    const QSignalBlocker bZ{m_posZ};
    m_posX->setValue(m_currentLocation.x());
    m_posY->setValue(m_currentLocation.y());
    m_posZ->setValue(m_currentLocation.z());
  }

  {
    const QSignalBlocker bX{m_rotX};
    const QSignalBlocker bY{m_rotY};
    const QSignalBlocker bZ{m_rotZ};
    m_rotX->setValue(m_currentRotation.x());
    m_rotY->setValue(m_currentRotation.y());
    m_rotZ->setValue(m_currentRotation.z());
  }

  {
    const QSignalBlocker bX{m_scaleX};
    const QSignalBlocker bY{m_scaleY};
    const QSignalBlocker bZ{m_scaleZ};
    m_scaleX->setValue(m_currentScale.x());
    m_scaleY->setValue(m_currentScale.y());
    m_scaleZ->setValue(m_currentScale.z());
  }

  const auto yawOnly = m_rotMode == RotationMode::YawOnly;
  m_rotX->setEnabled(!yawOnly && !m_lockRotX->isChecked());
  m_rotY->setEnabled(!yawOnly && !m_lockRotY->isChecked());
  m_lockRotX->setEnabled(!yawOnly);
  m_lockRotY->setEnabled(!yawOnly);
  m_rotModeCombo->setEnabled(isSinglePointEntity);
  m_updatingFromModel = false;
}

void TransformCard::onLocationChanged()
{
  if (m_updatingFromModel || m_locDragController->isMultiEditing())
  {
    return;
  }
  const auto newLoc = vm::vec3d{m_posX->value(), m_posY->value(), m_posZ->value()};
  applyLocation(newLoc);
}

void TransformCard::onRotationChanged()
{
  if (m_updatingFromModel || m_rotDragController->isMultiEditing())
  {
    return;
  }
  const auto newRot = vm::vec3d{m_rotX->value(), m_rotY->value(), m_rotZ->value()};
  applyRotation(newRot);
}

void TransformCard::onScaleChanged(const size_t axis)
{
  if (m_updatingFromModel || m_scaleDragController->isMultiEditing())
  {
    return;
  }
  const auto controls = std::array<SpinControl*, 3>{m_scaleX, m_scaleY, m_scaleZ};
  auto newScale = vm::vec3d{m_scaleX->value(), m_scaleY->value(), m_scaleZ->value()};
  if (m_uniformScale && !m_scaleDragController->hasMultipleChannels())
  {
    const auto factor =
      m_currentScale[axis] != 0.0 ? newScale[axis] / m_currentScale[axis] : 1.0;
    for (size_t i = 0; i < controls.size(); ++i)
    {
      newScale[i] =
        controls[i]->isEnabled() ? m_currentScale[i] * factor : m_currentScale[i];
      const QSignalBlocker blocker{controls[i]};
      controls[i]->setValue(newScale[i]);
      newScale[i] = controls[i]->value();
    }
  }

  applyScale(newScale);
}

void TransformCard::onRotationModeChanged(const int index)
{
  m_rotMode = static_cast<RotationMode>(index);
  updateFromSelection();
}

void TransformCard::applyLocation(const vm::vec3d& newLoc)
{
  if (m_updatingFromModel)
  {
    return;
  }

  const QScopedValueRollback updating{m_updatingFromModel, true};

  auto& map = m_document.map();
  const auto& selection = map.selection();
  if (!selection.hasNodes())
  {
    return;
  }

  const auto entityNodes = selection.allEntities();
  const auto isSinglePointEntity =
    (entityNodes.size() == 1 && entityNodes.front()->entity().pointEntity()
     && entityNodes.front()->entity().classname()
          != mdl::EntityPropertyValues::WorldspawnClassname);

  if (isSinglePointEntity)
  {
    const auto originStr = kdl::str_to_string(vm::correct(newLoc.x())) + " "
                           + kdl::str_to_string(vm::correct(newLoc.y())) + " "
                           + kdl::str_to_string(vm::correct(newLoc.z()));
    if (!mdl::setEntityProperty(map, mdl::EntityPropertyKeys::Origin, originStr))
    {
      return;
    }
  }
  else
  {
    const auto delta = newLoc - m_currentLocation;
    if (!vm::is_zero(delta, 0.0001))
    {
      if (!mdl::translateSelection(map, delta))
      {
        return;
      }
    }
  }

  m_currentLocation = newLoc;
}

void TransformCard::applyRotation(const vm::vec3d& newRot)
{
  if (m_updatingFromModel)
  {
    return;
  }

  const QScopedValueRollback updating{m_updatingFromModel, true};

  auto& map = m_document.map();
  const auto& selection = map.selection();
  if (!selection.hasNodes())
  {
    return;
  }

  const auto entityNodes = selection.allEntities();
  const auto isSinglePointEntity =
    (entityNodes.size() == 1 && entityNodes.front()->entity().pointEntity()
     && entityNodes.front()->entity().classname()
          != mdl::EntityPropertyValues::WorldspawnClassname);

  if (vm::is_equal(newRot, m_currentRotation, 0.00001))
  {
    return;
  }
  auto transaction = mdl::Transaction{map, "Edit Rotation"};
  auto success = true;
  if (isSinglePointEntity)
  {
    if (m_rotMode == RotationMode::YawOnly)
    {
      success = mdl::setEntityProperty(
        map, mdl::EntityPropertyKeys::Angle, kdl::str_to_string(vm::correct(newRot.z())));
      if (
        success
        && entityNodes.front()->entity().property(mdl::EntityPropertyKeys::Angles))
      {
        success = mdl::removeEntityProperty(map, mdl::EntityPropertyKeys::Angles);
      }
    }
    else
    {
      const auto angles = m_rotMode == RotationMode::XyzEuler
                            ? vm::vec3d{newRot.y(), newRot.z(), newRot.x()}
                            : newRot;
      success = mdl::setEntityProperty(
        map, mdl::EntityPropertyKeys::Angles, kdl::str_to_string(vm::correct(angles)));
      if (
        success && entityNodes.front()->entity().property(mdl::EntityPropertyKeys::Angle))
      {
        success = mdl::removeEntityProperty(map, mdl::EntityPropertyKeys::Angle);
      }
    }
  }
  else if (const auto bounds = map.selectionBounds())
  {
    const auto delta = newRot - m_currentRotation;
    for (size_t axis = 0; axis < 3 && success; ++axis)
    {
      if (std::abs(delta[axis]) > 0.001)
      {
        success = mdl::rotateSelection(
          map, bounds->center(), vm::vec3d::axis(axis), vm::to_radians(delta[axis]));
      }
    }
  }
  if (!success)
  {
    transaction.cancel();
    return;
  }
  if (!transaction.commit())
  {
    return;
  }

  m_currentRotation = newRot;
}

void TransformCard::applyScale(const vm::vec3d& newScale)
{
  if (m_updatingFromModel)
  {
    return;
  }

  const QScopedValueRollback updating{m_updatingFromModel, true};

  auto& map = m_document.map();
  const auto& selection = map.selection();
  if (!selection.hasNodes())
  {
    return;
  }

  const auto entityNodes = selection.allEntities();
  const auto isSinglePointEntity =
    (entityNodes.size() == 1 && entityNodes.front()->entity().pointEntity()
     && entityNodes.front()->entity().classname()
          != mdl::EntityPropertyValues::WorldspawnClassname);

  if (isSinglePointEntity)
  {
    std::string scaleStr;
    if (
      std::abs(newScale.x() - newScale.y()) < 0.0001
      && std::abs(newScale.x() - newScale.z()) < 0.0001)
    {
      scaleStr = kdl::str_to_string(vm::correct(newScale.x()));
    }
    else
    {
      scaleStr = kdl::str_to_string(vm::correct(newScale.x())) + " "
                 + kdl::str_to_string(vm::correct(newScale.y())) + " "
                 + kdl::str_to_string(vm::correct(newScale.z()));
    }
    if (!mdl::setEntityProperty(map, "scale", scaleStr))
    {
      return;
    }
  }
  else
  {
    if (
      m_currentScale.x() > 0.0001 && m_currentScale.y() > 0.0001
      && m_currentScale.z() > 0.0001)
    {
      const auto factor = vm::vec3d{
        newScale.x() / m_currentScale.x(),
        newScale.y() / m_currentScale.y(),
        newScale.z() / m_currentScale.z()};
      if (!vm::is_equal(factor, vm::vec3d{1, 1, 1}, 0.0001))
      {
        if (const auto bounds = map.selectionBounds())
        {
          const auto center = bounds->center();
          if (!mdl::scaleSelection(map, center, factor))
          {
            return;
          }
        }
      }
    }
  }

  m_currentScale = newScale;
}

} // namespace tb::ui
