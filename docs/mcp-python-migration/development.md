# TrenchBroom MCP Python 迁移开发规范

## 目标与生效范围

本次交付将 MCP 收敛为少量入口，通过受信任 Python 执行可组合的
trenchbroom 编辑 API，并删除旧的高度封装工具目录及专用分发。复杂几何、
场景组合和重复编辑由 Python 脚本表达；C++ 保留编辑器原生能力和执行安全边界。

本文件与 [MCP 治理规范](../mcp-development-governance.md) 是当前实施依据。
旧 lightweight、moderate 和 long-term 路线图仅供历史查询。本文的三个交付批次
取代原 G0–G5 顺序；历史阶段名称和 capability-map 的 planned 项不是新增任务授权。

迁移基线保持为 d98f869503f331809cd6727712438534c16c8c9e。本次文档修订的源码
核查点为 f713d9f14，工作区干净。自首个迁移提交 e0bfbe750（2026-09-08
00:30:50 +08:00）至核查点（10:58:07 +08:00），共 91 个提交，跨度为
10 小时 27 分 17 秒。相对基线，lib 下新增 10,590 行、删除 1,828 行，包含测试。
这些数字是一次源码审计记录，不代表持续更新的完成率或运行验收结果。

## 已有基础与下一步

在核查点，Python 执行器、API 检索、事务与回执已经实现；documents、objects、
entities、brushes、faces、materials 等领域已有接口和测试。旧目录仍在，Modeling
默认列表尚未包含六个新入口。capability-map 登记 48 项 implemented、92 项 planned、
2 项 placeholder；verification 仅一项 passed，登记状态与新增实现不完全同步。

下一位实现 Agent 从“批次 A：核心闭环与默认入口”开始，先运行已有核心场景找出
真实缺口，然后成批修改。已完成的执行器和原生能力直接复用；抽取一个 helper、
补一个 namespace 或更新一项迁移状态不构成独立交付目标。

## 最终公开接口

Edit 的 tools/list 仅包含以下六项；ReadOnly 为其中除执行器外的五项：

| 工具 | 职责 | 权限 |
| --- | --- | --- |
| tb_inspect | 有界状态、文档、选择、对象、资产、视口与动作查询 | ReadOnly |
| tb_api | 搜索和说明实际绑定的 Python API | ReadOnly |
| tb_execute_python | 执行内联代码或绝对路径 Python 脚本 | Edit |
| tb_history | 历史查询及原生 undo/redo | 查询 ReadOnly，变更 Edit |
| tb_validate | 地图问题与保留的原生几何检查 | ReadOnly |
| tb_capture | 当前视口截图与保留的原生 Review | ReadOnly |

Off 不公开可调用工具或资源。服务端以配置与 requestedMode 中较低权限授权，
tb_history 的写 action 再次检查 Edit。旧 Core/Modeling/Full 配置可以继续解析，
但最终都归一到按权限过滤的六入口集合，不能通过 Full 或精确名称恢复旧工具。

六入口之外的旧 schema、注册、分发、隐藏别名和专用兼容处理在批次 B 删除。
保留的验证、截图或历史算法可继续由相应入口调用，其内部实现按实际依赖整理。
六入口不接受任意旧工具名进行转发，也不把整份旧目录改成 action 参数表；
只保留各入口职责内的必要操作，组合编辑统一使用 Python。

## 能力归属与范围

对旧目录按能力族作一次归属决策，采用三类结果：

| 归属 | 实施要求 | 示例 |
| --- | --- | --- |
| 原生能力 | 复用已有绑定，或补齐核心场景确实缺少的通用 API | 文档、对象句柄、基础 brush、实体属性、CSG、材质/UV、undo/redo |
| Python 组合 | 用已有 API 的循环、函数、集合和批量操作表达；保留代表性脚本即可 | 连续放置、楼梯/走廊组合、按属性筛选再修改、重复结构 |
| 退役 | 记录用途与处理结果，删除旧入口及无人使用的专用实现 | 重复别名、旧脚本生成桥、专用 blockout 包装和未使用的便利入口 |

能力归属必须覆盖旧目录，验收以核心工作流为单位。一个工作流可以替代多个旧工具，
退役项无需创建同名 Python API。已有 Python 插件、控制台和原生 UI 的公共能力需要
保持兼容；删除旧 MCP 工具不授权删除它们仍在使用的底层实现。

首轮范围包括文档打开/查询/保存、对象查找/选择/变换/删除、盒体和凸棱柱批量创建、
实体创建和属性修改、材质/UV、选择 CSG、原生 undo/redo、地图问题查询和截图。
支持这些任务所需的通用能力优先于旧工具同名接口。

IR、模块预览/替换、路线专用分析、heightmap、path sweep、编译自动化和高级 Review
按现有可用程度作保留或退役决定。它们的完整 Python 迁移不阻塞六入口交付；缺失的
扩展需求另行记录，不在本任务中扩建。仍可调用的路径保留其原有 guard 和回滚约束；
无法满足约束的旧专用路径随入口退役。保留的公开功能必须如实说明支持范围。

capability-map.json 当前是旧目录审计资料。批次 A 一次性更新它的归属和证据结构，
使机器可读状态表达上述三类结果。不要按现有 replacementSymbols 列表逐项开发。
scripts/mcp-migration-gates.ps1 当前只检查目录/文档结构，且要求旧名称仍存在；
其 passed 不是迁移验收。批次 A 将其改为当前批次的检查，批次 B 检查旧注册已删除。

## 架构与执行边界

推荐调用路径为 MCP -> Python runtime -> trenchbroom API -> 原生命令。
查询、验证、截图和历史入口可直接访问相应原生服务。

现有 automation 服务用于真正共享的操作、对象身份和应用级状态。仅有一个调用方、
只转发一次原生命令的行为可以留在其现有 owner；不要求所有函数经过新的服务层。
按真实依赖抽取，禁止 Python 通过 JSON 反调旧 MCP handler。保留一份几何/命令算法，
删除旧适配器时同步移除无人使用的包装，避免再建立一套完整 IR 执行框架。

Python 使用命名参数、对象句柄、集合与必要批量操作。跨次执行可以用稳定 ID 或重新
查询恢复目标；模块元数据和 JSON selector 是可选能力。所有句柄访问必须检查删除、
重载、undo/redo、跨文档和节点地址复用后的有效性。

受信任 Python 在现有进程的 Qt 主线程运行，每次使用独立 globals。默认整段地图
事务；保存、打开/关闭、重载和通用编辑器 action 使用明确 action 模式。ReadOnly
不执行 Python，旧配置升级为 Off。保持既有控制台与插件运行模式。

所有编辑器访问受上下文与目标文档 guard 保护。拒绝嵌套 MCP 执行、后台线程编辑和
瞬时脚本留下 callback、timer 或面板。不使用 processEvents、杀线程或强制终止解释器
解决执行问题。超时是协作预算，不能承诺中断阻塞的原生调用。

## 执行回执与预算

tb_execute_python 接受 executionId、二选一的 code/path、arguments、document、
mode、name 和 timeoutMs。脚本源最大 256 KiB，请求最大 4 MiB；arguments 默认
为对象，mode 默认为 transaction，name 默认为 MCP Python，协作预算默认 30 秒、
最大 90 秒。文件路径必须绝对，设置真实 `__file__` 并临时加入脚本目录。

transaction 绑定 tb_inspect 返回的 fingerprint；已保存地图同时校验 path。
执行期间不能随活动窗口变化改目标。脚本使用 `import trenchbroom as tb`，通过
result 返回 JSON 兼容值，未设置时为 null。

成功编辑提交一个原生父事务。异常、SystemExit、KeyboardInterrupt、协作超时、
非法结果、结果超限或 native commit 失败时取消事务、恢复选择和暂存元数据。
超时被脚本捕获也不能提交。action 逐步记录已完成动作，失败如实报告 partialMutation。

回执保留 executionId、bridgeInstanceId、sourceHash、目标、状态、时长、
mutatedDocument、partialMutation、rolledBack、retrySafe、operationId、result、
日志摘要、截断标志与资源 URI。代码运行后的外部副作用无法由地图事务撤销，只有
执行前拒绝可报告 retrySafe:true。重复 executionId 的回放和冲突检测继续有效。

结构化摘要与兼容文本合计最多 16 KiB；完整 JSON 结果最大 1 MiB，提交前超限失败。
stdout/stderr 合计保留最多 1 MiB，并记录丢弃量。回执保留 1024 条；资源最多
128 组、128 MiB，只清理应用登记的缓存文件。缩小工具集不改变这些执行边界。

## 三个交付批次

每个批次是一个可构建、可验收的结果，允许跨多个能力族和文件实施。通常每批一个
源码与测试一起完成的提交；仅因可回滚性或独立修复需要才拆分。完成一批后继续下一批，
不把普通实现选择交还用户。遇到缺口先用现有 API 组合，仅核心场景阻塞才补原生 API。

| 批次 | 工作 | 结束证据 |
| --- | --- | --- |
| A：核心闭环与默认入口 | 成组补齐核心脚本缺口；默认发现切换六入口；更新能力归属和门禁脚本 | 核心场景、权限/回滚测试通过；默认 Edit 6 / ReadOnly 5 / Off 0；列明仍注册的旧入口 |
| B：旧工具整体下线 | 按归属删除全部旧 schema/注册/分发/隐藏别名；清理专用死代码；迁移客户端指引、脚本与 Skill | 所有 profile 归一；所有旧名称调用失败；保留路径无旧分发依赖；核心场景继续通过 |
| C：最终验收与交付 | 在最终源码上完成 Release、集成场景和必要回归；修复交付阻塞 | 场景原始证据、载荷对比、源码审计和清洁提交；无未解决的 P0/P1 |

批次 A 的 C1 验收检查默认发现和权限，C1 中所有 profile 归一及旧名称拒绝在批次 B
完成。A/B 可以在同一实现阶段连续完成；无需为维持过渡期而新建兼容框架。

批次 A/B 的源码修改均构建 Release TrenchBroom 和相关测试目标。构建同一 Ninja
树时串行执行。一个批次完成必要测试后直接推进，不为只更新进度文字重复全量验证。
只有新改动、失败或未解决风险才扩大或重跑检查。

每次进度报告写明已跑通的核心场景、剩余旧入口数量、尚需验证的安全边界和下一批
具体产物。提交数量、共享 helper 数量和迁移表标记数量不能替代交付证据。

## 验收与交付物

核心场景见 [scenarios.md](scenarios.md)。构建 TbMcpLibTest、TbUiLibTest 后运行
对应 CTest，保留 PythonApi、PythonPluginManifest、控制台、面板和 timer 的受影响
回归。执行器测试覆盖权限、文档 guard、异常/超时/结果错误回滚、预算、回执重放、
断连恢复和对象有效性；保留的状态必须与地图同步撤销。

最终在独立 Release 进程、隔离配置和一次性地图上跑核心场景。默认发现载荷实测
不超过 16 KiB 且不超过基线 Modeling 的 20%。旧名称失败要通过实际调用验证，
不能仅用源码字符串消失代替。静态检查同时确认专用注册、分发和别名已删除。

批次 C 运行一次 `ci-preflight.ps1 -Full -BaseRef d98f869503f331809cd6727712438534c16c8c9e`。
Skill/recipe 修改运行对应 validator 和同步检查；手册修改运行 GenerateManual 和双语
验证；设置 UI 修改运行样式治理及 preferences-misc 矩阵。按实际修改触发这些检查。
纯治理文档修订运行静态检查即可。

证据写入 `build-release-codex/codex-logs/mcp-python-migration/<commit>/`，集中记录
base/head/tested commit、工作区、环境、命令日志、核心场景结果、工具数量/载荷、
保留与退役能力及已知限制。已有有效测试和场景证据可引用；最终证据须对应最终源码。
日志、地图和截图不进 Git；只提交本次源码、测试与文档，不 push 或重写已有历史。
