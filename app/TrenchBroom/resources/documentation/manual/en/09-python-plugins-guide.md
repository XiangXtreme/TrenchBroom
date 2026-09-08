# Python Scripting and Plugins {#python_scripting_and_plugins}

## Python Console {#python_console}

Open the **Python** tab in the bottom info panel to run Python API scripts and quick commands against the active TrenchBroom session.

### Side-by-Side Split Workspace {#console_execution}

The console features a modern side-by-side split workspace:

- **Left Pane (Output Log)**: Displays execution logs, standard output from `print(...)`, interactive evaluation results, and error tracebacks.
- **Right Pane (Script Editor)**: A full multiline code editor for authoring and running single-line expressions, quick snippets, or complex procedural scripts. Drag the center splitter bar to adjust the width between the log and the editor.

![Python Console](images/PythonConsole.png)

### Zero-Boilerplate Global Helpers {#console_global_helpers}

The Python console automatically binds the active document, active selection, and math primitives into the global namespace. Import `trenchbroom` for domain modules and explicit-object edits:

- **Global Objects**:
  - `doc`: The active `Document` handle.
  - `sel`: The active `Selection` handle.
  - `Vec3`, `Plane`: 3D vector and plane mathematics classes.
- **Selection operations**: `sel.brushes`, `sel.entities`, `sel.brush_faces`, `sel.translate((x, y, z))`, `sel.rotate((x, y, z), degrees)`, `sel.scale(factors)`, and `sel.duplicate()`.
- **Explicit-object operations**: `trenchbroom.objects.translate(targets, offset)`, `rotate`, `scale`, `duplicate`, and `delete`. They preserve an unrelated selection.

### Keyboard Shortcuts and History {#console_shortcuts_and_history}

- **Execute**: Press #key(Enter) (or #key(Ctrl)+#key(Enter)) or click **Run** to execute the script in the editor.
- **Newline**: Press #key(Shift)+#key(Enter) to insert a newline without running the script.
- **Code Completion**: Press #key(Ctrl)+#key(Space) or #key(Tab) to open the completion popup; press #key(Tab) to insert the selected completion. #key(Enter) executes the current script even while the popup is visible.
- **History Navigation**: Press #key(Up) on the first line or #key(Down) on the last line to navigate through previous scripts (up to 100 history entries). Unsubmitted text in the editor is automatically preserved as a draft while browsing history.
- **Clear Output**: Click **Clear** in the tab header to clear all output logs.
- **Font Customization**: Configure the family and size under **Preferences > View > Fonts > Python Console**; only installed monospace families are listed.

### Output and Error Reporting {#console_output_and_errors}

All output from `print(...)` and evaluation results are logged to the output view. If an exception occurs, a formatted traceback with line numbers is printed in red.

### Practical Console Examples {#console_examples}

These examples can be pasted directly into the console. Before accessing an object in a list, use `[0]` to retrieve one element or a `for` loop to process each element; for example, `sel.brushes[0].entity` is the owning entity of the first selected brush.

#### Common Read-Only Commands {#example_common_queries}

```python
# Print the active map path / 打印当前地图路径
print(doc.path)

# Count selected brushes / 统计选中的 Brush 数量
print(f"Selected brushes: {len(sel.brushes)}")

# Print the owning entity and properties of one selected brush / 打印一个选中 Brush 所属实体及其属性
brush = sel.brush
if brush is not None:
    entity = brush.entity
    print(f"classname = {entity.classname}")
    for key, value in entity.items():
        print(f"{key} = {value}")
else:
    print("Select one brush first")

# Print materials used by selected faces / 打印选中面的材质
for face in sel.brush_faces:
    print(face.material)
```

#### Quick Selection Transforms {#example_quick_transforms}

```python
# Rotate the first selected brush by 45 degrees around the Z axis
first_brush = sel.brushes[0]
trenchbroom.objects.rotate(first_brush, (0, 0, 1), 45)

# Duplicate the active selection and translate it 64 units up
sel.duplicate()
sel.translate((0, 0, 64))
```

#### Inspecting Selection and Brush Information {#example_inspect_selection}

```python
brushes = sel.brushes
print(f"Selected {len(brushes)} brush(es):")
for i, brush in enumerate(brushes):
    faces = brush.faces()
    print(f"  Brush #{i}: {len(faces)} faces")
    for face in faces:
        print(f"    - Material: {face.material}, Vertices: {len(face.vertices)}")
```

#### Generating a Step Array {#example_step_array}

```python
for _ in range(8):
    sel.duplicate()
    sel.translate((64, 0, 16))
```

#### Creating a Base Brush {#example_create_brush}

```python
# Create a 64x64x64 cube brush centered at origin and select it
b = trenchbroom.brushes.create([
    (-32, -32, -32), (32, -32, -32), (32, 32, -32), (-32, 32, -32),
    (-32, -32, 32), (32, -32, 32), (32, 32, 32), (-32, 32, 32)
])
sel.set([b])
```

#### Batch Applying Face Materials {#example_batch_face_materials}

```python
for face in sel.brush_faces:
    face.set_material("common/caulk")
```

#### Batch Normalizing Entity Properties {#example_batch_entity_properties}

```python
with doc.transaction("Batch Align Entities"):
    for ent in doc.entities:
        if ent.classname == "light" and not ent.get("light"):
            ent.set("light", "300")
```

## Python Plugins {#python_plugins}

TrenchBroom features an embedded Python API runtime and extension system. Open **Preferences > Misc**, then click **Python Plugin Manager...** in the **Tools** section to manage manifest-based UI plugins. Configure directories with **Install UI Plugin...** inside the manager. It lists detected plugins, load status, metadata, and errors; use search or **Only show issues** to diagnose larger plugin sets, then refresh after changing files.

![Plugin Inspector](images/PluginInspector.png)

#### Plugin Types and Lifecycle {#plugin_types_and_lifecycle}

TrenchBroom distinguishes between two plugin types:

- **UI Plugins (`pluginType: "ui"`)**: Persistent plugins that declare a `trenchbroom-plugin.json` manifest. They are loaded at startup or when refreshing the plugin manager, and can register custom panels in the **Plugins** inspector tab, global actions, event listeners, and timers.
- **Script plugins (`pluginType: "script"`)**: Standalone scripts executed on demand through the [Python Console](#python_console), **Run > Run Python Script...**, or custom actions. The plugin manager reports script manifests but does not execute their entry files. Scripts access the editor with `import trenchbroom as tb`.

#### Manifest File Format {#plugin_manifest_format}

Each UI plugin directory must contain a `trenchbroom-plugin.json` manifest file at its root:

```json
{
  "id": "com.example.my_plugin",
  "name": "My Custom Plugin",
  "version": "1.0.0",
  "apiVersion": 2,
  "pluginType": "ui",
  "entry": "main.py",
  "description": "A sample plugin with a custom inspector panel.",
  "author": "Author Name"
}
```

The manifest fields are defined as follows:

| Field | Type | Description |
| :--- | :--- | :--- |
| `id` | string | Unique plugin identifier |
| `name` | string | Display name shown in the UI |
| `version` | string | Semantic version string |
| `apiVersion` | integer | Must be `2` for the current API contract |
| `pluginType` | string | `"ui"` for persistent UI plugins, or `"script"` for scripts |
| `entry` | string | Relative path to the Python entry script |
| `description` | string | Optional description of the plugin |
| `author` | string | Optional author name or contact |

`pluginType` defaults to `"script"`. Persistent UI plugins must set `"pluginType": "ui"` explicitly or the plugin manager will not load them.

#### The trenchbroom Python API {#the_trenchbroom_python_api}

All Python scripts and plugins access TrenchBroom through the embedded `trenchbroom` module (`import trenchbroom as tb`). Key components include:

- `trenchbroom.documents.current()`: Returns the active `Document` handle representing the open map.
- `doc.transaction(name)`: A context manager (`with doc.transaction("Action Name"):`) that groups modifications into a single undo/redo step and automatically rolls back on Python exceptions.
- `doc.selection`: The `Selection` handle for querying selected objects (`entity`, `brush`, `entities`, `all_entities`, `brushes`, `brush_faces`), reading the first relevant entity with `sel[key]`, writing all relevant entities with `sel[key] = value`, and applying transformations (`translate`, `rotate`, `scale`, `duplicate`, `chamfer_vertices`, `chamfer_edges`). Face-only selections expose the face's parent entity through `entity` and `all_entities`; an empty selection returns `None`/empty results.
- `doc.entities`: List of all `Entity` objects in the map. Access properties using `.get(key, default)` and `.set(key, value)`.
- `brush.faces()`: Returns the polygon `Face` objects comprising a brush, providing access to `.material`, `.offset`, `.scale`, `.rotation`, and `.vertices`.
- `trenchbroom.Vec3(x, y, z)` and `trenchbroom.Plane(normal, dist)`: 3D vector and plane math primitives.

These objects are live handles rather than snapshots. Closing or reloading a document and deleting nodes invalidates related handles; changing brush geometry also invalidates previously acquired `Face` handles. Access raises `RuntimeError` after invalidation, so reacquire objects after such changes.

#### Building Custom UI Panels {#building_custom_ui_panels}

UI plugins create interactive panels on the **Plugins** inspector tab using `trenchbroom.create_plugin_panel(title)`. The returned `PluginPanel` provides declarative controls:

- **Labels & Text**: `.add_label(text)`, `.add_label_named(key, text)`, `.set_label_text(key, text)`, and `.add_html_view(key, html, height, callback)`.
- **Form Inputs**: `.add_text_field(key, label, value)`, `.add_text_area(key, label, value)`, `.add_int_field(key, label, value, min, max)`, `.add_float_field(key, label, value, min, max, decimals, step)`, `.add_checkbox(key, text, checked)`, `.add_combo_box(key, label, items, callback, current)`, and `.add_color_field(key, label, color)`. Named controls have corresponding `get_*` methods; text fields and areas also have `set_*` methods. `.add_line_edit(text, callback)` is the compatibility callback form.
- **Buttons**: `.add_button(text, callback)`; `.add_button_callback(...)` is its compatibility alias.
- **Data Views**: `.add_table_widget(key, columns, rows, height, callback)` and `.add_tree_widget(key, columns, rows, height, callback)`, with `set_table_widget_rows` and `set_tree_widget_items` for updates.
- **Layout Containers**: `.add_group(key, title)`, `.add_row(key)`, and `.add_column(key)`. Use `.set_widget_visible(key, visible)` to change visibility and `.clear()` to rebuild a container.

#### Events and Timers {#plugin_events_and_timers}

Persistent UI plugins can react to editor events:

```python
import trenchbroom as tb

def on_selection_changed():
    print(len(tb.documents.current().selection.all_entities))

token = tb.register_callback("selection_changed", on_selection_changed)
# tb.unregister_callback(token)  # Stop early when needed.
```

The emitted event names are `selection_changed`, `document_loaded`, and `document_saved`; callbacks receive no arguments. `set_timeout`, `set_interval`, and `clear_interval` are also available inside persistent UI plugin sessions. Timers raise `RuntimeError` when called from the console or **Run Python Script...**. Plugin unload automatically removes its panels, callbacks, and timers.

#### Plugin Example {#plugin_example}

Below is a complete, runnable UI plugin script that duplicates the current selection and translates it along an offset vector:

```python
import trenchbroom as tb

panel = None

def on_generate():
    doc = tb.documents.current()
    if not doc.selection.brushes and not doc.selection.entities:
        panel.set_label_text("status", "Please select at least one brush or entity.")
        return

    count = panel.get_int_field("count")
    dx = panel.get_float_field("dx")
    dy = panel.get_float_field("dy")
    dz = panel.get_float_field("dz")

    with doc.transaction(f"Linear Array ({count} copies)"):
        for _ in range(count):
            doc.selection.duplicate()
            doc.selection.translate(dx, dy, dz)

    panel.set_label_text("status", f"Successfully created {count} copies.")

def init_plugin():
    global panel
    panel = tb.create_plugin_panel("Array Generator")
    panel.add_label("Duplicate the active selection along an offset vector:")

    group = panel.add_group("config", "Parameters")
    group.add_int_field("count", "Copies", value=3, min=1, max=50)
    group.add_float_field("dx", "Step X", value=64.0, min=-2048.0, max=2048.0, step=8.0)
    group.add_float_field("dy", "Step Y", value=0.0, min=-2048.0, max=2048.0, step=8.0)
    group.add_float_field("dz", "Step Z", value=0.0, min=-2048.0, max=2048.0, step=8.0)

    panel.add_button("Generate Array", on_generate)
    panel.add_label_named("status", "Ready")

init_plugin()
```
