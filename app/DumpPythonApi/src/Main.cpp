/*
 Copyright (C) 2026 XiangXtreme

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
*/

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTextStream>

#include "ui/python/PythonApiDocumentation.h"
#include "ui/python/PythonRuntime.h"

#undef slots
#include <Python.h>

#include <array>
#include <utility>

namespace
{
bool writeFile(const QString& path, const QByteArray& contents)
{
  auto file = QSaveFile{path};
  return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size()
         && file.commit();
}

bool generateStubs(const QString& outputDirectory)
{
  auto gil = PyGILState_Ensure();
  auto arguments = PyList_New(0);
  const auto appendArgument = [&](const QString& argument) {
    auto* value = PyUnicode_FromString(argument.toUtf8().constData());
    if (value == nullptr)
    {
      return false;
    }
    const auto appended = PyList_Append(arguments, value) == 0;
    Py_DECREF(value);
    return appended;
  };
  const auto configured = arguments != nullptr && appendArgument("pybind11-stubgen")
                          && appendArgument("-o") && appendArgument(outputDirectory)
                          && appendArgument("--ignore-invalid-expressions")
                          && appendArgument(".*")
                          && appendArgument("trenchbroom");
  if (configured)
  {
    PySys_SetObject("argv", arguments);
  }
  Py_XDECREF(arguments);
  if (!configured)
  {
    PyGILState_Release(gil);
    return false;
  }

  auto* globals = PyDict_New();
  if (globals == nullptr)
  {
    PyGILState_Release(gil);
    return false;
  }
  PyDict_SetItemString(globals, "__builtins__", PyEval_GetBuiltins());
  auto* result = PyRun_String(
    "import runpy; runpy.run_module('pybind11_stubgen', run_name='__main__')",
    Py_file_input,
    globals,
    globals);
  const auto success = result != nullptr;
  Py_XDECREF(result);
  if (!success)
  {
    PyErr_Print();
  }
  Py_DECREF(globals);
  PyGILState_Release(gil);
  return success;
}

bool normalizeStubPackage(const QString& path)
{
  auto file = QFile{path};
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
  {
    return false;
  }
  auto contents = QString::fromUtf8(file.readAll());
  file.close();
  const auto replacements = std::array{
    std::pair{"def faces(self) -> ...:", "def faces(self) -> list[Face]:"},
    std::pair{"def entities(self) -> ...:", "def entities(self) -> list[Entity]:"},
    std::pair{
      "def material_collections(self) -> ...:",
      "def material_collections(self) -> list[MaterialCollection]:"},
    std::pair{"def materials(self) -> ...:", "def materials(self) -> list[Material]:"},
    std::pair{"def selection(self) -> ...:", "def selection(self) -> Selection:"},
    std::pair{
      "def transaction(self, name: str = 'Python API Script') -> ...:",
      "def transaction(self, name: str = 'Python API Script') -> Transaction:"},
    std::pair{"def brushes(self) -> ...:", "def brushes(self) -> list[Brush]:"},
    std::pair{
      "def all_entities(self) -> ...:", "def all_entities(self) -> list[Entity]:"},
    std::pair{
      "def brush_faces(self) -> ...:", "def brush_faces(self) -> list[Face]:"},
  };
  for (const auto& [from, to] : replacements)
  {
    contents.replace(QString::fromLatin1(from), QString::fromLatin1(to));
  }
  return writeFile(path, contents.toUtf8());
}
} // namespace

int main(int argc, char* argv[])
{
  auto app = QCoreApplication{argc, argv};
  auto parser = QCommandLineParser{};
  parser.setApplicationDescription("Dump TrenchBroom Python API documentation and stubs.");
  parser.addHelpOption();
  const auto output = QCommandLineOption{
    "output", "Directory for generated trenchbroom-api.json.", "directory"};
  const auto stubs = QCommandLineOption{
    "stubs", "Generate stubs into <output>/stubs/trenchbroom.", ""};
  parser.addOption(output);
  parser.addOption(stubs);
  if (!parser.parse(app.arguments()) || !parser.isSet(output))
  {
    QTextStream{stderr} << (parser.errorText().isEmpty() ? "--output is required" : parser.errorText())
                        << Qt::endl;
    return 1;
  }

  const auto outputDirectory = QDir{parser.value(output)};
  if (!outputDirectory.mkpath("."))
  {
    QTextStream{stderr} << "Could not create output directory" << Qt::endl;
    return 1;
  }
  if (!tb::ui::PythonRuntime::instance().ensureInitialized())
  {
    QTextStream{stderr} << "Could not initialize the embedded Python API" << Qt::endl;
    return 1;
  }
  const auto documentation = QJsonDocument{tb::ui::pythonApiDocumentation()}.toJson(
    QJsonDocument::Indented);
  if (!writeFile(outputDirectory.filePath("trenchbroom-api.json"), documentation))
  {
    QTextStream{stderr} << "Could not write API documentation" << Qt::endl;
    return 1;
  }
  if (!parser.isSet(stubs))
  {
    return 0;
  }

  const auto stubRoot = outputDirectory.filePath("stubs");
  if (!QDir{}.mkpath(stubRoot) || !generateStubs(stubRoot))
  {
    QTextStream{stderr} << "Could not generate stubs. Install pybind11-stubgen==2.5.5 first."
                        << Qt::endl;
    return 1;
  }
  const auto generatedStub = QDir{stubRoot}.filePath("trenchbroom/__init__.pyi");
  if (!QFile::exists(generatedStub) || !normalizeStubPackage(generatedStub))
  {
    QTextStream{stderr} << "Stub generator did not create a package entry point" << Qt::endl;
    return 1;
  }
  return 0;
}
