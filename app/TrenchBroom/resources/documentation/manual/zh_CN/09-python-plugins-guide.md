# Python 脚本与插件开发 {#python_scripting_and_plugins}

## Python 控制台 {#python_console}

打开底部信息面板中的 **Python** 标签页，以针对当前活动的 TrenchBroom 会话运行 Python API 脚本与快捷指令。

### 左右分栏工作区 {#console_execution}

控制台采用了现代化的左右分栏工作区布局：

- **左侧窗格（输出日志区）**：展示指令执行日志、`print(...)` 的标准输出、表达式求值结果以及报错调用栈。
- **右侧窗格（脚本编辑器）**：提供完整的多行代码编辑器，便于编写和执行单行表达式、快速片段或复杂的过程化生成脚本。拖拽中央分隔条可自由调整日志与编辑器的宽度比例。

![Python 控制台](images/PythonConsole.png)

### 零样板全局快捷函数与变量 {#console_global_helpers}

Python 控制台会自动将当前活动文档、选区和数学原语注入全局命名空间。领域模块和显式对象编辑请导入 `trenchbroom`：

- **全局对象**：
  - `doc`：当前活动地图文档 `Document` 句柄。
  - `sel`：当前活动选区 `Selection` 句柄。
  - `Vec3`, `Plane`：3D 向量与平面几何数学类。
- **选区操作**：`sel.brushes`、`sel.entities`、`sel.brush_faces`、`sel.translate((x, y, z))`、`sel.rotate((x, y, z), degrees)`、`sel.scale(factors)` 与 `sel.duplicate()`。
- **显式对象操作**：`trenchbroom.objects.translate(targets, offset)`、`rotate`、`scale`、`duplicate` 与 `delete`。它们保留无关选区。

### 快捷键与历史记录 {#console_shortcuts_and_history}

- **执行脚本**：按 #key(Enter)（或 #key(Ctrl)+#key(Enter)）或点击 **Run** 按钮即可立即执行编辑器中的脚本。
- **换行编辑**：按 #key(Shift)+#key(Enter) 可在编辑器中换行，方便编写多行脚本。
- **代码智能补全**：按 #key(Ctrl)+#key(Space) 或 #key(Tab) 可主动呼出补全浮窗；按 #key(Tab) 插入选中的补全项。即使浮窗可见，按 #key(Enter) 也会执行当前脚本。
- **历史记录翻阅**：在编辑器首行按 #key(Up) 或在尾行按 #key(Down) 可快速翻阅历史脚本（最多 100 条）。若当前输入框中有未提交的代码，翻阅历史时会自动暂存为草稿。
- **清除输出**：点击顶部的 **Clear** 按钮可清空所有日志。
- **外观字体设置**：可在 **Preferences > View > Fonts > Python Console** 下自定义控制台的等宽字体系列和字号大小。

### 输出重定向与错误追踪 {#console_output_and_errors}

所有 `print(...)` 打印内容与表达式结果均会实时记录于输出视图中。当发生异常时，会自动以红色高亮显示带有行号的 Traceback 诊断信息。

### 实用控制台操作示例 {#console_examples}

以下示例可以直接粘贴到控制台。访问列表中的对象时，应先使用 `[0]` 取得一个元素，或者使用 `for` 循环逐个处理；例如 `sel.brushes[0].entity` 表示第一个选中 Brush 所属的实体。

#### 常用只读命令 {#example_common_queries}

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

#### 快速变换与选区操作 {#example_quick_transforms}

```python
# Rotate the first selected brush by 45 degrees around the Z axis
first_brush = sel.brushes[0]
trenchbroom.objects.rotate(first_brush, (0, 0, 1), 45)

# Duplicate the active selection and translate it 64 units up
sel.duplicate()
sel.translate((0, 0, 64))
```

#### 检查选区与打印 Brush 详细信息 {#example_inspect_selection}

```python
brushes = sel.brushes
print(f"Selected {len(brushes)} brush(es):")
for i, brush in enumerate(brushes):
    faces = brush.faces()
    print(f"  Brush #{i}: {len(faces)} faces")
    for face in faces:
        print(f"    - Material: {face.material}, Vertices: {len(face.vertices)}")
```

#### 循环生成阶梯阵列 {#example_step_array}

```python
for _ in range(8):
    sel.duplicate()
    sel.translate((64, 0, 16))
```

#### 创建基础几何体 Brush {#example_create_brush}

```python
# Create a 64x64x64 cube brush centered at origin and select it
b = trenchbroom.brushes.create([
    (-32, -32, -32), (32, -32, -32), (32, 32, -32), (-32, 32, -32),
    (-32, -32, 32), (32, -32, 32), (32, 32, 32), (-32, 32, 32)
])
sel.set([b])
```

#### 批量赋予表面材质 {#example_batch_face_materials}

```python
for face in sel.brush_faces:
    face.set_material("common/caulk")
```

#### 批量规范化实体属性 {#example_batch_entity_properties}

```python
with doc.transaction("Batch Align Entities"):
    for ent in doc.entities:
        if ent.classname == "light" and not ent.get("light"):
            ent.set("light", "300")
```

## Python 插件 {#python_plugins}

TrenchBroom 内置了嵌入式 Python API 运行时与扩展系统。打开 **Preferences > Misc**，然后在 **Tools** 分组中点击 **Python Plugin Manager...** 管理基于清单的 UI 插件。在管理器内使用 **Install UI Plugin...** 配置目录。管理器会列出检测到的插件、加载状态、元数据和错误；使用搜索或 **Only show issues** 可以诊断规模较大的插件集，在修改文件后点击刷新即可。

![插件检查器面板](images/PluginInspector.png)

#### 插件类型与生命周期 {#plugin_types_and_lifecycle}

TrenchBroom 区分两种插件类型：

- **UI 插件 (`pluginType: "ui"`)**：声明了 `trenchbroom-plugin.json` 清单的常驻插件。它们在启动或刷新插件管理器时加载，并可在 **Plugins** 检查器标签页中注册自定义面板、全局动作、事件监听器和定时器。
- **脚本插件 (`pluginType: "script"`)**：通过 [Python 控制台](#python_console)、**Run > Run Python Script...** 或自定义动作按需执行的独立脚本。插件管理器会报告脚本清单，但不会执行其入口文件。脚本使用 `import trenchbroom as tb` 访问编辑器。

#### 插件清单格式 {#plugin_manifest_format}

每个 UI 插件目录根目录下必须包含一个 `trenchbroom-plugin.json` 清单文件：

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

清单字段定义如下：

| 字段 | 类型 | 描述 |
| :--- | :--- | :--- |
| `id` | string | 插件全局唯一标识符 |
| `name` | string | 界面上展示的插件名称 |
| `version` | string | 语义化版本号 |
| `apiVersion` | integer | 当前 API 兼容级别必须固定为 `2` |
| `pluginType` | string | `"ui"` 为常驻 UI 插件，`"script"` 为脚本 |
| `entry` | string | Python 入口脚本的相对路径 |
| `description` | string | 插件功能可选描述 |
| `author` | string | 可选作者名称或信息 |

`pluginType` 的默认值是 `"script"`。常驻 UI 插件必须显式设置 `"pluginType": "ui"`，否则插件管理器不会加载它。

#### trenchbroom Python API {#the_trenchbroom_python_api}

所有 Python 脚本和插件均通过内置的 `trenchbroom` 模块（`import trenchbroom as tb`）访问 TrenchBroom。核心组件包括：

- `trenchbroom.documents.current()`：返回代表当前打开地图的活动 `Document` 句柄。
- `doc.transaction(name)`：事务上下文管理器（`with doc.transaction("Action Name"):`），将修改合并为一个撤销/重做步骤，并在发生 Python 异常时自动回滚。
- `doc.selection`：用于查询选中对象（`entity`、`brush`、`entities`、`all_entities`、`brushes`、`brush_faces`）的 `Selection` 句柄；`sel[key]` 从首个相关实体读取，`sel[key] = value` 写入全部相关实体；同时支持 `translate`、`rotate`、`scale`、`duplicate`、`chamfer_vertices` 和 `chamfer_edges` 等几何变换。只有面被选中时，`entity` 与 `all_entities` 会返回该面的父实体；空选区返回 `None` 或空结果。
- `doc.entities`：地图中所有 `Entity` 对象的列表。使用 `.get(key, default)` 和 `.set(key, value)` 访问属性。
- `brush.faces()`：返回构成 Brush 的多边形 `Face` 对象列表，支持访问 `.material`、`.offset`、`.scale`、`.rotation` 和 `.vertices`。
- `trenchbroom.Vec3(x, y, z)` 与 `trenchbroom.Plane(normal, dist)`：三维向量与平面数学基元。

这些对象是实时句柄，而不是数据快照。关闭或重新加载文档、删除节点会使相关句柄失效；改变 Brush 几何也会使此前取得的 `Face` 句柄失效。失效后访问会抛出 `RuntimeError`，因此应在这些变化后重新获取对象。

#### 构建自定义 UI 面板 {#building_custom_ui_panels}

UI 插件使用 `trenchbroom.create_plugin_panel(title)` 在 **Plugins** 检查器标签页中创建交互式面板。返回的 `PluginPanel` 提供声明式控件：

- **标签与文本**：`.add_label(text)`、`.add_label_named(key, text)`、`.set_label_text(key, text)` 以及 `.add_html_view(key, html, height, callback)`。
- **表单输入**：`.add_text_field(key, label, value)`、`.add_text_area(key, label, value)`、`.add_int_field(key, label, value, min, max)`、`.add_float_field(key, label, value, min, max, decimals, step)`、`.add_checkbox(key, text, checked)`、`.add_combo_box(key, label, items, callback, current)` 以及 `.add_color_field(key, label, color)`。命名控件提供对应的 `get_*` 方法，文本字段与文本区域还提供 `set_*` 方法；`.add_line_edit(text, callback)` 是兼容回调形式。
- **按钮**：`.add_button(text, callback)`；`.add_button_callback(...)` 是其兼容别名。
- **数据视图**：`.add_table_widget(key, columns, rows, height, callback)` 以及 `.add_tree_widget(key, columns, rows, height, callback)`，可使用 `set_table_widget_rows` 和 `set_tree_widget_items` 更新内容。
- **布局容器**：`.add_group(key, title)`、`.add_row(key)` 以及 `.add_column(key)`。使用 `.set_widget_visible(key, visible)` 改变可见性，使用 `.clear()` 重建容器。

#### 事件与定时器 {#plugin_events_and_timers}

常驻 UI 插件可以响应编辑器事件：

```python
import trenchbroom as tb

def on_selection_changed():
    print(len(tb.documents.current().selection.all_entities))

token = tb.register_callback("selection_changed", on_selection_changed)
# tb.unregister_callback(token)  # Stop early when needed.
```

当前事件名为 `selection_changed`、`document_loaded` 和 `document_saved`，回调不接收参数。常驻 UI 插件会话中还可使用 `set_timeout`、`set_interval` 和 `clear_interval`。从控制台或 **Run Python Script...** 调用定时器会抛出 `RuntimeError`。插件卸载时会自动清理其面板、回调和定时器。

#### 完整插件示例 {#plugin_example}

以下是一个完整可运行的 UI 插件脚本，用于复制当前选区并沿偏移向量阵列平移：

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
