# TrenchBroom MCP Agent 工作流

推荐路径是查询编辑器状态和 Python API，然后执行一段可组合的 Python 脚本，
用原生验证与截图检查结果。开发边界见 [MCP 治理规范](mcp-development-governance.md)，
当前交付状态与批次见 [Python 迁移规范](mcp-python-migration/development.md)。

## 当前可用性与发现

截至源码核查点 f713d9f14，六个新入口已注册，旧目录也仍保留；默认 Modeling/Core
列表尚未展示六入口，Full 可发现它们。此处描述过渡实现，最终所有 profile 都应返回
Edit 六项、ReadOnly 五项、Off 零项。使用前读取当前 tools/list 和 API 元数据，
不要把计划中的符号当成已经实现。需要使用核查点的 Python 路径时，使用现有 Full
profile 发现新入口；不要为迁移创建另一套 profile 或客户端。

优先通过 loopback HTTP /mcp 连接，确认进程、bridgeInstanceId、模式和目标地图。
MCP 默认 Off，开启后信任当前本机用户下的进程；Python 执行需要 Edit。
客户端 requestedMode 只能降低权限。

## 常用入口

| 工具 | 使用方式 |
| --- | --- |
| tb_inspect | 查询状态、文档、当前选择和有界地图摘要，取得 fingerprint/path。 |
| tb_api | 先搜索领域，再精确查询符号；以实际绑定说明确认参数、返回对象和限制。 |
| tb_execute_python | 传稳定 executionId、code 或绝对 path、arguments、document 和模式。 |
| tb_history | 查询回执/原生历史，执行受权限保护的 undo/redo。 |
| tb_validate | 比较修改前后的原生问题；附加几何检查仅使用当前支持的 action。 |
| tb_capture | 截图或使用当前支持的 Review；返回可打开资源作为视觉证据。 |

请求形状见 [API 迁移说明](mcp-python-migration/api-migration.md)。
一次执行可以完成查询、循环生成、属性修改和结果汇总；无需将每个对象操作拆成一次
MCP 往返。返回 counts、bounds、摘要和少量 ID，详细结果按需读取。

## Python 编辑闭环

1. 用 tb_inspect 确认目标地图身份和选择，记录原生问题基线。
2. 用 tb_api 核对本次需要的对象查询、编辑和批量 API。
3. 将编辑组合成一段 Python；使用 import trenchbroom as tb，通过 arguments 传参，
   通过 result 返回 JSON 兼容摘要。
4. 默认 transaction 模式绑定目标文档，整段成功后产生一个原生父事务。保存、
   打开/关闭、重载和通用 action 使用 action 模式，并明确处理部分完成状态。
5. 检查执行回执和地图事实；验证问题增量，按需要截图。
6. 在任务授权范围内保存或导出，报告实际保存、验证和截图状态。

场景布局可直接由 Python 循环、函数和集合组合原生 API。跨次执行重新查询对象或用
有效稳定 ID 恢复；当前用户选择适合所有权不明确的旧地图。模块元数据和 IR 是可选
路径，只有当前 API 明确支持时才使用。Python 脚本文件也可由执行入口加载。

坐标、网格、材质和实体定义依据当前游戏配置及地图事实确定。不要将 GoldSrc 的尺寸
习惯应用到所有地图。材质名称存在不等于资源加载成功，截图可读不等于几何、BSP 或
游戏碰撞正确。先声明要验证的事实，再选择原生检查。

## 失败与恢复

- fingerprint/path 不匹配：刷新目标身份后重新检查输入，不绕过 guard。
- 事务异常或非法结果：读取 rolledBack、mutatedDocument 和历史，核对实际地图。
- action 失败：读取已完成动作及 partialMutation，按事实恢复。
- 超时或断连：先按 executionId 查询回执/历史，不能盲目重复执行。
- 句柄失效：重新查询目标，不复用已删除或跨文档句柄。
- API 不存在：用已有通用 API 组合，或报告具体缺口；不退回已删除的工具名称。

Python 是受信任代码，地图回滚不能撤销它产生的外部文件和进程副作用。沿用用户已经
授权的任务范围；仅在动作超出授权或需要处理不可丢弃的用户修改时补充确认。

## 迁移与验收

核心场景见 [验收场景](mcp-python-migration/scenarios.md)。旧工具的归属可以是
原生能力、Python 组合或退役；一个脚本闭环可覆盖多个旧工具。开发 Agent 按迁移规范
的三个批次执行。旧 recipe 仍可输出 IR，但仅通过已支持且有完整 guard 的执行路径
应用；其存在不要求补齐所有 IR 操作。

切换时同步项目 Skill、客户端说明和调用旧名称的辅助脚本。capability-map 的历史
planned 项不是推荐工具列表；迁移完成后不能通过隐藏目录、Full 或别名调用旧工具。
