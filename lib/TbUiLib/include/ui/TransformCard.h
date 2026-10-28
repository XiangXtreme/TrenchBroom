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

#pragma once

#include "base/NotifierConnection.h"
#include "ui/CollapsibleTitledPanel.h"

#include "vm/vec.h"

#include <memory>

class QComboBox;
class QLabel;
class QToolButton;

namespace tb::ui
{
class MapDocument;
class SpinControl;
class VectorDragGroupController;

class TransformCard : public CollapsibleTitledPanel
{
  Q_OBJECT

public:
  enum class RotationMode
  {
    XyzEuler,
    QuakeEuler,
    YawOnly
  };

private:
  MapDocument& m_document;
  NotifierConnection m_notifierConnection;

  SpinControl* m_posX = nullptr;
  SpinControl* m_posY = nullptr;
  SpinControl* m_posZ = nullptr;
  QToolButton* m_lockLocX = nullptr;
  QToolButton* m_lockLocY = nullptr;
  QToolButton* m_lockLocZ = nullptr;

  SpinControl* m_rotX = nullptr;
  SpinControl* m_rotY = nullptr;
  SpinControl* m_rotZ = nullptr;
  QToolButton* m_lockRotX = nullptr;
  QToolButton* m_lockRotY = nullptr;
  QToolButton* m_lockRotZ = nullptr;
  QComboBox* m_rotModeCombo = nullptr;
  RotationMode m_rotMode = RotationMode::XyzEuler;

  SpinControl* m_scaleX = nullptr;
  SpinControl* m_scaleY = nullptr;
  SpinControl* m_scaleZ = nullptr;
  QToolButton* m_lockScaleX = nullptr;
  QToolButton* m_lockScaleY = nullptr;
  QToolButton* m_lockScaleZ = nullptr;
  QToolButton* m_uniformScaleButton = nullptr;
  bool m_uniformScale = false;

  std::unique_ptr<VectorDragGroupController> m_locDragController;
  std::unique_ptr<VectorDragGroupController> m_rotDragController;
  std::unique_ptr<VectorDragGroupController> m_scaleDragController;

  bool m_updatingFromModel = false;
  vm::vec3d m_currentLocation{0, 0, 0};
  vm::vec3d m_currentRotation{0, 0, 0};
  vm::vec3d m_currentScale{1, 1, 1};

public:
  explicit TransformCard(MapDocument& document, QWidget* parent = nullptr);
  ~TransformCard() override;

  RotationMode rotationMode() const;
  void setRotationMode(RotationMode mode);

  bool uniformScale() const;
  void setUniformScale(bool uniform);

  const vm::vec3d& currentLocation() const;
  const vm::vec3d& currentRotation() const;
  const vm::vec3d& currentScale() const;

  void updateFromSelection();

signals:
  void editingFinished();

private:
  void createGui();
  void connectObservers();

  void onLocationChanged();
  void onRotationChanged();
  void onScaleChanged(size_t axis);
  void onRotationModeChanged(int index);

  void applyLocation(const vm::vec3d& newLoc);
  void applyRotation(const vm::vec3d& newRot);
  void applyScale(const vm::vec3d& newScale);
};

} // namespace tb::ui
