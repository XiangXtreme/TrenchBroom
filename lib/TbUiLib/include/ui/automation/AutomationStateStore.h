/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 */

#pragma once

#include "ui/automation/AutomationStateRecords.h"

#include <map>

#include <cstddef>

namespace tb::ui::automation
{

/**
 * Application-owned protocol-neutral state for automation adapters.
 *
 * MCP and Python may use the same records, but neither protocol owns their
 * lifetime. The AppController creates this store before either adapter.
 */
class AutomationStateStore
{
public:
  static constexpr auto MaxIrPreviews = size_t{64u};
  static constexpr auto IrPreviewTtlMs = qint64{10 * 60 * 1000};

  std::map<QString, AutomationObjectMetadataRecord> objectMetadata;
  std::map<QString, AutomationModuleRecord> modules;
  std::map<QString, AutomationIrPreviewRecord> irPreviews;
  int nextIrPreviewIndex = 1;

  void clear()
  {
    objectMetadata.clear();
    modules.clear();
    irPreviews.clear();
    nextIrPreviewIndex = 1;
  }
};

} // namespace tb::ui::automation
