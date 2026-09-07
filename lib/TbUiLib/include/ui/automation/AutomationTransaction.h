/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 */

#pragma once

#include <memory>
#include <string>

namespace tb::mdl
{
class Map;
class Transaction;
} // namespace tb::mdl

namespace tb::ui::automation
{

/**
 * The native transaction boundary shared by automation adapters. The service
 * owns no MCP or Python state, so callers retain responsibility for document
 * guards and result publication around this boundary.
 */
class AutomationTransaction
{
private:
  std::unique_ptr<mdl::Transaction> m_transaction;

public:
  AutomationTransaction(mdl::Map& map, std::string name);
  ~AutomationTransaction();

  AutomationTransaction(const AutomationTransaction&) = delete;
  AutomationTransaction& operator=(const AutomationTransaction&) = delete;
  AutomationTransaction(AutomationTransaction&&) noexcept;
  AutomationTransaction& operator=(AutomationTransaction&&) noexcept;

  bool commit();
  void cancel();
  bool active() const;
};

} // namespace tb::ui::automation
