/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 */

#include "ui/automation/AutomationTransaction.h"

#include "mdl/Map.h"
#include "mdl/Transaction.h"

#include <utility>

namespace tb::ui::automation
{

AutomationTransaction::AutomationTransaction(mdl::Map& map, std::string name)
  : m_transaction{std::make_unique<mdl::Transaction>(map, std::move(name))}
{
}

AutomationTransaction::~AutomationTransaction()
{
  cancel();
}

AutomationTransaction::AutomationTransaction(AutomationTransaction&&) noexcept = default;

AutomationTransaction& AutomationTransaction::operator=(
  AutomationTransaction&&) noexcept = default;

bool AutomationTransaction::commit()
{
  if (!m_transaction)
  {
    return false;
  }

  const auto result = m_transaction->commit();
  m_transaction.reset();
  return result;
}

void AutomationTransaction::cancel()
{
  if (m_transaction)
  {
    m_transaction->cancel();
    m_transaction.reset();
  }
}

bool AutomationTransaction::active() const
{
  return m_transaction != nullptr;
}

} // namespace tb::ui::automation
