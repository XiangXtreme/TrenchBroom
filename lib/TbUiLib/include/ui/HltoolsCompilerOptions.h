/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QString>
#include <QStringList>

#include <vector>

namespace tb::ui
{
struct HltoolsCompilerOption
{
  QString flag;
  QString stage;
  int arity;
  bool repeatable;
  bool visible;
  QString unavailableReason;
};

const std::vector<HltoolsCompilerOption>& hltoolsCompilerOptions();
QString canonicalHltoolsFlag(const QString& flag);
const HltoolsCompilerOption* findHltoolsCompilerOption(const QString& flag);
// Parameters have already been interpolated. Returns an empty string on success.
QString validateHltoolsArguments(const QStringList& arguments);
} // namespace tb::ui
