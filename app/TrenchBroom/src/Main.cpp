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

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDebug>
#include <QFileInfo>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QString>
#include <QTemporaryDir>
#include <QTranslator>
#include <QtGlobal>

#include "ApplicationStyle.h"
#include "UiSnapshotRunner.h"
#include "base/PreferenceManager.h"
#include "prefs/Preferences.h"
#include "ui/Action.h"
#include "ui/ActionBuilder.h"
#include "ui/ActionExecutionContext.h"
#include "ui/AppController.h"
#include "ui/Contracts.h"
#include "ui/CrashReporter.h"
#include "ui/FileEventFilter.h"
#include "ui/GlFunctions.h"
#include "ui/QPathUtils.h"
#include "ui/QPreferenceStore.h"
#include "ui/RecentDocuments.h"
#include "ui/SystemPaths.h"
#include "ui/ThemeRegistry.h"

#include <cstring>
#include <memory>
#include <optional>
#include <unordered_map>

using namespace tb;
using namespace tb::ui;

static_assert(
  QT_VERSION >= QT_VERSION_CHECK(6, 8, 0), "TrenchBroom requires Qt 6.8.0 or later");

namespace
{

struct CommandLineOptions
{
  bool enableDraftReleaseUpdates = false;
  QStringList fileNames;
  std::optional<UiSnapshotCommandLineOptions> uiSnapshot;
};

void loadTranslations(QApplication& app)
{
  if (pref(Preferences::Language) != Preferences::languageChinese())
  {
    return;
  }

  static auto trenchBroomTranslator = QTranslator{};
  if (trenchBroomTranslator.load(":/translations/trenchbroom_zh_CN"))
  {
    app.installTranslator(&trenchBroomTranslator);
  }
}

auto createAppController(const bool snapshotMode = false)
{
  const auto options = AppControllerOptions{!snapshotMode, !snapshotMode, true};
  return AppController::create(options) | kdl::if_error([](auto e) {
           const auto msg =
             fmt::format(R"(Game configurations could not be loaded: {})", e.msg);

           QMessageBox::critical(
             nullptr, "TrenchBroom", QString::fromStdString(msg), QMessageBox::Ok);
           QCoreApplication::exit(1);
         })
         | kdl::value();
}

[[maybe_unused]] void populateMainMenu(AppController& appController)
{
  auto* menuBar = new QMenuBar{};
  auto actionMap = std::unordered_map<const Action*, QAction*>{};

  auto menuBuilderResult = populateMenuBar(
    appController.actionManager(), *menuBar, actionMap, [&](const Action& action) {
      auto context = ActionExecutionContext{appController, nullptr, nullptr};
      action.execute(context);
    });

  appController.recentDocuments().addMenu(*menuBuilderResult.recentDocumentsMenu);

  auto context = ActionExecutionContext{appController, nullptr, nullptr};
  for (auto [tbAction, qtAction] : actionMap)
  {
    qtAction->setEnabled(tbAction->enabled(context));
    if (qtAction->isCheckable())
    {
      qtAction->setChecked(tbAction->checked(context));
    }
  }
}

[[maybe_unused]] void installFileEventFilter(AppController& appController)
{
  qApp->installEventFilter(new FileEventFilter{appController, qApp});
}

bool openFiles(AppController& appController, const QStringList& fileNames)
{
  const auto filesToOpen = AppController::useSDI && !fileNames.empty()
                             ? QStringList{fileNames.front()}
                             : fileNames;

  auto anyDocumentOpened = false;
  for (const auto& fileName : filesToOpen)
  {
    const auto path = ui::pathFromQString(fileName);
    if (!path.empty() && appController.openDocument(path))
    {
      anyDocumentOpened = true;
    }
  }

  return anyDocumentOpened;
}

std::optional<CommandLineOptions> parseCommandLine(QApplication& app)
{
  auto parser = QCommandLineParser{};
  parser.setApplicationDescription("TrenchBroom level editor");
  parser.addHelpOption();

  const auto portableOption = QCommandLineOption{
    QStringList{"portable"}, "Store application settings next to the executable."};
  const auto draftUpdatesOption = QCommandLineOption{
    QStringList{"enableDraftReleaseUpdates"}, "Enable draft release updates."};
  const auto uiSnapshotOption = QCommandLineOption{
    QStringList{"ui-snapshot"},
    "Capture a deterministic application surface and exit.",
    "path"};
  const auto uiSnapshotThemeOption = QCommandLineOption{
    QStringList{"ui-snapshot-theme"},
    "Override the snapshot theme by ID (system/light/dark/blender aliases supported).",
    "theme",
    "system"};
  const auto uiSnapshotPageOption = QCommandLineOption{
    QStringList{"ui-snapshot-page"},
    "Select the surface to capture: map, outliner, "
    "outliner-hierarchy, outliner-filter, outliner-properties-entity, "
    "outliner-reparent-layer-before, outliner-reparent-layer-after, "
    "outliner-brush-entity-before, outliner-brush-entity-after, entity-browser, "
    "entity-browser-empty, face-inspector, material-browser-empty, plugin-inspector, "
    "supporting, path-tool-preview, python-console, command-palette, components, "
    "preferences, "
    "preferences-colors, preferences-mouse, preferences-keyboard, preferences-misc, "
    "or preferences-kreedzbroom.",
    "page",
    "map"};
  const auto uiSnapshotGamePathOption = QCommandLineOption{
    QStringList{"ui-snapshot-game-path"},
    "Set the detected game's path while capturing a UI snapshot.",
    "path"};

  parser.addOption(portableOption);
  parser.addOption(draftUpdatesOption);
  parser.addOption(uiSnapshotOption);
  parser.addOption(uiSnapshotThemeOption);
  parser.addOption(uiSnapshotPageOption);
  parser.addOption(uiSnapshotGamePathOption);
  parser.addPositionalArgument("files", "Map files to open.", "[files...]");
  parser.process(app);

  auto options = CommandLineOptions{
    parser.isSet(draftUpdatesOption), parser.positionalArguments(), std::nullopt};

  if (!parser.isSet(uiSnapshotOption))
  {
    if (
      parser.isSet(uiSnapshotThemeOption) || parser.isSet(uiSnapshotPageOption)
      || parser.isSet(uiSnapshotGamePathOption))
    {
      qCritical() << "UI snapshot options require --ui-snapshot";
      return std::nullopt;
    }
    return options;
  }

  if (options.fileNames.size() > 1)
  {
    qCritical() << "--ui-snapshot accepts at most one map file";
    return std::nullopt;
  }

  const auto snapshotTheme = parser.value(uiSnapshotThemeOption).trimmed().toLower();
  if (ThemeRegistry::instance().findTheme(snapshotTheme) == nullptr)
  {
    qCritical() << "Unsupported UI snapshot theme:" << snapshotTheme;
    return std::nullopt;
  }

  const auto snapshotPage = parser.value(uiSnapshotPageOption).trimmed().toLower();
  if (!isSupportedUiSnapshotPage(snapshotPage))
  {
    qCritical() << "Unsupported UI snapshot page:" << snapshotPage;
    return std::nullopt;
  }
  if (options.fileNames.empty() && uiSnapshotPageRequiresMap(snapshotPage))
  {
    qCritical() << "The selected UI snapshot page requires one map file";
    return std::nullopt;
  }

  auto snapshotGamePath = QString{};
  if (parser.isSet(uiSnapshotGamePathOption))
  {
    const auto gamePathInfo = QFileInfo{parser.value(uiSnapshotGamePathOption)};
    if (!gamePathInfo.exists() || !gamePathInfo.isDir())
    {
      qCritical() << "UI snapshot game path is not a directory:"
                  << gamePathInfo.filePath();
      return std::nullopt;
    }
    if (options.fileNames.empty())
    {
      qCritical() << "--ui-snapshot-game-path requires one map file";
      return std::nullopt;
    }
    snapshotGamePath = gamePathInfo.absoluteFilePath();
  }

  options.uiSnapshot = UiSnapshotCommandLineOptions{
    parser.value(uiSnapshotOption), snapshotTheme, snapshotPage, snapshotGamePath};
  return options;
}

void enableDraftReleaseUpdates()
{
  auto& prefs = PreferenceManager::instance();
  prefs.set(Preferences::EnableDraftReleaseUpdates, true);
  prefs.set(Preferences::IncludeDraftReleaseUpdates, true);
  prefs.saveChanges();
}

} // namespace

int main(int argc, char* argv[])
{
  configureDefaultGlFormat();

  // Makes all QOpenGLWidget in the application share a single context
  // (default behaviour would be for QOpenGLWidget's in a single top-level window to share
  // a context.) see: http://doc.qt.io/qt-5/qopenglwidget.html#context-sharing
  QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

  // Set up Hi DPI scaling
  // Enables non-integer scaling (e.g. 150% scaling on Windows)
  QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
    Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

  // When this flag is enabled, font and palette changes propagate as though the user
  // had manually called the corresponding QWidget methods.
  QGuiApplication::setAttribute(Qt::AA_UseStyleSheetPropagationInWidgetStyles);

  // Don't show icons in menus, they are scaled down and don't look very good.
  QGuiApplication::setAttribute(Qt::AA_DontShowIconsInMenus);

  // Store settings in INI format
  QSettings::setDefaultFormat(QSettings::IniFormat);

  // Workaround bug in Qt's Ctrl+Click = RMB emulation (a macOS feature.)
  // In Qt 5.13.0 / macOS 10.14.6, Ctrl+trackpad click+Drag produces no mouse events at
  // all, but it should produce RMB down/move events. This environment variable disables
  // Qt's emulation so we can implement it ourselves in InputEventRecorder::recordEvent
  qputenv("QT_MAC_DONT_OVERRIDE_CTRL_LMB", "1");

  // Disable Qt OpenGL buglist; since we require desktop OpenGL 2.1 there's no point in
  // having Qt disable it (also we've had reports of some Intel drivers being blocked that
  // actually work with TB.)
  qputenv("QT_OPENGL_BUGLIST", ":/opengl_buglist.json");

  // parse portable arg out manually at first to ensure it's set before any settings load
  if (argc > 1)
  {
    for (int i = 1; i < argc; i++)
    {
      if (std::strcmp(argv[i], "--portable") == 0)
      {
        SystemPaths::setPortable();
        QSettings::setPath(
          QSettings::IniFormat, QSettings::UserScope, QString("./config"));
      }
    }
  }

  // Needs to be set before creating the preference manager
  QApplication::setApplicationName("TrenchBroom");
  // Needs to be "" otherwise Qt adds this to the paths returned by QStandardPaths
  // which would cause preferences to move from where they were with wx
  QApplication::setOrganizationName("");
  QApplication::setOrganizationDomain("io.github.trenchbroom");

  // QApplication must be created before QPreferenceStore because QPreferenceStore uses
  // QFileSystemWatcher, which requires a QApplication instance
  auto app = QApplication{argc, argv};

  installCrashHandlers();

  const auto commandLineOptions = parseCommandLine(app);
  if (!commandLineOptions)
  {
    return 2;
  }

  auto snapshotSettingsDirectory = std::unique_ptr<QTemporaryDir>{};
  auto preferenceFilePath = pathAsQString(SystemPaths::preferenceFilePath());
  if (commandLineOptions->uiSnapshot)
  {
    snapshotSettingsDirectory = std::make_unique<QTemporaryDir>();
    if (!snapshotSettingsDirectory->isValid())
    {
      qCritical() << "Could not create isolated UI snapshot settings directory";
      return 2;
    }

    QSettings::setPath(
      QSettings::IniFormat, QSettings::UserScope, snapshotSettingsDirectory->path());
    preferenceFilePath = snapshotSettingsDirectory->filePath("Preferences.json");
  }

  // PreferenceManager is destroyed by TrenchBroomApp::~TrenchBroomApp()
  PreferenceManager::createInstance(
    std::make_unique<QPreferenceStore>(preferenceFilePath));

  loadTranslations(app);

  // Styles must be loaded before creating the app controller, or they won't apply to
  // the welcome window, which the app controller creates.
  auto themeOverride = std::optional<QString>{};
  if (commandLineOptions->uiSnapshot)
  {
    themeOverride = commandLineOptions->uiSnapshot->theme;
  }
  if (auto error = QString{}; !installApplicationStyle(app, themeOverride, &error))
  {
    qWarning().noquote() << "Could not load application stylesheet:" << error;
    if (commandLineOptions->uiSnapshot)
    {
      return 3;
    }
  }

  auto appController = createAppController(commandLineOptions->uiSnapshot.has_value());
  auto crashReporter = CrashReporter{*appController};
  setContractViolationHandler(crashReporter);

#ifdef __APPLE__
  app.setQuitOnLastWindowClosed(false);
  populateMainMenu(*appController);
  installFileEventFilter(*appController);
#endif

  if (commandLineOptions->enableDraftReleaseUpdates)
  {
    enableDraftReleaseUpdates();
  }

  if (commandLineOptions->uiSnapshot)
  {
    return runUiSnapshot(
      app,
      *appController,
      *commandLineOptions->uiSnapshot,
      commandLineOptions->fileNames,
      [&](const auto& fileNames) { return openFiles(*appController, fileNames); });
  }

  appController->askForAutoUpdates();
  appController->triggerAutoUpdateCheck();

  if (!openFiles(*appController, commandLineOptions->fileNames))
  {
    appController->showWelcomeWindow();
  }

  return app.exec();
}
