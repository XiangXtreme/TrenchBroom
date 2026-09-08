# TrenchBroom Python 插件开发完全指南

TrenchBroom 引入了基于 Python API 架构的嵌入式脚本与插件系统（模块命名为 `trenchbroom`）。通过 `trenchbroom`，开发者可以编写自动化关卡构建脚本、批量资产处理工具，或在编辑器的 **Plugins** 检查器面板中注册完整的可视化交互界面。

---

## 目录
1. [插件体系与架构概述](#1-插件体系与架构概述)
2. [插件目录与安装规范](#2-插件目录与安装规范)
3. [插件清单规范 (trenchbroom-plugin.json)](#3-插件清单规范-trenchbroom-pluginjson)
4. [trenchbroom 核心 API 与对象模型](#4-trenchbroom-核心-api-与对象模型)
   - [文档与事务安全 (Document & Transaction)](#文档与事务安全-document--transaction)
   - [选区管理 (Selection)](#选区管理-selection)
   - [实体操作 (Entity)](#实体操作-entity)
   - [Brush 与面 (Brush & Face)](#brush-与面-brush--face)
   - [数学与几何 (Vec3, Plane)](#数学与几何-vec3-plane)
5. [UI 插件开发 (PluginPanel 声明式控件库)](#5-ui-插件开发-pluginpanel-声明式控件库)
6. [事件系统与定时器](#6-事件系统与定时器)
7. [动作执行与快捷键集成](#7-动作执行与快捷键集成)
8. [完整实战项目示例](#8-完整实战项目示例)
   - [实战示例 1：计算选中 Brush 的建议光源位置 (Script Plugin)](#实战示例-1计算选中-brush-的建议光源位置-script-plugin)
   - [实战示例 2：全功能几何阵列复制面板插件 (UI Plugin)](#实战示例-2全功能几何阵列复制面板插件-ui-plugin)

---

## 1. 插件体系与架构概述

TrenchBroom 的 Python 插件系统分为两种形态：

1. **脚本插件 (Script Plugin)**：
   - 一次性执行的自动化脚本，适用于批量实体属性修改、UV 坐标计算、特定几何生成或自动化检查。
   - 可在 **Python 控制台 (Python Console)** 中直接交互执行，或通过菜单 **Run Python Script** 载入运行。
2. **UI 插件 (UI Plugin)**：
   - 常驻界面的可视化插件，拥有独立的生命周期。
   - 必须通过 `trenchbroom-plugin.json` 清单定义。在编辑器启动或重新载入插件时激活，并在右侧 **Plugins** 检查器页面中渲染专属的交互控件面板。

> [!NOTE]
> **关于命名空间**：公共模块名为 `trenchbroom`。脚本和插件统一使用 `import trenchbroom as tb`。

---

## 2. 插件目录与安装规范

TrenchBroom 支持在首选项中配置自定义插件目录，插件管理器会自动扫描这些目录下的插件子文件夹。

### 推荐目录结构
每个 UI 插件应存放在独立的文件夹中，例如：

```text
my_plugins_folder/
└── array_generator_plugin/
    ├── trenchbroom-plugin.json    # 插件元数据与配置清单
    ├── main.py                    # 插件入口主脚本
    └── utils.py                   # 辅助业务逻辑（可选）
```

### 首选项配置
打开 **Preferences > Misc > Tools**：
- 点击 **Python Plugin Manager...** 打开插件管理器。
- 在管理器的 **Plugin Directories** 区域点击 **Install UI Plugin...**，选择单个 UI 插件目录或包含多个插件子目录的根目录。
- 使用 **Refresh** 重新扫描，并在 **Detected Plugins** 中查看加载状态、元数据及清单错误。

---

## 3. 插件清单规范 (trenchbroom-plugin.json)

每个常驻 UI 插件的根目录下必须包含一个名为 `trenchbroom-plugin.json` 的 JSON 清单文件。

### 清单字段定义

| 字段名 | 类型 | 必填 | 默认值 | 描述 |
| :--- | :--- | :---: | :---: | :--- |
| `id` | string | 是 | - | 插件全局唯一标识符（建议使用反向域名格式，如 `com.author.toolname`） |
| `name` | string | 是 | - | 在 UI 和插件管理器中展示的可读名称 |
| `version` | string | 是 | - | 语义化版本号，如 `"1.0.0"` |
| `apiVersion` | integer | 是 | `2` | 插件 API 版本，当前必须固定为 `2` |
| `pluginType` | string | 否 | `"script"` | 插件类型：`"ui"`（常驻 UI 插件）或 `"script"`（按需运行脚本） |
| `entry` | string | 是 | - | 插件入口 Python 脚本文件名（相对于清单文件路径，如 `"main.py"`） |
| `description` | string | 否 | `""` | 插件功能简短介绍 |
| `author` | string | 否 | `""` | 插件作者信息或组织名称 |

### 标准清单示例

```json
{
  "id": "io.trenchbroom.array_tool",
  "name": "Array Generator",
  "version": "1.0.0",
  "apiVersion": 2,
  "pluginType": "ui",
  "entry": "main.py",
  "description": "Generate linear and grid arrays of selected brushes and entities.",
  "author": "Community Developer"
}
```

常驻 UI 插件必须显式设置 `"pluginType": "ui"`。省略该字段或设置为 `"script"` 时，插件管理器只报告这是按需运行脚本，不会自动执行入口文件；请使用 **Run > Run Python Script...** 直接选择对应的 `.py` 文件。

---

## 4. trenchbroom 核心 API 与对象模型

在插件运行环境中，`trenchbroom` 模块已自动内置并嵌入。

```python
import trenchbroom as tb
```

### 文档与事务安全 (Document & Transaction)

所有的地图修改必须在当前文档上下文中进行，并且**强烈建议使用事务封装修改**以确保完整的撤销/重做（Undo/Redo）支持与异常安全回滚。

```python
# 获取当前活动的地图文档
doc = tb.documents.current()

# 安全事务上下文管理器
with doc.transaction("My Custom Operation"):
    # 在事务中执行的所有修改都会打包为一个原子操作
    # 如果发生 Python 异常，事务会自动回滚 (cancel)
    # 正常退出上下文时会自动提交 (commit)
    doc.selection.translate(32.0, 0.0, 0.0)
```

#### Document 常用属性与方法
- `doc.path`: 当前地图路径；新建未保存文档通常为 `"unnamed.map"`，仅内部路径为空时为 `None`。
- `doc.entities`: 获取当前地图中的所有实体列表（`list[trenchbroom.Entity]`）。
- `doc.selection`: 获取当前文档的选区对象（`trenchbroom.Selection`）。
- `doc.materials`: 获取当前地图已加载的所有材质列表（`list[trenchbroom.Material]`）。
- `doc.material_collections`: 获取材质集合列表（`list[trenchbroom.MaterialCollection]`）。
- `doc.save()`: 保存当前地图。
- `doc.reload()`: 重新加载地图文件。
- `doc.select(objects)`: 选中指定的一组对象（实体或 Brush）。
- `doc.clear_selection()`: 清空当前选区。

---

### 选区管理 (Selection)

通过 `doc.selection` 可以查询和变换当前选中的地图元素：

```python
sel = doc.selection

# 查询选中项
first_entity     = sel.entity             # 首个相关实体（含 Brush 或选中面的父实体）
first_brush      = sel.brush              # 首个选中的 Brush
selected_entities = sel.entities          # 直接选中的实体
all_entities     = sel.all_entities       # 直接实体与选中 Brush/面的父实体
selected_brushes  = sel.brushes           # 选中的 Brush 列表
selected_faces    = sel.brush_faces       # 单独选中的 Brush 面列表

# 读取首个相关实体；写入会作用于全部选中实体
if "targetname" in sel:
    print(sel["targetname"])
sel["targetname"] = "box_01"

# 几何变换
sel.translate(dx, dy, dz)                 # 平移
sel.rotate(ax, ay, az, angle_deg, cx, cy, cz) # 绕指定轴和中心点旋转
sel.scale(sx, sy, sz, cx, cy, cz)         # 缩放
sel.duplicate()                           # 原地复制选中对象并转移选区到副本

# 倒角
sel.chamfer_vertices(distance=8.0)        # 顶点倒角
sel.chamfer_edges(distance=8.0, segments=2) # 边倒角

# 属性批量设置
sel.set_property("targetname", "box_01", create_if_missing=True)
```

空选区时，`sel.entity`、`sel.properties` 和 `sel.classname` 为 `None`，`sel.all_entities` 为空列表。只有面被选中时，相关实体是该面所属 Brush 的父实体；`sel.brush` 仍为 `None`。使用 `create_if_missing=False` 时，只更新已经包含该属性键的相关实体，没有匹配实体则返回 `False`。

### 对象句柄生命周期

`Document`、`Entity`、`Brush` 和 `Face` 是指向当前编辑器状态的句柄。文档关闭或重新加载、节点删除后，相关句柄会失效；Brush 几何发生变化后，此前取得的 `Face` 句柄也会失效。访问失效句柄会抛出 `RuntimeError`。不要在长期回调中永久缓存这些对象；应从 `trenchbroom.documents.current()`、当前选区或父对象重新获取。

---

### 实体操作 (Entity)

实体对象（`trenchbroom.Entity`）代表地图中的点实体（如 `light`、`info_player_start`）或 Brush 实体（如 `func_door`）：

```python
entity = doc.entities[0]

# 访问类名
print(entity.classname)

# 读写属性字典
all_keys = entity.keys()                  # 所有属性名列表
target = entity.get("target", default="") # 读取属性（支持默认值）
entity.set("light", "300")                # 设置或新增属性
entity.set("color", "1 0.5 0.2")
entity.remove("wait")                     # 删除属性

# 获取属于该实体的 Brush 列表（点实体返回空列表）
brushes = entity.brushes
```

---

### Brush 与面 (Brush & Face)

Brush 是 TrenchBroom 的核心几何体，由多个多边形面（`trenchbroom.Face`）围成：

```python
for brush in doc.selection.brushes:
    for face in brush.faces():
        # 读取面的属性
        print("Material:", face.material)
        print("Offset:", face.offset)       # (u_offset, v_offset)
        print("Scale:", face.scale)         # (u_scale, v_scale)
        print("Rotation:", face.rotation)   # 旋转角度
        print("Vertices:", face.vertices)   # 顶点坐标列表 list[tb.Vec3]

        # 修改面材质
        face.set_material("textures/metal/wall01")

        # 修改 UV 变换
        face.offset = (0.0, 16.0)
        face.scale = (1.0, 1.0)
        face.rotation = 90.0
```

---

### 数学与几何 (Vec3, Plane)

`trenchbroom` 提供了内置的轻量级 3D 数学向量与平面结构：

```python
# 创建三维向量
p1 = tb.Vec3(0.0, 0.0, 0.0)
p2 = tb.Vec3(64.0, 0.0, 0.0)
p3 = tb.Vec3(0.0, 64.0, 0.0)

# 向量运算
diff = p2 - p1
dist = diff.length()
normal = diff.normalize()
cross_prod = p2.cross(p3)
dot_prod = p2.dot(p3)

# 平面结构
plane = tb.Plane.from_points(p1, p2, p3)
print(plane.normal, plane.dist)
distance_to_point = plane.distance(tb.Vec3(10, 10, 50))
projected_point = plane.project(tb.Vec3(10, 10, 50))
```

---

## 5. UI 插件开发 (PluginPanel 声明式控件库)

对于 `pluginType: "ui"` 的插件，可以在主脚本中使用 `trenchbroom.create_plugin_panel` 创建挂载在 **Plugins** 检查器页面的自定义面板。

### 面板创建与生命周期

```python
import trenchbroom as tb

def on_run_clicked():
    doc = tb.documents.current()
    with doc.transaction("Plugin Action"):
        doc.selection.translate(0, 0, 64)

# 创建面板（标题显示在 Plugins 检查器中）
panel = tb.create_plugin_panel("My Custom Tool")

# 添加说明文本
panel.add_label("选中对象并点击下方按钮以快速抬升：")

# 添加输入框与按钮
panel.add_float_field("offset_z", "Z 轴偏移量", value=64.0, min=-512.0, max=512.0)
panel.add_button("执行平移", on_run_clicked)
```

### 完整控件库速查

#### 1. 文本与展示
- `panel.add_label(text)`：添加自动换行的说明文字。
- `panel.add_label_named(key, text)`：添加带命名 ID 的标签。
- `panel.set_label_text(key, text)`：动态更新标签文本。
- `panel.add_html_view(key, html_content, height=200, callback=None)`：添加富文本/HTML 视图（支持点击链接回调）。

#### 2. 表单输入控件
- `panel.add_line_edit(text, callback)`：添加无命名键的即时回调文本框（兼容接口）。
- `panel.add_text_field(key, label, value="", placeholder="")`：单行文本输入框。
- `panel.get_text_field(key) -> str` / `panel.set_text_field(key, value)`：读写文本。
- `panel.add_text_area(key, label, value="", height=120, placeholder="")`：多行文本编辑框。
- `panel.get_text_area(key) -> str` / `panel.set_text_area(key, value)`：读写多行文本。
- `panel.add_int_field(key, label, value=0, min=..., max=...)`：整数微调框。
- `panel.get_int_field(key) -> int`：读取整数值。
- `panel.add_float_field(key, label, value=0.0, min=..., max=..., decimals=2, step=1.0)`：浮点数微调框。
- `panel.get_float_field(key) -> float`：读取浮点数值。
- `panel.add_checkbox(key, text, checked=False)`：复选框。
- `panel.get_checkbox(key) -> bool`：读取复选框勾选状态。
- `panel.add_combo_box(key, label, items, callback=None, current=None)`：下拉选择框。
- `panel.get_combo_box_text(key) -> str`：读取当前选中的下拉项文本。
- `panel.add_color_field(key, label, (r, g, b))`：颜色选择器。
- `panel.get_color_field(key) -> tuple[int, int, int]`：读取 RGB 颜色值。

#### 3. 按钮与操作
- `panel.add_button(text, callback_fn)`：标准操作按钮。
- `panel.add_button_callback(text, callback_fn)`：`add_button` 的兼容别名。

#### 4. 列表与数据表格
- `panel.add_table_widget(key, columns, rows, height=200, callback=None)`：只读数据表格（支持行选中回调）。
- `panel.set_table_widget_rows(key, rows)`：动态刷新表格数据。
- `panel.add_tree_widget(key, columns, rows, height=200, callback=None)`：树形/层次列表。
- `panel.set_tree_widget_items(key, rows)`：动态刷新树形列表。

#### 5. 容器与分组布局
- `group = panel.add_group(key, title)`：创建带标题和边框的折叠分组容器（返回子 `PluginPanel`）。
- `row = panel.add_row(key)`：创建水平排列的行容器（返回子 `PluginPanel`）。
- `col = panel.add_column(key)`：创建垂直排列的列容器（返回子 `PluginPanel`）。
- `panel.set_widget_visible(key, visible)`：动态显示或隐藏指定控件。
- `panel.clear()`：清空面板中所有子控件。

---

## 6. 事件系统与定时器

插件可以注册全局编辑器事件或定时器任务，以响应用户的交互行为：

### 编辑器事件
```python
def on_selection_changed():
    print(f"Selected related entities: {len(tb.documents.current().selection.all_entities)}")

callback_id = tb.register_callback("selection_changed", on_selection_changed)

# 需要提前停止监听时：
tb.unregister_callback(callback_id)
```

当前编辑器发出的事件为 `selection_changed`、`document_loaded` 和 `document_saved`。回调不接收参数，可在回调中通过 `trenchbroom.documents.current()` 获取最新状态。

### 异步定时任务
```python
# 单次延迟执行（毫秒）
timer_id = tb.set_timeout(lambda: print("Executed after 500ms"), 500)

# 周期定时执行
interval_id = tb.set_interval(lambda: print("Tick every 1s"), 1000)

# 取消定时任务
tb.clear_interval(interval_id)
```

定时器要求活动的常驻 UI 插件会话；从 Python 控制台或 **Run Python Script...** 直接调用会抛出 `RuntimeError`。

### 插件卸载与会话清理
当插件被重新加载或文档关闭时，TrenchBroom 会自动清理该插件注册的所有面板控件、定时器与回调函数，无需开发者手动注销。

---

## 7. 动作执行与快捷键集成

插件可以查询或触发 TrenchBroom 内置的编辑命令：

```python
# 列出所有可用的编辑器 Action ID
actions = tb.actions.list()

# 触发指定动作（如取消全选、网格切换等）
tb.actions.execute("Menu/Edit/Deselect All")
```

---

## 8. 完整实战项目示例

### 实战示例 1：计算选中 Brush 的建议光源位置 (Script Plugin)

本脚本演示如何遍历选中项，计算每个选中 Brush 中心正上方 32 单位处的建议光源位置。当前 `trenchbroom` API 尚未公开点实体创建接口，因此脚本输出坐标供后续使用，不会修改地图。

```python
# align_and_light.py
import trenchbroom as tb

def print_light_positions_above_selected():
    doc = tb.documents.current()
    brushes = doc.selection.brushes
    if not brushes:
        print("[警告] 请先在编辑器中选中至少一个 Brush！")
        return

    planned_count = 0
    for brush in brushes:
        # 计算该 Brush 所有顶点的中心包围点
        all_verts = []
        for face in brush.faces():
            all_verts.extend(face.vertices)

        if not all_verts:
            continue

        cx = sum(v.x for v in all_verts) / len(all_verts)
        cy = sum(v.y for v in all_verts) / len(all_verts)
        max_z = max(v.z for v in all_verts)

        # 计算顶点最高面上方 32 单位的位置
        light_pos = f"{cx:.1f} {cy:.1f} {max_z + 32.0:.1f}"

        print(f"建议 light origin: {light_pos}")
        planned_count += 1

    print(f"共计算 {planned_count} 个建议光源位置")

if __name__ == "__main__":
    print_light_positions_above_selected()
```

---

### 实战示例 2：全功能几何阵列复制面板插件 (UI Plugin)

本示例为一个完整的常驻插件工程，包含清单配置、自定义 UI 输入面板、参数校验与 Undo 事务支持。

#### 1. 插件清单 `trenchbroom-plugin.json`
```json
{
  "id": "com.trenchbroom.array_generator",
  "name": "Linear Array Generator",
  "version": "1.0.0",
  "apiVersion": 2,
  "pluginType": "ui",
  "entry": "main.py",
  "description": "Duplicates and translates the current selection in an array.",
  "author": "TB Contributor"
}
```

#### 2. 主脚本 `main.py`
```python
import trenchbroom as tb

panel = None

def generate_array():
    doc = tb.documents.current()
    if not doc.selection.brushes and not doc.selection.entities:
        panel.set_label_text("status", "错误：请先选中要复制的 Brush 或实体！")
        return

    # 读取面板参数
    count = panel.get_int_field("count")
    dx = panel.get_float_field("dx")
    dy = panel.get_float_field("dy")
    dz = panel.get_float_field("dz")

    if count < 1:
        panel.set_label_text("status", "错误：生成数量必须大于 0！")
        return

    # 使用事务封装全部复制与位移操作
    with doc.transaction(f"Generate Array ({count} copies)"):
        for step in range(count):
            # 复制当前选区
            doc.selection.duplicate()
            # 平移新副本
            doc.selection.translate(dx, dy, dz)

    panel.set_label_text("status", f"成功：已生成 {count} 个阵列副本！")

def init_ui():
    global panel
    panel = tb.create_plugin_panel("阵列生成器")

    panel.add_label("将当前选中的对象按指定偏移量批量阵列复制：")

    # 参数配置分组
    group = panel.add_group("config_group", "阵列参数")
    group.add_int_field("count", "副本数量", value=3, min=1, max=100)
    group.add_float_field("dx", "X 轴步长", value=64.0, min=-4096.0, max=4096.0, step=8.0)
    group.add_float_field("dy", "Y 轴步长", value=0.0, min=-4096.0, max=4096.0, step=8.0)
    group.add_float_field("dz", "Z 轴步长", value=0.0, min=-4096.0, max=4096.0, step=8.0)

    # 操作按钮
    panel.add_button("生成阵列副本", generate_array)

    # 状态提示标签
    panel.add_label_named("status", "就绪：等待选区...")

init_ui()
```
