/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/CompilationPlan.h"

#include <QCoreApplication>
#include <QDir>

#include "el/Interpolate.h"
#include "mdl/CompilationProfile.h"
#include "mdl/GameInfo.h"
#include "mdl/Map.h"
#include "ui/CompilationVariables.h"
#include "ui/QPathUtils.h"

#include <algorithm>
#include <array>
#include <type_traits>

namespace tb::ui
{
namespace
{
QString tr(const char* text)
{
  return QCoreApplication::translate("CompilationPlan", text);
}
} // namespace

QString compilationTaskName(const mdl::CompilationTask& task)
{
  static const auto names = std::array{
    "Export map",
    "Copy files",
    "Rename file",
    "Delete files",
    "Run tool",
    "Built-in hltools",
    "Launch engine"};
  return tr(names[task.index()]);
}

CompilationPlan buildCompilationPlan(
  const mdl::CompilationProfile& profile,
  const mdl::Map& map,
  const std::filesystem::path& mapPath,
  const std::optional<ProjectCompileInfo>& project,
  const bool checkWorkDirectory)
{
  auto plan = CompilationPlan{};
  auto workVariables = el::VariableTable{CompilationWorkDirVariables{map, mapPath}};
  if (project)
  {
    // Project paths are useful in the working directory too. WORK_DIR_PATH and
    // CPU_COUNT intentionally remain task-only variables.
    auto projectVariables = CompilationVariables{map, "", mapPath};
    projectVariables.setProject(project->projectPath.string(), project->mapName);
    for (const auto& name :
         {CompilationVariableNames::PROJECT_PATH,
          CompilationVariableNames::MAP_NAME,
          CompilationVariableNames::OUTPUT_DIR_PATH})
      workVariables.set(name, projectVariables.value(name));
  }
  auto resolve = [&](
                   const std::string& spec,
                   const el::VariableTable& variables,
                   const std::optional<size_t> index,
                   const CompilationField field,
                   const QString& label,
                   const bool required) -> std::optional<std::string> {
    const auto result = el::interpolate(variables, spec);
    if (result.is_error())
    {
      plan.diagnostic = CompilationDiagnostic{
        index,
        field,
        tr("%1: %2\nExpression: %3")
          .arg(
            label,
            QString::fromStdString(std::get<Error>(result.error()).msg),
            QString::fromStdString(spec))};
      return std::nullopt;
    }
    if (required && QString::fromStdString(result.value()).trimmed().isEmpty())
    {
      plan.diagnostic = CompilationDiagnostic{index, field, tr("%1 is empty").arg(label)};
      return std::nullopt;
    }
    return result.value();
  };
  const auto work = resolve(
    profile.workDirSpec,
    workVariables,
    std::nullopt,
    CompilationField::WorkDirectory,
    tr("Working directory"),
    false);
  if (!work)
    return plan;
  plan.workDirectory = *work;
  if (checkWorkDirectory && !QDir{QString::fromStdString(*work)}.exists())
  {
    plan.diagnostic = CompilationDiagnostic{
      std::nullopt,
      CompilationField::WorkDirectory,
      tr("Working directory does not exist: %1").arg(QString::fromStdString(*work))};
    return plan;
  }
  auto variables = CompilationVariables{map, *work, mapPath};
  if (project)
    variables.setProject(project->projectPath.string(), project->mapName);
  plan.variables = variables;
  for (size_t i = 0; i < profile.tasks.size(); ++i)
  {
    const auto& task = profile.tasks[i];
    if (!std::visit([](const auto& t) { return t.enabled; }, task))
      continue;
    ++plan.enabledTaskCount;
    const auto label =
      tr("Task %1 — %2").arg(qulonglong(i + 1)).arg(compilationTaskName(task));
    const auto field =
      [&](
        const auto& spec, const auto kind, const auto& name, const bool required = true) {
        return resolve(spec, variables, i, kind, label + " / " + name, required);
      };
    std::visit(
      [&](const auto& t) {
        using T = std::decay_t<decltype(t)>;
        if constexpr (
          std::is_same_v<T, mdl::CompilationCopyFiles>
          || std::is_same_v<T, mdl::CompilationRenameFile>)
        {
          if (!field(t.sourceSpec, CompilationField::Source, tr("Source")))
            return;
        }
        if constexpr (requires { t.targetSpec; })
        {
          const auto target = field(t.targetSpec, CompilationField::Target, tr("Target"));
          if (!target)
            return;
          if constexpr (std::is_same_v<T, mdl::CompilationHltools>)
          {
            auto path = pathFromQString(QString::fromStdString(*target));
            if (path.is_relative())
              path = pathFromQString(QDir{QString::fromStdString(*work)}.absoluteFilePath(
                pathAsQString(path)));
            plan.outputs.push_back(path.lexically_normal());
          }
        }
        if constexpr (std::is_same_v<T, mdl::CompilationRunTool>)
        {
          if (!field(t.toolSpec, CompilationField::Tool, tr("Tool")))
            return;
        }
        if constexpr (requires { t.parameterSpec; })
          field(t.parameterSpec, CompilationField::Parameters, tr("Parameters"), false);
        if constexpr (std::is_same_v<T, mdl::CompilationHltools>)
        {
          if (!plan.diagnostic)
            variables.set(
              CompilationVariableNames::BSP_PATH,
              el::Value{plan.outputs.back().string()});
        }
        if constexpr (std::is_same_v<T, mdl::CompilationLaunchEngine>)
        {
          const auto& profiles = map.gameInfo().gameEngineConfig.profiles;
          if (
            t.treatLaunchFailureAsError
            && std::ranges::none_of(
              profiles, [&](const auto& p) { return p.id == t.engineProfileId; }))
            plan.diagnostic = CompilationDiagnostic{
              i,
              CompilationField::Engine,
              label + " / " + tr("Select an engine profile")};
        }
      },
      task);
    if (plan.diagnostic)
      return plan;
  }
  if (plan.enabledTaskCount == 0)
    plan.diagnostic = CompilationDiagnostic{
      std::nullopt, CompilationField::Target, tr("Enable at least one task")};
  return plan;
}
} // namespace tb::ui
