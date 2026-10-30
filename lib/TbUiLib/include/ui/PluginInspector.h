/*
 Copyright (C) 2010 Kristian Duske

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

#include <QPointer>

#include "ui/TabBook.h"

#include <vector>

class QLabel;
class QScrollArea;
class QVBoxLayout;
class QWidget;
class QString;

namespace tb::ui
{

class PluginInspector;

// A non-owning registration. The host owns the widgets; callers explicitly close
// registrations when their plugin session ends. Closing is safe after either the
// user or the host has already removed the panel.
class PluginPanelRegistration
{
  friend class PluginInspector;

private:
  QPointer<PluginInspector> m_host;
  QPointer<QWidget> m_panel;
  QPointer<QWidget> m_content;

  PluginPanelRegistration(PluginInspector& host, QWidget& panel, QWidget& content);

public:
  QWidget* content() const;
  void close() const;
};

class PluginInspector : public TabBookPage
{
  friend class PluginPanelRegistration;

private:
  QScrollArea* m_scrollArea = nullptr;
  QWidget* m_container = nullptr;
  QVBoxLayout* m_containerLayout = nullptr;
  QLabel* m_emptyLabel = nullptr;
  std::vector<QWidget*> m_panels;

public:
  explicit PluginInspector(QWidget* parent = nullptr);

  PluginPanelRegistration addPluginPanel(const QString& title);

private:
  void removePluginPanel(QWidget* panel);
  void updateEmptyState();
};

} // namespace tb::ui
