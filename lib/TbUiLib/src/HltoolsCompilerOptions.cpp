/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/HltoolsCompilerOptions.h"

#include <QCoreApplication>

#include <algorithm>

namespace tb::ui
{
namespace
{
QString tr(const char* text)
{
  return QCoreApplication::translate("HltoolsCompilerOptions", text);
}
} // namespace

QString canonicalHltoolsFlag(const QString& argument)
{
  const auto flag = argument.toLower();
  if (flag == "-noopaque")
    return "-nopaque";
  if (flag == "--dump-intermediates")
    return "-dumpintermediates";
  if (flag == "--embed-source")
    return "-embedsource";
  if (flag == "--no-embed-source")
    return "-noembedsource";
  return flag;
}

const std::vector<HltoolsCompilerOption>& hltoolsCompilerOptions()
{
  static const auto result = [] {
    constexpr unsigned csg = 1, bsp = 2, vis = 4, rad = 8, all = 15;
    struct Spec
    {
      const char* flag;
      unsigned stages;
      int arity;
      bool persist;
      bool repeatable;
    };
    static const Spec registry[] = {
#include "HltoolsOptionRegistry.inc"
    };
    auto options = std::vector<HltoolsCompilerOption>{};
    for (const auto& spec : registry)
    {
      const auto flag = QString::fromLatin1(spec.flag);
      auto stages = QStringList{};
      if (spec.stages != all)
        for (const auto& [bit, name] :
             {std::pair{csg, "CSG"}, {bsp, "BSP"}, {vis, "VIS"}, {rad, "RAD"}})
          if ((spec.stages & bit) != 0)
            stages.push_back(QString::fromLatin1(name));
      auto reason = QString{};
      if (flag == "-onlyents")
        reason = tr("Entity-only updates require the standalone CSG command.");
      else if (flag == "-maxdistance")
        reason = tr("Maximum visibility distance is not implemented by hltools.");
      else if (flag == "-embedsource" || flag == "-noembedsource")
        reason = tr("Source embedding is unavailable in the bundled compiler.");
      else if (
        spec.stages == 0 && flag != "-nochart" && flag != "-dumpintermediates"
        && canonicalHltoolsFlag(flag) == flag)
        reason = tr("This option is not used by the unified compiler.");
      const auto visible = canonicalHltoolsFlag(flag) == flag && flag != "-full"
        && flag != "-fast" && flag != "-extra" && flag != "-gpu"
        && (spec.stages != 0 || flag == "-nochart" || flag == "-dumpintermediates"
            || flag == "-embedsource" || flag == "-noembedsource");
      options.push_back(
        {flag,
         spec.stages == 0 ? QString{"Compile"} : stages.join(" / "),
         spec.arity,
         spec.repeatable,
         visible,
         reason});
    }
    return options;
  }();
  return result;
}

const HltoolsCompilerOption* findHltoolsCompilerOption(const QString& flag)
{
  const auto canonical = canonicalHltoolsFlag(flag);
  const auto& options = hltoolsCompilerOptions();
  const auto found = std::ranges::find(options, canonical, &HltoolsCompilerOption::flag);
  return found == options.end() ? nullptr : &*found;
}

QString validateHltoolsArguments(const QStringList& arguments)
{
  for (qsizetype i = 0; i < arguments.size(); ++i)
  {
    const auto& flag = arguments[i];
    const auto* option = findHltoolsCompilerOption(flag);
    if (!option)
      return tr("Unknown compiler option: %1").arg(flag);
    if (!option->unavailableReason.isEmpty())
      return flag + ": " + option->unavailableReason;
    for (int value = 0; value < option->arity; ++value)
    {
      if (++i >= arguments.size() || arguments[i].isEmpty())
        return tr("%1 requires %2 value(s).").arg(flag).arg(option->arity);
      auto numeric = false;
      arguments[i].toDouble(&numeric);
      if (arguments[i].startsWith('-') && !numeric)
        return tr("%1 requires %2 value(s).").arg(flag).arg(option->arity);
    }
  }
  return {};
}
} // namespace tb::ui
