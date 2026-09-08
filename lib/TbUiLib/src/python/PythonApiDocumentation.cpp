#include "ui/python/PythonApiDocumentation.h"

#include <QJsonObject>
#include <QStringList>

#include "ui/python/PythonApiCatalog.h"

#if defined(slots)
#undef slots
#endif
#include <pybind11/pybind11.h>

namespace py = pybind11;

namespace tb::ui
{
namespace
{
QJsonArray g_documentation;

QString effectName(const PythonApiEffect effect)
{
  switch (effect)
  {
  case PythonApiEffect::Read:
    return "read";
  case PythonApiEffect::Edit:
    return "edit";
  case PythonApiEffect::Action:
    return "action";
  case PythonApiEffect::Plugin:
    return "plugin";
  case PythonApiEffect::Value:
    return "value";
  }
  return {};
}

QString docString(const py::handle object)
{
  const auto doc = object.attr("__doc__");
  return doc.is_none() ? QString{} : QString::fromStdString(py::cast<std::string>(doc));
}
} // namespace

void initializePythonApiDocumentation(const py::module_& module)
{
  auto result = QJsonArray{};
  for (const auto& type : pythonApiTypes())
  {
    const auto owner = type.type == PythonApiType::Module ? py::object{module}
                                                          : module.attr(type.name.data());
    for (const auto& symbol : pythonApiSymbols(type.type))
    {
      const auto binding = owner.attr(symbol.name.data());
      const auto name = QString::fromUtf8(symbol.name);
      const auto qualified =
        type.type == PythonApiType::Module
          ? "trenchbroom." + name
          : "trenchbroom." + QString::fromUtf8(type.name) + "." + name;
      auto entry = QJsonObject{
        {"symbol", qualified},
        {"kind", static_cast<int>(symbol.kind)},
        {"effect", effectName(symbol.effect)},
      };
      auto signatures = QString{};
      if (PyObject_TypeCheck(binding.ptr(), &PyProperty_Type))
      {
        const auto writable = !binding.attr("fset").is_none();
        entry.insert("writable", writable);
        entry.insert("effect", writable ? effectName(symbol.effect) : "read");
        signatures = docString(binding.attr("fget"));
        const auto description = docString(binding).trimmed();
        if (!description.isEmpty())
          entry.insert("description", description);
      }
      else if (symbol.kind == PythonApiSymbolKind::Property)
      {
        // Module namespaces are values, not writable editor properties.
        entry.insert("effect", "read");
        signatures = QString::fromUtf8(symbol.detail);
      }
      else if (symbol.kind == PythonApiSymbolKind::Class)
      {
        entry.insert("effect", "value");
        signatures = docString(binding.attr("__init__"));
      }
      else
      {
        signatures = docString(binding);
      }
      // Keep actual pybind signatures, with authored binding documentation in
      // its own field. Preserve the complete signature block for overloads.
      const auto separator = signatures.indexOf("\n\n");
      if (separator >= 0 && !signatures.contains("Overloaded function."))
      {
        const auto description = signatures.mid(separator + 2).trimmed();
        if (!description.isEmpty())
          entry.insert("description", description);
        signatures = signatures.left(separator);
      }
      entry.insert("signature", signatures.trimmed());
      result.append(entry);
    }
  }
  g_documentation = std::move(result);
}

const QJsonArray& pythonApiDocumentation()
{
  return g_documentation;
}
} // namespace tb::ui
