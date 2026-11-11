/*
 Copyright (C) 2026 XiangXtreme
 This file is part of TrenchBroom, licensed under GPL version 3 or later.
 */
#include "ui/CompilationHltoolsTaskRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryDir>

#include "fs/File.h"
#include "mdl/EntityNode.h"
#include "mdl/ExportOptions.h"
#include "mdl/GameFileSystem.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/NodeQueries.h"
#include "mdl/WorldNode.h"
#include "ui/BuiltinGoldSrcAssets.h"
#include "ui/CompilationVariables.h"
#include "ui/HltoolsCompilerOptions.h"
#include "ui/ProjectSession.h"
#include "ui/QPathUtils.h"

#include "kd/cmd_utils.h"

#include <algorithm>
#include <set>
#include <thread>

namespace tb::ui
{
namespace
{
Result<void> copyArtifact(
  const std::filesystem::path& source, const std::filesystem::path& target)
{
  auto input = QFile{pathAsQString(source)};
  auto output = QSaveFile{pathAsQString(target)};
  if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly))
    return Error{"Cannot open compiler output: " + target.string()};
  while (!input.atEnd())
  {
    const auto bytes = input.read(1024 * 1024);
    if (input.error() != QFileDevice::NoError || output.write(bytes) != bytes.size())
      return Error{"Cannot write compiler output: " + target.string()};
  }
  if (!output.commit())
    return Error{"Cannot commit compiler output: " + target.string()};
  return {};
}

Result<void> stageModels(const mdl::Map& map, const std::filesystem::path& root)
{
  auto models = std::set<std::filesystem::path>{};
  for (const auto* node : mdl::collectNodesAndDescendants(map.worldNode().children()))
  {
    const auto* entity = dynamic_cast<const mdl::EntityNode*>(node);
    if (!entity)
      continue;
    if (const auto* layer = entity->containingLayer();
        layer && layer->layer().omitFromExport())
      continue;
    const auto* model = entity->entity().property("model");
    if (!model || model->empty() || model->front() == '*')
      continue;
    auto name = QString::fromStdString(*model).replace('\\', '/');
    if (
      !name.endsWith(".mdl", Qt::CaseInsensitive) && !QFileInfo{name}.suffix().isEmpty())
      continue;
    if (!name.endsWith(".mdl", Qt::CaseInsensitive))
      name += ".mdl";
    const auto relative = pathFromQString(name).lexically_normal();
    if (relative.has_root_path())
      continue; // hltools resolves rooted model paths directly, including drive-relative
                // paths.
    if (std::ranges::any_of(relative, [](const auto& part) { return part == ".."; }))
      return Error{"A compiler model path escapes the game directory: " + *model};
    models.insert(relative);
  }
  for (const auto& relative : models)
  {
    const auto target = root / relative;
    if (!QDir{}.mkpath(pathAsQString(target.parent_path())))
      return Error{"Cannot stage compiler model: " + relative.string()};
    const auto file = map.gameFileSystem().openFile(relative);
    if (file.is_success())
    {
      try
      {
        auto reader = file.value()->reader().buffer();
        const auto bytes = reader.stringView();
        auto output = QSaveFile{pathAsQString(target)};
        if (
          !output.open(QIODevice::WriteOnly)
          || output.write(bytes.data(), qint64(bytes.size())) != qint64(bytes.size())
          || !output.commit())
          return Error{"Cannot stage compiler model: " + relative.string()};
      }
      catch (const std::exception& error)
      {
        return Error{"Cannot read compiler model: " + std::string{error.what()}};
      }
    }
    else
    {
      // Preserve map-local search roots as well as mounted game/archive resources.
      for (const auto& sourceRoot :
           {map.path().parent_path().parent_path(), map.path().parent_path()})
      {
        const auto source = sourceRoot / relative;
        if (QFile::exists(pathAsQString(source)))
        {
          if (const auto copied = copyArtifact(source, target); copied.is_error())
            return std::get<Error>(copied.error());
          break;
        }
      }
      // Missing models retain the compiler's own warning/error behavior.
    }
  }
  return {};
}
} // namespace

std::filesystem::path hltoolsWorkerPath()
{
#ifdef Q_OS_WIN
  constexpr auto name = "HltoolsLightmapWorker.exe";
#else
  constexpr auto name = "HltoolsLightmapWorker";
#endif
  return pathFromQString(QDir{QCoreApplication::applicationDirPath()}.filePath(name));
}

CompilationHltoolsTaskRunner::CompilationHltoolsTaskRunner(
  CompilationContext& context, mdl::CompilationHltools task)
  : CompilationTaskRunner{context}
  , m_task{std::move(task)}
{
}

CompilationHltoolsTaskRunner::~CompilationHltoolsTaskRunner()
{
  releaseProcess();
}

Result<QStringList> CompilationHltoolsTaskRunner::prepare()
{
  const auto format = m_context.map().worldNode().mapFormat();
  if (format != mdl::MapFormat::Standard && format != mdl::MapFormat::Valve)
    return Error{"The built-in hltools compiler supports GoldSrc MAP inputs only."};
  if (m_task.quality != "normal" && m_task.quality != "fast" && m_task.quality != "extra")
    return Error{"Unknown hltools lighting quality: " + m_task.quality};
  const auto target = interpolate(m_task.targetSpec);
  if (target.is_error())
    return std::get<Error>(target.error());
  if (target.value().empty())
    return Error{"Choose a BSP output path for the built-in compiler."};
  m_target = pathFromQString(QString::fromStdString(target.value()));
  const auto workingDirectory =
    m_context.variableValue(CompilationVariableNames::WORK_DIR_PATH);
  if (workingDirectory.is_error())
    return std::get<Error>(workingDirectory.error());
  if (m_target.is_relative())
    m_target = pathFromQString(
      QDir{QString::fromStdString(workingDirectory.value())}.absoluteFilePath(
        pathAsQString(m_target)));
  m_target = m_target.lexically_normal();
  if (
    QString::fromStdString(m_target.extension().string())
      .compare(".bsp", Qt::CaseInsensitive)
    != 0)
    return Error{"The built-in compiler output must have a .bsp extension."};

  const auto parameters = interpolate(m_task.parameterSpec);
  if (parameters.is_error())
    return std::get<Error>(parameters.error());
  auto arguments = QStringList{};
  for (const auto& argument : kdl::cmd_parse_args(parameters.value()))
    arguments.append(QString::fromStdString(argument));
  if (const auto error = validateHltoolsArguments(arguments); !error.isEmpty())
    return Error{error.toStdString()};
  for (qsizetype i = 0; i < arguments.size(); ++i)
  {
    const auto flag = canonicalHltoolsFlag(arguments[i]);
    if (flag == "-incremental")
      m_incremental = true;
    if (
      flag == "-dumpintermediates" || flag == "-dump" || flag == "-dumpgather"
      || flag.startsWith("-draw"))
      m_preserveArtifacts = true;
    i += findHltoolsCompilerOption(flag)->arity;
  }

  m_context << "#### Built-in hltools: " << pathAsQString(m_target) << "\n";
  if (m_context.test())
    return arguments;
  if (!QFile::exists(pathAsQString(hltoolsWorkerPath())))
    return Error{
      "The bundled hltools compiler is missing: " + hltoolsWorkerPath().string()};
  if (!QDir{QString::fromStdString(workingDirectory.value())}.exists())
    return Error{
      "Compiler working directory does not exist: " + workingDirectory.value()};
  if (!QDir{}.mkpath(pathAsQString(m_target.parent_path())))
    return Error{
      "Cannot create compiler output directory: " + m_target.parent_path().string()};
  m_scratch = std::make_shared<QTemporaryDir>(QDir::temp().filePath("tb-compile-XXXXXX"));
  if (!m_scratch->isValid())
    return Error{"Cannot create the compiler scratch directory."};
  const auto mapName = m_context.variableValue(CompilationVariableNames::MAP_BASE_NAME);
  if (mapName.is_error())
    return std::get<Error>(mapName.error());
  auto inputName = pathFromQString(QString::fromStdString(mapName.value())).filename();
  if (inputName.empty())
    inputName = "input";
  const auto root = pathFromQString(m_scratch->path());
  const auto maps = root / "maps";
  if (!QDir{}.mkpath(pathAsQString(maps)))
    return Error{"Cannot create the compiler MAP directory."};
  m_inputPath = maps / (inputName.string() + ".map");
  if (m_incremental)
  {
    auto cache = m_target;
    cache.replace_extension(".inc");
    auto staged = m_inputPath;
    staged.replace_extension(".inc");
    if (QFile::exists(pathAsQString(cache)))
      if (const auto copied = copyArtifact(cache, staged); copied.is_error())
        return std::get<Error>(copied.error());
  }
  if (const auto exported =
        m_context.map().exportAs(mdl::MapExportOptions{m_inputPath, true, {}, {}});
      exported.is_error())
    return std::get<Error>(exported.error());
  // Preserve hltools' map-local texlight cascade when moving the MAP into scratch.
  for (const auto& name :
       {std::filesystem::path{"lights.rad"},
        std::filesystem::path{inputName.string() + ".rad"}})
  {
    const auto source = m_context.map().path().parent_path() / name;
    if (QFile::exists(pathAsQString(source)))
      if (const auto copied = copyArtifact(source, maps / name); copied.is_error())
        return std::get<Error>(copied.error());
  }
  auto resources = QStringList{};
  if (const auto& snapshot = m_context.projectSnapshotDirectory())
  {
    if (const auto staged = ProjectSession::publishSnapshotResources(*snapshot, root);
        staged.is_error())
      return std::get<Error>(staged.error());
    const auto stagedWad = root / "textures.wad";
    if (const auto copied = copyArtifact(*snapshot / "textures.wad", stagedWad);
        copied.is_error())
      return std::get<Error>(copied.error());
    const auto wad = pathAsQString(stagedWad);
    const auto* named = m_context.map().worldNode().entity().property("wadconfig");
    if (
      wad.contains('"') || wad.contains('\n') || wad.contains('\r')
      || (named && named->find_first_of("\"\r\n") != std::string::npos))
      return Error{
        "A project WAD path cannot be represented in the compiler configuration."};
    auto contents = QString{'"'} + wad + "\"\n";
    if (named && !named->empty())
      contents = "\"" + QString::fromStdString(*named) + "\" {\n" + contents + "}\n";
    const auto configPath = root / "wads.cfg";
    auto config = QSaveFile{pathAsQString(configPath)};
    const auto bytes = contents.toUtf8();
    if (
      !config.open(QIODevice::WriteOnly) || config.write(bytes) != bytes.size()
      || !config.commit())
      return Error{"Cannot stage the project WAD configuration."};
    resources << "-wadcfgfile" << pathAsQString(configPath);
  }
  else
  {
    if (const auto staged = stageModels(m_context.map(), root); staged.is_error())
      return std::get<Error>(staged.error());
    if (mapUsesEmptyTexture(m_context.map()))
    {
      const auto config = prepareLightmapWads(m_context.map(), root);
      if (config.is_error())
        return std::get<Error>(config.error());
      resources << "-wadcfgfile" << pathAsQString(config.value());
    }
  }
  const auto threads = std::max(
    1u,
    std::thread::hardware_concurrency() > 2 ? std::thread::hardware_concurrency() - 2
                                            : 1u);
  auto command = QStringList{
    "compile", pathAsQString(m_inputPath), "-threads", QString::number(threads)};
  if (m_task.gpu)
    command << "-gpu";
  if (m_task.quality == "fast")
    command << "-fast";
  else if (m_task.quality == "extra")
    command << "-extra";
  command.append(resources);
  command.append(arguments);
  return command;
}

void CompilationHltoolsTaskRunner::doExecute()
{
  emit start();
  emit stageChanged(tr("Preparing hltools input"));
  const auto arguments = prepare();
  if (arguments.is_error())
  {
    fail(std::get<Error>(arguments.error()).msg);
    return;
  }
  if (m_context.test())
  {
    m_context.setBspPath(m_target);
    emit stageChanged(tr("CSG → BSP → VIS → RAD (planned)"));
    m_context << "#### Test: export current map, compile CSG/BSP/VIS/RAD ("
              << QString::fromStdString(m_task.quality) << "), GPU "
              << (m_task.gpu ? "requested" : "off") << "\n";
    emit end();
    return;
  }
  m_process = new QProcess{this};
  m_process->setProcessChannelMode(QProcess::MergedChannels);
  m_process->setWorkingDirectory(QString::fromStdString(
    m_context.variableValue(CompilationVariableNames::WORK_DIR_PATH).value()));
  m_process->setProgram(pathAsQString(hltoolsWorkerPath()));
  m_process->setArguments(arguments.value());
  connect(
    m_process, &QProcess::readyRead, this, &CompilationHltoolsTaskRunner::readOutput);
  connect(m_process, &QProcess::finished, this, &CompilationHltoolsTaskRunner::finish);
  connect(m_process, &QProcess::errorOccurred, this, [this](const auto error) {
    if (error == QProcess::FailedToStart)
      fail(
        "Could not start the bundled hltools compiler: "
        + m_process->errorString().toStdString());
  });
  m_process->start();
}

void CompilationHltoolsTaskRunner::readOutput()
{
  const auto bytes = m_process->readAll();
  m_log += bytes;
  m_context << m_decoder(bytes);
  // The bundled compiler's documented console sections are deliberately adapted
  // here, not in the dialog or for arbitrary external tools. Keep partial records
  // across pipe reads (live progress uses carriage returns).
  m_pendingStatus += bytes;
  while (true)
  {
    auto end = m_pendingStatus.indexOf('\n');
    const auto carriage = m_pendingStatus.indexOf('\r');
    if (end < 0 || (carriage >= 0 && carriage < end))
      end = carriage;
    if (end < 0)
      break;
    const auto line = m_pendingStatus.left(end).trimmed();
    m_pendingStatus.remove(0, end + 1);
    if (line == "processing")
      m_stage = "CSG";
    else if (line == "building bsp tree")
      m_stage = "BSP";
    else if (line == "computing visibility")
      m_stage = "VIS";
    else if (line == "lighting")
      m_stage = "RAD";
    if (line.contains("gathering light on the gpu"))
      m_backend = tr("GPU gather");
    else if (line.contains("-gpu:") && line.contains("cpu"))
      m_backend = tr("CPU fallback reported");
    const auto status = m_stage + (m_backend.isEmpty() ? QString{} : " · " + m_backend);
    if (!m_stage.isEmpty() && status != m_reportedStage)
    {
      m_reportedStage = status;
      emit stageChanged(status);
    }
  }
}

Result<void> CompilationHltoolsTaskRunner::writeLog() const
{
  auto contents = m_log;
  auto details = QFile{pathAsQString(
    m_inputPath.parent_path() / "logs" / (m_inputPath.stem().string() + ".log"))};
  if (details.open(QIODevice::ReadOnly))
  {
    const auto bytes = details.readAll();
    if (details.error() != QFileDevice::NoError)
      return Error{"Cannot read the detailed compiler log."};
    contents += "\n#### Detailed compiler log\n";
    contents += bytes;
  }
  auto logPath = m_target;
  logPath.replace_extension(".log");
  auto log = QSaveFile{pathAsQString(logPath)};
  if (
    !log.open(QIODevice::WriteOnly) || log.write(contents) != contents.size()
    || !log.commit())
    return Error{"Cannot save compiler log: " + logPath.string()};
  return {};
}

Result<void> CompilationHltoolsTaskRunner::publishAuxiliaryArtifacts(
  const bool succeeded) const
{
  const auto prefix = pathAsQString(m_inputPath.stem());
  const auto targetPrefix = pathAsQString(m_target.stem());
  const auto directory = QDir{pathAsQString(m_inputPath.parent_path())};
  for (const auto& file : directory.entryInfoList(QDir::Files))
  {
    const auto name = file.fileName();
    if (!name.startsWith(prefix + '.') && !name.startsWith(prefix + '_'))
      continue;
    const auto suffix = name.mid(prefix.size());
    const auto cache = suffix == ".inc";
    if (cache ? !m_incremental || !succeeded : !m_preserveArtifacts)
      continue;
    // Staged source/texlight inputs stay private; publish generated companions.
    if (suffix == ".map" || suffix == ".bsp" || suffix == ".rad" || suffix == ".log")
      continue;
    const auto target = m_target.parent_path() / pathFromQString(targetPrefix + suffix);
    if (const auto copied =
          copyArtifact(pathFromQString(file.absoluteFilePath()), target);
        copied.is_error())
      return std::get<Error>(copied.error());
    m_context << "#### Wrote " << pathAsQString(target) << "\n";
  }
  return {};
}

void CompilationHltoolsTaskRunner::finish(
  const int exitCode, const QProcess::ExitStatus status)
{
  if (m_finished)
    return;
  readOutput();
  const auto artifacts =
    publishAuxiliaryArtifacts(status == QProcess::NormalExit && exitCode == 0);
  if (artifacts.is_error())
  {
    fail(std::get<Error>(artifacts.error()).msg);
    return;
  }
  const auto logged = writeLog();
  if (logged.is_error())
  {
    fail(std::get<Error>(logged.error()).msg);
    return;
  }
  for (const auto* extension : {".lin", ".pts"})
  {
    auto leak = m_inputPath;
    leak.replace_extension(extension);
    auto target = m_target;
    target.replace_extension(extension);
    if (QFile::exists(pathAsQString(leak)))
    {
      if (const auto copied = copyArtifact(leak, target); copied.is_error())
        m_context << "#### "
                  << QString::fromStdString(std::get<Error>(copied.error()).msg) << "\n";
    }
    else if (
      status == QProcess::NormalExit && exitCode == 0
      && QFile::exists(pathAsQString(target)) && !QFile::remove(pathAsQString(target)))
      m_context << "#### Cannot remove stale leak file: " << pathAsQString(target)
                << "\n";
  }
  if (status != QProcess::NormalExit || exitCode != 0)
  {
    fail(
      "hltools failed with exit code " + std::to_string(exitCode)
      + ". See the compiler log.");
    return;
  }
  auto bspPath = m_inputPath;
  bspPath.replace_extension(".bsp");
  const auto copied = copyArtifact(bspPath, m_target);
  if (copied.is_error())
  {
    fail(std::get<Error>(copied.error()).msg);
    return;
  }
  m_finished = true;
  m_context << "#### Wrote " << pathAsQString(m_target) << "\n";
  m_context.setBspPath(m_target);
  emit end();
}

void CompilationHltoolsTaskRunner::fail(const std::string& message)
{
  if (m_finished)
    return;
  m_finished = true;
  m_context << "#### Compilation failed: " << QString::fromStdString(message) << "\n";
  emit error();
}

void CompilationHltoolsTaskRunner::doTerminate()
{
  m_finished = true;
  if (m_process)
  {
    readOutput();
    writeLog() | kdl::transform_error([&](const auto& e) {
      m_context << "#### " << QString::fromStdString(e.msg) << "\n";
    });
  }
  releaseProcess();
  m_context << "#### Terminated\n";
}

void CompilationHltoolsTaskRunner::releaseProcess()
{
  if (!m_process)
    return;
  auto* process = m_process;
  m_process = nullptr;
  process->disconnect(this);
  if (process->state() != QProcess::NotRunning)
  {
    // A killed process may still hold its scratch files. Keep them alive until exit.
    process->setParent(QCoreApplication::instance());
    connect(process, &QProcess::finished, process, &QObject::deleteLater);
    connect(process, &QProcess::errorOccurred, process, [process](const auto error) {
      if (error == QProcess::FailedToStart)
        process->deleteLater();
    });
    connect(process, &QObject::destroyed, [scratch = std::move(m_scratch)] {});
    if (process->state() == QProcess::Starting)
      connect(process, &QProcess::started, process, &QProcess::kill);
    else
      process->kill();
  }
  else
    delete process;
}
} // namespace tb::ui
