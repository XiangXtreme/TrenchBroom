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

在迁移基础阶段，`mode` 仅支持 `transaction`。保存、关闭、导出、编译、打开和其他
非原子 UI 动作将在 action API 完成后开放。文件脚本必须是绝对路径，运行时会设置真实
`__file__` 并临时将父目录放入 `sys.path`。

使用 `tb_api` 搜索已绑定的 `trenchbroom` 符号。模糊查询最多返回八项摘要；传入
精确 `symbol`（如 `Document.path`）才展开对应符号。现有 public Python API 的目录
由 `PythonApiCatalog` 维护，并有测试确保它和模块绑定同步。

旧 MCP 工具仍处于过渡目录。它们不构成推荐的新组合接口，也不得由新的 Python API
通过 JSON 反调。完整替代关系与阶段状态见 `capability-map.json`；该文件在 G0 的
逐能力审计完成前只表达计划，不表达能力对等。
