/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.
 */

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QUuid>

#include "McpThinBridgeTools.h"
#include "ui/AppController.h"
#include "ui/MapViewBase.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"

namespace tb::ui
{
namespace
{

QString capturePath(const QJsonObject& params)
{
  const auto requested = params.value("path").toString().trimmed();
  if (!requested.isEmpty())
  {
    return requested;
  }
  return QDir::tempPath() + "/trenchbroom-mcp-capture-"
         + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".png";
}

bool hasVisiblePixels(const QImage& image)
{
  const auto rgba = image.convertToFormat(QImage::Format_RGBA8888);
  const auto* data = rgba.constBits();
  const auto size = rgba.sizeInBytes();
  for (qsizetype index = 3; index < size; index += 4)
  {
    if (data[index] != 0)
    {
      return true;
    }
  }
  return false;
}

} // namespace

McpBridgeToolResult viewportCaptureCurrentResult(
  AppController& appController, const QJsonObject& params)
{
  auto* mapWindow = appController.mapWindowManager().topMapWindow();
  if (mapWindow == nullptr || mapWindow->currentMapViewBase() == nullptr)
  {
    return noActiveDocumentFailure();
  }

  const auto path = capturePath(params);
  const auto info = QFileInfo{path};
  if (!info.isAbsolute() || info.suffix().compare("png", Qt::CaseInsensitive) != 0)
  {
    return invalidParamsFailure("tb_capture path must be an absolute .png path");
  }
  if (!QDir{}.mkpath(info.absolutePath()))
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not create the capture output directory");
  }

  const auto image = mapWindow->currentMapViewBase()->grabFramebuffer();
  if (image.isNull() || image.width() < 1 || image.height() < 1 || !hasVisiblePixels(image))
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Current viewport did not produce a readable image");
  }
  if (!image.save(path, "PNG"))
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Could not write the viewport capture");
  }

  const auto saved = QImage{path};
  if (saved.isNull() || saved.size() != image.size() || !hasVisiblePixels(saved))
  {
    return McpBridgeToolResult::failure(
      mcp::McpErrorCode::InternalError, "Saved viewport capture is not readable");
  }
  return McpBridgeToolResult::success(
    QJsonObject{{"path", info.absoluteFilePath()},
                {"format", "png"},
                {"width", saved.width()},
                {"height", saved.height()},
                {"readable", true},
                {"mutatedDocument", false}});
}

} // namespace tb::ui
