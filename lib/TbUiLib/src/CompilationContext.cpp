/*
 Copyright (C) 2010 Kristian Duske

 This file is part of TrenchBroom.

 TrenchBroom is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 TrenchBroom is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with TrenchBroom. If not, see <http://www.gnu.org/licenses/>.
 */

#include "ui/CompilationContext.h"

#include "el/EvaluationContext.h"
#include "el/Interpolate.h"
#include "el/Types.h"
#include "ui/CompilationVariables.h"

#include "kd/contracts.h"

namespace tb::ui
{

CompilationContext::CompilationContext(
  el::VariableTable variables, TextOutputAdapter output, const bool test)
  : m_variables{std::move(variables)}
  , m_output{std::move(output)}
  , m_test{test}
{
}

CompilationContext::CompilationContext(
  const mdl::Map& map,
  const gl::PerspectiveCamera& camera,
  el::VariableTable variables,
  TextOutputAdapter output,
  bool test,
  std::shared_ptr<const mdl::Map> snapshot)
  : m_snapshot{std::move(snapshot)}
  , m_map{m_snapshot ? m_snapshot.get() : &map}
  , m_camera{&camera}
  , m_variables{std::move(variables)}
  , m_output{std::move(output)}
  , m_test{test}
{
}

const mdl::Map& CompilationContext::map() const
{
  contract_assert(m_map != nullptr);
  return *m_map;
}

const gl::PerspectiveCamera& CompilationContext::camera() const
{
  contract_assert(m_camera != nullptr);
  return *m_camera;
}

bool CompilationContext::test() const
{
  return m_test;
}

void CompilationContext::setBspPath(std::filesystem::path path)
{
  m_bspPath = std::move(path);
  m_variables.set(CompilationVariableNames::BSP_PATH, el::Value{m_bspPath->string()});
}

const std::optional<std::filesystem::path>& CompilationContext::bspPath() const
{
  return m_bspPath;
}

void CompilationContext::setProjectSnapshotDirectory(std::filesystem::path path)
{
  m_projectSnapshotDirectory = std::move(path);
}

const std::optional<std::filesystem::path>& CompilationContext::projectSnapshotDirectory()
  const
{
  return m_projectSnapshotDirectory;
}

Result<std::string> CompilationContext::interpolate(const std::string& input) const
{
  return el::interpolate(m_variables, input);
}

Result<std::string> CompilationContext::variableValue(
  const std::string& variableName) const
{
  return el::withEvaluationContext(
    [&](auto& context) {
      return context.variableValue(variableName)
        .convertTo(el::ValueType::String)
        .stringValue();
    },
    m_variables);
}

} // namespace tb::ui
