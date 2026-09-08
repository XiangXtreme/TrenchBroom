# MCP Python API 迁移说明

`tb_execute_python` 在 Edit 模式执行受信任 Python。执行请求必须携带稳定的
`executionId` 和 `tb_inspect` 返回的目标文档 fingerprint；保存的地图还应带 path。
脚本在新的 globals 中运行：

```python
import trenchbroom as tb

document = tb.current_document()
# arguments 是 MCP 请求传入的 JSON 对象。
result = {"path": document.path, "argument": arguments.get("name")}
```

`result` 只能是 JSON 的 null、bool、有限 number、string、array 或 string-keyed object。
未设置时回执返回 null。每段 transaction 脚本包在一个原生地图事务中；发生异常、
结果转换错误、超时或提交错误时地图事务回滚。可信 Python 已开始运行后，回执不会
宣称安全重试，因为脚本可能触及地图外的文件或程序。

`mode:"transaction"` 是默认值，整段地图编辑在一个原生父事务中提交。保存、重载和
通用编辑器 action 必须使用 `mode:"action"`；该模式不承诺整体回滚，失败回执会标记
`partialMutation`。瞬时 MCP 脚本不能创建插件面板或持久 callback。文件脚本必须是绝对
路径，运行时会设置真实 `__file__` 并临时将父目录放入 `sys.path`。

使用 `tb_api` 搜索已绑定的 `trenchbroom` 符号。模糊查询最多返回八项摘要；传入
精确 `symbol`（如 `Document.path`）才展开对应符号。现有 public Python API 的目录
由 `PythonApiCatalog` 维护，并有测试确保它和模块绑定同步。

旧 MCP 工具仍处于过渡目录。它们不构成推荐的新组合接口，也不得由新的 Python API
通过 JSON 反调。完整替代关系与阶段状态见 `capability-map.json`；该文件在 G0 的
逐能力审计完成前只表达计划，不表达能力对等。

## 选择几何分析

`tb.geometry.analyze_selection(grid=1.0, detail="summary", max_brushes=100)` 读取当前
选择及其下的 brush，返回闭合/凸性、网格对齐、材质和合并 bounds 的原生几何事实。默认
只返回计数、材质和 bounds；`detail="full"` 才返回 `brushes`，其中每项是可继续用于
`tb.objects`、`tb.faces` 或编辑操作的 `Brush` 句柄。`max_brushes` 限制 full 结果，超过时
返回 `truncated:true`，调用方应缩小选择后再请求完整句柄。

该接口和过渡期的 `geometry_analyze_selection` 共享 automation 服务，服务层不依赖 MCP
JSON-RPC 或 Python 运行时。它只报告编辑器中的 brush 几何；它不验证 BSP、游戏碰撞或
视觉质量。

## 选择 CSG

`tb.geometry.csg_selection(operation)` 对当前选择运行原生 CSG，其中 `operation` 为
`convex_merge`、`subtract`、`intersect` 或 `hollow`。这是选择驱动的原子几何编辑：调用前可用
`tb.objects.set_selection(...)` 设置明确的 `Brush` 集合，或保留用户在编辑器中的当前选择。
操作本身产生一个原生可撤销父操作，成功返回新的选中 `Brush` 句柄、删除数量和事务名称。

接口与过渡期 `geometry_csg_selection` 复用同一 automation 服务；该服务集中执行选择合法性
检查和 CSG 调度，适配层不复制原生几何算法。选择不符合操作要求或原生 CSG 无法产生修改时，
接口在写入前抛出 `ValueError`。

## UV 对齐

`tb.materials.align_face(faces, mode)` 对给定的 `Face` 集合应用原生 UV 轴对齐，返回实际处理的
去重 face 数。`mode` 可为 `reset`、`paraxial`（或 `world`）、`parallel`（或 `face`）。所有 face
必须属于当前文档，空集合、跨文档句柄和未知模式会在事务开始前拒绝。Python 适配层使用既有选择
恢复事务，调用不会改变用户原有选择；过渡 MCP `texture_align_face` 使用相同 automation 服务，但
继续保留其历史记录和协议目标解析。

`tb.materials.copy_from_face(source, targets)` 把一个 `Face` 的材质、UV 与 surface 属性复制到显式
的目标 `Face` 集合，返回去重后的目标数。源和目标必须处于当前文档，空目标会在提交前失败。
该接口与过渡 MCP `texture_copy_from_face` 复用同一个 automation 操作。

## 批量盒体

`tb.brushes.create_box(min, max, material=None, select=True)` 创建一个轴对齐盒体，
`tb.brushes.create_boxes_batch(boxes, material=None, select=True)` 在一个原生事务中创建
多个盒体。每个 `boxes` 项是带 `min`、`max` 和可选 `material` 的字典；项目级
`material` 是没有逐项材质时的默认值。所有盒体先由 automation 服务完成原生构建，
任何一项坐标非有限或任一轴的 `min >= max` 都会在写入前失败，因而不会留下前面已
构建的盒体。

过渡期 MCP 的 `box`、`stepped_mass` 和 `support_posts_between` 批量生成分支复用同一
服务。该服务只构造未附加的原生节点；事务、选择和历史发布仍由调用适配层负责。

`tb.brushes.create_prism(points2d, min_z, max_z, material=None, select=True)` 与
`tb.brushes.create_polygon_batch(polygons, material=None, select=True)` 使用同一凸多边形
棱柱构建器。批量项需包含 `points2d`、`min_z` 和 `max_z`，并可覆盖 `material`。服务会
拒绝非凸、退化、非有限或上下界颠倒的棱柱；所有项在附加到地图前构建完成，因此失败不会
留下部分 batch。过渡期 MCP 的所有 prism 生成路径也使用这个构建器。

## Checked Point Entities

`tb.entities.create_checked_batch(entities, select=False)` 在一个原生事务中创建多个 point
entity，并在写入前根据当前 FGD 验证每个 `classname`。每项包含 `classname`、可选的字符串
`properties` 和可选三数字 `origin`；FGD 默认属性先写入，再由传入属性覆盖，空值属性会
删除，brush entity 定义会拒绝。automation 服务会先构造全部节点，再由事务附加，因此无效
定义或 entity payload 不会改动地图。过渡期 MCP `entity_create_checked_batch` 使用同一节点
构造服务，同时保留协议专属诊断和历史记录。

`tb.entities.entities_list(type="", query="", limit=200)` 返回当前地图 FGD 的紧凑实体
定义摘要；`type` 可为 `point` 或 `brush`。`tb.entities.schema(classname)` 返回一个定义的完整
属性 schema、默认值和 point bounds。`tb.entities.create_from_schema(...)` 与
`tb.entities.create_checked(...)` 使用这个 FGD 定义创建 point entity，应用 FGD 默认属性后再覆盖
传入属性；它们与批量 checked 接口共享构造及事务服务。

`tb.entities.tie_brushes(classname, brushes=None)` 会把显式 `Brush` 集合或当前选中笔刷绑定到
一个 FGD brush entity，并返回新建的 `Entity`。`tb.entities.untie_brushes(objects=None)` 接受
显式 `Brush`／brush `Entity` 集合或当前选中笔刷，返回移回原生父级的 `Brush` 集合。两者通过
automation 服务执行原生命令，保留原生撤销语义；point entity、空集合、跨文档句柄和未知 FGD
定义会在写入前失败。

`tb.entities.link_chain_inspect(start=None, classname="", name_key="targetname", next_key="target",
detail="summary", include_all_nodes=False)` 在实时地图中追踪实体属性
链路。起点可以是显式 `Entity`，或当前选择中唯一匹配的实体；结果的 `nodes` 与
`warnings[].entity` 都是可继续操作的 `Entity` 句柄，并报告重复名称、缺失目标和环路，
不会读取 map 文件或猜测歧义目标。

## 文件 IR 预览

`tb.ir.compile_preview_from_file(path)` 只接受绝对路径，读取受 10 MiB 上限约束的 JSON
IR 文件，并复用与 MCP 相同的 schema 校验。成功时返回已规范化的 `ir`、兼容性
`warnings` 和 `preview`。preview 包含 `python-ir-preview-*` ID、canonical source path、
当前文档 fingerprint 和十分钟的过期信息；预览缓存由 `AppController` 的 automation state
管理，最多保留 64 项。该操作不修改地图，也不执行 IR。

## IR 执行基础闭环

`tb.ir.apply(ir, name="Python API Apply IR")` 与
`tb.ir.apply_from_file(path, name="Python API Apply IR")` 通过 automation 执行服务应用
已校验的 IR，而不反调 MCP handler。当前服务支持 `box`（`min`/`max` 或 `origin`/`size`）、
严格凸 `prism` 和 FGD 已定义的 point entity。它会在构造所有原生节点后才开启单一事务；任一
操作、实体或原生提交失败时，不会留下此次请求的几何或实体。成功结果同时包含规范化 IR、预览、
brush/entity 计数和可继续传给 `tb.objects`、`tb.brushes` 或 `tb.entities` 的句柄。

`replace_module` 及其 preview guard、完整 blockout 操作集合仍在服务迁移中。当前它们会在
写入前明确拒绝，不能以普通 `create` 代替其版本和内容 hash guard。
