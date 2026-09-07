#include "ui/python/PythonApiModule.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSizePolicy>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextBrowser>
#include <QTextEdit>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include "base/Logger.h"
#include "gl/Material.h"
#include "gl/MaterialCollection.h"
#include "gl/MaterialManager.h"
#include "mdl/Brush.h"
#include "mdl/BrushBuilder.h"
#include "mdl/BrushFace.h"
#include "mdl/BrushFaceHandle.h"
#include "mdl/BrushNode.h"
#include "mdl/Entity.h"
#include "mdl/EntityNode.h"
#include "mdl/EntityNodeBase.h"
#include "mdl/ExportOptions.h"
#include "mdl/Grid.h"
#include "mdl/GroupNode.h"
#include "mdl/LayerNode.h"
#include "mdl/Map.h"
#include "mdl/MapFormat.h"
#include "mdl/Map_Brushes.h"
#include "mdl/Map_Entities.h"
#include "mdl/Map_Geometry.h"
#include "mdl/Map_Groups.h"
#include "mdl/Map_Nodes.h"
#include "mdl/Map_Selection.h"
#include "mdl/NodeHandles.h"
#include "mdl/PatchNode.h"
#include "mdl/Selection.h"
#include "mdl/UpdateBrushFaceAttributes.h"
#include "mdl/WorldNode.h"
#include "ui/Action.h"
#include "ui/ActionExecutionContext.h"
#include "ui/ActionManager.h"
#include "ui/AppController.h"
#include "ui/Inspector.h"
#include "ui/MapDocument.h"
#include "ui/MapWindow.h"
#include "ui/MapWindowManager.h"
#include "ui/QPathUtils.h"
#include "ui/automation/AutomationAssets.h"
#include "ui/automation/AutomationDocuments.h"
#include "ui/automation/AutomationObjectRegistry.h"
#include "ui/automation/AutomationTransaction.h"
#include "ui/automation/AutomationValidation.h"
#include "ui/python/PythonApiCatalog.h"
#include "ui/python/PythonExecutionContext.h"
#include "ui/python/PythonHandleRegistry.h"
#include "ui/python/PythonPluginSession.h"
#include "ui/python/PythonRuntime.h"

#include "kd/overload.h"

#include "vm/bbox.h"
#include "vm/plane.h"

#if defined(slots)
#undef slots
#endif

#include <pybind11/embed.h>
#include <pybind11/functional.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace py = pybind11;

namespace tb::ui
{
namespace
{
std::unordered_map<MapDocument*, size_t> g_activePythonTransactions;

template <typename Result>
void throwIfError(const Result& result)
{
  auto message = std::optional<std::string>{};
  static_cast<void>(result.if_error([&](const auto& error) { message = error.msg; }));
  if (message)
  {
    throw std::runtime_error{*message};
  }
}

std::string filenameAsUtf8(const std::filesystem::path& path)
{
  if (path.empty())
  {
    return "untitled";
  }

  const auto filename = path.filename().u8string();
  return {reinterpret_cast<const char*>(filename.data()), filename.size()};
}

struct Vec3
{
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

struct Plane
{
  Vec3 normal;
  double dist = 0.0;
};

Vec3 operator+(const Vec3& lhs, const Vec3& rhs)
{
  return Vec3{lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

Vec3 operator-(const Vec3& lhs, const Vec3& rhs)
{
  return Vec3{lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

Vec3 operator*(const Vec3& lhs, const double rhs)
{
  return Vec3{lhs.x * rhs, lhs.y * rhs, lhs.z * rhs};
}

Vec3 operator/(const Vec3& lhs, const double rhs)
{
  if (rhs == 0.0)
  {
    throw std::runtime_error{"Cannot divide Vec3 by zero"};
  }
  return Vec3{lhs.x / rhs, lhs.y / rhs, lhs.z / rhs};
}

double dot(const Vec3& lhs, const Vec3& rhs)
{
  return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

Vec3 cross(const Vec3& lhs, const Vec3& rhs)
{
  return Vec3{
    lhs.y * rhs.z - lhs.z * rhs.y,
    lhs.z * rhs.x - lhs.x * rhs.z,
    lhs.x * rhs.y - lhs.y * rhs.x};
}

double length(const Vec3& value)
{
  return std::sqrt(dot(value, value));
}

Vec3 normalize(const Vec3& value)
{
  return value / length(value);
}

vm::vec3d toVmVec3(const Vec3& value)
{
  return vm::vec3d{value.x, value.y, value.z};
}

Vec3 fromVmVec3(const vm::vec3d& value)
{
  return Vec3{value.x(), value.y(), value.z()};
}

vm::plane3d toVmPlane(const Plane& value)
{
  return vm::plane3d{value.dist, toVmVec3(value.normal)};
}

struct DocumentHandle
{
  MapDocument* document = nullptr;
  size_t generation = 0;

  MapDocument& get() const
  {
    if (
      document == nullptr
      || generation != PythonHandleRegistry::instance().documentGeneration(document))
    {
      throw std::runtime_error{"Document is no longer valid"};
    }
    return *document;
  }
};

struct SelectionHandle
{
  MapDocument* document = nullptr;
  size_t generation = 0;
  MapDocument& getDocument() const { return DocumentHandle{document, generation}.get(); }
};

struct EntityHandle
{
  MapDocument* document = nullptr;
  size_t generation = 0;
  mdl::EntityNodeBase* entity = nullptr;
  size_t nodeGeneration = 0;

  mdl::EntityNodeBase& get() const
  {
    DocumentHandle{document, generation}.get();
    if (
      entity == nullptr
      || nodeGeneration != PythonHandleRegistry::instance().nodeGeneration(entity))
    {
      throw std::runtime_error{"Entity is no longer valid"};
    }
    return *entity;
  }
};

struct BrushHandle
{
  MapDocument* document = nullptr;
  size_t generation = 0;
  mdl::BrushNode* brush = nullptr;
  size_t nodeLifetimeGeneration = 0;

  mdl::BrushNode& get() const
  {
    DocumentHandle{document, generation}.get();
    if (
      brush == nullptr
      || nodeLifetimeGeneration
           != PythonHandleRegistry::instance().nodeLifetimeGeneration(brush))
    {
      throw std::runtime_error{"Brush is no longer valid"};
    }
    return *brush;
  }
};

struct FaceHandle
{
  MapDocument* document = nullptr;
  size_t generation = 0;
  mdl::BrushNode* brush = nullptr;
  size_t nodeLifetimeGeneration = 0;
  vm::plane3d boundary;
  size_t faceIndex = 0;

  mdl::BrushNode& getBrushNode() const
  {
    auto& brushNode =
      BrushHandle{document, generation, brush, nodeLifetimeGeneration}.get();
    if (faceIndex >= brushNode.brush().faceCount())
    {
      throw std::runtime_error{"Face is no longer valid"};
    }
    if (!vm::is_equal(
          brushNode.brush().face(faceIndex).boundary(), boundary, vm::Cd::almost_zero()))
    {
      throw std::runtime_error{"Face is no longer valid"};
    }
    return brushNode;
  }

  const mdl::BrushFace& get() const
  {
    auto& brushNode = getBrushNode();
    return brushNode.brush().face(faceIndex);
  }
};

struct MaterialHandle
{
  const gl::Material* material = nullptr;

  const gl::Material& get() const
  {
    if (material == nullptr)
    {
      throw std::runtime_error{"Material is no longer valid"};
    }
    return *material;
  }
};

struct MaterialCollectionHandle
{
  const gl::MaterialCollection* collection = nullptr;

  const gl::MaterialCollection& get() const
  {
    if (collection == nullptr)
    {
      throw std::runtime_error{"Material collection is no longer valid"};
    }
    return *collection;
  }
};

struct TransactionHandle
{
  MapDocument* document = nullptr;
  size_t generation = 0;
  std::string name;
  std::unique_ptr<automation::AutomationTransaction> transaction;

  TransactionHandle(
    MapDocument* i_document, const size_t i_generation, std::string i_name)
    : document{i_document}
    , generation{i_generation}
    , name{std::move(i_name)}
  {
  }

  TransactionHandle& enter()
  {
    if (transaction)
    {
      throw std::runtime_error{"Transaction already started"};
    }
    auto& doc = DocumentHandle{document, generation}.get();
    transaction = std::make_unique<automation::AutomationTransaction>(doc.map(), name);
    ++g_activePythonTransactions[&doc];
    return *this;
  }

  bool commit()
  {
    if (!transaction)
    {
      throw std::runtime_error{"Transaction not started"};
    }
    const auto result = transaction->commit();
    transaction.reset();
    if (auto it = g_activePythonTransactions.find(document);
        it != std::end(g_activePythonTransactions) && it->second > 0u)
    {
      --it->second;
    }
    return result;
  }

  void cancel()
  {
    if (transaction)
    {
      transaction->cancel();
      transaction.reset();
      if (auto it = g_activePythonTransactions.find(document);
          it != std::end(g_activePythonTransactions) && it->second > 0u)
      {
        --it->second;
      }
    }
  }
};

class ScopedPythonTransaction
{
private:
  MapDocument& m_document;
  std::unique_ptr<automation::AutomationTransaction> m_transaction;

public:
  ScopedPythonTransaction(MapDocument& document, std::string name)
    : m_document{document}
  {
    if (g_activePythonTransactions[&m_document] == 0u)
    {
      m_transaction = std::make_unique<automation::AutomationTransaction>(
        m_document.map(), std::move(name));
    }
  }

  ~ScopedPythonTransaction()
  {
    if (m_transaction)
    {
      m_transaction->cancel();
    }
  }

  bool commit()
  {
    if (!m_transaction)
    {
      return true;
    }
    const auto result = m_transaction->commit();
    m_transaction.reset();
    return result;
  }

  void cancel()
  {
    if (m_transaction)
    {
      m_transaction->cancel();
      m_transaction.reset();
    }
  }
};

struct PluginPanelHandle
{
  QPointer<QWidget> container;

  QWidget& get() const
  {
    if (container == nullptr)
    {
      throw std::runtime_error{"Plugin panel is no longer valid"};
    }
    return *container;
  }
};

QLayout& ensurePanelLayout(QWidget& widget)
{
  auto* layout = widget.layout();
  if (layout == nullptr)
  {
    auto* vbox = new QVBoxLayout{};
    widget.setLayout(vbox);
    layout = vbox;
  }
  return *layout;
}

void clearLayout(QLayout& layout)
{
  while (auto* item = layout.takeAt(0))
  {
    if (auto* widget = item->widget())
    {
      widget->deleteLater();
    }
    if (auto* childLayout = item->layout())
    {
      clearLayout(*childLayout);
      childLayout->deleteLater();
    }
    delete item;
  }
}

QString panelObjectName(const std::string& prefix, const std::string& key)
{
  return QString::fromStdString("trenchbroom_panel_" + prefix + "_" + key);
}

template <typename T>
T& findPanelChild(QWidget& panel, const std::string& prefix, const std::string& key)
{
  auto* child = panel.findChild<T*>(panelObjectName(prefix, key));
  if (child == nullptr)
  {
    throw std::runtime_error{"Plugin panel control not found: " + key};
  }
  return *child;
}

void addFormRow(QWidget& panel, const std::string& label, QWidget* field)
{
  auto& layout = ensurePanelLayout(panel);
  auto* row = new QWidget{};
  auto* rowLayout = new QFormLayout{};
  rowLayout->setContentsMargins(0, 0, 0, 0);
  rowLayout->addRow(QString::fromStdString(label), field);
  row->setLayout(rowLayout);
  layout.addWidget(row);
}

QStringList toQStringList(const std::vector<std::string>& values)
{
  auto result = QStringList{};
  for (const auto& value : values)
  {
    result.push_back(QString::fromStdString(value));
  }
  return result;
}

void setTableRows(QTableWidget& table, const std::vector<std::vector<std::string>>& rows)
{
  const auto colCount = table.columnCount();
  table.blockSignals(true);
  table.clearContents();
  table.setRowCount(static_cast<int>(rows.size()));
  for (int row = 0; row < static_cast<int>(rows.size()); ++row)
  {
    const auto& values = rows[static_cast<size_t>(row)];
    for (int col = 0; col < std::min<int>(colCount, static_cast<int>(values.size()));
         ++col)
    {
      table.setItem(
        row,
        col,
        new QTableWidgetItem{QString::fromStdString(values[static_cast<size_t>(col)])});
    }
  }
  table.blockSignals(false);
}

void setTreeItems(QTreeWidget& tree, const std::vector<std::vector<std::string>>& rows)
{
  const auto colCount = tree.columnCount();
  tree.blockSignals(true);
  tree.clear();
  for (const auto& values : rows)
  {
    auto* item = new QTreeWidgetItem{};
    for (int col = 0; col < std::min<int>(colCount, static_cast<int>(values.size()));
         ++col)
    {
      item->setText(col, QString::fromStdString(values[static_cast<size_t>(col)]));
    }
    tree.addTopLevelItem(item);
  }
  tree.blockSignals(false);
}

QColor colorFromObject(const py::handle& object)
{
  auto sequence = py::reinterpret_borrow<py::sequence>(object);
  if (sequence.size() < 3)
  {
    throw py::type_error{"Expected a color sequence with at least 3 items"};
  }
  return QColor{
    py::cast<int>(sequence[0]), py::cast<int>(sequence[1]), py::cast<int>(sequence[2])};
}

struct CallbackEntry
{
  std::string pluginId;
  py::object callback;
};

std::unordered_map<int, CallbackEntry> g_callbacks;
std::unordered_map<std::string, std::vector<int>> g_eventCallbacks;
int g_nextCallbackToken = 1;

void logPythonCallbackError(PythonPluginSession& session, const py::error_already_set& e)
{
  if (session.context().logger != nullptr)
  {
    session.context().logger->error() << e.what();
  }
}

int registerPanelCallback(py::object callback)
{
  if (!PyCallable_Check(callback.ptr()))
  {
    throw py::type_error{"callback must be callable"};
  }

  auto* session = currentPythonPluginSession();
  auto* context = currentPythonExecutionContext();
  auto pluginId = std::string{};
  if (session != nullptr)
  {
    pluginId = session->pluginId();
  }
  else if (context != nullptr)
  {
    pluginId = context->pluginId;
  }

  const auto token = g_nextCallbackToken++;
  g_callbacks.emplace(token, CallbackEntry{std::move(pluginId), std::move(callback)});
  if (session != nullptr)
  {
    session->addCallbackToken(token);
  }
  return token;
}

template <typename... Args>
void invokeSessionCallback(PythonPluginSession* session, const int token, Args&&... args)
{
  auto gil = py::gil_scoped_acquire{};
  const auto callbackIt = g_callbacks.find(token);
  if (callbackIt == std::end(g_callbacks))
  {
    return;
  }

  if constexpr (sizeof...(Args) == 0)
  {
    if (session != nullptr)
    {
      PythonRuntime::instance().runCallback(*session, callbackIt->second.callback.ptr());
      return;
    }
  }

  try
  {
    callbackIt->second.callback(std::forward<Args>(args)...);
  }
  catch (const py::error_already_set& e)
  {
    if (session != nullptr)
    {
      logPythonCallbackError(*session, e);
    }
    else
    {
      PyErr_Print();
    }
  }
}

PythonExecutionContext& requireContext()
{
  auto* context = currentPythonExecutionContext();
  if (context == nullptr)
  {
    throw std::runtime_error{"No active Python execution context"};
  }
  return *context;
}

automation::AutomationObjectRegistry& objectRegistry()
{
  auto& context = requireContext();
  if (context.objectRegistry != nullptr)
  {
    return *context.objectRegistry;
  }

  // Console and plugin execution have no MCP session owner. The local service
  // still enforces map switching and stale-node detection for their handles.
  static auto registry = automation::AutomationObjectRegistry{};
  return registry;
}

std::string documentId(DocumentHandle& document)
{
  return objectRegistry().documentFingerprint(document.get().map()).toStdString();
}

std::string nodeId(MapDocument& document, mdl::Node& node)
{
  return objectRegistry().registerNode(document.map(), node).toStdString();
}

std::string faceId(FaceHandle& face)
{
  auto& brush = face.getBrushNode();
  return "face:" + nodeId(DocumentHandle{face.document, face.generation}.get(), brush)
         + ":" + std::to_string(face.faceIndex);
}

DocumentHandle currentDocument()
{
  auto& context = requireContext();
  if (context.document == nullptr)
  {
    throw std::runtime_error{"No active document"};
  }
  return DocumentHandle{
    context.document,
    PythonHandleRegistry::instance().documentGeneration(context.document)};
}

std::vector<DocumentHandle> openDocuments()
{
  auto& context = requireContext();
  auto result = std::vector<DocumentHandle>{};
  if (context.appController != nullptr)
  {
    for (auto* mapWindow : context.appController->mapWindowManager().mapWindows())
    {
      if (mapWindow == nullptr)
      {
        continue;
      }
      auto& document = mapWindow->document();
      result.push_back(DocumentHandle{
        &document, PythonHandleRegistry::instance().documentGeneration(&document)});
    }
  }
  if (
    context.document != nullptr
    && std::none_of(result.begin(), result.end(), [&](const auto& document) {
         return document.document == context.document;
       }))
  {
    result.push_back(DocumentHandle{
      context.document,
      PythonHandleRegistry::instance().documentGeneration(context.document)});
  }
  return result;
}

std::filesystem::path absolutePathFromPython(const std::string& value)
{
  const auto path = pathFromQString(
    QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size())));
  if (!path.is_absolute())
  {
    throw py::value_error{"path must be absolute"};
  }
  return path;
}

void requirePythonActionMode(const char* const action)
{
  const auto& context = requireContext();
  if (context.mcpExecution && !context.allowNonTransactionalActions)
  {
    throw std::runtime_error{
      std::string{"MCP Python "} + action + " requires mode='action'"};
  }
}

void saveDocument(DocumentHandle& document)
{
  requirePythonActionMode("save");
  throwIfError(document.get().map().save());
}

void saveDocumentAs(DocumentHandle& document, const std::string& path)
{
  requirePythonActionMode("save_as");
  throwIfError(document.get().map().saveAs(absolutePathFromPython(path)));
}

void exportDocument(
  DocumentHandle& document, const std::string& path, const bool stripTbProperties)
{
  requirePythonActionMode("export");
  const auto exportPath = absolutePathFromPython(path);
  if (exportPath == document.get().map().path())
  {
    throw py::value_error{"export path must not overwrite the document"};
  }
  throwIfError(document.get().map().exportAs(mdl::MapExportOptions{
    exportPath,
    stripTbProperties,
    std::nullopt,
    std::nullopt,
  }));
}

MapWindow& mapWindowForDocument(DocumentHandle& document)
{
  auto& context = requireContext();
  if (context.appController == nullptr)
  {
    throw std::runtime_error{"No application controller in Python execution context"};
  }

  auto& targetDocument = document.get();
  for (auto* mapWindow : context.appController->mapWindowManager().mapWindows())
  {
    if (mapWindow != nullptr && &mapWindow->document() == &targetDocument)
    {
      return *mapWindow;
    }
  }
  throw std::runtime_error{"Document window is no longer available"};
}

DocumentHandle openDocument(const std::string& path)
{
  requirePythonActionMode("open");
  auto& context = requireContext();
  if (context.appController == nullptr || context.document == nullptr)
  {
    throw std::runtime_error{"No active document"};
  }

  const auto openPath = absolutePathFromPython(path);
  if (!std::filesystem::is_regular_file(openPath))
  {
    throw py::value_error{"document path must name an existing regular file"};
  }

  auto contextPathError = std::error_code{};
  if (
    context.document->map().path().lexically_normal() == openPath
    || std::filesystem::equivalent(
      context.document->map().path(), openPath, contextPathError))
  {
    return DocumentHandle{
      context.document,
      PythonHandleRegistry::instance().documentGeneration(context.document)};
  }

  auto& mapWindowManager = context.appController->mapWindowManager();
  for (auto* mapWindow : mapWindowManager.mapWindows())
  {
    auto pathError = std::error_code{};
    const auto samePath =
      mapWindow != nullptr
      && (mapWindow->document().map().path().lexically_normal() == openPath
          || std::filesystem::equivalent(
            mapWindow->document().map().path(), openPath, pathError));
    if (samePath && !pathError)
    {
      if (!mapWindowManager.activateMapWindow(*mapWindow))
      {
        throw std::runtime_error{"Document window is no longer available"};
      }
      auto& document = mapWindow->document();
      return DocumentHandle{
        &document, PythonHandleRegistry::instance().documentGeneration(&document)};
    }
  }

  throwIfError(openAutomationDocument(
    *context.appController, openPath, &context.document->map().gameInfo()));

  auto* openedWindow = mapWindowManager.topMapWindow();
  if (openedWindow == nullptr)
  {
    throw std::runtime_error{"Document open completed without an active window"};
  }
  if (&openedWindow->document() == context.document)
  {
    PythonHandleRegistry::instance().invalidateDocument(context.document);
  }
  return DocumentHandle{
    &openedWindow->document(),
    PythonHandleRegistry::instance().documentGeneration(&openedWindow->document())};
}

DocumentHandle activateDocument(DocumentHandle& document)
{
  requirePythonActionMode("activate");
  auto& context = requireContext();
  if (context.appController == nullptr)
  {
    throw std::runtime_error{"No application controller in Python execution context"};
  }
  if (&document.get() == context.document)
  {
    return document;
  }
  auto& window = mapWindowForDocument(document);
  if (!context.appController->mapWindowManager().activateMapWindow(window))
  {
    throw std::runtime_error{"Document window is no longer available"};
  }
  return document;
}

void closeDocument(DocumentHandle& document, const bool discardChanges)
{
  requirePythonActionMode("close");
  auto& targetDocument = document.get();
  if (targetDocument.map().modified() && !discardChanges)
  {
    throw py::value_error{
      "Document has unsaved changes; pass discard_changes=True to close it"};
  }

  auto& window = mapWindowForDocument(document);
  PythonHandleRegistry::instance().invalidateDocument(&targetDocument);
  window.closeDocument(discardChanges);
}

py::dict historyStatus(DocumentHandle& document)
{
  const auto& map = document.get().map();
  auto result = py::dict{};
  result["can_undo"] = map.canUndoCommand();
  result["can_redo"] = map.canRedoCommand();
  result["undo_name"] =
    map.undoCommandName() != nullptr ? py::cast(*map.undoCommandName()) : py::none();
  result["redo_name"] =
    map.redoCommandName() != nullptr ? py::cast(*map.redoCommandName()) : py::none();
  return result;
}

bool undoDocument(DocumentHandle& document)
{
  requirePythonActionMode("history.undo");
  auto& window = mapWindowForDocument(document);
  if (!window.canUndo())
  {
    return false;
  }
  window.undo();
  return true;
}

bool redoDocument(DocumentHandle& document)
{
  requirePythonActionMode("history.redo");
  auto& window = mapWindowForDocument(document);
  if (!window.canRedo())
  {
    return false;
  }
  window.redo();
  return true;
}

Vec3 vec3FromObject(const py::handle& object)
{
  if (py::isinstance<Vec3>(object))
  {
    return py::cast<Vec3>(object);
  }
  auto sequence = py::reinterpret_borrow<py::sequence>(object);
  if (sequence.size() != 3)
  {
    throw py::type_error{"Expected Vec3 or a 3-item sequence"};
  }
  return Vec3{
    py::cast<double>(sequence[0]),
    py::cast<double>(sequence[1]),
    py::cast<double>(sequence[2])};
}

std::vector<vm::vec3d> pointsFromObjects(const py::iterable& objects)
{
  auto result = std::vector<vm::vec3d>{};
  for (const auto object : objects)
  {
    const auto point = vec3FromObject(object);
    result.push_back(toVmVec3(point));
  }
  return result;
}

py::tuple vec2ToTuple(const vm::vec2f& value)
{
  return py::make_tuple(value.x(), value.y());
}

py::tuple vec3ToTuple(const vm::vec3d& value)
{
  return py::make_tuple(value.x(), value.y(), value.z());
}

vm::vec2f vec2FromObject(const py::handle& object)
{
  auto sequence = py::reinterpret_borrow<py::sequence>(object);
  if (sequence.size() != 2)
  {
    throw py::type_error{"Expected a 2-item sequence"};
  }
  return vm::vec2f{py::cast<float>(sequence[0]), py::cast<float>(sequence[1])};
}

vm::vec2f textureCoords(const mdl::BrushFace& face, const vm::vec3d& point)
{
  const auto uvAttributes = face.uvAttributes();
  return vm::vec2f{
    face.toUvCoordSystemMatrix(uvAttributes.offset, uvAttributes.scale) * point};
}

size_t addVertex(std::vector<vm::vec3d>& vertices, const vm::vec3d& point)
{
  const auto it = std::ranges::find(vertices, point);
  if (it != std::end(vertices))
  {
    return static_cast<size_t>(std::distance(std::begin(vertices), it));
  }

  vertices.push_back(point);
  return vertices.size() - 1u;
}

std::vector<EntityHandle> allEntities(MapDocument& document)
{
  auto result = std::vector<mdl::EntityNodeBase*>{};
  auto visitNode = std::function<void(mdl::Node&)>{};
  visitNode = [&](mdl::Node& node) {
    node.accept(kdl::overload(
      [&](mdl::WorldNode& worldNode) {
        result.push_back(&worldNode);
        worldNode.visitChildren(visitNode);
      },
      [&](mdl::LayerNode& layerNode) { layerNode.visitChildren(visitNode); },
      [&](mdl::GroupNode& groupNode) { groupNode.visitChildren(visitNode); },
      [&](mdl::EntityNode& entityNode) { result.push_back(&entityNode); },
      [&](mdl::BrushNode& brushNode) { result.push_back(brushNode.entity()); },
      [&](mdl::PatchNode& patchNode) { result.push_back(patchNode.entity()); }));
  };
  visitNode(document.map().worldNode());

  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());

  auto handles = std::vector<EntityHandle>{};
  handles.reserve(result.size());
  const auto generation = PythonHandleRegistry::instance().documentGeneration(&document);
  for (auto* entity : result)
  {
    handles.push_back(EntityHandle{
      &document,
      generation,
      entity,
      PythonHandleRegistry::instance().nodeGeneration(entity)});
  }
  return handles;
}

std::vector<BrushHandle> allBrushes(MapDocument& document)
{
  auto brushes = std::vector<mdl::BrushNode*>{};
  auto visitNode = std::function<void(mdl::Node&)>{};
  visitNode = [&](mdl::Node& node) {
    if (auto* brushNode = dynamic_cast<mdl::BrushNode*>(&node))
    {
      brushes.push_back(brushNode);
      return;
    }
    for (auto* child : node.children())
    {
      if (child != nullptr)
      {
        visitNode(*child);
      }
    }
  };
  visitNode(document.map().worldNode());

  auto result = std::vector<BrushHandle>{};
  result.reserve(brushes.size());
  const auto generation = PythonHandleRegistry::instance().documentGeneration(&document);
  for (auto* brush : brushes)
  {
    result.push_back(BrushHandle{
      &document,
      generation,
      brush,
      PythonHandleRegistry::instance().nodeLifetimeGeneration(brush)});
  }
  return result;
}

bool containsCaseInsensitive(const std::string_view value, const std::string_view query)
{
  if (query.empty())
  {
    return true;
  }
  const auto match =
    std::ranges::search(value, query, [](const char lhs, const char rhs) {
      return std::tolower(static_cast<unsigned char>(lhs))
             == std::tolower(static_cast<unsigned char>(rhs));
    });
  return match.begin() != value.end();
}

std::vector<EntityHandle> findEntities(
  MapDocument& document,
  const std::optional<std::string>& classname,
  const std::optional<std::string>& property,
  const std::optional<std::string>& value)
{
  auto result = std::vector<EntityHandle>{};
  for (auto entity : allEntities(document))
  {
    const auto& model = entity.get().entity();
    if (classname && model.classname() != *classname)
    {
      continue;
    }
    if (property && !model.hasProperty(*property))
    {
      continue;
    }
    if (value)
    {
      auto matches = false;
      for (const auto& candidate : model.properties())
      {
        if (
          (!property || candidate.key() == *property)
          && containsCaseInsensitive(candidate.value(), *value))
        {
          matches = true;
          break;
        }
      }
      if (!matches)
      {
        continue;
      }
    }
    result.push_back(std::move(entity));
  }
  return result;
}

std::vector<EntityHandle> selectedEntities(SelectionHandle& selection);
std::vector<FaceHandle> selectedBrushFaces(SelectionHandle& selection);

py::dict boundsSnapshot(const vm::bbox3d& bounds)
{
  auto result = py::dict{};
  result["min"] = py::make_tuple(bounds.min.x(), bounds.min.y(), bounds.min.z());
  result["max"] = py::make_tuple(bounds.max.x(), bounds.max.y(), bounds.max.z());
  return result;
}

struct MapContentSummary
{
  int entityCount = 0;
  int brushCount = 0;
  int patchCount = 0;
  std::optional<vm::bbox3d> bounds;
};

void collectMapContentSummary(const mdl::Node& node, MapContentSummary& summary)
{
  const auto contentNode = dynamic_cast<const mdl::EntityNode*>(&node) != nullptr
                           || dynamic_cast<const mdl::BrushNode*>(&node) != nullptr
                           || dynamic_cast<const mdl::PatchNode*>(&node) != nullptr;
  if (dynamic_cast<const mdl::EntityNode*>(&node) != nullptr)
  {
    ++summary.entityCount;
  }
  else if (dynamic_cast<const mdl::BrushNode*>(&node) != nullptr)
  {
    ++summary.brushCount;
  }
  else if (dynamic_cast<const mdl::PatchNode*>(&node) != nullptr)
  {
    ++summary.patchCount;
  }
  if (contentNode)
  {
    summary.bounds = summary.bounds ? vm::merge(*summary.bounds, node.logicalBounds())
                                    : node.logicalBounds();
  }
  for (const auto* child : node.children())
  {
    if (child != nullptr)
    {
      collectMapContentSummary(*child, summary);
    }
  }
}

py::dict documentSnapshot(DocumentHandle document)
{
  auto& map = document.get().map();
  const auto& selection = map.selection();
  const auto& path = map.path();
  auto content = MapContentSummary{};
  collectMapContentSummary(map.worldNode(), content);
  auto worldspawn = py::dict{};
  for (const auto& property : map.worldNode().entity().properties())
  {
    worldspawn[py::cast(property.key())] = py::cast(property.value());
  }
  auto result = py::dict{};
  result["path"] = path.empty() ? py::none() : py::cast(path.u8string());
  result["persistent"] = map.persistent();
  result["modified"] = map.modified();
  result["entity_count"] = py::int_(allEntities(document.get()).size());
  result["point_entity_count"] = content.entityCount;
  result["brush_count"] = content.brushCount;
  result["patch_count"] = content.patchCount;
  result["node_count"] = py::int_(map.worldNode().descendantCount() + 1u);
  result["worldspawn"] = std::move(worldspawn);
  if (content.bounds)
  {
    result["content_bounds"] = boundsSnapshot(*content.bounds);
  }
  else
  {
    result["content_bounds"] = py::none();
  }
  result["map_format"] = mdl::formatName(map.worldNode().mapFormat());
  result["selected_node_count"] = py::int_(selection.nodes.size());
  result["selected_entity_count"] = py::int_(selection.entities.size());
  result["selected_brush_count"] = py::int_(selection.brushes.size());
  result["selected_face_count"] = py::int_(selection.brushFaces.size());
  result["grid_size"] = map.grid().size();
  result["grid_actual_size"] = map.grid().actualSize();
  result["grid_snap"] = map.grid().snap();
  result["grid_visible"] = map.grid().visible();
  return result;
}

py::dict selectionSnapshot(SelectionHandle selection)
{
  const auto& mapSelection = selection.getDocument().map().selection();
  auto result = py::dict{};
  result["has_selection"] = mapSelection.hasAny();
  result["node_count"] = py::int_(mapSelection.nodes.size());
  result["entity_count"] = py::int_(mapSelection.entities.size());
  result["brush_count"] = py::int_(mapSelection.brushes.size());
  result["face_count"] = py::int_(mapSelection.brushFaces.size());
  if (const auto& bounds = selection.getDocument().map().selectionBounds())
  {
    result["bounds"] = boundsSnapshot(*bounds);
  }
  else
  {
    result["bounds"] = py::none();
  }
  result["entities"] = selectedEntities(selection);
  auto brushes = std::vector<BrushHandle>{};
  brushes.reserve(mapSelection.brushes.size());
  for (auto* brush : mapSelection.brushes)
  {
    brushes.push_back(BrushHandle{
      &selection.getDocument(),
      selection.generation,
      brush,
      PythonHandleRegistry::instance().nodeLifetimeGeneration(brush)});
  }
  result["brushes"] = std::move(brushes);
  result["faces"] = selectedBrushFaces(selection);
  return result;
}

py::object selectedObjectBounds(SelectionHandle selection)
{
  const auto& bounds = selection.getDocument().map().selectionBounds();
  return bounds ? py::object{boundsSnapshot(*bounds)} : py::none();
}

py::dict validationCheck(const bool includeHidden, const size_t limit)
{
  auto document = currentDocument();
  const auto issues =
    collectAutomationValidationIssues(document.get().map(), includeHidden);
  const auto returnedCount = std::min(limit, issues.size());

  auto result = py::dict{};
  auto summaries = py::list{};
  auto safeFixableCount = size_t{0u};
  for (auto index = size_t{0u}; index < returnedCount; ++index)
  {
    const auto& issue = issues[index];
    auto summary = py::dict{};
    summary["id"] = issue.id;
    summary["stable_key"] = issue.stableKey;
    summary["type"] = issue.type;
    summary["severity"] = "warning";
    summary["message"] = issue.message;
    summary["object_id"] = issue.objectId;
    summary["object_type"] = issue.objectType;
    summary["line_number"] = issue.lineNumber;
    summary["hidden"] = issue.hidden;
    auto bounds = py::dict{};
    bounds["min"] =
      py::make_tuple(issue.boundsMin[0], issue.boundsMin[1], issue.boundsMin[2]);
    bounds["max"] =
      py::make_tuple(issue.boundsMax[0], issue.boundsMax[1], issue.boundsMax[2]);
    summary["bounds"] = std::move(bounds);
    summary["safe_quick_fixes"] = issue.safeQuickFixes;
    summary["face_index"] = issue.faceIndex ? py::cast(*issue.faceIndex) : py::none();
    summary["property_key"] =
      issue.propertyKey ? py::cast(*issue.propertyKey) : py::none();
    if (!issue.safeQuickFixes.empty())
    {
      ++safeFixableCount;
    }
    summaries.append(std::move(summary));
  }

  result["valid"] = issues.empty();
  result["passed"] = issues.empty();
  result["count"] = returnedCount;
  result["total_count"] = issues.size();
  result["truncated"] = returnedCount < issues.size();
  result["safe_fixable_count"] = safeFixableCount;
  result["issues"] = std::move(summaries);
  return result;
}

py::dict moduleSummary(const automation::AutomationModuleRecord& module)
{
  auto result = py::dict{};
  result["id"] = module.moduleId;
  result["document_fingerprint"] = module.documentFingerprint;
  result["metadata"] = module.metadata.toVariantMap();
  result["revision"] = module.revision;
  result["active_operation_id"] = module.activeOperationId;
  result["content_hash"] = module.contentHash;
  result["quality_policy"] = module.qualityPolicy.toVariantMap();
  result["object_count"] = module.objectIds.size();
  result["operation_count"] = module.operationIds.size();
  return result;
}

std::vector<py::dict> modulesForCurrentDocument()
{
  const auto& context = requireContext();
  if (context.moduleStore == nullptr)
  {
    return {};
  }

  const auto document = currentDocument();
  const auto fingerprint = objectRegistry().documentFingerprint(document.get().map());
  auto result = std::vector<py::dict>{};
  auto seen = std::set<QString>{};
  for (const auto& [key, module] : *context.moduleStore)
  {
    Q_UNUSED(key);
    if (
      module.documentFingerprint != fingerprint || module.moduleId.isEmpty()
      || seen.contains(module.moduleId))
    {
      continue;
    }
    seen.insert(module.moduleId);
    result.push_back(moduleSummary(module));
  }
  return result;
}

py::dict inspectModule(const std::string& moduleId)
{
  for (auto& summary : modulesForCurrentDocument())
  {
    if (py::cast<std::string>(summary["id"]) == moduleId)
    {
      return summary;
    }
  }
  throw py::key_error{"Unknown module '" + moduleId + "'"};
}

py::dict groupSummary(const mdl::GroupNode& group)
{
  auto result = py::dict{};
  result["name"] = group.group().name();
  result["bounds"] = boundsSnapshot(group.logicalBounds());
  result["child_count"] = group.childCount();
  result["descendant_count"] = group.descendantCount();
  result["opened"] = group.opened();
  result["closed"] = group.closed();
  return result;
}

std::vector<mdl::GroupNode*> selectedGroups(SelectionHandle selection)
{
  const auto& groups = selection.getDocument().map().selection().groups;
  return {groups.begin(), groups.end()};
}

py::dict createGroupFromSelection(SelectionHandle selection, const std::string& name)
{
  if (name.empty())
  {
    throw py::value_error{"name must not be empty"};
  }

  auto* group = mdl::groupSelectedNodes(selection.getDocument().map(), name);
  if (group == nullptr)
  {
    throw std::runtime_error{"Selected objects cannot be grouped"};
  }
  return groupSummary(*group);
}

py::list inspectSelectedGroups(SelectionHandle selection)
{
  auto result = py::list{};
  for (const auto* group : selectedGroups(selection))
  {
    result.append(groupSummary(*group));
  }
  return result;
}

py::list renameSelectedGroups(SelectionHandle selection, const std::string& name)
{
  if (name.empty())
  {
    throw py::value_error{"name must not be empty"};
  }

  auto& map = selection.getDocument().map();
  if (!map.selection().hasOnlyGroups())
  {
    throw py::value_error{"Current selection must contain only groups"};
  }
  mdl::renameSelectedGroups(map, name);
  return inspectSelectedGroups(selection);
}

py::dict ungroupSelectedGroups(SelectionHandle selection)
{
  if (selectedGroups(selection).empty())
  {
    throw py::value_error{"Current selection must contain groups"};
  }
  mdl::ungroupSelectedNodes(selection.getDocument().map());
  return selectionSnapshot(selection);
}

std::vector<BrushHandle> entityBrushes(EntityHandle& entity)
{
  auto& entityNode = entity.get();
  auto brushes = std::vector<mdl::BrushNode*>{};
  auto visitNode = std::function<void(mdl::Node&)>{};
  visitNode = [&](mdl::Node& node) {
    node.accept(kdl::overload(
      [&](mdl::WorldNode& worldNode) { worldNode.visitChildren(visitNode); },
      [&](mdl::LayerNode& layerNode) { layerNode.visitChildren(visitNode); },
      [&](mdl::GroupNode& groupNode) { groupNode.visitChildren(visitNode); },
      [&](mdl::EntityNode& childEntityNode) {
        if (&childEntityNode == &entityNode)
        {
          childEntityNode.visitChildren(visitNode);
        }
      },
      [&](mdl::BrushNode& brushNode) {
        if (brushNode.entity() == &entityNode)
        {
          brushes.push_back(&brushNode);
        }
      },
      [](mdl::PatchNode&) {}));
  };
  visitNode(entityNode);

  auto result = std::vector<BrushHandle>{};
  result.reserve(brushes.size());
  for (auto* brush : brushes)
  {
    result.push_back(BrushHandle{
      entity.document,
      entity.generation,
      brush,
      PythonHandleRegistry::instance().nodeLifetimeGeneration(brush)});
  }
  return result;
}

EntityHandle brushEntity(BrushHandle& self)
{
  auto& brushNode = self.get();
  auto* entityNode = brushNode.entity();
  if (entityNode == nullptr)
  {
    throw std::runtime_error{"Brush does not belong to any entity"};
  }
  return EntityHandle{
    self.document,
    self.generation,
    entityNode,
    PythonHandleRegistry::instance().nodeGeneration(entityNode)};
}

std::vector<EntityHandle> selectedEntities(SelectionHandle& selection)
{
  auto& document = selection.getDocument();
  auto result = std::vector<EntityHandle>{};
  const auto generation = PythonHandleRegistry::instance().documentGeneration(&document);
  for (auto* entity : document.map().selection().entities)
  {
    result.push_back(EntityHandle{
      &document,
      generation,
      entity,
      PythonHandleRegistry::instance().nodeGeneration(entity)});
  }
  return result;
}

std::vector<mdl::EntityNodeBase*> selectedEntityNodes(SelectionHandle& selection)
{
  const auto& mapSelection = selection.getDocument().map().selection();
  auto result = std::vector<mdl::EntityNodeBase*>{};
  auto visitNode = std::function<void(mdl::Node&)>{};
  visitNode = [&](mdl::Node& node) {
    node.accept(kdl::overload(
      [](mdl::WorldNode&) {},
      [](mdl::LayerNode&) {},
      [&](mdl::GroupNode& groupNode) { groupNode.visitChildren(visitNode); },
      [&](mdl::EntityNode& entityNode) { result.push_back(&entityNode); },
      [&](mdl::BrushNode& brushNode) { result.push_back(brushNode.entity()); },
      [&](mdl::PatchNode& patchNode) { result.push_back(patchNode.entity()); }));
  };

  for (auto* node : mapSelection.nodes)
  {
    visitNode(*node);
  }
  for (const auto& faceHandle : mapSelection.brushFaces)
  {
    result.push_back(faceHandle.node()->entity());
  }

  return kdl::vec_sort_and_remove_duplicates(std::move(result));
}

std::vector<EntityHandle> selectedAllEntities(SelectionHandle& selection)
{
  auto& document = selection.getDocument();
  auto result = std::vector<EntityHandle>{};
  const auto generation = PythonHandleRegistry::instance().documentGeneration(&document);
  for (auto* entity : selectedEntityNodes(selection))
  {
    result.push_back(EntityHandle{
      &document,
      generation,
      entity,
      PythonHandleRegistry::instance().nodeGeneration(entity)});
  }
  return result;
}

py::object selectionFirstEntity(SelectionHandle& self)
{
  auto allEnts = selectedAllEntities(self);
  if (!allEnts.empty())
  {
    return py::cast(allEnts.front());
  }
  return py::none();
}

py::object selectionFirstBrush(SelectionHandle& self)
{
  const auto& brushes = self.getDocument().map().selection().brushes;
  if (!brushes.empty())
  {
    auto* brush = brushes.front();
    return py::cast(BrushHandle{
      &self.getDocument(),
      self.generation,
      brush,
      PythonHandleRegistry::instance().nodeLifetimeGeneration(brush)});
  }
  return py::none();
}

py::object selectionProperties(SelectionHandle& self)
{
  auto allEnts = selectedAllEntities(self);
  if (!allEnts.empty())
  {
    auto dict = py::dict{};
    for (const auto& property : allEnts.front().get().entity().properties())
    {
      dict[py::cast(property.key())] = py::cast(property.value());
    }
    return dict;
  }
  return py::none();
}

std::vector<Vec3> vertexToolVertices(DocumentHandle& document)
{
  auto result = std::vector<Vec3>{};
  auto handles = document.get().map().nodeHandles().selectedHandles<mdl::VertexHandle>();
  for (const auto& handle : handles)
  {
    result.push_back(fromVmVec3(handle.position));
  }
  return result;
}

std::vector<std::vector<Vec3>> selectedBrushVertices(SelectionHandle& selection)
{
  const auto& mapSelection = selection.getDocument().map().selection();

  auto brushNodes = std::vector<mdl::BrushNode*>{};
  brushNodes.insert(
    brushNodes.end(), mapSelection.allBrushes().begin(), mapSelection.allBrushes().end());

  if (mapSelection.hasBrushFaces())
  {
    auto nodesFromFaces = mdl::toNodes(mapSelection.brushFaces);
    brushNodes.insert(brushNodes.end(), nodesFromFaces.begin(), nodesFromFaces.end());
  }

  brushNodes = kdl::vec_sort_and_remove_duplicates(std::move(brushNodes));

  auto result = std::vector<std::vector<Vec3>>{};
  result.reserve(brushNodes.size());
  for (auto* brushNode : brushNodes)
  {
    auto vertices = std::vector<Vec3>{};
    for (const auto& vertex : brushNode->brush().vertexPositions())
    {
      vertices.push_back(fromVmVec3(vertex));
    }
    result.push_back(std::move(vertices));
  }
  return result;
}

std::vector<mdl::BrushFaceHandle> selectedTriangleFaceHandles(SelectionHandle& selection)
{
  const auto& mapSelection = selection.getDocument().map().selection();
  auto faceHandles = mapSelection.brushFaces;
  if (faceHandles.empty())
  {
    for (auto* brushNode : mapSelection.brushes)
    {
      for (size_t i = 0; i < brushNode->brush().faceCount(); ++i)
      {
        faceHandles.emplace_back(brushNode, i);
      }
    }
  }

  auto result = std::vector<mdl::BrushFaceHandle>{};
  for (const auto& faceHandle : faceHandles)
  {
    if (faceHandle.face().vertexCount() == 3u)
    {
      result.push_back(faceHandle);
    }
  }
  return result;
}

py::dict selectedTriangleUVs(SelectionHandle& selection)
{
  auto vertices = std::vector<vm::vec3d>{};
  auto triangles = py::list{};
  for (const auto& faceHandle : selectedTriangleFaceHandles(selection))
  {
    const auto& face = faceHandle.face();
    const auto faceVertices = face.vertexPositions();

    auto triangleVertices = py::list{};
    auto loopList = py::list{};
    for (const auto& vertex : faceVertices)
    {
      const auto vertexIndex = addVertex(vertices, vertex);
      triangleVertices.append(vertexIndex);

      auto loop = py::dict{};
      loop["vertex"] = vertexIndex;
      loop["uv"] = vec2ToTuple(textureCoords(face, vertex));
      loopList.append(loop);
    }

    auto triangle = py::dict{};
    triangle["id"] = "tri" + std::to_string(py::len(triangles));
    triangle["vertices"] = triangleVertices;
    triangle["loops"] = loopList;
    triangles.append(triangle);
  }

  auto vertexList = py::list{};
  for (const auto& vertex : vertices)
  {
    vertexList.append(vec3ToTuple(vertex));
  }

  auto result = py::dict{};
  result["vertices"] = vertexList;
  result["triangles"] = triangles;
  return result;
}

py::list faceVertices(FaceHandle& face)
{
  auto result = py::list{};
  for (const auto& vertex : face.get().vertexPositions())
  {
    result.append(vec3ToTuple(vertex));
  }
  return result;
}

py::list faceUVLoops(FaceHandle& face)
{
  const auto& brushFace = face.get();
  const auto vertices = brushFace.vertexPositions();
  auto result = py::list{};
  for (size_t i = 0; i < vertices.size(); ++i)
  {
    auto loop = py::dict{};
    loop["vertex"] = i;
    loop["uv"] = vec2ToTuple(textureCoords(brushFace, vertices[i]));
    result.append(loop);
  }
  return result;
}

std::vector<FaceHandle> selectedBrushFaces(SelectionHandle& selection)
{
  auto result = std::vector<FaceHandle>{};
  const auto& faceHandles = selection.getDocument().map().selection().brushFaces;
  result.reserve(faceHandles.size());
  for (const auto& faceHandle : faceHandles)
  {
    auto* brushNode = faceHandle.node();
    result.push_back(FaceHandle{
      &selection.getDocument(),
      selection.generation,
      brushNode,
      PythonHandleRegistry::instance().nodeLifetimeGeneration(brushNode),
      faceHandle.face().boundary(),
      faceHandle.faceIndex()});
  }
  return result;
}

std::vector<FaceHandle> allFaces(MapDocument& document)
{
  auto result = std::vector<FaceHandle>{};
  for (auto brush : allBrushes(document))
  {
    const auto& model = brush.get().brush();
    for (auto index = size_t{0}; index < model.faceCount(); ++index)
    {
      result.push_back(FaceHandle{
        brush.document,
        brush.generation,
        brush.brush,
        brush.nodeLifetimeGeneration,
        model.face(index).boundary(),
        index});
    }
  }
  return result;
}

std::vector<mdl::Node*> selectableNodesFromObjects(const py::iterable& objects)
{
  auto result = std::vector<mdl::Node*>{};
  for (const auto object : objects)
  {
    auto pyObject = py::reinterpret_borrow<py::object>(object);
    if (py::isinstance<EntityHandle>(pyObject))
    {
      auto& entity = py::cast<EntityHandle&>(pyObject);
      result.push_back(&entity.get());
    }
    else if (py::isinstance<BrushHandle>(pyObject))
    {
      auto& brush = py::cast<BrushHandle&>(pyObject);
      result.push_back(&brush.get());
    }
    else
    {
      throw py::type_error{"Expected Entity or Brush"};
    }
  }
  return result;
}

vm::vec3d selectionCenter(mdl::Map& map)
{
  const auto bounds = map.selectionBounds();
  if (!bounds)
  {
    throw std::runtime_error{"Selection bounds are not available"};
  }
  return bounds->min + bounds->size() / 2.0;
}

bool updateSelection(
  SelectionHandle& selection,
  const std::vector<mdl::Node*>& nodes,
  const std::string& name)
{
  auto& document = selection.getDocument();
  auto transaction = ScopedPythonTransaction{document, name};
  try
  {
    mdl::selectNodes(document.map(), nodes);
    if (!transaction.commit())
    {
      throw std::runtime_error{"Could not update selection"};
    }
    return true;
  }
  catch (...)
  {
    transaction.cancel();
    throw;
  }
}

bool setSelection(SelectionHandle& selection, const py::iterable& objects)
{
  return updateSelection(
    selection, selectableNodesFromObjects(objects), "Python API Set Selection");
}

bool addSelection(SelectionHandle& selection, const py::iterable& objects)
{
  auto& document = selection.getDocument();
  auto nodes = document.map().selection().nodes;
  auto nodesToAdd = selectableNodesFromObjects(objects);
  nodes.insert(nodes.end(), nodesToAdd.begin(), nodesToAdd.end());
  nodes = kdl::vec_sort_and_remove_duplicates(std::move(nodes));
  return updateSelection(selection, nodes, "Python API Add Selection");
}

bool deselectAllSelection(SelectionHandle& selection)
{
  auto& document = selection.getDocument();
  auto transaction = ScopedPythonTransaction{document, "Python API Clear Selection"};
  try
  {
    mdl::deselectAll(document.map());
    if (!transaction.commit())
    {
      throw std::runtime_error{"Could not clear selection"};
    }
    return true;
  }
  catch (...)
  {
    transaction.cancel();
    throw;
  }
}

bool duplicateSelection(SelectionHandle& selection)
{
  auto& document = selection.getDocument();
  auto transaction = ScopedPythonTransaction{document, "Python API Duplicate Selection"};
  try
  {
    mdl::duplicateSelectedNodes(document.map());
    if (!transaction.commit())
    {
      throw std::runtime_error{"Could not duplicate selection"};
    }
    return true;
  }
  catch (...)
  {
    transaction.cancel();
    throw;
  }
}

bool translateSelection(
  SelectionHandle& selection, const double x, const double y, const double z)
{
  auto& document = selection.getDocument();
  auto transaction = ScopedPythonTransaction{document, "Python API Translate Selection"};
  try
  {
    const auto ok = mdl::translateSelection(document.map(), vm::vec3d{x, y, z});
    if (!ok || !transaction.commit())
    {
      transaction.cancel();
      return false;
    }
    return true;
  }
  catch (...)
  {
    transaction.cancel();
    throw;
  }
}

bool rotateSelection(
  SelectionHandle& selection,
  const double axisX,
  const double axisY,
  const double axisZ,
  const double angleDegrees,
  std::optional<double> centerX,
  std::optional<double> centerY,
  std::optional<double> centerZ)
{
  auto& document = selection.getDocument();
  auto& map = document.map();
  auto transaction = ScopedPythonTransaction{document, "Python API Rotate Selection"};
  try
  {
    const auto center = centerX && centerY && centerZ
                          ? vm::vec3d{*centerX, *centerY, *centerZ}
                          : selectionCenter(map);
    constexpr auto pi = 3.1415926535897932384626433832795;
    const auto ok = mdl::rotateSelection(
      map, center, vm::vec3d{axisX, axisY, axisZ}, angleDegrees * (pi / 180.0));
    if (!ok || !transaction.commit())
    {
      transaction.cancel();
      return false;
    }
    return true;
  }
  catch (...)
  {
    transaction.cancel();
    throw;
  }
}

bool scaleSelection(
  SelectionHandle& selection,
  const double scaleX,
  const double scaleY,
  const double scaleZ,
  std::optional<double> centerX,
  std::optional<double> centerY,
  std::optional<double> centerZ)
{
  auto& document = selection.getDocument();
  auto& map = document.map();
  auto transaction = ScopedPythonTransaction{document, "Python API Scale Selection"};
  try
  {
    const auto center = centerX && centerY && centerZ
                          ? vm::vec3d{*centerX, *centerY, *centerZ}
                          : selectionCenter(map);
    const auto ok = mdl::scaleSelection(map, center, vm::vec3d{scaleX, scaleY, scaleZ});
    if (!ok || !transaction.commit())
    {
      transaction.cancel();
      return false;
    }
    return true;
  }
  catch (...)
  {
    transaction.cancel();
    throw;
  }
}

bool chamferSelectionVertices(SelectionHandle& selection, const double distance)
{
  auto& document = selection.getDocument();
  auto& map = document.map();
  auto transaction = ScopedPythonTransaction{document, "Python API Chamfer Vertices"};
  try
  {
    const auto ok = mdl::chamferVertices(
      map,
      "Python API Chamfer Vertices",
      mdl::VertexHandle::getPositions(
        map.nodeHandles().selectedHandles<mdl::VertexHandle>()),
      distance);
    if (!ok || !transaction.commit())
    {
      transaction.cancel();
      return false;
    }
    return true;
  }
  catch (...)
  {
    transaction.cancel();
    throw;
  }
}

bool chamferSelectionEdges(
  SelectionHandle& selection, const double distance, const int segments)
{
  auto& document = selection.getDocument();
  auto& map = document.map();
  auto transaction = ScopedPythonTransaction{document, "Python API Chamfer Edges"};
  try
  {
    const auto safeSegments = std::max(segments, 1);
    const auto ok = mdl::chamferEdges(
      map,
      "Python API Chamfer Edges",
      mdl::EdgeHandle::getPositions(map.nodeHandles().selectedHandles<mdl::EdgeHandle>()),
      distance,
      safeSegments);
    if (!ok || !transaction.commit())
    {
      transaction.cancel();
      return false;
    }
    return true;
  }
  catch (...)
  {
    transaction.cancel();
    throw;
  }
}

void withPreservedSelection(
  MapDocument& document,
  std::string transactionName,
  const std::function<bool(mdl::Map&)>& operation)
{
  auto& map = document.map();
  const auto previousNodes = map.selection().nodes;
  const auto previousBrushFaces = map.selection().brushFaces;

  auto transaction = ScopedPythonTransaction{document, std::move(transactionName)};
  try
  {
    if (!operation(map))
    {
      throw std::runtime_error{"Python API edit failed"};
    }

    mdl::deselectAll(map);
    if (!previousNodes.empty())
    {
      mdl::selectNodes(map, previousNodes);
    }
    if (!previousBrushFaces.empty())
    {
      mdl::selectBrushFaces(map, previousBrushFaces);
    }

    if (!transaction.commit())
    {
      throw std::runtime_error{"Python API edit failed"};
    }
  }
  catch (...)
  {
    transaction.cancel();
    throw;
  }
}

void setEntityProperty(
  EntityHandle& entity, const std::string& key, const std::string& value)
{
  auto& document = DocumentHandle{entity.document, entity.generation}.get();
  auto* entityNode = &entity.get();

  withPreservedSelection(document, "Python API Set Entity Property", [&](auto& map) {
    mdl::deselectAll(map);
    mdl::selectNodes(map, {entityNode});
    return mdl::setEntityProperty(map, key, value);
  });

  entity.nodeGeneration = PythonHandleRegistry::instance().nodeGeneration(entity.entity);
}

void removeEntityProperty(EntityHandle& entity, const std::string& key)
{
  auto& document = DocumentHandle{entity.document, entity.generation}.get();
  auto* entityNode = &entity.get();

  withPreservedSelection(document, "Python API Remove Entity Property", [&](auto& map) {
    mdl::deselectAll(map);
    mdl::selectNodes(map, {entityNode});
    return mdl::removeEntityProperty(map, key);
  });

  entity.nodeGeneration = PythonHandleRegistry::instance().nodeGeneration(entity.entity);
}

EntityHandle createEntity(
  const std::string& classname,
  const py::dict& properties,
  const py::object& origin,
  const bool select)
{
  if (classname.empty())
  {
    throw py::value_error{"classname must not be empty"};
  }

  auto& document = currentDocument().get();
  auto& map = document.map();
  auto entity = mdl::Entity{{{mdl::EntityPropertyKeys::Classname, classname}}};
  for (const auto& item : properties)
  {
    const auto key = py::cast<std::string>(item.first);
    if (key != mdl::EntityPropertyKeys::Classname)
    {
      entity.addOrUpdateProperty(key, py::cast<std::string>(item.second));
    }
  }
  if (!origin.is_none())
  {
    entity.setOrigin(toVmVec3(vec3FromObject(origin)));
  }

  auto transaction = ScopedPythonTransaction{document, "Python API Create Entity"};
  auto* entityNode = new mdl::EntityNode{std::move(entity)};
  try
  {
    const auto addedNodes =
      mdl::addNodes(map, {{&mdl::parentForNodes(map), {entityNode}}});
    if (addedNodes.empty())
    {
      delete entityNode;
      throw std::runtime_error{"Could not add entity"};
    }
    if (select)
    {
      mdl::selectNodes(map, {entityNode});
    }
    if (!transaction.commit())
    {
      throw std::runtime_error{"Could not create entity"};
    }
  }
  catch (...)
  {
    transaction.cancel();
    throw;
  }

  return EntityHandle{
    &document,
    PythonHandleRegistry::instance().documentGeneration(&document),
    entityNode,
    PythonHandleRegistry::instance().nodeGeneration(entityNode)};
}

EntityHandle placeAsset(
  const std::string& path,
  const std::string_view expectedExtension,
  const std::string& classname,
  const std::string& property,
  const py::object& origin,
  const bool select)
{
  auto extension = std::filesystem::path{path}.extension().string();
  std::ranges::transform(extension, extension.begin(), [](const unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  if (extension != expectedExtension)
  {
    throw py::value_error{
      "asset path must have the " + std::string{expectedExtension} + " extension"};
  }
  if (property.empty())
  {
    throw py::value_error{"property must not be empty"};
  }

  auto properties = py::dict{};
  properties[py::str{property}] = py::str{path};
  return createEntity(classname, properties, origin, select);
}

std::string assetTypeName(const BrowserCellType type)
{
  switch (type)
  {
  case BrowserCellType::Model:
    return "model";
  case BrowserCellType::Sprite:
    return "sprite";
  case BrowserCellType::Sound:
    return "sound";
  case BrowserCellType::Folder:
  case BrowserCellType::Prefab:
    return "unknown";
  }
  return "unknown";
}

std::optional<BrowserCellType> assetTypeFromName(const std::string& type)
{
  if (type == "model")
  {
    return BrowserCellType::Model;
  }
  if (type == "sprite")
  {
    return BrowserCellType::Sprite;
  }
  if (type == "sound")
  {
    return BrowserCellType::Sound;
  }
  return std::nullopt;
}

py::list searchAssets(
  const std::string& query, const py::object& type, const size_t limit)
{
  auto options = AutomationAssetSearchOptions{};
  options.query = query;
  options.limit = limit;
  if (!type.is_none())
  {
    const auto typeName = py::cast<std::string>(type);
    options.type = assetTypeFromName(typeName);
    if (!options.type)
    {
      throw py::value_error{"type must be model, sprite, sound, or None"};
    }
  }

  auto document = currentDocument();
  const auto assets = searchAutomationAssets(document.get().map(), options);
  if (!assets)
  {
    throw std::runtime_error{"Could not scan assets"};
  }

  auto result = py::list{};
  for (const auto& asset : *assets)
  {
    auto summary = py::dict{};
    summary["type"] = assetTypeName(asset.type);
    summary["path"] = asset.path.generic_string();
    summary["absolute_path"] = asset.absolutePath.generic_string();
    summary["display_name"] = asset.displayName;
    if (asset.lastModified)
    {
      summary["last_modified"] = asset.lastModified->time_since_epoch().count();
    }
    else
    {
      summary["last_modified"] = py::none();
    }
    result.append(std::move(summary));
  }
  return result;
}

void deleteEntity(EntityHandle& entity)
{
  auto& document = DocumentHandle{entity.document, entity.generation}.get();
  auto* entityNode = &entity.get();
  withPreservedSelection(document, "Python API Delete Entity", [&](auto& map) {
    mdl::deselectAll(map);
    mdl::selectNodes(map, {entityNode});
    mdl::removeSelectedNodes(map);
    return true;
  });
}

mdl::Entity updatedEntity(
  const mdl::Entity& source,
  const py::dict& properties,
  const std::vector<std::string>& removeKeys)
{
  auto result = source;
  for (const auto& item : properties)
  {
    result.addOrUpdateProperty(
      py::cast<std::string>(item.first), py::cast<std::string>(item.second));
  }
  for (const auto& key : removeKeys)
  {
    if (key != mdl::EntityPropertyKeys::Classname)
    {
      result.removeProperty(key);
    }
  }
  return result;
}

void updateEntity(
  EntityHandle& entity,
  const py::dict& properties,
  const std::vector<std::string>& removeKeys)
{
  auto& document = DocumentHandle{entity.document, entity.generation}.get();
  auto* entityNode = &entity.get();
  auto replacement = updatedEntity(entityNode->entity(), properties, removeKeys);
  withPreservedSelection(document, "Python API Update Entity", [&](auto& map) {
    return mdl::updateNodeContents(
      map,
      "Python API Update Entity",
      {{entityNode, mdl::NodeContents{std::move(replacement)}}});
  });
  entity.nodeGeneration = PythonHandleRegistry::instance().nodeGeneration(entity.entity);
}

void updateEntityProperties(
  const py::iterable& entities,
  const py::dict& properties,
  const std::vector<std::string>& removeKeys)
{
  auto handles = std::vector<EntityHandle>{};
  for (const auto& entity : entities)
  {
    handles.push_back(py::cast<EntityHandle>(entity));
  }
  std::ranges::sort(handles, {}, &EntityHandle::entity);
  handles.erase(
    std::unique(
      handles.begin(),
      handles.end(),
      [](const auto& lhs, const auto& rhs) { return lhs.entity == rhs.entity; }),
    handles.end());
  if (handles.empty())
  {
    return;
  }

  auto& document =
    DocumentHandle{handles.front().document, handles.front().generation}.get();
  auto replacements = std::vector<std::pair<mdl::Node*, mdl::NodeContents>>{};
  replacements.reserve(handles.size());
  for (auto& handle : handles)
  {
    if (handle.document != &document)
    {
      throw py::value_error{"All entities must belong to the same document"};
    }
    auto* entityNode = &handle.get();
    replacements.emplace_back(
      entityNode,
      mdl::NodeContents{updatedEntity(entityNode->entity(), properties, removeKeys)});
  }

  withPreservedSelection(document, "Python API Update Entity Properties", [&](auto& map) {
    return mdl::updateNodeContents(
      map, "Python API Update Entity Properties", std::move(replacements));
  });
}

bool setSelectionProperty(
  SelectionHandle& selection,
  const std::string& key,
  const std::string& value,
  const bool createIfMissing)
{
  auto& document = selection.getDocument();
  auto entityNodes = selectedEntityNodes(selection);
  if (!createIfMissing)
  {
    std::erase_if(entityNodes, [&](const auto* entityNode) {
      return !entityNode->entity().hasProperty(key);
    });
  }
  if (entityNodes.empty())
  {
    return false;
  }

  const auto nodes = std::vector<mdl::Node*>{entityNodes.begin(), entityNodes.end()};
  withPreservedSelection(document, "Python API Set Selection Property", [&](auto& map) {
    mdl::deselectAll(map);
    mdl::selectNodes(map, nodes);
    return mdl::setEntityProperty(map, key, value);
  });
  return true;
}

py::list triangleListFromObject(const py::handle& object)
{
  if (py::isinstance<py::dict>(object))
  {
    auto dict = py::reinterpret_borrow<py::dict>(object);
    return py::reinterpret_borrow<py::list>(dict["triangles"]);
  }
  return py::reinterpret_borrow<py::list>(object);
}

bool setDocumentTriangleUVs(DocumentHandle& document, const py::object& trianglesObject)
{
  auto& doc = document.get();
  auto selection =
    SelectionHandle{&doc, PythonHandleRegistry::instance().documentGeneration(&doc)};
  const auto faceHandles = selectedTriangleFaceHandles(selection);
  const auto triangles = triangleListFromObject(trianglesObject);
  if (
    faceHandles.empty() || static_cast<size_t>(py::len(triangles)) != faceHandles.size())
  {
    return false;
  }

  auto updates = std::vector<mdl::TriangleUVUpdate>{};
  updates.reserve(faceHandles.size());
  for (size_t i = 0; i < faceHandles.size(); ++i)
  {
    const auto triangle = py::reinterpret_borrow<py::dict>(triangles[i]);
    const auto loops = py::reinterpret_borrow<py::list>(triangle["loops"]);
    if (py::len(loops) != 3u)
    {
      return false;
    }

    const auto vertices = faceHandles[i].face().vertexPositions();
    auto uvs = std::array<vm::vec2f, 3>{};
    auto assigned = std::array<bool, 3>{};
    auto triangleVertices = py::list{};
    if (triangle.contains("vertices"))
    {
      triangleVertices = py::reinterpret_borrow<py::list>(triangle["vertices"]);
      if (py::len(triangleVertices) != 3u)
      {
        return false;
      }
    }

    for (size_t j = 0; j < 3u; ++j)
    {
      const auto loop = py::reinterpret_borrow<py::dict>(loops[j]);
      auto localIndex = j;
      if (py::len(triangleVertices) == 3u && loop.contains("vertex"))
      {
        const auto vertexIndex = py::cast<size_t>(loop["vertex"]);
        const auto it = std::ranges::find_if(triangleVertices, [&](const auto item) {
          return py::cast<size_t>(item) == vertexIndex;
        });
        if (it == std::end(triangleVertices))
        {
          return false;
        }
        localIndex = static_cast<size_t>(std::distance(std::begin(triangleVertices), it));
      }
      uvs[localIndex] = vec2FromObject(loop["uv"]);
      assigned[localIndex] = true;
    }
    if (!std::ranges::all_of(assigned, [](const auto value) { return value; }))
    {
      return false;
    }

    updates.push_back(mdl::TriangleUVUpdate{
      faceHandles[i],
      {vertices[0], vertices[1], vertices[2]},
      uvs,
    });
  }
  return mdl::setTriangleUVs(doc.map(), updates);
}

bool setDocumentFaceUVsImpl(
  DocumentHandle& document, const py::iterable& updateObjects, const bool splitNonAffine)
{
  auto& doc = document.get();
  auto updates = std::vector<mdl::FaceUVUpdate>{};
  for (const auto updateObject : updateObjects)
  {
    const auto update = py::reinterpret_borrow<py::dict>(updateObject);
    auto face = py::cast<FaceHandle>(update["face"]);
    if (face.document != &doc)
    {
      return false;
    }
    auto& brushNode = face.getBrushNode();

    const auto vertices = brushNode.brush().face(face.faceIndex).vertexPositions();
    const auto loops = py::reinterpret_borrow<py::list>(update["loops"]);
    if (static_cast<size_t>(py::len(loops)) != vertices.size())
    {
      return false;
    }

    auto uvs = std::vector<vm::vec2f>(vertices.size());
    auto assigned = std::vector<bool>(vertices.size(), false);
    for (const auto loopObject : loops)
    {
      const auto loop = py::reinterpret_borrow<py::dict>(loopObject);
      const auto vertexIndex = py::cast<size_t>(loop["vertex"]);
      if (vertexIndex >= vertices.size() || assigned[vertexIndex])
      {
        return false;
      }
      uvs[vertexIndex] = vec2FromObject(loop["uv"]);
      assigned[vertexIndex] = true;
    }
    if (!std::ranges::all_of(assigned, [](const auto value) { return value; }))
    {
      return false;
    }

    auto materialName = std::optional<std::string>{};
    if (update.contains("material") && !update["material"].is_none())
    {
      materialName = py::cast<std::string>(update["material"]);
    }
    updates.push_back(mdl::FaceUVUpdate{
      mdl::BrushFaceHandle{&brushNode, face.faceIndex},
      vertices,
      std::move(uvs),
      std::move(materialName),
    });
  }

  return !updates.empty()
         && (splitNonAffine ? mdl::setFaceUVsWithSplit(doc.map(), updates)
                            : mdl::setFaceUVs(doc.map(), updates));
}

bool setDocumentFaceUVs(DocumentHandle& document, const py::iterable& updateObjects)
{
  return setDocumentFaceUVsImpl(document, updateObjects, false);
}

bool setDocumentFaceUVsWithSplit(
  DocumentHandle& document, const py::iterable& updateObjects)
{
  return setDocumentFaceUVsImpl(document, updateObjects, true);
}

bool setFaceUVLoops(FaceHandle& face, const py::iterable& loopObjects)
{
  auto& document = DocumentHandle{face.document, face.generation}.get();
  auto& brushNode = face.getBrushNode();

  const auto vertices = brushNode.brush().face(face.faceIndex).vertexPositions();
  auto uvs = std::vector<vm::vec2f>(vertices.size());
  auto assigned = std::vector<bool>(vertices.size(), false);

  for (const auto loopObject : loopObjects)
  {
    const auto loop = py::reinterpret_borrow<py::dict>(loopObject);
    const auto vertexIndex = py::cast<size_t>(loop["vertex"]);
    if (vertexIndex >= vertices.size())
    {
      return false;
    }
    uvs[vertexIndex] = vec2FromObject(loop["uv"]);
    assigned[vertexIndex] = true;
  }
  if (!std::ranges::all_of(assigned, [](const auto value) { return value; }))
  {
    return false;
  }

  const auto ok = mdl::setFaceUVs(
    document.map(),
    {{
      mdl::BrushFaceHandle{&brushNode, face.faceIndex},
      vertices,
      uvs,
    }});
  return ok;
}

void setFaceMaterial(FaceHandle& face, const std::string& materialName)
{
  auto& document = DocumentHandle{face.document, face.generation}.get();
  auto& brushNode = face.getBrushNode();

  withPreservedSelection(document, "Python API Set Face Material", [&](auto& map) {
    mdl::deselectAll(map);
    mdl::selectBrushFaces(map, {mdl::BrushFaceHandle{&brushNode, face.faceIndex}});
    return mdl::setBrushFaceAttributes(map, {.materialName = materialName});
  });
}

void setFacesMaterial(const py::iterable& faces, const std::string& materialName)
{
  auto handles = std::vector<FaceHandle>{};
  for (const auto& face : faces)
  {
    handles.push_back(py::cast<FaceHandle>(face));
  }
  std::ranges::sort(
    handles, {}, [](const auto& face) { return std::pair{face.brush, face.faceIndex}; });
  handles.erase(
    std::unique(
      handles.begin(),
      handles.end(),
      [](const auto& lhs, const auto& rhs) {
        return lhs.brush == rhs.brush && lhs.faceIndex == rhs.faceIndex;
      }),
    handles.end());
  if (handles.empty())
  {
    return;
  }

  auto& document =
    DocumentHandle{handles.front().document, handles.front().generation}.get();
  auto brushFaces = std::vector<mdl::BrushFaceHandle>{};
  brushFaces.reserve(handles.size());
  for (const auto& face : handles)
  {
    if (face.document != &document)
    {
      throw py::value_error{"All faces must belong to the same document"};
    }
    auto& brushNode = face.getBrushNode();
    brushFaces.emplace_back(&brushNode, face.faceIndex);
  }

  withPreservedSelection(document, "Python API Set Face Materials", [&](auto& map) {
    mdl::deselectAll(map);
    mdl::selectBrushFaces(map, brushFaces);
    return mdl::setBrushFaceAttributes(map, {.materialName = materialName});
  });
}

void updateFace(FaceHandle& face, mdl::UpdateBrushFaceAttributes update)
{
  auto& document = DocumentHandle{face.document, face.generation}.get();
  auto& brushNode = face.getBrushNode();

  withPreservedSelection(document, "Python API Set Face Attributes", [&](auto& map) {
    mdl::deselectAll(map);
    mdl::selectBrushFaces(map, {mdl::BrushFaceHandle{&brushNode, face.faceIndex}});
    return mdl::setBrushFaceAttributes(map, update);
  });
}

void setFaceOffset(FaceHandle& face, const py::object& offset)
{
  const auto value = vec2FromObject(offset);
  updateFace(
    face,
    mdl::UpdateBrushFaceAttributes{
      .xOffset = mdl::SetValue{value.x()}, .yOffset = mdl::SetValue{value.y()}});
}

void setFaceScale(FaceHandle& face, const py::object& scale)
{
  const auto value = vec2FromObject(scale);
  updateFace(
    face,
    mdl::UpdateBrushFaceAttributes{
      .xScale = mdl::SetValue{value.x()}, .yScale = mdl::SetValue{value.y()}});
}

void setFaceRotation(FaceHandle& face, const float rotation)
{
  updateFace(face, mdl::UpdateBrushFaceAttributes{.rotation = mdl::SetValue{rotation}});
}

void setFaceSurfaceContents(FaceHandle& face, const py::object& value)
{
  updateFace(
    face,
    mdl::UpdateBrushFaceAttributes{
      .surfaceContents = mdl::SetFlags{
        value.is_none() ? std::nullopt : std::make_optional(py::cast<int>(value))}});
}

void setFaceSurfaceFlags(FaceHandle& face, const py::object& value)
{
  updateFace(
    face,
    mdl::UpdateBrushFaceAttributes{
      .surfaceFlags = mdl::SetFlags{
        value.is_none() ? std::nullopt : std::make_optional(py::cast<int>(value))}});
}

void setFaceSurfaceValue(FaceHandle& face, const py::object& value)
{
  updateFace(
    face,
    mdl::UpdateBrushFaceAttributes{
      .surfaceValue = mdl::SetValue{
        value.is_none() ? std::nullopt : std::make_optional(py::cast<float>(value))}});
}

BrushHandle createBrush(const py::iterable& pointObjects, py::object materialName)
{
  auto& document = currentDocument().get();
  auto& map = document.map();
  const auto points = pointsFromObjects(pointObjects);
  const auto material = materialName.is_none() ? map.currentMaterialName()
                                               : py::cast<std::string>(materialName);
  auto transaction = ScopedPythonTransaction{document, "Python API Create Brush"};
  auto builder = mdl::BrushBuilder{map.worldNode().mapFormat(), map.worldBounds()};
  auto brush = builder.createBrush(points, material);
  if (brush.is_error())
  {
    transaction.cancel();
    throw std::runtime_error{"Could not create brush from points"};
  }

  auto* brushNode = new mdl::BrushNode{std::move(brush).value()};
  const auto addedNodes = mdl::addNodes(map, {{&mdl::parentForNodes(map), {brushNode}}});
  if (addedNodes.empty())
  {
    transaction.cancel();
    throw std::runtime_error{"Could not add brush"};
  }
  mdl::selectNodes(map, {brushNode});
  if (!transaction.commit())
  {
    throw std::runtime_error{"Could not create brush"};
  }

  const auto generation = PythonHandleRegistry::instance().documentGeneration(&document);
  return BrushHandle{
    &document,
    generation,
    brushNode,
    PythonHandleRegistry::instance().nodeLifetimeGeneration(brushNode)};
}

void executeAction(const std::string& actionPath)
{
  auto& context = requireContext();
  if (context.mcpExecution && !context.allowNonTransactionalActions)
  {
    throw std::runtime_error{"MCP Python actions require mode='action'"};
  }
  if (context.mapWindow == nullptr || context.appController == nullptr)
  {
    throw std::runtime_error{"No active map window"};
  }

  const auto path = std::filesystem::path{actionPath};
  const auto& actionsMap = ActionManager::instance().actionsMap();
  const auto actionIt = actionsMap.find(path);
  if (actionIt == std::end(actionsMap))
  {
    throw py::key_error{actionPath};
  }

  auto actionContext = ActionExecutionContext{
    *context.appController, context.mapWindow, context.currentMapView};
  const auto& action = actionIt->second;
  if (!action.enabled(actionContext))
  {
    throw std::runtime_error{"Action is disabled"};
  }
  action.execute(actionContext);
}

std::vector<std::string> listActions()
{
  auto result = std::vector<std::string>{};
  const auto& actionsMap = ActionManager::instance().actionsMap();
  result.reserve(actionsMap.size());
  for (const auto& [path, action] : actionsMap)
  {
    unused(action);
    result.push_back(path.generic_string());
  }
  return result;
}

PluginPanelHandle createPluginPanel(const std::string& title)
{
  auto& context = requireContext();
  if (!context.allowPersistentUi)
  {
    throw std::runtime_error{"MCP Python cannot create persistent plugin panels"};
  }
  if (context.mapWindow == nullptr)
  {
    throw std::runtime_error{"No active map window"};
  }

  auto* container = context.mapWindow->addPluginPanel(QString::fromStdString(title));
  context.mapWindow->switchToInspectorPage(InspectorPage::Plugin);
  if (auto* session = currentPythonPluginSession())
  {
    session->addPluginPanel(container);
  }
  return PluginPanelHandle{QPointer<QWidget>{container}};
}

int registerCallback(const std::string& eventName, py::object callback)
{
  if (!PyCallable_Check(callback.ptr()))
  {
    throw py::type_error{"callback must be callable"};
  }

  auto& context = requireContext();
  if (!context.allowPersistentUi)
  {
    throw std::runtime_error{"MCP Python cannot register persistent callbacks"};
  }
  const auto token = g_nextCallbackToken++;
  g_callbacks.emplace(token, CallbackEntry{context.pluginId, std::move(callback)});
  g_eventCallbacks[eventName].push_back(token);
  if (auto* session = currentPythonPluginSession())
  {
    session->addCallbackToken(token);
  }
  return token;
}

int setInterval(py::object callback, const int milliseconds)
{
  if (!PyCallable_Check(callback.ptr()))
  {
    throw py::type_error{"callback must be callable"};
  }
  if (milliseconds <= 0)
  {
    throw py::value_error{"milliseconds must be greater than zero"};
  }
  auto* session = currentPythonPluginSession();
  if (session == nullptr)
  {
    throw std::runtime_error{"Timers require an active Python plugin session"};
  }
  return session->addIntervalTimer(callback.ptr(), milliseconds, false);
}

int setTimeout(py::object callback, const int milliseconds)
{
  if (!PyCallable_Check(callback.ptr()))
  {
    throw py::type_error{"callback must be callable"};
  }
  if (milliseconds <= 0)
  {
    throw py::value_error{"milliseconds must be greater than zero"};
  }
  auto* session = currentPythonPluginSession();
  if (session == nullptr)
  {
    throw std::runtime_error{"Timers require an active Python plugin session"};
  }
  return session->addIntervalTimer(callback.ptr(), milliseconds, true);
}

void clearInterval(const int token)
{
  auto* session = currentPythonPluginSession();
  if (session == nullptr)
  {
    throw std::runtime_error{"Timers require an active Python plugin session"};
  }
  session->clearTimer(token);
}

void unregisterCallback(const int token)
{
  g_callbacks.erase(token);
  for (auto it = g_eventCallbacks.begin(); it != g_eventCallbacks.end();)
  {
    auto& tokens = it->second;
    tokens.erase(std::remove(tokens.begin(), tokens.end(), token), tokens.end());
    if (tokens.empty())
    {
      it = g_eventCallbacks.erase(it);
    }
    else
    {
      ++it;
    }
  }
}

void emitEvent(const std::string& eventName)
{
  const auto eventIt = g_eventCallbacks.find(eventName);
  if (eventIt == std::end(g_eventCallbacks))
  {
    return;
  }

  auto tokens = eventIt->second;
  for (const auto token : tokens)
  {
    const auto callbackIt = g_callbacks.find(token);
    if (callbackIt != std::end(g_callbacks))
    {
      callbackIt->second.callback();
    }
  }
}

bool hasEventCallbacks(const std::string& eventName)
{
  const auto eventIt = g_eventCallbacks.find(eventName);
  return eventIt != std::end(g_eventCallbacks) && !eventIt->second.empty();
}

void cleanupPlugin(const std::string& pluginId)
{
  auto tokensToRemove = std::vector<int>{};
  for (const auto& [token, entry] : g_callbacks)
  {
    if (entry.pluginId == pluginId)
    {
      tokensToRemove.push_back(token);
    }
  }
  for (const auto token : tokensToRemove)
  {
    unregisterCallback(token);
  }
}

void cleanupPluginSession(PythonPluginSession& session)
{
  for (const auto token : session.takeCallbackTokens())
  {
    unregisterCallback(token);
  }
  cleanupPlugin(session.pluginId());
  session.clearTimers();
  session.closePluginPanels();
}

void defineModule(py::module_& module)
{
  module.doc() = "TrenchBroom Python API";

  py::class_<Vec3>(module, "Vec3")
    .def(py::init<double, double, double>())
    .def_readwrite("x", &Vec3::x)
    .def_readwrite("y", &Vec3::y)
    .def_readwrite("z", &Vec3::z)
    .def("__add__", [](const Vec3& self, const Vec3& other) { return self + other; })
    .def("__sub__", [](const Vec3& self, const Vec3& other) { return self - other; })
    .def("__mul__", [](const Vec3& self, const double factor) { return self * factor; })
    .def("__rmul__", [](const Vec3& self, const double factor) { return self * factor; })
    .def(
      "__truediv__",
      [](const Vec3& self, const double divisor) { return self / divisor; })
    .def("dot", dot)
    .def("cross", cross)
    .def("length", length)
    .def("normalize", normalize)
    .def("normalized", normalize)
    .def(
      "__repr__",
      [](const Vec3& self) {
        return "Vec3(" + std::to_string(self.x) + ", " + std::to_string(self.y) + ", "
               + std::to_string(self.z) + ")";
      })
    .def("__iter__", [](const Vec3& self) {
      return py::iter(py::make_tuple(self.x, self.y, self.z));
    });

  py::class_<Plane>(module, "Plane")
    .def(py::init<Vec3, double>())
    .def_readwrite("normal", &Plane::normal)
    .def_readwrite("dist", &Plane::dist)
    .def_static(
      "from_points",
      [](const Vec3& p1, const Vec3& p2, const Vec3& p3) {
        try
        {
          const auto normal = normalize(cross(p2 - p1, p3 - p1));
          return Plane{normal, dot(p1, normal)};
        }
        catch (const std::exception& e)
        {
          throw py::value_error{e.what()};
        }
      })
    .def(
      "distance",
      [](const Plane& self, const Vec3& point) {
        return toVmPlane(self).point_distance(toVmVec3(point));
      })
    .def(
      "project",
      [](const Plane& self, const Vec3& point) {
        return fromVmVec3(toVmPlane(self).project_point(toVmVec3(point)));
      })
    .def("__repr__", [](const Plane& self) {
      return "Plane(normal=" + py::repr(py::cast(self.normal)).cast<std::string>()
             + ", dist=" + std::to_string(self.dist) + ")";
    });

  py::class_<DocumentHandle>(module, "Document")
    .def_property_readonly("id", documentId)
    .def_property_readonly(
      "path",
      [](DocumentHandle& self) -> py::object {
        const auto& path = self.get().map().path();
        if (path.empty())
        {
          return py::none();
        }
        return py::cast(path.u8string());
      })
    .def_property_readonly(
      "entities", [](DocumentHandle& self) { return allEntities(self.get()); })
    .def_property_readonly(
      "selection",
      [](DocumentHandle& self) { return SelectionHandle{&self.get(), self.generation}; })
    .def_property_readonly(
      "materials",
      [](DocumentHandle& self) {
        auto result = std::vector<MaterialHandle>{};
        const auto& materials = self.get().map().materialManager().materials();
        result.reserve(materials.size());
        for (const auto* material : materials)
        {
          result.push_back(MaterialHandle{material});
        }
        return result;
      })
    .def_property_readonly(
      "material_collections",
      [](DocumentHandle& self) {
        auto result = std::vector<MaterialCollectionHandle>{};
        const auto& collections = self.get().map().materialManager().collections();
        result.reserve(collections.size());
        for (const auto& collection : collections)
        {
          result.push_back(MaterialCollectionHandle{&collection});
        }
        return result;
      })
    .def("vertex_tool_vertices", vertexToolVertices)
    .def("save", saveDocument)
    .def("close", closeDocument, py::arg("discard_changes") = false)
    .def(
      "reload",
      [](DocumentHandle& self) {
        const auto& context = requireContext();
        if (context.mcpExecution && !context.allowNonTransactionalActions)
        {
          throw std::runtime_error{"MCP Python reload requires mode='action'"};
        }
        auto& document = self.get();
        throwIfError(document.reload());
        PythonHandleRegistry::instance().invalidateDocument(&document);
      })
    .def("save_as", saveDocumentAs, py::arg("path"))
    .def("export", exportDocument, py::arg("path"), py::arg("strip_tb_properties") = true)
    .def(
      "transaction",
      [](DocumentHandle& self, std::string name) {
        return TransactionHandle{&self.get(), self.generation, std::move(name)};
      },
      py::arg("name") = "Python API Script")
    .def("set_triangle_uvs", setDocumentTriangleUVs, py::arg("triangles"))
    .def("set_face_uvs", setDocumentFaceUVs, py::arg("updates"))
    .def("set_face_uvs_with_split", setDocumentFaceUVsWithSplit, py::arg("updates"))
    .def(
      "select",
      [](DocumentHandle& self, const py::iterable& objects) {
        auto& document = self.get();
        auto nodes = selectableNodesFromObjects(objects);
        auto transaction = ScopedPythonTransaction{document, "Python API Select"};
        try
        {
          mdl::deselectAll(document.map());
          mdl::selectNodes(document.map(), nodes);
          if (!transaction.commit())
          {
            throw std::runtime_error{"Could not update selection"};
          }
        }
        catch (...)
        {
          transaction.cancel();
          throw;
        }
      })
    .def(
      "clear_selection",
      [](DocumentHandle& self) {
        auto& document = self.get();
        auto transaction =
          ScopedPythonTransaction{document, "Python API Clear Selection"};
        try
        {
          mdl::deselectAll(document.map());
          if (!transaction.commit())
          {
            throw std::runtime_error{"Could not clear selection"};
          }
        }
        catch (...)
        {
          transaction.cancel();
          throw;
        }
      })
    .def(
      "__repr__",
      [](DocumentHandle& self) {
        return "Document(name='" + filenameAsUtf8(self.get().map().path()) + "')";
      })
    .def("__str__", [](DocumentHandle& self) {
      return "Document(name='" + filenameAsUtf8(self.get().map().path()) + "')";
    });

  py::class_<SelectionHandle>(module, "Selection")
    .def_property_readonly("entity", selectionFirstEntity)
    .def_property_readonly("brush", selectionFirstBrush)
    .def_property_readonly("properties", selectionProperties)
    .def_property_readonly(
      "classname",
      [](SelectionHandle& self) -> py::object {
        auto ent = selectionFirstEntity(self);
        if (!ent.is_none())
        {
          return py::cast(ent.cast<EntityHandle>().get().entity().classname());
        }
        return py::none();
      })
    .def_property_readonly("entities", selectedEntities)
    .def_property_readonly("all_entities", selectedAllEntities)
    .def_property_readonly(
      "brushes",
      [](SelectionHandle& self) {
        auto result = std::vector<BrushHandle>{};
        const auto& brushes = self.getDocument().map().selection().brushes;
        result.reserve(brushes.size());
        for (auto* brush : brushes)
        {
          result.push_back(BrushHandle{
            &self.getDocument(),
            self.generation,
            brush,
            PythonHandleRegistry::instance().nodeLifetimeGeneration(brush)});
        }
        return result;
      })
    .def_property_readonly("brush_faces", selectedBrushFaces)
    .def(
      "set_property",
      setSelectionProperty,
      py::arg("key"),
      py::arg("value"),
      py::arg("create_if_missing") = true)
    .def("brush_vertices", selectedBrushVertices)
    .def("triangle_uvs", selectedTriangleUVs)
    .def("set", setSelection)
    .def("add", addSelection)
    .def("deselect_all", deselectAllSelection)
    .def("clear", deselectAllSelection)
    .def("duplicate", duplicateSelection)
    .def("translate", translateSelection)
    .def(
      "rotate",
      rotateSelection,
      py::arg("axis_x"),
      py::arg("axis_y"),
      py::arg("axis_z"),
      py::arg("angle_degrees"),
      py::arg("center_x") = std::nullopt,
      py::arg("center_y") = std::nullopt,
      py::arg("center_z") = std::nullopt)
    .def(
      "scale",
      scaleSelection,
      py::arg("scale_x"),
      py::arg("scale_y"),
      py::arg("scale_z"),
      py::arg("center_x") = std::nullopt,
      py::arg("center_y") = std::nullopt,
      py::arg("center_z") = std::nullopt)
    .def("chamfer_vertices", chamferSelectionVertices, py::arg("distance"))
    .def(
      "chamfer_edges",
      chamferSelectionEdges,
      py::arg("distance"),
      py::arg("segments") = 1)
    .def(
      "__getitem__",
      [](SelectionHandle& self, const std::string& key) {
        auto allEnts = selectedAllEntities(self);
        if (allEnts.empty())
        {
          throw py::key_error{"No entity in current selection"};
        }
        const auto* value = allEnts.front().get().entity().property(key);
        if (value == nullptr)
        {
          throw py::key_error{"Entity has no property '" + key + "'"};
        }
        return *value;
      })
    .def(
      "__setitem__",
      [](SelectionHandle& self, const std::string& key, const std::string& value) {
        setSelectionProperty(self, key, value, true);
      })
    .def(
      "__contains__",
      [](SelectionHandle& self, const std::string& key) {
        auto allEnts = selectedAllEntities(self);
        if (allEnts.empty())
        {
          return false;
        }
        return allEnts.front().get().entity().hasProperty(key);
      })
    .def(
      "__repr__",
      [](SelectionHandle& self) {
        const auto& sel = self.getDocument().map().selection();
        return "Selection(brushes=" + std::to_string(sel.brushes.size())
               + ", entities=" + std::to_string(sel.entities.size()) + ")";
      })
    .def("__str__", [](SelectionHandle& self) {
      const auto& sel = self.getDocument().map().selection();
      return "Selection(brushes=" + std::to_string(sel.brushes.size())
             + ", entities=" + std::to_string(sel.entities.size()) + ")";
    });

  py::class_<EntityHandle>(module, "Entity")
    .def_property_readonly(
      "id",
      [](EntityHandle& self) {
        return nodeId(DocumentHandle{self.document, self.generation}.get(), self.get());
      })
    .def_property_readonly(
      "classname", [](EntityHandle& self) { return self.get().entity().classname(); })
    .def_property_readonly("brushes", entityBrushes)
    .def_property_readonly(
      "properties",
      [](EntityHandle& self) {
        auto dict = py::dict{};
        for (const auto& property : self.get().entity().properties())
        {
          dict[py::cast(property.key())] = py::cast(property.value());
        }
        return dict;
      })
    .def(
      "keys",
      [](EntityHandle& self) {
        auto result = std::vector<std::string>{};
        for (const auto& property : self.get().entity().properties())
        {
          result.push_back(property.key());
        }
        return result;
      })
    .def(
      "values",
      [](EntityHandle& self) {
        auto result = std::vector<std::string>{};
        for (const auto& property : self.get().entity().properties())
        {
          result.push_back(property.value());
        }
        return result;
      })
    .def(
      "items",
      [](EntityHandle& self) {
        auto result = std::vector<std::pair<std::string, std::string>>{};
        for (const auto& property : self.get().entity().properties())
        {
          result.emplace_back(property.key(), property.value());
        }
        return result;
      })
    .def(
      "get",
      [](EntityHandle& self, const std::string& key, py::object defaultValue) {
        const auto* value = self.get().entity().property(key);
        return value != nullptr ? py::cast(*value) : defaultValue;
      },
      py::arg("key"),
      py::arg("default") = py::none())
    .def("set", setEntityProperty)
    .def("remove", removeEntityProperty)
    .def(
      "__getitem__",
      [](EntityHandle& self, const std::string& key) {
        const auto* value = self.get().entity().property(key);
        if (value == nullptr)
        {
          throw py::key_error("Entity has no property '" + key + "'");
        }
        return *value;
      })
    .def(
      "__setitem__",
      [](EntityHandle& self, const std::string& key, const std::string& value) {
        setEntityProperty(self, key, value);
      })
    .def(
      "__delitem__",
      [](EntityHandle& self, const std::string& key) { removeEntityProperty(self, key); })
    .def(
      "__contains__",
      [](EntityHandle& self, const std::string& key) {
        return self.get().entity().hasProperty(key);
      })
    .def(
      "__len__",
      [](EntityHandle& self) { return self.get().entity().properties().size(); })
    .def(
      "__iter__",
      [](EntityHandle& self) {
        auto keys = std::vector<std::string>{};
        for (const auto& property : self.get().entity().properties())
        {
          keys.push_back(property.key());
        }
        return py::iter(py::cast(keys));
      })
    .def(
      "__repr__",
      [](EntityHandle& self) {
        const auto& ent = self.get().entity();
        auto propsStr = std::string{"{"};
        bool first = true;
        for (const auto& property : ent.properties())
        {
          if (!first)
          {
            propsStr += ", ";
          }
          first = false;
          propsStr += "'" + property.key() + "': '" + property.value() + "'";
        }
        propsStr += "}";
        return "Entity(classname='" + ent.classname() + "', properties=" + propsStr + ")";
      })
    .def("__str__", [](EntityHandle& self) {
      const auto& ent = self.get().entity();
      auto propsStr = std::string{"{"};
      bool first = true;
      for (const auto& property : ent.properties())
      {
        if (!first)
        {
          propsStr += ", ";
        }
        first = false;
        propsStr += "'" + property.key() + "': '" + property.value() + "'";
      }
      propsStr += "}";
      return "Entity(classname='" + ent.classname() + "', properties=" + propsStr + ")";
    });

  py::class_<BrushHandle>(module, "Brush")
    .def_property_readonly(
      "id",
      [](BrushHandle& self) {
        return nodeId(DocumentHandle{self.document, self.generation}.get(), self.get());
      })
    .def_property_readonly("entity", brushEntity)
    .def(
      "faces",
      [](BrushHandle& self) {
        auto result = std::vector<FaceHandle>{};
        const auto& brush = self.get().brush();
        result.reserve(brush.faceCount());
        for (size_t i = 0; i < brush.faceCount(); ++i)
        {
          result.push_back(FaceHandle{
            self.document,
            self.generation,
            self.brush,
            self.nodeLifetimeGeneration,
            brush.face(i).boundary(),
            i});
        }
        return result;
      })
    .def(
      "__repr__",
      [](BrushHandle& self) {
        const auto& brush = self.get().brush();
        auto* entityNode = self.get().entity();
        const auto classname =
          (entityNode != nullptr) ? entityNode->entity().classname() : "worldspawn";
        return "Brush(faces=" + std::to_string(brush.faceCount()) + ", entity='"
               + classname + "')";
      })
    .def("__str__", [](BrushHandle& self) {
      const auto& brush = self.get().brush();
      auto* entityNode = self.get().entity();
      const auto classname =
        (entityNode != nullptr) ? entityNode->entity().classname() : "worldspawn";
      return "Brush(faces=" + std::to_string(brush.faceCount()) + ", entity='" + classname
             + "')";
    });

  py::class_<FaceHandle>(module, "Face")
    .def_property_readonly("id", faceId)
    .def_property_readonly("vertices", faceVertices)
    .def_property_readonly("uv_loops", faceUVLoops)
    .def_property(
      "texture_name",
      [](FaceHandle& self) { return self.get().materialName(); },
      setFaceMaterial)
    .def_property(
      "material",
      [](FaceHandle& self) { return self.get().materialName(); },
      setFaceMaterial)
    .def_property(
      "offset",
      [](FaceHandle& self) { return vec2ToTuple(self.get().uvAttributes().offset); },
      setFaceOffset)
    .def_property(
      "scale",
      [](FaceHandle& self) { return vec2ToTuple(self.get().uvAttributes().scale); },
      setFaceScale)
    .def_property(
      "rotation",
      [](FaceHandle& self) { return self.get().uvAttributes().rotation; },
      setFaceRotation)
    .def_property(
      "surface_contents",
      [](FaceHandle& self) {
        const auto& value = self.get().surfaceAttributes().contents;
        return value ? py::cast(*value) : py::none();
      },
      setFaceSurfaceContents)
    .def_property(
      "surface_flags",
      [](FaceHandle& self) {
        const auto& value = self.get().surfaceAttributes().flags;
        return value ? py::cast(*value) : py::none();
      },
      setFaceSurfaceFlags)
    .def_property(
      "surface_value",
      [](FaceHandle& self) {
        const auto& value = self.get().surfaceAttributes().value;
        return value ? py::cast(*value) : py::none();
      },
      setFaceSurfaceValue)
    .def("set_uv_loops", setFaceUVLoops)
    .def("set_material", setFaceMaterial)
    .def(
      "__repr__",
      [](FaceHandle& self) {
        return "Face(material='" + self.get().materialName() + "')";
      })
    .def("__str__", [](FaceHandle& self) {
      return "Face(material='" + self.get().materialName() + "')";
    });

  py::class_<MaterialHandle>(module, "Material")
    .def_property_readonly("name", [](MaterialHandle& self) { return self.get().name(); })
    .def_property_readonly(
      "collection_name", [](MaterialHandle& self) { return self.get().collectionName(); })
    .def_property_readonly(
      "width",
      [](MaterialHandle& self) {
        const auto* texture = self.get().texture();
        return texture != nullptr ? texture->width() : 0u;
      })
    .def_property_readonly("height", [](MaterialHandle& self) {
      const auto* texture = self.get().texture();
      return texture != nullptr ? texture->height() : 0u;
    });

  py::class_<MaterialCollectionHandle>(module, "MaterialCollection")
    .def_property_readonly(
      "name",
      [](MaterialCollectionHandle& self) { return self.get().path().generic_string(); })
    .def_property_readonly(
      "path",
      [](MaterialCollectionHandle& self) { return self.get().path().generic_string(); })
    .def_property_readonly(
      "material_count",
      [](MaterialCollectionHandle& self) { return self.get().materialCount(); })
    .def_property_readonly("materials", [](MaterialCollectionHandle& self) {
      auto result = std::vector<MaterialHandle>{};
      const auto& materials = self.get().materials();
      result.reserve(materials.size());
      for (const auto& material : materials)
      {
        result.push_back(MaterialHandle{&material});
      }
      return result;
    });

  py::class_<TransactionHandle>(module, "Transaction")
    .def("__enter__", &TransactionHandle::enter, py::return_value_policy::reference)
    .def(
      "__exit__",
      [](TransactionHandle& self, py::object excType, py::object, py::object) {
        if (!excType.is_none())
        {
          self.cancel();
          return false;
        }
        self.commit();
        return false;
      })
    .def("commit", &TransactionHandle::commit)
    .def("cancel", &TransactionHandle::cancel);

  py::class_<PluginPanelHandle>(module, "PluginPanel")
    .def(
      "add_label",
      [](PluginPanelHandle& self, const std::string& text) {
        auto* label = new QLabel{QString::fromStdString(text)};
        label->setWordWrap(true);
        ensurePanelLayout(self.get()).addWidget(label);
      })
    .def(
      "add_label_named",
      [](PluginPanelHandle& self, const std::string& key, const std::string& text) {
        auto* label = new QLabel{QString::fromStdString(text)};
        label->setObjectName(panelObjectName("label", key));
        label->setWordWrap(true);
        ensurePanelLayout(self.get()).addWidget(label);
      })
    .def(
      "set_label_text",
      [](PluginPanelHandle& self, const std::string& key, const std::string& text) {
        findPanelChild<QLabel>(self.get(), "label", key)
          .setText(QString::fromStdString(text));
      })
    .def(
      "add_group",
      [](PluginPanelHandle& self, const std::string& key, const std::string& title) {
        auto* groupBox = new QGroupBox{QString::fromStdString(title)};
        groupBox->setObjectName(panelObjectName("group", key));
        auto* layout = new QVBoxLayout{};
        layout->setContentsMargins(6, 6, 6, 6);
        layout->setSpacing(4);
        groupBox->setLayout(layout);
        ensurePanelLayout(self.get()).addWidget(groupBox);
        return PluginPanelHandle{QPointer<QWidget>{groupBox}};
      })
    .def(
      "add_row",
      [](PluginPanelHandle& self, const std::string& key) {
        auto* row = new QWidget{};
        row->setObjectName(panelObjectName("row", key));
        auto* layout = new QHBoxLayout{};
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);
        row->setLayout(layout);
        ensurePanelLayout(self.get()).addWidget(row);
        return PluginPanelHandle{QPointer<QWidget>{row}};
      })
    .def(
      "add_column",
      [](PluginPanelHandle& self, const std::string& key) {
        auto* column = new QWidget{};
        column->setObjectName(panelObjectName("column", key));
        auto* layout = new QVBoxLayout{};
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);
        column->setLayout(layout);
        ensurePanelLayout(self.get()).addWidget(column);
        return PluginPanelHandle{QPointer<QWidget>{column}};
      })
    .def(
      "set_widget_visible",
      [](PluginPanelHandle& self, const std::string& key, const bool visible) {
        const auto suffix = QStringLiteral("_") + QString::fromStdString(key);
        const auto widgets = self.get().findChildren<QWidget*>();
        for (auto* widget : widgets)
        {
          if (widget->objectName().endsWith(suffix))
          {
            widget->setVisible(visible);
            return;
          }
        }
        throw std::runtime_error{"Plugin panel control not found: " + key};
      })
    .def(
      "add_button",
      [](PluginPanelHandle& self, const std::string& text, py::object callback) {
        auto* button = new QPushButton{QString::fromStdString(text)};
        auto* session = currentPythonPluginSession();
        const auto token = registerPanelCallback(std::move(callback));
        QObject::connect(button, &QPushButton::clicked, [session, token]() {
          invokeSessionCallback(session, token);
        });
        ensurePanelLayout(self.get()).addWidget(button);
      })
    .def(
      "add_button_callback",
      [](PluginPanelHandle& self, const std::string& text, py::object callback) {
        auto* button = new QPushButton{QString::fromStdString(text)};
        auto* session = currentPythonPluginSession();
        const auto token = registerPanelCallback(std::move(callback));
        QObject::connect(button, &QPushButton::clicked, [session, token]() {
          invokeSessionCallback(session, token);
        });
        ensurePanelLayout(self.get()).addWidget(button);
      })
    .def(
      "add_checkbox",
      [](
        PluginPanelHandle& self,
        const std::string& text,
        const bool checked,
        py::object callback) {
        auto* checkbox = new QCheckBox{QString::fromStdString(text)};
        checkbox->setChecked(checked);
        auto* session = currentPythonPluginSession();
        const auto token = registerPanelCallback(std::move(callback));
        QObject::connect(
          checkbox, &QCheckBox::toggled, [session, token](const bool value) {
            invokeSessionCallback(session, token, value);
          });
        ensurePanelLayout(self.get()).addWidget(checkbox);
      })
    .def(
      "add_checkbox",
      [](
        PluginPanelHandle& self,
        const std::string& key,
        const std::string& text,
        const bool checked) {
        auto* checkbox = new QCheckBox{QString::fromStdString(text)};
        checkbox->setObjectName(panelObjectName("checkbox", key));
        checkbox->setChecked(checked);
        ensurePanelLayout(self.get()).addWidget(checkbox);
      })
    .def(
      "get_checkbox",
      [](PluginPanelHandle& self, const std::string& key) {
        return findPanelChild<QCheckBox>(self.get(), "checkbox", key).isChecked();
      })
    .def(
      "add_line_edit",
      [](PluginPanelHandle& self, const std::string& text, py::object callback) {
        auto* lineEdit = new QLineEdit{QString::fromStdString(text)};
        auto* session = currentPythonPluginSession();
        const auto token = registerPanelCallback(std::move(callback));
        QObject::connect(
          lineEdit, &QLineEdit::textChanged, [session, token](const QString& value) {
            invokeSessionCallback(session, token, value.toStdString());
          });
        ensurePanelLayout(self.get()).addWidget(lineEdit);
      })
    .def(
      "add_text_field",
      [](
        PluginPanelHandle& self,
        const std::string& key,
        const std::string& label,
        const std::string& value,
        const std::string& placeholder) {
        auto* lineEdit = new QLineEdit{QString::fromStdString(value)};
        lineEdit->setObjectName(panelObjectName("text", key));
        lineEdit->setPlaceholderText(QString::fromStdString(placeholder));
        addFormRow(self.get(), label, lineEdit);
      },
      py::arg("key"),
      py::arg("label"),
      py::arg("value") = "",
      py::arg("placeholder") = "")
    .def(
      "get_text_field",
      [](PluginPanelHandle& self, const std::string& key) {
        return findPanelChild<QLineEdit>(self.get(), "text", key).text().toStdString();
      })
    .def(
      "set_text_field",
      [](PluginPanelHandle& self, const std::string& key, const std::string& value) {
        findPanelChild<QLineEdit>(self.get(), "text", key)
          .setText(QString::fromStdString(value));
      })
    .def(
      "add_text_area",
      [](
        PluginPanelHandle& self,
        const std::string& key,
        const std::string& label,
        const std::string& value,
        const int height,
        const std::string& placeholder) {
        auto* textEdit = new QTextEdit{};
        textEdit->setObjectName(panelObjectName("text_area", key));
        textEdit->setPlainText(QString::fromStdString(value));
        textEdit->setPlaceholderText(QString::fromStdString(placeholder));
        textEdit->setAcceptRichText(false);
        textEdit->setTabChangesFocus(true);
        if (height > 0)
        {
          textEdit->setMinimumHeight(height);
        }
        addFormRow(self.get(), label, textEdit);
      },
      py::arg("key"),
      py::arg("label"),
      py::arg("value") = "",
      py::arg("height") = 120,
      py::arg("placeholder") = "")
    .def(
      "get_text_area",
      [](PluginPanelHandle& self, const std::string& key) {
        return findPanelChild<QTextEdit>(self.get(), "text_area", key)
          .toPlainText()
          .toStdString();
      })
    .def(
      "set_text_area",
      [](PluginPanelHandle& self, const std::string& key, const std::string& value) {
        findPanelChild<QTextEdit>(self.get(), "text_area", key)
          .setPlainText(QString::fromStdString(value));
      })
    .def(
      "add_int_field",
      [](
        PluginPanelHandle& self,
        const std::string& key,
        const std::string& label,
        const int value,
        const int min,
        const int max) {
        auto* spinBox = new QSpinBox{};
        spinBox->setObjectName(panelObjectName("int", key));
        spinBox->setRange(min, max);
        spinBox->setValue(value);
        addFormRow(self.get(), label, spinBox);
      },
      py::arg("key"),
      py::arg("label"),
      py::arg("value") = 0,
      py::arg("min") = std::numeric_limits<int>::lowest(),
      py::arg("max") = std::numeric_limits<int>::max())
    .def(
      "get_int_field",
      [](PluginPanelHandle& self, const std::string& key) {
        return findPanelChild<QSpinBox>(self.get(), "int", key).value();
      })
    .def(
      "add_float_field",
      [](
        PluginPanelHandle& self,
        const std::string& key,
        const std::string& label,
        const double value,
        const double min,
        const double max,
        const int decimals,
        const double step) {
        auto* spinBox = new QDoubleSpinBox{};
        spinBox->setObjectName(panelObjectName("float", key));
        spinBox->setRange(min, max);
        spinBox->setDecimals(decimals);
        spinBox->setSingleStep(step);
        spinBox->setValue(value);
        addFormRow(self.get(), label, spinBox);
      },
      py::arg("key"),
      py::arg("label"),
      py::arg("value") = 0.0,
      py::arg("min") = -1000000.0,
      py::arg("max") = 1000000.0,
      py::arg("decimals") = 2,
      py::arg("step") = 1.0)
    .def(
      "get_float_field",
      [](PluginPanelHandle& self, const std::string& key) {
        return findPanelChild<QDoubleSpinBox>(self.get(), "float", key).value();
      })
    .def(
      "add_combo_box",
      [](
        PluginPanelHandle& self,
        const std::vector<std::string>& items,
        const int currentIndex,
        py::object callback) {
        auto* comboBox = new QComboBox{};
        for (const auto& item : items)
        {
          comboBox->addItem(QString::fromStdString(item));
        }
        if (currentIndex >= 0 && currentIndex < comboBox->count())
        {
          comboBox->setCurrentIndex(currentIndex);
        }
        auto* session = currentPythonPluginSession();
        const auto token = registerPanelCallback(std::move(callback));
        QObject::connect(
          comboBox,
          &QComboBox::currentTextChanged,
          [session, token](const QString& value) {
            invokeSessionCallback(session, token, value.toStdString());
          });
        ensurePanelLayout(self.get()).addWidget(comboBox);
      })
    .def(
      "add_combo_box",
      [](
        PluginPanelHandle& self,
        const std::string& key,
        const std::string& label,
        const std::vector<std::string>& items,
        py::object callback,
        py::object current) {
        auto* comboBox = new QComboBox{};
        comboBox->setObjectName(panelObjectName("combo", key));
        for (const auto& item : items)
        {
          comboBox->addItem(QString::fromStdString(item));
        }
        if (current.is_none())
        {
          comboBox->setCurrentIndex(0);
        }
        else if (py::isinstance<py::int_>(current))
        {
          const auto index = py::cast<int>(current);
          if (index >= 0 && index < comboBox->count())
          {
            comboBox->setCurrentIndex(index);
          }
        }
        else
        {
          const auto text = QString::fromStdString(py::cast<std::string>(current));
          const auto index = comboBox->findText(text);
          if (index >= 0)
          {
            comboBox->setCurrentIndex(index);
          }
        }
        if (!callback.is_none())
        {
          auto* session = currentPythonPluginSession();
          const auto token = registerPanelCallback(std::move(callback));
          QObject::connect(
            comboBox,
            &QComboBox::currentTextChanged,
            [session, token](const QString& value) {
              invokeSessionCallback(session, token, value.toStdString());
            });
        }
        addFormRow(self.get(), label, comboBox);
      },
      py::arg("key"),
      py::arg("label"),
      py::arg("items"),
      py::arg("callback") = py::none(),
      py::arg("current") = py::none())
    .def(
      "get_combo_box_text",
      [](PluginPanelHandle& self, const std::string& key) {
        return findPanelChild<QComboBox>(self.get(), "combo", key)
          .currentText()
          .toStdString();
      })
    .def(
      "add_color_field",
      [](
        PluginPanelHandle& self,
        const std::string& key,
        const std::string& label,
        py::object colorObject) {
        const auto color = colorFromObject(colorObject);
        auto* button = new QPushButton{};
        button->setObjectName(panelObjectName("color", key));
        button->setProperty("trenchbroom_color", color);
        button->setText(QStringLiteral("%1, %2, %3")
                          .arg(color.red())
                          .arg(color.green())
                          .arg(color.blue()));
        addFormRow(self.get(), label, button);
      })
    .def(
      "get_color_field",
      [](PluginPanelHandle& self, const std::string& key) {
        const auto color = findPanelChild<QPushButton>(self.get(), "color", key)
                             .property("trenchbroom_color")
                             .value<QColor>();
        return py::make_tuple(color.red(), color.green(), color.blue());
      })
    .def(
      "add_table_widget",
      [](
        PluginPanelHandle& self,
        const std::string& key,
        const std::vector<std::string>& columns,
        const std::vector<std::vector<std::string>>& rows,
        const int height,
        py::object callback) {
        auto* table = new QTableWidget{};
        table->setObjectName(panelObjectName("table", key));
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setShowGrid(false);
        table->verticalHeader()->setVisible(false);
        table->horizontalHeader()->setStretchLastSection(true);
        table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft);
        table->setColumnCount(static_cast<int>(columns.size()));
        table->setHorizontalHeaderLabels(toQStringList(columns));
        setTableRows(*table, rows);
        if (height > 0)
        {
          table->setMinimumHeight(height);
        }
        if (!callback.is_none())
        {
          auto* session = currentPythonPluginSession();
          const auto token = registerPanelCallback(std::move(callback));
          QObject::connect(
            table,
            &QTableWidget::currentCellChanged,
            [session, token](const int row, const int column, int, int) {
              invokeSessionCallback(session, token, row, column);
            });
        }
        ensurePanelLayout(self.get()).addWidget(table);
      },
      py::arg("key"),
      py::arg("columns"),
      py::arg("rows"),
      py::arg("height") = 200,
      py::arg("callback") = py::none())
    .def(
      "set_table_widget_rows",
      [](
        PluginPanelHandle& self,
        const std::string& key,
        const std::vector<std::vector<std::string>>& rows) {
        setTableRows(findPanelChild<QTableWidget>(self.get(), "table", key), rows);
      })
    .def(
      "add_tree_widget",
      [](
        PluginPanelHandle& self,
        const std::string& key,
        const std::vector<std::string>& columns,
        const std::vector<std::vector<std::string>>& rows,
        const int height,
        py::object callback) {
        auto* tree = new QTreeWidget{};
        tree->setObjectName(panelObjectName("tree", key));
        tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
        tree->setSelectionMode(QAbstractItemView::SingleSelection);
        tree->setRootIsDecorated(false);
        tree->setAlternatingRowColors(true);
        tree->setColumnCount(static_cast<int>(columns.size()));
        tree->setHeaderLabels(toQStringList(columns));
        setTreeItems(*tree, rows);
        if (height > 0)
        {
          tree->setMinimumHeight(height);
        }
        if (!callback.is_none())
        {
          auto* session = currentPythonPluginSession();
          const auto token = registerPanelCallback(std::move(callback));
          QObject::connect(
            tree,
            &QTreeWidget::currentItemChanged,
            [session, token, tree](QTreeWidgetItem* current, QTreeWidgetItem*) {
              invokeSessionCallback(
                session,
                token,
                current != nullptr ? tree->indexOfTopLevelItem(current) : -1);
            });
        }
        ensurePanelLayout(self.get()).addWidget(tree);
      },
      py::arg("key"),
      py::arg("columns"),
      py::arg("rows"),
      py::arg("height") = 200,
      py::arg("callback") = py::none())
    .def(
      "set_tree_widget_items",
      [](
        PluginPanelHandle& self,
        const std::string& key,
        const std::vector<std::vector<std::string>>& rows) {
        setTreeItems(findPanelChild<QTreeWidget>(self.get(), "tree", key), rows);
      })
    .def(
      "add_html_view",
      [](
        PluginPanelHandle& self,
        const std::string& key,
        const std::string& html,
        const int height,
        py::object callback) {
        auto* browser = new QTextBrowser{};
        browser->setObjectName(panelObjectName("html_view", key));
        browser->setHtml(QString::fromStdString(html));
        browser->setOpenExternalLinks(false);
        browser->setFrameShape(QFrame::NoFrame);
        browser->setProperty("pluginPanelOutput", true);
        browser->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
        if (height > 0 && height <= 40)
        {
          browser->setFixedHeight(height);
          browser->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
          browser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
          browser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
          browser->document()->setDocumentMargin(0);
        }
        else if (height > 0)
        {
          browser->setMinimumHeight(height);
        }
        if (!callback.is_none())
        {
          auto* session = currentPythonPluginSession();
          const auto token = registerPanelCallback(std::move(callback));
          QObject::connect(
            browser, &QTextBrowser::anchorClicked, [session, token](const QUrl& url) {
              invokeSessionCallback(session, token, url.toString().toStdString());
            });
        }
        ensurePanelLayout(self.get()).addWidget(browser);
      },
      py::arg("key"),
      py::arg("html"),
      py::arg("height") = 200,
      py::arg("callback") = py::none())
    .def(
      "set_html_view",
      [](PluginPanelHandle& self, const std::string& key, const std::string& html) {
        findPanelChild<QTextBrowser>(self.get(), "html_view", key)
          .setHtml(QString::fromStdString(html));
      })
    .def("clear", [](PluginPanelHandle& self) {
      if (auto* layout = self.get().layout())
      {
        clearLayout(*layout);
      }
    });

  auto currentSelection = []() {
    auto doc = currentDocument();
    return SelectionHandle{&doc.get(), doc.generation};
  };

  auto selectedBrushes = [currentSelection]() {
    auto selection = currentSelection();
    auto result = std::vector<BrushHandle>{};
    const auto& brushes = selection.getDocument().map().selection().brushes;
    result.reserve(brushes.size());
    for (auto* brush : brushes)
    {
      result.push_back(BrushHandle{
        &selection.getDocument(),
        selection.generation,
        brush,
        PythonHandleRegistry::instance().nodeLifetimeGeneration(brush)});
    }
    return result;
  };
  auto selectedEntities = [currentSelection](bool includeBrushes = false) {
    auto selection = currentSelection();
    if (includeBrushes)
    {
      return selectedAllEntities(selection);
    }
    return tb::ui::selectedEntities(selection);
  };
  auto selectedFaces = [currentSelection]() {
    auto selection = currentSelection();
    return selectedBrushFaces(selection);
  };

  auto translateHelper = [currentSelection](const py::args& args) {
    auto selection = currentSelection();
    if (args.size() == 1)
    {
      const auto v = vec3FromObject(args[0]);
      return translateSelection(selection, v.x, v.y, v.z);
    }
    if (args.size() == 2)
    {
      if (py::isinstance<py::iterable>(args[0]) && !py::isinstance<py::str>(args[0]))
      {
        setSelection(selection, py::reinterpret_borrow<py::iterable>(args[0]));
      }
      else
      {
        setSelection(selection, py::make_tuple(args[0]));
      }
      const auto v = vec3FromObject(args[1]);
      return translateSelection(selection, v.x, v.y, v.z);
    }
    if (args.size() == 3)
    {
      const auto x = py::cast<double>(args[0]);
      const auto y = py::cast<double>(args[1]);
      const auto z = py::cast<double>(args[2]);
      return translateSelection(selection, x, y, z);
    }
    if (args.size() == 4)
    {
      if (py::isinstance<py::iterable>(args[0]) && !py::isinstance<py::str>(args[0]))
      {
        setSelection(selection, py::reinterpret_borrow<py::iterable>(args[0]));
      }
      else
      {
        setSelection(selection, py::make_tuple(args[0]));
      }
      const auto x = py::cast<double>(args[1]);
      const auto y = py::cast<double>(args[2]);
      const auto z = py::cast<double>(args[3]);
      return translateSelection(selection, x, y, z);
    }
    throw py::type_error{"translate() takes 1, 2, 3, or 4 arguments"};
  };

  auto rotateHelper = [currentSelection](const py::args& args) {
    auto selection = currentSelection();
    if (args.size() == 3)
    {
      const auto rx = py::cast<double>(args[0]);
      const auto ry = py::cast<double>(args[1]);
      const auto rz = py::cast<double>(args[2]);
      auto& document = selection.getDocument();
      auto transaction = ScopedPythonTransaction{document, "Python API Rotate Selection"};
      try
      {
        if (rx != 0.0)
        {
          rotateSelection(
            selection, 1.0, 0.0, 0.0, rx, std::nullopt, std::nullopt, std::nullopt);
        }
        if (ry != 0.0)
        {
          rotateSelection(
            selection, 0.0, 1.0, 0.0, ry, std::nullopt, std::nullopt, std::nullopt);
        }
        if (rz != 0.0)
        {
          rotateSelection(
            selection, 0.0, 0.0, 1.0, rz, std::nullopt, std::nullopt, std::nullopt);
        }
        return transaction.commit();
      }
      catch (...)
      {
        transaction.cancel();
        throw;
      }
    }
    if (args.size() == 4)
    {
      if (!py::isinstance<py::float_>(args[0]) && !py::isinstance<py::int_>(args[0]))
      {
        if (py::isinstance<py::iterable>(args[0]) && !py::isinstance<py::str>(args[0]))
        {
          setSelection(selection, py::reinterpret_borrow<py::iterable>(args[0]));
        }
        else
        {
          setSelection(selection, py::make_tuple(args[0]));
        }
        const auto rx = py::cast<double>(args[1]);
        const auto ry = py::cast<double>(args[2]);
        const auto rz = py::cast<double>(args[3]);
        auto& document = selection.getDocument();
        auto transaction =
          ScopedPythonTransaction{document, "Python API Rotate Selection"};
        try
        {
          if (rx != 0.0)
          {
            rotateSelection(
              selection, 1.0, 0.0, 0.0, rx, std::nullopt, std::nullopt, std::nullopt);
          }
          if (ry != 0.0)
          {
            rotateSelection(
              selection, 0.0, 1.0, 0.0, ry, std::nullopt, std::nullopt, std::nullopt);
          }
          if (rz != 0.0)
          {
            rotateSelection(
              selection, 0.0, 0.0, 1.0, rz, std::nullopt, std::nullopt, std::nullopt);
          }
          return transaction.commit();
        }
        catch (...)
        {
          transaction.cancel();
          throw;
        }
      }
      else
      {
        return rotateSelection(
          selection,
          py::cast<double>(args[0]),
          py::cast<double>(args[1]),
          py::cast<double>(args[2]),
          py::cast<double>(args[3]),
          std::nullopt,
          std::nullopt,
          std::nullopt);
      }
    }
    if (args.size() == 5)
    {
      if (py::isinstance<py::iterable>(args[0]) && !py::isinstance<py::str>(args[0]))
      {
        setSelection(selection, py::reinterpret_borrow<py::iterable>(args[0]));
      }
      else
      {
        setSelection(selection, py::make_tuple(args[0]));
      }
      return rotateSelection(
        selection,
        py::cast<double>(args[1]),
        py::cast<double>(args[2]),
        py::cast<double>(args[3]),
        py::cast<double>(args[4]),
        std::nullopt,
        std::nullopt,
        std::nullopt);
    }
    if (args.size() == 7)
    {
      return rotateSelection(
        selection,
        py::cast<double>(args[0]),
        py::cast<double>(args[1]),
        py::cast<double>(args[2]),
        py::cast<double>(args[3]),
        py::cast<double>(args[4]),
        py::cast<double>(args[5]),
        py::cast<double>(args[6]));
    }
    if (args.size() == 8)
    {
      if (py::isinstance<py::iterable>(args[0]) && !py::isinstance<py::str>(args[0]))
      {
        setSelection(selection, py::reinterpret_borrow<py::iterable>(args[0]));
      }
      else
      {
        setSelection(selection, py::make_tuple(args[0]));
      }
      return rotateSelection(
        selection,
        py::cast<double>(args[1]),
        py::cast<double>(args[2]),
        py::cast<double>(args[3]),
        py::cast<double>(args[4]),
        py::cast<double>(args[5]),
        py::cast<double>(args[6]),
        py::cast<double>(args[7]));
    }
    throw py::type_error{"rotate() takes 3, 4, 5, 7, or 8 arguments"};
  };

  auto scaleHelper = [currentSelection](const py::args& args) {
    auto selection = currentSelection();
    if (args.size() == 1)
    {
      if (py::isinstance<Vec3>(args[0]) || py::isinstance<py::sequence>(args[0]))
      {
        const auto v = vec3FromObject(args[0]);
        return scaleSelection(
          selection, v.x, v.y, v.z, std::nullopt, std::nullopt, std::nullopt);
      }
      const auto s = py::cast<double>(args[0]);
      return scaleSelection(selection, s, s, s, std::nullopt, std::nullopt, std::nullopt);
    }
    if (args.size() == 2)
    {
      if (py::isinstance<py::iterable>(args[0]) && !py::isinstance<py::str>(args[0]))
      {
        setSelection(selection, py::reinterpret_borrow<py::iterable>(args[0]));
      }
      else
      {
        setSelection(selection, py::make_tuple(args[0]));
      }
      if (py::isinstance<Vec3>(args[1]) || py::isinstance<py::sequence>(args[1]))
      {
        const auto v = vec3FromObject(args[1]);
        return scaleSelection(
          selection, v.x, v.y, v.z, std::nullopt, std::nullopt, std::nullopt);
      }
      const auto s = py::cast<double>(args[1]);
      return scaleSelection(selection, s, s, s, std::nullopt, std::nullopt, std::nullopt);
    }
    if (args.size() == 3)
    {
      const auto sx = py::cast<double>(args[0]);
      const auto sy = py::cast<double>(args[1]);
      const auto sz = py::cast<double>(args[2]);
      return scaleSelection(
        selection, sx, sy, sz, std::nullopt, std::nullopt, std::nullopt);
    }
    if (args.size() == 4)
    {
      if (py::isinstance<py::iterable>(args[0]) && !py::isinstance<py::str>(args[0]))
      {
        setSelection(selection, py::reinterpret_borrow<py::iterable>(args[0]));
      }
      else
      {
        setSelection(selection, py::make_tuple(args[0]));
      }
      const auto sx = py::cast<double>(args[1]);
      const auto sy = py::cast<double>(args[2]);
      const auto sz = py::cast<double>(args[3]);
      return scaleSelection(
        selection, sx, sy, sz, std::nullopt, std::nullopt, std::nullopt);
    }
    throw py::type_error{"scale() takes 1, 2, 3, or 4 arguments"};
  };

  auto duplicateHelper = [currentSelection,
                          selectedBrushes](const py::args& args) -> py::object {
    auto selection = currentSelection();
    if (args.size() == 1)
    {
      if (py::isinstance<py::iterable>(args[0]) && !py::isinstance<py::str>(args[0]))
      {
        setSelection(selection, py::reinterpret_borrow<py::iterable>(args[0]));
      }
      else
      {
        setSelection(selection, py::make_tuple(args[0]));
      }
    }
    else if (args.size() > 1)
    {
      setSelection(selection, args);
    }
    duplicateSelection(selection);
    const auto brushes = selectedBrushes();
    if (!brushes.empty())
    {
      return py::cast(brushes);
    }
    return py::cast(tb::ui::selectedEntities(selection));
  };

  auto deleteSelectionHelper = [currentSelection]() {
    auto selection = currentSelection();
    auto& document = selection.getDocument();
    auto transaction = ScopedPythonTransaction{document, "Python API Delete Selection"};
    try
    {
      mdl::removeSelectedNodes(document.map());
      if (!transaction.commit())
      {
        throw std::runtime_error{"Could not delete selection"};
      }
      return true;
    }
    catch (...)
    {
      transaction.cancel();
      throw;
    }
  };

  auto deselectAllHelper = [currentSelection]() {
    auto selection = currentSelection();
    return deselectAllSelection(selection);
  };

  module.def("selected_brushes", selectedBrushes);
  module.def("selectedBrushes", selectedBrushes);
  module.def("selected_entities", selectedEntities, py::arg("include_brushes") = false);
  module.def("selectedEntities", selectedEntities, py::arg("include_brushes") = false);
  module.def("selected_all_entities", [currentSelection]() {
    auto selection = currentSelection();
    return selectedAllEntities(selection);
  });
  module.def("selectedAllEntities", [currentSelection]() {
    auto selection = currentSelection();
    return selectedAllEntities(selection);
  });
  module.def("selection", [currentSelection]() { return currentSelection(); });
  module.def("selected_faces", selectedFaces);
  module.def("selectedFaces", selectedFaces);
  module.def("translate", translateHelper);
  module.def("rotate", rotateHelper);
  module.def("scale", scaleHelper);
  module.def("duplicate", duplicateHelper);
  module.def("delete_selection", deleteSelectionHelper);
  module.def("deleteSelection", deleteSelectionHelper);
  module.def("deselect_all", deselectAllHelper);
  module.def("deselectAll", deselectAllHelper);

  module.def("current_document", currentDocument);
  module.def("document", currentDocument);
  module.def("execute_action", executeAction);
  module.def("list_actions", listActions);
  module.def(
    "create_brush", createBrush, py::arg("points"), py::arg("material") = py::none());
  module.def("create_plugin_panel", createPluginPanel);
  module.def("register_callback", registerCallback);
  module.def("unregister_callback", unregisterCallback);
  module.def("set_interval", setInterval);
  module.def("clear_interval", clearInterval);
  module.def("set_timeout", setTimeout);

  auto documents = module.def_submodule("documents", "Document lifecycle operations.");
  documents.def("current", currentDocument);
  documents.def("list", openDocuments);
  documents.def("snapshot", []() { return documentSnapshot(currentDocument()); });
  documents.def("open", openDocument, py::arg("path"));
  documents.def("activate", activateDocument, py::arg("document"));
  documents.def(
    "close", closeDocument, py::arg("document"), py::arg("discard_changes") = false);
  auto saveCurrentDocument = [](const py::object& path) {
    auto document = currentDocument();
    if (path.is_none())
    {
      saveDocument(document);
    }
    else
    {
      saveDocumentAs(document, py::cast<std::string>(path));
    }
    return document;
  };
  documents.def("save", saveCurrentDocument, py::arg("path") = py::none());
  documents.def("save_as", [](const std::string& path) {
    auto document = currentDocument();
    saveDocumentAs(document, path);
    return document;
  });
  documents.def("save_current", saveCurrentDocument, py::arg("path") = py::none());
  documents.def(
    "export",
    [](const std::string& path, const bool stripTbProperties) {
      auto document = currentDocument();
      exportDocument(document, path, stripTbProperties);
      return document;
    },
    py::arg("path"),
    py::arg("strip_tb_properties") = true);

  auto objects = module.def_submodule("objects", "Selection-backed object operations.");
  objects.def("selection", [currentSelection]() { return currentSelection(); });
  objects.def("snapshot", []() { return documentSnapshot(currentDocument()); });
  objects.def(
    "bounds", [currentSelection]() { return selectedObjectBounds(currentSelection()); });
  objects.def("inspect", [currentSelection]() {
    auto selection = currentSelection();
    return selectionSnapshot(selection);
  });
  objects.def("translate", translateHelper);
  objects.def("rotate", rotateHelper);
  objects.def("scale", scaleHelper);
  objects.def("duplicate", duplicateHelper);
  objects.def("delete_selection", deleteSelectionHelper);
  objects.def("deselect_all", deselectAllHelper);
  objects.def("set_selection", [currentSelection](const py::iterable& objects) {
    auto selection = currentSelection();
    setSelection(selection, objects);
  });

  auto entities = module.def_submodule("entities", "Entity collection operations.");
  entities.def("list", []() {
    auto document = currentDocument();
    return allEntities(document.get());
  });
  entities.def("selected", selectedEntities, py::arg("include_brushes") = false);
  entities.def(
    "create",
    createEntity,
    py::arg("classname"),
    py::arg("properties") = py::dict{},
    py::arg("origin") = py::none(),
    py::arg("select") = false);
  entities.def("delete", deleteEntity, py::arg("entity"));
  entities.def(
    "update",
    updateEntity,
    py::arg("entity"),
    py::arg("properties") = py::dict{},
    py::arg("remove_keys") = std::vector<std::string>{});
  entities.def(
    "properties_update",
    updateEntityProperties,
    py::arg("entities"),
    py::arg("properties") = py::dict{},
    py::arg("remove_keys") = std::vector<std::string>{});
  entities.def(
    "properties_delete",
    [](const py::iterable& entities, const std::vector<std::string>& keys) {
      updateEntityProperties(entities, py::dict{}, keys);
    },
    py::arg("entities"),
    py::arg("keys"));
  entities.def(
    "find",
    [](
      const std::optional<std::string>& classname,
      const std::optional<std::string>& property,
      const std::optional<std::string>& value) {
      auto document = currentDocument();
      return findEntities(document.get(), classname, property, value);
    },
    py::arg("classname") = py::none(),
    py::arg("property") = py::none(),
    py::arg("value") = py::none());

  auto brushes =
    module.def_submodule("brushes", "Brush collection and creation operations.");
  brushes.def("list", []() {
    auto document = currentDocument();
    return allBrushes(document.get());
  });
  brushes.def("selected", selectedBrushes);
  brushes.def("create", createBrush, py::arg("points"), py::arg("material") = py::none());

  auto faces = module.def_submodule("faces", "Face collection operations.");
  faces.def("list", []() {
    auto document = currentDocument();
    return allFaces(document.get());
  });
  faces.def("selected", selectedFaces);
  faces.def("set_material", setFacesMaterial, py::arg("faces"), py::arg("material"));

  auto groups = module.def_submodule("groups", "Native group organization operations.");
  groups.def("create_from_selection", [currentSelection](const std::string& name) {
    return createGroupFromSelection(currentSelection(), name);
  });
  groups.def("inspect_selected", [currentSelection]() {
    return inspectSelectedGroups(currentSelection());
  });
  groups.def("rename_selected", [currentSelection](const std::string& name) {
    return renameSelectedGroups(currentSelection(), name);
  });
  groups.def("ungroup_selected", [currentSelection]() {
    return ungroupSelectedGroups(currentSelection());
  });

  auto modules = module.def_submodule("modules", "Generated map module queries.");
  modules.def("list", modulesForCurrentDocument);
  modules.def("inspect", inspectModule, py::arg("module_id"));

  auto placeModel = [](
                      const std::string& path,
                      const py::object& origin,
                      const std::string& classname,
                      const std::string& property,
                      const bool select) {
    return placeAsset(path, ".mdl", classname, property, origin, select);
  };
  auto placeSprite = [](
                       const std::string& path,
                       const py::object& origin,
                       const std::string& classname,
                       const std::string& property,
                       const bool select) {
    return placeAsset(path, ".spr", classname, property, origin, select);
  };
  auto placeSound = [](
                      const std::string& path,
                      const py::object& origin,
                      const std::string& classname,
                      const std::string& property,
                      const bool select) {
    return placeAsset(path, ".wav", classname, property, origin, select);
  };
  auto assets = module.def_submodule("assets", "GoldSrc asset placement operations.");
  assets.def(
    "search",
    searchAssets,
    py::arg("query") = "",
    py::arg("type") = py::none(),
    py::arg("limit") = 50u);
  assets.def(
    "place_model",
    placeModel,
    py::arg("path"),
    py::arg("origin") = py::none(),
    py::arg("classname") = "cycler_sprite",
    py::arg("property") = "model",
    py::arg("select") = false);
  assets.def(
    "place_sprite",
    placeSprite,
    py::arg("path"),
    py::arg("origin") = py::none(),
    py::arg("classname") = "cycler_sprite",
    py::arg("property") = "model",
    py::arg("select") = false);
  assets.def(
    "place_sound",
    placeSound,
    py::arg("path"),
    py::arg("origin") = py::none(),
    py::arg("classname") = "ambient_generic",
    py::arg("property") = "message",
    py::arg("select") = false);

  auto listMaterials = []() {
    auto document = currentDocument();
    auto result = std::vector<MaterialHandle>{};
    const auto& materials = document.get().map().materialManager().materials();
    result.reserve(materials.size());
    for (const auto* material : materials)
    {
      result.push_back(MaterialHandle{material});
    }
    return result;
  };
  auto listMaterialCollections = []() {
    auto document = currentDocument();
    auto result = std::vector<MaterialCollectionHandle>{};
    const auto& collections = document.get().map().materialManager().collections();
    result.reserve(collections.size());
    for (const auto& collection : collections)
    {
      result.push_back(MaterialCollectionHandle{&collection});
    }
    return result;
  };
  auto searchMaterials = [listMaterials](const std::string& query, const size_t limit) {
    auto result = std::vector<MaterialHandle>{};
    if (limit == 0u)
    {
      return result;
    }
    for (auto material : listMaterials())
    {
      if (containsCaseInsensitive(material.get().name(), query))
      {
        result.push_back(std::move(material));
        if (result.size() == limit)
        {
          break;
        }
      }
    }
    return result;
  };
  auto materials = module.def_submodule("materials", "Material collection operations.");
  materials.def("list", listMaterials);
  materials.def("collections", listMaterialCollections);
  materials.def("search", searchMaterials, py::arg("query"), py::arg("limit") = 50u);
  materials.def(
    "current", []() { return currentDocument().get().map().currentMaterialName(); });

  auto historyDocument = [](const py::object& document) {
    return document.is_none() ? currentDocument() : py::cast<DocumentHandle>(document);
  };
  auto history = module.def_submodule("history", "Native undo and redo operations.");
  history.def(
    "status",
    [historyDocument](const py::object& document) {
      auto target = historyDocument(document);
      return historyStatus(target);
    },
    py::arg("document") = py::none());
  history.def(
    "undo",
    [historyDocument](const py::object& document) {
      auto target = historyDocument(document);
      return undoDocument(target);
    },
    py::arg("document") = py::none());
  history.def(
    "redo",
    [historyDocument](const py::object& document) {
      auto target = historyDocument(document);
      return redoDocument(target);
    },
    py::arg("document") = py::none());

  auto actions =
    module.def_submodule("actions", "Native action discovery and execution.");
  actions.def("list", listActions);
  actions.def("execute", executeAction, py::arg("action_id"));

  auto validation = module.def_submodule("validation", "Map validation operations.");
  validation.def(
    "check", validationCheck, py::arg("include_hidden") = false, py::arg("limit") = 500u);

  auto apiCatalog = py::dict{};
  for (const auto& typeInfo : pythonApiTypes())
  {
    auto symbols = py::list{};
    for (const auto& symbol : pythonApiSymbols(typeInfo.type))
    {
      symbols.append(std::string{symbol.name});
    }
    apiCatalog[std::string{typeInfo.name}.c_str()] = std::move(symbols);
  }
  module.attr("_api_catalog") = std::move(apiCatalog);

  module.def("_emit_event", emitEvent);
  module.def("_has_event_callbacks", hasEventCallbacks);
  module.def("_cleanup_plugin", cleanupPlugin);
  module.def("_cleanup_plugin_session", [](py::capsule sessionCapsule) {
    cleanupPluginSession(
      *reinterpret_cast<PythonPluginSession*>(sessionCapsule.get_pointer()));
  });
  module.def("_invalidate_document", [](py::capsule documentCapsule) {
    PythonHandleRegistry::instance().invalidateDocument(
      reinterpret_cast<MapDocument*>(documentCapsule.get_pointer()));
  });
}
} // namespace

PYBIND11_EMBEDDED_MODULE(trenchbroom, module)
{
  defineModule(module);
}

bool installPythonApiModule()
{
  static auto installed = false;
  if (installed)
  {
    return true;
  }

  if (PyImport_AppendInittab("trenchbroom", PyInit_trenchbroom) != 0)
  {
    return false;
  }
  installed = true;
  return true;
}

struct PythonDocumentTransaction::Impl
{
  std::unique_ptr<automation::AutomationTransaction> transaction;
};

PythonDocumentTransaction::PythonDocumentTransaction(
  MapDocument& document, std::string name)
  : m_document{&document}
  , m_impl{std::make_unique<Impl>()}
{
  if (g_activePythonTransactions[m_document] == 0u)
  {
    m_impl->transaction = std::make_unique<automation::AutomationTransaction>(
      m_document->map(), std::move(name));
  }
  ++g_activePythonTransactions[m_document];
}

PythonDocumentTransaction::~PythonDocumentTransaction()
{
  cancel();
}

PythonDocumentTransaction::PythonDocumentTransaction(
  PythonDocumentTransaction&& other) noexcept
  : m_document{other.m_document}
  , m_impl{std::move(other.m_impl)}
{
  other.m_document = nullptr;
}

PythonDocumentTransaction& PythonDocumentTransaction::operator=(
  PythonDocumentTransaction&& other) noexcept
{
  if (this != &other)
  {
    cancel();
    m_document = other.m_document;
    m_impl = std::move(other.m_impl);
    other.m_document = nullptr;
  }
  return *this;
}

bool PythonDocumentTransaction::commit()
{
  if (m_document == nullptr)
  {
    return true;
  }
  auto result = true;
  if (m_impl && m_impl->transaction)
  {
    result = m_impl->transaction->commit();
    m_impl->transaction.reset();
  }
  if (auto it = g_activePythonTransactions.find(m_document);
      it != std::end(g_activePythonTransactions) && it->second > 0u)
  {
    --it->second;
  }
  m_document = nullptr;
  return result;
}

void PythonDocumentTransaction::cancel()
{
  if (m_document == nullptr)
  {
    return;
  }
  if (m_impl && m_impl->transaction)
  {
    m_impl->transaction->cancel();
    m_impl->transaction.reset();
  }
  if (auto it = g_activePythonTransactions.find(m_document);
      it != std::end(g_activePythonTransactions) && it->second > 0u)
  {
    --it->second;
  }
  m_document = nullptr;
}

} // namespace tb::ui
