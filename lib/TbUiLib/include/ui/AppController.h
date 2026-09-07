/*
 Copyright (C) 2025 Kristian Duske

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

#include <QObject>
#include <QString>
#include <QtSystemDetection>

#include "base/Result.h"

#include <filesystem>
#include <memory>

class QJsonObject;

class QMenu;
class QNetworkAccessManager;
class QOffscreenSurface;
class QOpenGLContext;
class QTimer;

namespace kdl
{
class task_manager;
}

namespace upd
{
class HttpClient;
class Updater;
} // namespace upd

namespace tb
{
namespace gl
{
class GlManager;
} // namespace gl

namespace mdl
{
class GameManager;
struct EnvironmentConfig;
} // namespace mdl

namespace ui
{
class AboutDialog;
class ActionManager;
class MapWindowManager;
class McpBridgeServer;
class McpHttpServer;
class RecentDocuments;
class WelcomeWindow;
namespace automation
{
class AutomationStateStore;
}

struct AppControllerOptions
{
  bool enableBackgroundServices = true;
  bool showMapWindows = true;
  bool enableGlResourceProcessing = true;
};

class AppController : public QObject
{
  Q_OBJECT
private:
  std::unique_ptr<kdl::task_manager> m_taskManager;
  std::unique_ptr<mdl::EnvironmentConfig> m_environmentConfig;
  std::unique_ptr<mdl::GameManager> m_gameManager;

  std::unique_ptr<gl::GlManager> m_glManager;

  QOpenGLContext* m_glContext = nullptr;
  QOffscreenSurface* m_offscreenSurface = nullptr;

  QNetworkAccessManager* m_networkManager = nullptr;
  QTimer* m_reloadRecentDocumentsTimer = nullptr;
  QTimer* m_processResourcesTimer = nullptr;

  upd::HttpClient* m_httpClient = nullptr;
  upd::Updater* m_updater = nullptr;

  MapWindowManager* m_mapWindowManager = nullptr;
  RecentDocuments* m_recentDocuments = nullptr;
  std::unique_ptr<ActionManager> m_actionManager;
  std::unique_ptr<automation::AutomationStateStore> m_automationState;
  std::unique_ptr<McpBridgeServer> m_mcpBridgeServer;
  std::unique_ptr<McpHttpServer> m_mcpHttpServer;
  QString m_mcpStartupError;
  std::unique_ptr<WelcomeWindow> m_welcomeWindow;
  std::unique_ptr<AboutDialog> m_aboutDialog;

public:
  AppController(
    std::unique_ptr<kdl::task_manager> taskManager,
    std::unique_ptr<mdl::EnvironmentConfig> environmentConfig,
    std::unique_ptr<mdl::GameManager> gameManager,
    AppControllerOptions options = {});

  static constexpr auto useSDI =
#if defined(Q_OS_WIN)
    true;
#else
    false;
#endif

  static Result<std::unique_ptr<AppController>> create(AppControllerOptions options = {});

  ~AppController() override;

  kdl::task_manager& taskManager();

  gl::GlManager& glManager();

  const mdl::EnvironmentConfig& environmentConfig() const;

  mdl::GameManager& gameManager();

  upd::Updater& updater();

  MapWindowManager& mapWindowManager();

  const RecentDocuments& recentDocuments() const;
  RecentDocuments& recentDocuments();

  ActionManager& actionManager();

  automation::AutomationStateStore& automationState();

  const QJsonObject& mcpOverlayState() const;
  void refreshMcpOverlayViews();
  void restartMcpBridge();
  bool mcpBridgeIsListening() const;
  bool mcpHttpServerIsListening() const;
  QString mcpHttpServerUrl() const;
  const QString& mcpStartupError() const;

  void askForAutoUpdates();
  void triggerAutoUpdateCheck();

  bool newDocument();
  void openDocument();
  bool openDocument(const std::filesystem::path& path);

  void showWelcomeWindow();

  void showManual();
  void showPreferences();
  void showPieMenuSettings();
  void showPythonPluginManager();
  void showAboutDialog();
  void debugShowCrashReportDialog();

private:
  void connectObservers();
  void startMcpBridge();

  void processGlResources();
};

} // namespace ui
} // namespace tb
