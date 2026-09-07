#include "ui/python/PythonApiCatalog.h"

#include <array>

namespace tb::ui
{
namespace
{
using enum PythonApiSymbolKind;

constexpr auto ApiTypes = std::array{
  PythonApiTypeInfo{PythonApiType::Module, "trenchbroom"},
  PythonApiTypeInfo{PythonApiType::Documents, "documents"},
  PythonApiTypeInfo{PythonApiType::Objects, "objects"},
  PythonApiTypeInfo{PythonApiType::Entities, "entities"},
  PythonApiTypeInfo{PythonApiType::Brushes, "brushes"},
  PythonApiTypeInfo{PythonApiType::Faces, "faces"},
  PythonApiTypeInfo{PythonApiType::Materials, "materials"},
  PythonApiTypeInfo{PythonApiType::Groups, "groups"},
  PythonApiTypeInfo{PythonApiType::Modules, "modules"},
  PythonApiTypeInfo{PythonApiType::Ir, "ir"},
  PythonApiTypeInfo{PythonApiType::Geometry, "geometry"},
  PythonApiTypeInfo{PythonApiType::History, "history"},
  PythonApiTypeInfo{PythonApiType::Assets, "assets"},
  PythonApiTypeInfo{PythonApiType::Actions, "actions"},
  PythonApiTypeInfo{PythonApiType::Validation, "validation"},
  PythonApiTypeInfo{PythonApiType::Vec3, "Vec3"},
  PythonApiTypeInfo{PythonApiType::Plane, "Plane"},
  PythonApiTypeInfo{PythonApiType::Document, "Document"},
  PythonApiTypeInfo{PythonApiType::Selection, "Selection"},
  PythonApiTypeInfo{PythonApiType::Entity, "Entity"},
  PythonApiTypeInfo{PythonApiType::Brush, "Brush"},
  PythonApiTypeInfo{PythonApiType::Face, "Face"},
  PythonApiTypeInfo{PythonApiType::Material, "Material"},
  PythonApiTypeInfo{PythonApiType::MaterialCollection, "MaterialCollection"},
  PythonApiTypeInfo{PythonApiType::Transaction, "Transaction"},
  PythonApiTypeInfo{PythonApiType::PluginPanel, "PluginPanel"},
};

constexpr auto ModuleSymbols = std::array{
  PythonApiSymbol{"Vec3", Class, "(x, y, z)", PythonApiValueType{PythonApiType::Vec3}},
  PythonApiSymbol{
    "Plane", Class, "(normal, dist)", PythonApiValueType{PythonApiType::Plane}},
  PythonApiSymbol{"Document", Class, "handle"},
  PythonApiSymbol{"Selection", Class, "handle"},
  PythonApiSymbol{"Entity", Class, "handle"},
  PythonApiSymbol{"Brush", Class, "handle"},
  PythonApiSymbol{"Face", Class, "handle"},
  PythonApiSymbol{"Material", Class, "handle"},
  PythonApiSymbol{"MaterialCollection", Class, "handle"},
  PythonApiSymbol{"Transaction", Class, "context manager"},
  PythonApiSymbol{"PluginPanel", Class, "handle"},
  PythonApiSymbol{
    "documents", Property, "documents", PythonApiValueType{PythonApiType::Documents}},
  PythonApiSymbol{
    "objects", Property, "objects", PythonApiValueType{PythonApiType::Objects}},
  PythonApiSymbol{
    "entities", Property, "entities", PythonApiValueType{PythonApiType::Entities}},
  PythonApiSymbol{
    "brushes", Property, "brushes", PythonApiValueType{PythonApiType::Brushes}},
  PythonApiSymbol{"faces", Property, "faces", PythonApiValueType{PythonApiType::Faces}},
  PythonApiSymbol{
    "materials", Property, "materials", PythonApiValueType{PythonApiType::Materials}},
  PythonApiSymbol{
    "groups", Property, "groups", PythonApiValueType{PythonApiType::Groups}},
  PythonApiSymbol{
    "modules", Property, "modules", PythonApiValueType{PythonApiType::Modules}},
  PythonApiSymbol{"ir", Property, "ir", PythonApiValueType{PythonApiType::Ir}},
  PythonApiSymbol{
    "geometry", Property, "geometry", PythonApiValueType{PythonApiType::Geometry}},
  PythonApiSymbol{
    "history", Property, "history", PythonApiValueType{PythonApiType::History}},
  PythonApiSymbol{
    "assets", Property, "assets", PythonApiValueType{PythonApiType::Assets}},
  PythonApiSymbol{
    "actions", Property, "actions", PythonApiValueType{PythonApiType::Actions}},
  PythonApiSymbol{
    "validation", Property, "validation", PythonApiValueType{PythonApiType::Validation}},
  PythonApiSymbol{
    "selected_brushes",
    Function,
    "() -> list[Brush]",
    PythonApiValueType{PythonApiType::Brush, 1u}},
  PythonApiSymbol{
    "selectedBrushes",
    Function,
    "() -> list[Brush]",
    PythonApiValueType{PythonApiType::Brush, 1u}},
  PythonApiSymbol{
    "selected_entities",
    Function,
    "(include_brushes=False) -> list[Entity]",
    PythonApiValueType{PythonApiType::Entity, 1u}},
  PythonApiSymbol{
    "selectedEntities",
    Function,
    "(include_brushes=False) -> list[Entity]",
    PythonApiValueType{PythonApiType::Entity, 1u}},
  PythonApiSymbol{
    "selected_all_entities",
    Function,
    "() -> list[Entity]",
    PythonApiValueType{PythonApiType::Entity, 1u}},
  PythonApiSymbol{
    "selectedAllEntities",
    Function,
    "() -> list[Entity]",
    PythonApiValueType{PythonApiType::Entity, 1u}},
  PythonApiSymbol{
    "selection",
    Function,
    "() -> Selection",
    PythonApiValueType{PythonApiType::Selection}},
  PythonApiSymbol{
    "selected_faces",
    Function,
    "() -> list[Face]",
    PythonApiValueType{PythonApiType::Face, 1u}},
  PythonApiSymbol{
    "selectedFaces",
    Function,
    "() -> list[Face]",
    PythonApiValueType{PythonApiType::Face, 1u}},
  PythonApiSymbol{"translate", Function, "(...)"},
  PythonApiSymbol{"rotate", Function, "(...)"},
  PythonApiSymbol{"scale", Function, "(...)"},
  PythonApiSymbol{"duplicate", Function, "(target=None)"},
  PythonApiSymbol{"delete_selection", Function, "()"},
  PythonApiSymbol{"deleteSelection", Function, "()"},
  PythonApiSymbol{"deselect_all", Function, "()"},
  PythonApiSymbol{"deselectAll", Function, "()"},
  PythonApiSymbol{
    "current_document",
    Function,
    "() -> Document",
    PythonApiValueType{PythonApiType::Document}},
  PythonApiSymbol{
    "document", Function, "() -> Document", PythonApiValueType{PythonApiType::Document}},
  PythonApiSymbol{"execute_action", Function, "(action_id)"},
  PythonApiSymbol{"list_actions", Function, "() -> list[str]"},
  PythonApiSymbol{
    "create_brush",
    Function,
    "(points, material=None) -> Brush",
    PythonApiValueType{PythonApiType::Brush}},
  PythonApiSymbol{
    "create_plugin_panel",
    Function,
    "(title) -> PluginPanel",
    PythonApiValueType{PythonApiType::PluginPanel}},
  PythonApiSymbol{"register_callback", Function, "(event, callback) -> int"},
  PythonApiSymbol{"unregister_callback", Function, "(token)"},
  PythonApiSymbol{"set_interval", Function, "(callback, milliseconds) -> int"},
  PythonApiSymbol{"clear_interval", Function, "(token)"},
  PythonApiSymbol{"set_timeout", Function, "(callback, milliseconds) -> int"},
};

constexpr auto DocumentsSymbols = std::array{
  PythonApiSymbol{
    "current", Function, "() -> Document", PythonApiValueType{PythonApiType::Document}},
  PythonApiSymbol{
    "list",
    Function,
    "() -> list[Document]",
    PythonApiValueType{PythonApiType::Document, 1u}},
  PythonApiSymbol{"snapshot", Function, "() -> dict"},
  PythonApiSymbol{
    "open", Function, "(path) -> Document", PythonApiValueType{PythonApiType::Document}},
  PythonApiSymbol{
    "open_verified",
    Function,
    "(path) -> Document",
    PythonApiValueType{PythonApiType::Document}},
  PythonApiSymbol{
    "activate",
    Function,
    "(document) -> Document",
    PythonApiValueType{PythonApiType::Document}},
  PythonApiSymbol{"close", Function, "(document, discard_changes=False)"},
  PythonApiSymbol{
    "save",
    Function,
    "(path=None) -> Document",
    PythonApiValueType{PythonApiType::Document}},
  PythonApiSymbol{
    "save_as",
    Function,
    "(path) -> Document",
    PythonApiValueType{PythonApiType::Document}},
  PythonApiSymbol{
    "save_current",
    Function,
    "(path=None) -> Document",
    PythonApiValueType{PythonApiType::Document}},
  PythonApiSymbol{
    "export",
    Function,
    "(path, strip_tb_properties=True) -> Document",
    PythonApiValueType{PythonApiType::Document}},
};

constexpr auto ObjectsSymbols = std::array{
  PythonApiSymbol{
    "selection",
    Function,
    "() -> Selection",
    PythonApiValueType{PythonApiType::Selection}},
  PythonApiSymbol{"snapshot", Function, "() -> dict"},
  PythonApiSymbol{"bounds", Function, "() -> dict | None"},
  PythonApiSymbol{"inspect", Function, "() -> dict"},
  PythonApiSymbol{"translate", Function, "(...)"},
  PythonApiSymbol{"rotate", Function, "(...)"},
  PythonApiSymbol{"scale", Function, "(...)"},
  PythonApiSymbol{"duplicate", Function, "(target=None)"},
  PythonApiSymbol{"delete_selection", Function, "() -> bool"},
  PythonApiSymbol{"deselect_all", Function, "()"},
  PythonApiSymbol{"set_selection", Function, "(objects)"},
};

constexpr auto EntitiesSymbols = std::array{
  PythonApiSymbol{
    "list",
    Function,
    "() -> list[Entity]",
    PythonApiValueType{PythonApiType::Entity, 1u}},
  PythonApiSymbol{
    "selected",
    Function,
    "(include_brushes=False) -> list[Entity]",
    PythonApiValueType{PythonApiType::Entity, 1u}},
  PythonApiSymbol{
    "create",
    Function,
    "(classname, properties={}, origin=None, select=False) -> Entity",
    PythonApiValueType{PythonApiType::Entity}},
  PythonApiSymbol{"delete", Function, "(entity)"},
  PythonApiSymbol{"update", Function, "(entity, properties={}, remove_keys=[])"},
  PythonApiSymbol{
    "properties_update", Function, "(entities, properties={}, remove_keys=[])"},
  PythonApiSymbol{"properties_delete", Function, "(entities, keys)"},
  PythonApiSymbol{
    "find",
    Function,
    "(classname=None, property=None, value=None) -> list[Entity]",
    PythonApiValueType{PythonApiType::Entity, 1u}},
};

constexpr auto BrushesSymbols = std::array{
  PythonApiSymbol{
    "list", Function, "() -> list[Brush]", PythonApiValueType{PythonApiType::Brush, 1u}},
  PythonApiSymbol{
    "selected",
    Function,
    "() -> list[Brush]",
    PythonApiValueType{PythonApiType::Brush, 1u}},
  PythonApiSymbol{
    "create",
    Function,
    "(points, material=None) -> Brush",
    PythonApiValueType{PythonApiType::Brush}},
  PythonApiSymbol{
    "create_box",
    Function,
    "(min, max, material=None, select=True) -> Brush",
    PythonApiValueType{PythonApiType::Brush}},
  PythonApiSymbol{
    "create_boxes_batch",
    Function,
    "(boxes, material=None, select=True) -> list[Brush]",
    PythonApiValueType{PythonApiType::Brush, 1u}},
  PythonApiSymbol{
    "create_prism",
    Function,
    "(points2d, min_z, max_z, material=None, select=True) -> Brush",
    PythonApiValueType{PythonApiType::Brush}},
  PythonApiSymbol{
    "create_polygon_batch",
    Function,
    "(polygons, material=None, select=True) -> list[Brush]",
    PythonApiValueType{PythonApiType::Brush, 1u}},
};

constexpr auto FacesSymbols = std::array{
  PythonApiSymbol{
    "list", Function, "() -> list[Face]", PythonApiValueType{PythonApiType::Face, 1u}},
  PythonApiSymbol{
    "selected",
    Function,
    "() -> list[Face]",
    PythonApiValueType{PythonApiType::Face, 1u}},
  PythonApiSymbol{"set_material", Function, "(faces, material)"},
};

constexpr auto MaterialsSymbols = std::array{
  PythonApiSymbol{
    "list",
    Function,
    "() -> list[Material]",
    PythonApiValueType{PythonApiType::Material, 1u}},
  PythonApiSymbol{
    "collections",
    Function,
    "() -> list[MaterialCollection]",
    PythonApiValueType{PythonApiType::MaterialCollection, 1u}},
  PythonApiSymbol{
    "search",
    Function,
    "(query, limit=50) -> list[Material]",
    PythonApiValueType{PythonApiType::Material, 1u}},
  PythonApiSymbol{"current", Function, "() -> str"},
};

constexpr auto HistorySymbols = std::array{
  PythonApiSymbol{"status", Function, "(document=None) -> dict"},
  PythonApiSymbol{"undo", Function, "(document=None) -> bool"},
  PythonApiSymbol{"redo", Function, "(document=None) -> bool"},
};

constexpr auto GroupsSymbols = std::array{
  PythonApiSymbol{"create_from_selection", Function, "(name) -> dict"},
  PythonApiSymbol{"inspect_selected", Function, "() -> list[dict]"},
  PythonApiSymbol{"rename_selected", Function, "(name) -> list[dict]"},
  PythonApiSymbol{"ungroup_selected", Function, "() -> dict"},
};

constexpr auto ModulesSymbols = std::array{
  PythonApiSymbol{
    "list", Function, "(include_stale=False, include_empty=False) -> list[dict]"},
  PythonApiSymbol{"inspect", Function, "(module_id) -> dict"},
  PythonApiSymbol{"select", Function, "(module_id) -> dict"},
  PythonApiSymbol{"compact", Function, "(module_id) -> dict"},
  PythonApiSymbol{"forget", Function, "(module_id)"},
};

constexpr auto IrSymbols = std::array{
  PythonApiSymbol{"validate", Function, "(ir) -> dict"},
  PythonApiSymbol{"preview", Function, "(ir) -> dict"},
  PythonApiSymbol{"compile_preview_from_file", Function, "(path) -> dict"},
};

constexpr auto GeometrySymbols = std::array{
  PythonApiSymbol{
    "analyze_selection", Function, "(grid=1.0, detail='summary', max_brushes=100) -> dict"},
};

constexpr auto AssetsSymbols = std::array{
  PythonApiSymbol{"search", Function, "(query='', type=None, limit=50) -> list[dict]"},
  PythonApiSymbol{
    "place_model",
    Function,
    "(path, origin=None, classname='cycler_sprite', ...) -> Entity",
    PythonApiValueType{PythonApiType::Entity}},
  PythonApiSymbol{
    "place_sprite",
    Function,
    "(path, origin=None, classname='cycler_sprite', ...) -> Entity",
    PythonApiValueType{PythonApiType::Entity}},
  PythonApiSymbol{
    "place_sound",
    Function,
    "(path, origin=None, classname='ambient_generic', ...) -> Entity",
    PythonApiValueType{PythonApiType::Entity}},
};

constexpr auto ActionsSymbols = std::array{
  PythonApiSymbol{"list", Function, "() -> list[str]"},
  PythonApiSymbol{"execute", Function, "(action_id)"},
};

constexpr auto ValidationSymbols = std::array{
  PythonApiSymbol{"check", Function, "(include_hidden=False, limit=500) -> dict"},
};

constexpr auto Vec3Symbols = std::array{
  PythonApiSymbol{"x", Property, "float"},
  PythonApiSymbol{"y", Property, "float"},
  PythonApiSymbol{"z", Property, "float"},
  PythonApiSymbol{"dot", Method, "(other) -> float"},
  PythonApiSymbol{
    "cross", Method, "(other) -> Vec3", PythonApiValueType{PythonApiType::Vec3}},
  PythonApiSymbol{"length", Method, "() -> float"},
  PythonApiSymbol{
    "normalize", Method, "() -> Vec3", PythonApiValueType{PythonApiType::Vec3}},
  PythonApiSymbol{
    "normalized", Method, "() -> Vec3", PythonApiValueType{PythonApiType::Vec3}},
};

constexpr auto PlaneSymbols = std::array{
  PythonApiSymbol{"normal", Property, "Vec3", PythonApiValueType{PythonApiType::Vec3}},
  PythonApiSymbol{"dist", Property, "float"},
  PythonApiSymbol{
    "from_points",
    Function,
    "(p1, p2, p3) -> Plane",
    PythonApiValueType{PythonApiType::Plane}},
  PythonApiSymbol{"distance", Method, "(point) -> float"},
  PythonApiSymbol{
    "project", Method, "(point) -> Vec3", PythonApiValueType{PythonApiType::Vec3}},
};

constexpr auto DocumentSymbols = std::array{
  PythonApiSymbol{"id", Property, "str"},
  PythonApiSymbol{"path", Property, "str | None"},
  PythonApiSymbol{
    "entities", Property, "list[Entity]", PythonApiValueType{PythonApiType::Entity, 1u}},
  PythonApiSymbol{
    "selection", Property, "Selection", PythonApiValueType{PythonApiType::Selection}},
  PythonApiSymbol{
    "materials",
    Property,
    "list[Material]",
    PythonApiValueType{PythonApiType::Material, 1u}},
  PythonApiSymbol{
    "material_collections",
    Property,
    "list[MaterialCollection]",
    PythonApiValueType{PythonApiType::MaterialCollection, 1u}},
  PythonApiSymbol{
    "vertex_tool_vertices",
    Method,
    "() -> list[Vec3]",
    PythonApiValueType{PythonApiType::Vec3, 1u}},
  PythonApiSymbol{"save", Method, "()"},
  PythonApiSymbol{"close", Method, "(discard_changes=False)"},
  PythonApiSymbol{"save_as", Method, "(path)"},
  PythonApiSymbol{"export", Method, "(path, strip_tb_properties=True)"},
  PythonApiSymbol{"reload", Method, "()"},
  PythonApiSymbol{
    "transaction",
    Method,
    "(name) -> Transaction",
    PythonApiValueType{PythonApiType::Transaction}},
  PythonApiSymbol{"set_triangle_uvs", Method, "(triangles)"},
  PythonApiSymbol{"set_face_uvs", Method, "(updates)"},
  PythonApiSymbol{"set_face_uvs_with_split", Method, "(updates)"},
  PythonApiSymbol{"select", Method, "(objects)"},
  PythonApiSymbol{"clear_selection", Method, "()"},
};

constexpr auto SelectionSymbols = std::array{
  PythonApiSymbol{
    "entity", Property, "Entity | None", PythonApiValueType{PythonApiType::Entity}},
  PythonApiSymbol{
    "brush", Property, "Brush | None", PythonApiValueType{PythonApiType::Brush}},
  PythonApiSymbol{"properties", Property, "dict | None"},
  PythonApiSymbol{"classname", Property, "str | None"},
  PythonApiSymbol{
    "entities", Property, "list[Entity]", PythonApiValueType{PythonApiType::Entity, 1u}},
  PythonApiSymbol{
    "all_entities",
    Property,
    "list[Entity]",
    PythonApiValueType{PythonApiType::Entity, 1u}},
  PythonApiSymbol{
    "brushes", Property, "list[Brush]", PythonApiValueType{PythonApiType::Brush, 1u}},
  PythonApiSymbol{
    "brush_faces", Property, "list[Face]", PythonApiValueType{PythonApiType::Face, 1u}},
  PythonApiSymbol{"set_property", Method, "(key, value, create_if_missing=True)"},
  PythonApiSymbol{
    "brush_vertices",
    Method,
    "() -> list[list[Vec3]]",
    PythonApiValueType{PythonApiType::Vec3, 2u}},
  PythonApiSymbol{"triangle_uvs", Method, "() -> dict"},
  PythonApiSymbol{"set", Method, "(objects)"},
  PythonApiSymbol{"add", Method, "(objects)"},
  PythonApiSymbol{"deselect_all", Method, "()"},
  PythonApiSymbol{"clear", Method, "()"},
  PythonApiSymbol{"duplicate", Method, "()"},
  PythonApiSymbol{"translate", Method, "(dx, dy, dz)"},
  PythonApiSymbol{"rotate", Method, "(axis_x, axis_y, axis_z, angle, ...)"},
  PythonApiSymbol{"scale", Method, "(scale_x, scale_y, scale_z, ...)"},
  PythonApiSymbol{"chamfer_vertices", Method, "(distance)"},
  PythonApiSymbol{"chamfer_edges", Method, "(distance, segments=1)"},
};

constexpr auto EntitySymbols = std::array{
  PythonApiSymbol{"id", Property, "str"},
  PythonApiSymbol{"classname", Property, "str"},
  PythonApiSymbol{
    "brushes", Property, "list[Brush]", PythonApiValueType{PythonApiType::Brush, 1u}},
  PythonApiSymbol{"properties", Property, "dict[str, str]"},
  PythonApiSymbol{"keys", Method, "() -> list[str]"},
  PythonApiSymbol{"values", Method, "() -> list[str]"},
  PythonApiSymbol{"items", Method, "() -> list[tuple[str, str]]"},
  PythonApiSymbol{"get", Method, "(key, default=None)"},
  PythonApiSymbol{"set", Method, "(key, value)"},
  PythonApiSymbol{"remove", Method, "(key)"},
};

constexpr auto BrushSymbols = std::array{
  PythonApiSymbol{"id", Property, "str"},
  PythonApiSymbol{
    "entity", Property, "Entity", PythonApiValueType{PythonApiType::Entity}},
  PythonApiSymbol{
    "faces", Method, "() -> list[Face]", PythonApiValueType{PythonApiType::Face, 1u}},
};

constexpr auto FaceSymbols = std::array{
  PythonApiSymbol{"id", Property, "str"},
  PythonApiSymbol{
    "vertices", Property, "list[Vec3]", PythonApiValueType{PythonApiType::Vec3, 1u}},
  PythonApiSymbol{"uv_loops", Property, "list"},
  PythonApiSymbol{"texture_name", Property, "str"},
  PythonApiSymbol{"material", Property, "str"},
  PythonApiSymbol{"offset", Property, "tuple[float, float]"},
  PythonApiSymbol{"scale", Property, "tuple[float, float]"},
  PythonApiSymbol{"rotation", Property, "float"},
  PythonApiSymbol{"surface_contents", Property, "int | None"},
  PythonApiSymbol{"surface_flags", Property, "int | None"},
  PythonApiSymbol{"surface_value", Property, "float | None"},
  PythonApiSymbol{"set_uv_loops", Method, "(loops)"},
  PythonApiSymbol{"set_material", Method, "(name)"},
};

constexpr auto MaterialSymbols = std::array{
  PythonApiSymbol{"name", Property, "str"},
  PythonApiSymbol{"collection_name", Property, "str"},
  PythonApiSymbol{"width", Property, "int"},
  PythonApiSymbol{"height", Property, "int"},
};

constexpr auto MaterialCollectionSymbols = std::array{
  PythonApiSymbol{"name", Property, "str"},
  PythonApiSymbol{"path", Property, "str"},
  PythonApiSymbol{"material_count", Property, "int"},
  PythonApiSymbol{
    "materials",
    Property,
    "list[Material]",
    PythonApiValueType{PythonApiType::Material, 1u}},
};

constexpr auto TransactionSymbols = std::array{
  PythonApiSymbol{"commit", Method, "() -> bool"},
  PythonApiSymbol{"cancel", Method, "()"},
};

constexpr auto PluginPanelSymbols = std::array{
  PythonApiSymbol{"add_label", Method, "(text)"},
  PythonApiSymbol{"add_label_named", Method, "(key, text)"},
  PythonApiSymbol{"set_label_text", Method, "(key, text)"},
  PythonApiSymbol{
    "add_group",
    Method,
    "(key, title) -> PluginPanel",
    PythonApiValueType{PythonApiType::PluginPanel}},
  PythonApiSymbol{
    "add_row",
    Method,
    "(key) -> PluginPanel",
    PythonApiValueType{PythonApiType::PluginPanel}},
  PythonApiSymbol{
    "add_column",
    Method,
    "(key) -> PluginPanel",
    PythonApiValueType{PythonApiType::PluginPanel}},
  PythonApiSymbol{"set_widget_visible", Method, "(key, visible)"},
  PythonApiSymbol{"add_button", Method, "(text, callback)"},
  PythonApiSymbol{"add_button_callback", Method, "(text, callback)"},
  PythonApiSymbol{"add_checkbox", Method, "(...)"},
  PythonApiSymbol{"get_checkbox", Method, "(key) -> bool"},
  PythonApiSymbol{"add_line_edit", Method, "(text, callback)"},
  PythonApiSymbol{"add_text_field", Method, "(key, label, value='')"},
  PythonApiSymbol{"get_text_field", Method, "(key) -> str"},
  PythonApiSymbol{"set_text_field", Method, "(key, value)"},
  PythonApiSymbol{"add_text_area", Method, "(key, label, value='')"},
  PythonApiSymbol{"get_text_area", Method, "(key) -> str"},
  PythonApiSymbol{"set_text_area", Method, "(key, value)"},
  PythonApiSymbol{"add_int_field", Method, "(key, label, value=0, ...)"},
  PythonApiSymbol{"get_int_field", Method, "(key) -> int"},
  PythonApiSymbol{"add_float_field", Method, "(key, label, value=0.0, ...)"},
  PythonApiSymbol{"get_float_field", Method, "(key) -> float"},
  PythonApiSymbol{"add_combo_box", Method, "(...)"},
  PythonApiSymbol{"get_combo_box_text", Method, "(key) -> str"},
  PythonApiSymbol{"add_color_field", Method, "(key, label, color)"},
  PythonApiSymbol{"get_color_field", Method, "(key) -> tuple[int, int, int]"},
  PythonApiSymbol{"add_table_widget", Method, "(key, columns, rows, ...)"},
  PythonApiSymbol{"set_table_widget_rows", Method, "(key, rows)"},
  PythonApiSymbol{"add_tree_widget", Method, "(key, columns, rows, ...)"},
  PythonApiSymbol{"set_tree_widget_items", Method, "(key, rows)"},
  PythonApiSymbol{"add_html_view", Method, "(key, html, ...)"},
  PythonApiSymbol{"set_html_view", Method, "(key, html)"},
  PythonApiSymbol{"clear", Method, "()"},
};

constexpr auto ConsoleHelperNames = std::array<std::string_view, 24>{
  "selected_brushes",
  "selectedBrushes",
  "selected_entities",
  "selectedEntities",
  "selected_faces",
  "selectedFaces",
  "translate",
  "rotate",
  "scale",
  "duplicate",
  "delete_selection",
  "deleteSelection",
  "deselect_all",
  "deselectAll",
  "current_document",
  "document",
  "selection",
  "selected_all_entities",
  "selectedAllEntities",
  "create_brush",
  "execute_action",
  "list_actions",
  "Vec3",
  "Plane",
};
} // namespace

std::span<const PythonApiTypeInfo> pythonApiTypes()
{
  return ApiTypes;
}

std::span<const PythonApiSymbol> pythonApiSymbols(const PythonApiType type)
{
  switch (type)
  {
  case PythonApiType::Module:
    return ModuleSymbols;
  case PythonApiType::Documents:
    return DocumentsSymbols;
  case PythonApiType::Objects:
    return ObjectsSymbols;
  case PythonApiType::Entities:
    return EntitiesSymbols;
  case PythonApiType::Brushes:
    return BrushesSymbols;
  case PythonApiType::Faces:
    return FacesSymbols;
  case PythonApiType::Materials:
    return MaterialsSymbols;
  case PythonApiType::Groups:
    return GroupsSymbols;
  case PythonApiType::Modules:
    return ModulesSymbols;
  case PythonApiType::Ir:
    return IrSymbols;
  case PythonApiType::Geometry:
    return GeometrySymbols;
  case PythonApiType::History:
    return HistorySymbols;
  case PythonApiType::Assets:
    return AssetsSymbols;
  case PythonApiType::Actions:
    return ActionsSymbols;
  case PythonApiType::Validation:
    return ValidationSymbols;
  case PythonApiType::Vec3:
    return Vec3Symbols;
  case PythonApiType::Plane:
    return PlaneSymbols;
  case PythonApiType::Document:
    return DocumentSymbols;
  case PythonApiType::Selection:
    return SelectionSymbols;
  case PythonApiType::Entity:
    return EntitySymbols;
  case PythonApiType::Brush:
    return BrushSymbols;
  case PythonApiType::Face:
    return FaceSymbols;
  case PythonApiType::Material:
    return MaterialSymbols;
  case PythonApiType::MaterialCollection:
    return MaterialCollectionSymbols;
  case PythonApiType::Transaction:
    return TransactionSymbols;
  case PythonApiType::PluginPanel:
    return PluginPanelSymbols;
  }
  return {};
}

std::span<const std::string_view> pythonConsoleHelperNames()
{
  return ConsoleHelperNames;
}

} // namespace tb::ui
