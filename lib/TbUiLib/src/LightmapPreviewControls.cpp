/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/LightmapPreviewControls.h"

#include <QActionGroup>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QMenu>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "base/PreferenceManager.h"
#include "prefs/Preferences.h"
#include "ui/LightmapPreview.h"
#include "ui/QPathUtils.h"

namespace tb::ui
{
namespace
{
QString tr(const char* text)
{
  return QCoreApplication::translate("LightmapPreviewControls", text);
}

void showLog(LightmapPreview& preview, QWidget* parent)
{
  auto* dialog = new QDialog{parent};
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(tr("Lightmap preview log"));
  dialog->resize(800, 500);
  auto* output = new QPlainTextEdit{dialog};
  output->setReadOnly(true);
  output->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  output->setPlainText(preview.statusText() + "\n\n" + preview.output());
  auto* buttons = new QDialogButtonBox{QDialogButtonBox::Close, dialog};
  QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
  auto* layout = new QVBoxLayout{dialog};
  layout->addWidget(output);
  layout->addWidget(buttons);
  dialog->show();
}
} // namespace

QMenu* createLightmapPreviewMenu(LightmapPreview& preview, QWidget* parent)
{
  auto* menu = new QMenu{parent};
  auto* qualities = new QActionGroup{menu};
  const auto addQuality = [&](const QString& label, const char* value) {
    auto* action = menu->addAction(label);
    action->setCheckable(true);
    action->setData(QString::fromLatin1(value));
    qualities->addAction(action);
    QObject::connect(action, &QAction::triggered, menu, [value] {
      setPref(Preferences::LightmapQuality, value);
    });
  };
  addQuality(tr("Fast"), Preferences::LightmapQualityFast);
  addQuality(tr("Normal"), Preferences::LightmapQualityNormal);
  addQuality(tr("Ultra"), Preferences::LightmapQualityUltra);
  menu->addSeparator();
  auto* update = menu->addAction(tr("Bake now"));
  update->setObjectName("LightmapBakeNow");
  QObject::connect(update, &QAction::triggered, &preview, [&preview] {
    if (!pref(Preferences::ShowLightmap))
      setPref(Preferences::ShowLightmap, true);
    else
      preview.requestBake();
  });
  auto* automatic = menu->addAction(tr("Automatic update"));
  automatic->setObjectName("LightmapAutomaticUpdate");
  automatic->setCheckable(true);
  QObject::connect(automatic, &QAction::triggered, menu, [](bool enabled) {
    setPref(Preferences::LightmapAutoUpdate, enabled);
  });
  auto* cancel = menu->addAction(tr("Cancel bake"));
  cancel->setObjectName("LightmapCancelBake");
  QObject::connect(cancel, &QAction::triggered, &preview, &LightmapPreview::cancel);
  menu->addSeparator();
  auto* only = menu->addAction(tr("Lighting only"));
  only->setCheckable(true);
  QObject::connect(only, &QAction::triggered, menu, [](bool enabled) {
    setPref(Preferences::LightmapOnly, enabled);
  });
  menu->addSeparator();
  auto* log = menu->addAction(tr("View log…"));
  QObject::connect(
    log, &QAction::triggered, menu, [&preview, parent] { showLog(preview, parent); });
  auto* files = menu->addAction(tr("Open diagnostic folder"));
  QObject::connect(files, &QAction::triggered, menu, [&preview] {
    QDesktopServices::openUrl(
      QUrl::fromLocalFile(pathAsQString(preview.diagnosticDirectory())));
  });
  const auto refresh = [&preview, qualities, automatic, cancel, only, files] {
    const auto quality = pref(Preferences::LightmapQuality);
    for (auto* action : qualities->actions())
      action->setChecked(action->data().toString() == QString::fromStdString(quality));
    automatic->setChecked(pref(Preferences::LightmapAutoUpdate));
    only->setChecked(pref(Preferences::LightmapOnly));
    cancel->setEnabled(preview.canCancel());
    files->setEnabled(!preview.diagnosticDirectory().empty());
  };
  QObject::connect(menu, &QMenu::aboutToShow, menu, refresh);
  QObject::connect(&preview, &LightmapPreview::stateChanged, menu, refresh);
  refresh();
  return menu;
}

QWidget* createLightmapPreviewStatus(LightmapPreview& preview, QWidget* parent)
{
  auto* button = new QToolButton{parent};
  button->setObjectName("LightmapPreviewStatus");
  button->setProperty("tbControlRole", "compact");
  button->setPopupMode(QToolButton::InstantPopup);
  button->setMenu(createLightmapPreviewMenu(preview, button));
  const auto refresh = [&preview, button] {
    button->setVisible(pref(Preferences::ShowLightmap));
    button->setText(
      preview.error().isEmpty() ? preview.statusText() : tr("Lightmap bake failed"));
    button->setToolTip(preview.statusText());
  };
  QObject::connect(&preview, &LightmapPreview::stateChanged, button, refresh);
  refresh();
  return button;
}
} // namespace tb::ui
