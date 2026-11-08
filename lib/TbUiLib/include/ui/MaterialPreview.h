/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once
#include "base/NotifierConnection.h"
#include "ui/RenderView.h"
namespace tb::gl
{
class Material;
}
namespace tb::ui
{
class MapDocument;
/** A small live preview of the selection's material, sharing the editor's GL resources.
 */
class MaterialPreview : public RenderView
{
  Q_OBJECT
private:
  MapDocument& m_document;
  NotifierConnection m_connection;

public:
  MaterialPreview(
    AppController& appController, MapDocument& document, QWidget* parent = nullptr);
  const gl::Material* material() const;

private:
  void processEvent(const KeyEvent&) override {}
  void processEvent(const MouseEvent&) override {}
  void processEvent(const ScrollEvent&) override {}
  void processEvent(const GestureEvent&) override {}
  void processEvent(const CancelEvent&) override {}
  bool shouldRenderFocusIndicator() const override;
  Color getBackgroundColor() override;
  void renderContents(gl::Gl& gl) override;
};
} // namespace tb::ui
