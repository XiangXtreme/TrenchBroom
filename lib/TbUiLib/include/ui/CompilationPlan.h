/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QString>

#include "el/VariableStore.h"
#include "mdl/CompilationTask.h"

#include <filesystem>
#include <optional>
#include <vector>

namespace tb::mdl
{
class Map;
struct CompilationProfile;
} // namespace tb::mdl

namespace tb::ui
{
struct ProjectCompileInfo
{
  std::filesystem::path projectPath;
  std::string mapName;
};

enum class CompilationOutcome
{
  Succeeded,
  Failed,
  Cancelled
};

enum class CompilationField
{
  WorkDirectory,
  Source,
  Target,
  Tool,
  Parameters,
  Engine
};

struct CompilationDiagnostic
{
  std::optional<size_t> taskIndex;
  CompilationField field;
  QString message;
};

/** A side-effect-free preview, also used to validate the actual frozen input. */
struct CompilationPlan
{
  el::VariableTable variables;
  std::string workDirectory;
  std::vector<std::filesystem::path> outputs;
  size_t enabledTaskCount = 0;
  std::optional<CompilationDiagnostic> diagnostic;
};

QString compilationTaskName(const mdl::CompilationTask& task);
CompilationPlan buildCompilationPlan(
  const mdl::CompilationProfile& profile,
  const mdl::Map& map,
  const std::filesystem::path& mapPath,
  const std::optional<ProjectCompileInfo>& project = std::nullopt,
  bool checkWorkDirectory = true);
} // namespace tb::ui
