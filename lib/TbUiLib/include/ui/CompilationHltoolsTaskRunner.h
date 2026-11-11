/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#pragma once

#include <QByteArray>
#include <QStringList>

#include "ui/CompilationRunner.h"

#include <filesystem>
#include <memory>

class QTemporaryDir;

namespace tb::ui
{
/** The compiler shipped alongside the editor, shared by compile and preview. */
std::filesystem::path hltoolsWorkerPath();

class CompilationHltoolsTaskRunner : public CompilationTaskRunner
{
private:
  mdl::CompilationHltools m_task;
  std::shared_ptr<QTemporaryDir> m_scratch;
  QProcess* m_process = nullptr;
  std::filesystem::path m_target;
  std::filesystem::path m_inputPath;
  QByteArray m_log;
  bool m_finished = false;

public:
  CompilationHltoolsTaskRunner(CompilationContext& context, mdl::CompilationHltools task);
  ~CompilationHltoolsTaskRunner() override;

private:
  void doExecute() override;
  void doTerminate() override;
  Result<QStringList> prepare();
  void readOutput();
  Result<void> writeLog() const;
  void finish(int exitCode, QProcess::ExitStatus status);
  void fail(const std::string& message);
  void releaseProcess();
};
} // namespace tb::ui
