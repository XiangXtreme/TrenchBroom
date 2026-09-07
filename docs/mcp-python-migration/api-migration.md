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
