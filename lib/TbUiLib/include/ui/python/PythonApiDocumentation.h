#pragma once

#include <QJsonArray>

namespace pybind11
{
class module_;
}

namespace tb::ui
{
// Capture native binding descriptors once, before plugins or transient scripts run.
// Discovery reads this C++ snapshot and never evaluates caller-supplied Python.
void initializePythonApiDocumentation(const pybind11::module_& module);
const QJsonArray& pythonApiDocumentation();
} // namespace tb::ui
