# TrenchBroom MCP Python 工作流

使用少量 MCP 入口发现编辑器状态和 Python API，通过一段脚本完成组合编辑。
开发依据为 [治理规范](mcp-development-governance.md) 和
[执行层交付规范](mcp-python-migration/development.md)。

## 状态与目标

当前执行层只公开四入口。使用前读取实际 tools/list 和 tb_api；不为使用旧 workflow
添加代理或兼容层。

| 入口 | 用途 |
| --- | --- |
| tb_inspect | 读取状态、目标文档身份、选择和基本地图事实。 |
| tb_api | 搜索领域并精确查询实际 Python 符号。 |
| tb_execute_python | 执行内联代码或绝对路径脚本，传 arguments，读结果与日志。 |
| tb_capture | 获取当前视口截图和输出路径。 |

最终 Edit 四项、ReadOnly 三项、Off 零项。详细问题检查、undo/redo、保存和其他
编辑操作均通过 Python API。ReadOnly 仅查询原生事实和截图，不执行 Python。

## 编辑闭环

1. 用 tb_inspect 确认实例、目标 fingerprint/path 与选择。
2. 用 tb_api 核对当前需要的对象查询、编辑和批量 API。
3. 用 import trenchbroom as tb 编写脚本，普通循环/函数组合编辑；arguments 输入，
   result 返回紧凑的 counts、bounds 和必要对象摘要。
4. transaction 模式提交一个原生父事务；保存、打开/关闭、原生 history.undo/redo
   使用 action 模式。
5. 用 tb.validation.check 比较原生问题，按需截图，在任务授权范围内保存或导出。

TrenchBroom 是真实地图状态来源。通过有效对象句柄或重新查询定位对象，必要时使用
当前用户选择。复杂结构直接由 Python 组合，不要求 IR、moduleId 或 JSON selector。
跨次执行使用当前 API 支持的恢复方式，不复用失效句柄。

尺寸、网格、材质和实体定义依据当前游戏配置确定。材质名称不证明资源加载成功；
截图可读不证明几何、BSP 或游戏碰撞正确。只报告实际检查过的事实。

## 失败处理

- 目标不匹配或句柄失效：刷新文档/对象，再检查输入。
- 事务异常或非法结果：核对 rolledBack 和地图事实。
- action 失败：读取已完成动作与部分修改，按实际结果恢复。
- 超时或断连：先检查已有执行回执和地图状态，再决定是否重试。
- API 缺失：用已有通用能力组合；确需原生能力时报告具体缺口。

Python 是受信任代码。地图回滚不能撤销脚本的文件/进程副作用，沿用用户已授权
范围，动作超出范围才补充确认。不要编辑 live .map 文件或绕过原生命令。

## 接口切换

旧工具、profile、IR/模块协议、MCP 操作历史和别名整体退役，旧脚本可能需要修改。
使用 tb_api 的实际结果，不按 capability-map 的 planned 符号猜测调用。仓库内 Skill、
示例和调用脚本随切换更新。
