# TrenchBroom MCP Python 迁移开发规范

## 状态与基线

本文件是 MCP Python 迁移的实施契约。基线为 `main` 的
`d98f869503f331809cd6727712438534c16c8c9e`。实施开始、每个门禁和最终
审核都必须记录实际 HEAD 与工作区状态；基线不一致时必须在交付物中说明，
不得静默替换。

当前分支处于 G3 的增量迁移：G2 执行基础、回执和权限边界已经落地；Python
API 已公开基础 documents、objects、entities、brushes、faces、materials 和 actions
namespace，并已验证 `documents_list` 的替代 `tb.documents.list`。其余 capability-map
条目仍是 `planned`，旧目录仍保留，不能将此阶段报告为能力对等或最终六入口切换。

以下决定不可自行变更：受信任 Python 在现有进程和 Qt 主线程运行；每次执行
使用独立 globals；默认整段地图事务；特殊 UI 或文件动作使用明确 action
模式；ReadOnly 不执行 Python；旧配置升级后为 Off；只有能力对等后才一次性
删除旧入口。持久 Python 会话、后台编辑进程、异步编辑调度和永久兼容目录不在
本交付范围内。

## 最终公开接口

最终 `tools/list` 在 Edit 模式仅包含六项：

| 工具 | 职责 | 权限 |
| --- | --- | --- |
| `tb_inspect` | 有界的状态、文档、选择、对象、资产、视口与动作查询 | ReadOnly |
| `tb_api` | Python API 搜索和精确符号说明 | ReadOnly |
| `tb_execute_python` | 执行受控内联代码或绝对路径脚本 | Edit |
| `tb_history` | 查询历史；撤销、重做和恢复 | 查询 ReadOnly，变更 Edit |
| `tb_validate` | 地图、对象、模块、坡度、路线与壳体接缝检查 | ReadOnly |
| `tb_capture` | 视口截图和隔离 Review | ReadOnly |

Off 不公开可调用工具或资源。服务端必须以实际模式授权，`requestedMode` 只能
降低权限。`tb_history` 必须按 action 再次检查 Edit，不能只依赖目录显示。

过渡期可以同时保留旧目录，但每个旧能力必须记录其 Python 替代符号和验收证据；
最终切换时必须删除旧 schema、注册、分发和隐藏别名。

## Python 执行契约

`tb_execute_python` 接受 `executionId`、二选一的 `code` 或绝对 `path`、
`arguments`、`document`、`mode`、`name` 和 `timeoutMs`。脚本源最大 256 KiB，
请求沿用 4 MiB 传输限制；`arguments` 默认为对象；默认模式为 transaction，
名称为 `MCP Python`，默认协作预算为 30 秒、最大 90 秒。

transaction 必须绑定 `tb_inspect` 返回的 `{fingerprint,path}`。已保存文档需同时
匹配 fingerprint 与 path；未保存文档至少匹配 fingerprint。整个执行不得随活动
窗口变化而改目标。每次创建独立 globals，提供 `arguments`，脚本以
`import trenchbroom as tb` 使用 API。文件脚本设置真实 `__file__` 并临时加入
脚本目录；内联代码使用包含 executionId 的虚拟文件名。脚本只能通过 `result`
返回 JSON 兼容值，未设置为 null。

成功的地图编辑提交一个原生父事务。异常、`SystemExit`、`KeyboardInterrupt`、
协作超时、结果转换失败、输出超限或 native commit 失败都必须取消事务，恢复
本次执行控制的选择，并丢弃暂存元数据。超时一旦触发，即使脚本捕获异常也不得
提交。action 不承诺整段回滚，必须逐步报告已完成动作和部分修改。

执行回执至少包含 executionId、bridgeInstanceId、sourceHash、目标文档、状态、
时长、mutatedDocument、partialMutation、rolledBack、retrySafe、operationId、
result、日志摘要、截断标志和资源 URI。只有执行前拒绝才可报告 `retrySafe:true`。
可信代码一旦运行，即使地图回滚，也可能已有外部副作用。

普通工具结构化摘要和兼容文本合计不得超过 16 KiB；完整 JSON 结果上限 1 MiB，
提交前超过即失败。stdout/stderr 捕获合计最多 1 MiB，并记录丢弃量。回执保留
1024 条；资源最多 128 组、128 MiB，只淘汰应用自行登记的缓存文件。

## 架构规则

唯一编辑服务位于 `TbUiLib` automation 领域，由 `AppController` 管理生命周期。
依赖方向必须为 MCP/Python 适配层 -> automation 服务 -> 地图、命令、渲染等
原生模块。automation 不得依赖 Python 或 JSON-RPC。不得通过
`tb.call(old_tool, dict)` 或逐项 JSON 包装旧工具完成迁移。

Python API 必须提供可组合的对象、命名参数和批量操作，并扩展 documents、objects、
entities、brushes、faces、materials、assets、groups、modules、ir、geometry、history、
validation、review、viewport、actions、compile 和 heightmaps 领域。稳定 ID 用于跨次
恢复，句柄必须检查删除、重载、undo/redo 与节点地址复用后的有效性。

所有编辑器对象访问都在主线程、有效上下文和目标文档 guard 下进行。拒绝嵌套 MCP
执行和后台线程编辑；不得依赖 `processEvents()`、杀线程或强制终止解释器。瞬时
MCP 脚本不得留下 callback、timer 或面板。

## 门禁

| 门禁 | 必须证据 | 进入条件 |
| --- | --- | --- |
| G0 | capability-map、旧目录初始化载荷、环境与失败基线 | 142 项登记，140 项有效能力有归属 |
| G1 | 共用 automation 服务、旧入口回归 | 原子修改、IR、历史恢复通过，无 Python -> MCP 依赖 |
| G2 | 执行器、API 检索、配置升级、回执与预算 | guard、回滚、超时、结果错误、权限与资源测试通过 |
| G3 | 按查询/几何/模块/验证/动作顺序的 API、元数据与对照测试 | 140 项都有替代符号与验证 |
| G4 | 删除旧 schema、注册与分发；Skill 和场景迁移 | Edit 6 项，ReadOnly 5 项，旧名称失败 |
| G5 | 构建、真实 Release 场景与审核包 | 无 P0/P1，状态 `ready_for_review` |

门禁失败必须留下原始日志、影响和备选方案，不能降低阈值、添加跳过开关或自行标记
通过。每个通过阶段只暂存本阶段文件并提交，不 push、不重写历史。

## 必需验证与审核包

T1 覆盖六入口集合、模式授权、requestedMode 降权、HTTP/stdio 语义、资源分页和
配置的首次安装、旧 Off/ReadOnly/Edit、失败迁移与二次启动。默认发现载荷必须实测
不超过 16 KiB 且不超过旧 Modeling 载荷的 20%。

T2 覆盖成功、纯查询、语法和运行错误、嵌套事务、取消、SystemExit、
KeyboardInterrupt、捕获超时、非法结果、日志/结果上限、executionId 冲突、断连和
文档 guard；在 native commit 前后注入失败，并核对地图、dirty、选择、undo/redo、
身份、模块与元数据。

T3 保留 PythonApi、PythonPluginManifest、控制台、插件面板和 timer 回归，对每项
迁移能力建立新旧语义对照。T4 以独立 Release 进程和隔离配置运行批量属性、CSG、
UV、GoldSrc 资产、heightmap、path sweep、IR 替换、两类 Review、undo/redo、保存和
编译场景。截图不能替代几何验收。

G5 必须串行构建 `TbMcpLibTest`、`TbUiLibTest` 并运行对应 CTest，构建 Release
`TrenchBroom`，执行 `ci-preflight.ps1 -Full -BaseRef <G0 commit>`、Recipe validator、
Skill 同步、GenerateManual、双语验证、样式治理和 preferences-misc 主题矩阵。

审核包位于 `build-release-codex/codex-logs/mcp-python-migration/<commit>/`，包含
`delivery.json`、`REVIEW.md`、机器可读门禁结果、完整命令日志、载荷对比、场景回执、
manifest 和截图。`delivery.json` 必须记录 base/head/tested commit、工作区、OS、
编译器、Qt、Python、门禁证据、能力覆盖、上下文指标、已知问题。源码和可复现测试
进入 Git；日志、地图、截图与临时资源不进入 Git。
