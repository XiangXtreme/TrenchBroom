# TrenchBroom MCP Python 执行层交付规范

## 目标

交付一个薄 MCP 桥接层，让 Agent 通过 Python 调用 trenchbroom 原生编辑 API。
复杂编辑用脚本中的循环、函数和对象集合表达。旧 MCP 工具集及其专用兼容体系整体退役。

[MCP 治理规范](../mcp-development-governance.md) 与本文是当前实施依据。旧路线图、
G0–G5 门禁及 capability-map 中的替代符号列表仅作历史资料。完成标准是新架构可用，
不承担旧工具、旧脚本、旧 profile、旧 IR 或迁移期间新增封装的兼容承诺。

## 参考实现与已有基础

本次参考用户指定的 Blender addon.py：
`C:/Users/Trh/AppData/Roaming/Blender Foundation/Blender/5.2/scripts/addons/addon.py`。
文件自报 addon 1.6、protocol 5；这是对本地版本的审阅，不代表全部 Blender MCP 版本。

| 实现位置 | 已确认的行为 | 对 TrenchBroom 的启示 |
| --- | --- | --- |
| _handle_client，684 行 | 接收 JSON，将 command/client 放入队列 | 传输只负责收发和调度 |
| _drain_command_queue，654 行 | 主线程 timer 取请求、执行、返回 JSON | 编辑器对象只在主线程访问 |
| _execute_command_internal，748 行 | 少量通用命令和可选资产接入分发 | 通用编辑交给代码执行入口 |
| execute_code，1367 行 | 新建含 bpy 的 namespace，捕获 stdout，exec，返回输出或异常 | 编辑能力来自原生 Python API |
| get_viewport_screenshot，1274 行 | 读取视口并生成图像 | 截图保留为独立能力 |

该文件还包含资产平台、遥测和场景快照等代码，不将其整体体量或命令数量作为目标。
其 execute_code 没有实现整段自动事务回滚；TrenchBroom 复用已经实现的原生事务，
而不是照搬这处行为。这里借鉴分层方式，不复制第三方实现。

迁移基线保持 d98f869503f331809cd6727712438534c16c8c9e。源码核查点 f713d9f14 已有
Python 执行器、对象绑定、原生事务、日志/结果处理、截图和检查能力。原生 Python
history.status/undo/redo、validation.check 也已绑定，可直接用于组合工作流。

从首个迁移提交 e0bfbe750（2026-09-08 00:30:50 +08:00）到 f713d9f14
（10:58:07 +08:00），91 个提交跨越 10 小时 27 分 17 秒。相对基线 lib 下新增
10,590 行、删除 1,828 行，包含测试。这是审计记录，不是后续进度考核指标。

## 四个公开入口

| 工具 | 职责 | 权限 |
| --- | --- | --- |
| tb_inspect | 有界状态、文档身份、选择和地图基本事实；必要的原生问题摘要 | ReadOnly |
| tb_api | 搜索并说明实际绑定的 Python 符号 | ReadOnly |
| tb_execute_python | 内联代码或绝对路径脚本，参数、结果、日志和执行状态 | Edit |
| tb_capture | 当前视口截图及输出路径 | ReadOnly |

最终 Edit 为四项、ReadOnly 为三项、Off 为零项。所有发现、精确查找与实际分发使用
同一注册集合。独立 tb_history/tb_validate 入口与其余旧工具一同移除；undo/redo 和
详细验证通过 Python 调用原生 API。ReadOnly 的必要问题摘要由 tb_inspect 直接读取，
不运行 Python，也不重建完整验证工具家族。

移除 Core/Modeling/Full 工具 profile、旧别名、schema 和专用分发。不实现旧目录
到新入口的代理，也不把旧名称编码为 action 参数。旧配置启动采用新默认值 Off，
可忽略退役字段；不维护 profile 的解析、映射和兼容测试矩阵。

## 原生 API 与依赖边界

目标路径是 MCP -> Python runtime -> trenchbroom API -> 原生命令。
tb_inspect、tb_api 和 tb_capture 直接使用它们所需的只读原生能力。

复用现有文档、选择、对象、brush、实体、材质/UV、CSG 和原生 undo/redo。仅当
核心场景无法用现有 API 表达时补一个通用绑定。已实现的功能也必须按实际依赖判断，
不能因曾经迁移过就要求保留。可以合并或删除迁移期间新增的重复 namespace 和包装。

automation 服务只保留核心路径真实使用的共享状态或逻辑。无需先把旧 handler 全部
抽成服务才能删除。Python 不反调旧 MCP JSON handler。原生几何/命令算法由已有
owner 提供，避免为两个适配层保留两套实现。

原生 UI、地图文件和用户资产不属于本次退役对象。控制台/插件可以继续使用同一个
Python runtime；其旧 Python 名称和封装不构成新 MCP 的兼容约束。必要的 API
调整同步修改仓库内调用方、示例和测试；第三方脚本的破坏性变化列入说明，不加兼容层。
只移除确认无保留调用方的原生实现，不删除用户安装的插件或修改用户地图。

## 直接退役的旧 MCP 设施

旧 MCP 专用 IR/blockout 执行、preview cache、module metadata/revision/hash、
JSON selector DSL、operationId 历史映射、审计子操作、隔离 Review/contact-sheet
编排、旧对象 ID 别名及协议转发包装默认删除。与它们一同迁入 automation/Python 的
专用状态和包装也在清理范围内，不因目录名改变而自动保留。

地图编辑需要的文档身份、有效对象句柄、原生事务和原生 undo/redo 继续保留；若其
实现混有旧模块或别名状态，裁掉该依赖。需要复用某个几何算法时直接保留原生算法，
不连带保留其旧 MCP schema、元数据模型和兼容回执。

现有纯数据 recipe 可作为离线脚本保留或归档，但不要求新架构执行旧 IR。
新的可运行示例直接使用 Python API。移除 tb.ir/tb.modules 等迁移专用绑定时，
同步清理目录、测试和第一方调用；不能为保留旧 recipe 新建 IR 到 Python 的解释器。

capability-map.json 不再是门禁或逐项开发清单，可以归档或删除。只需按实际删除的
子系统记录破坏性变化，不要求先重写 140 项替代关系才能开始删代码。
scripts/mcp-migration-gates.ps1 改为新入口与核心场景检查，或并入现有测试后删除；
不保留“旧目录必须存在”的检查。

## 最小执行契约

复用现有 Qt 主线程执行器，每次新 globals，以 `import trenchbroom as tb` 调用 API。
默认整段地图事务；文档保存/打开/关闭和原生 undo/redo 使用明确的 action 模式。
transaction 绑定目标 fingerprint，已保存文档同时检查 path；执行中不跟随活动窗口
改变目标。所有句柄访问检查删除、重载、跨文档和 undo/redo 后的有效性。

异常、协作超时、非法/超限结果和原生提交失败取消事务并恢复本次选择。
action 失败如实报告已完成的动作和部分修改。保留请求身份、执行结果、错误、日志、
地图是否修改/回滚等必要诊断；不要求生成旧 operationId、历史账本或模块审计资源。
现有 executionId 去重可保留为有界请求缓存，与旧操作历史解耦。

保留已有 source 256 KiB、request 4 MiB、JSON result 1 MiB、stdout/stderr 1 MiB
和默认 30 秒/最大 90 秒协作预算。摘要最多 16 KiB；大型结果按需使用有界资源。
这些保护复用当前实现，不为此次收敛另建预算框架或持久任务系统。

ReadOnly 不运行 Python，MCP 默认 Off。执行的是受信任代码，地图回滚不能撤销外部
文件或进程副作用；超时/断连后先检查已有回执和地图事实，不能盲目重跑。
不引入后台编辑进程、持久 Python 会话、线程强杀或 processEvents 调度。
原生调用阻塞时不承诺协作超时能强制中断。

## 两个交付批次

| 批次 | 工作 | 结束条件 |
| --- | --- | --- |
| A：新执行层切换与清理 | 注册四入口；删除旧目录和专用依赖；复用/补齐核心 Python 原语；更新调用方、Skill、文档和测试 | 新入口权限正确；旧名称不可调用；核心编辑、回滚、原生撤销与截图可运行；Release 可构建 |
| B：最终验收 | 在最终源码上执行核心场景和受影响回归，修复阻塞，记录破坏性变化和实际支持范围 | 四入口与真实 Release 证据齐全；无未解决 P0/P1；完成提交 |

批次 A 应作为一个完整架构改动推进，允许跨目录大幅删除和调整依赖。无需先交付
保留新旧两个目录的过渡版本。源码、测试、CMake 调整和 API 文档一起完成；
普通实现选择与已授权的旧接口删除无需再次确认。必要时按可构建的边界拆分提交，
不把每个 helper、API 符号或状态登记单独作为交付。

进度只报告新架构已跑通的闭环、残留旧子系统、当前真实阻塞和下一步。
不为已退役工具补语义对照测试，不以 capability-map 完成率驱动实现。

## 验收与交付

场景见 [scenarios.md](scenarios.md)。先构建相关 TbMcpLibTest/TbUiLibTest 再运行
CTest；MCP 源码改动构建 Release TrenchBroom，同一 Ninja 树串行构建。保护现有
控制台/插件运行机制的受影响回归，更新或删除仅断言退役 API/工具的测试。

最终使用独立 Release 进程、隔离配置和一次性地图验收。工具集合与分发集合精确匹配，
参数化检查旧名称拒绝；默认发现载荷不超过 16 KiB 且不超过基线 Modeling 的 20%。
只对新架构承诺的行为验收；不为消除旧测试失败复活兼容层。

最终运行一次 ci-preflight.ps1 -Full -BaseRef d98f869503f331809cd6727712438534c16c8c9e。
Skill/recipe、手册或 UI 的修改分别运行其适用 validator/同步、GenerateManual/双语、
样式治理/快照检查。旧脚本若依赖退役接口，先适配或删除对应检查。保留能验证原生
编辑正确性的证据，不将旧自动化体系的全功能场景作为发布前提。

纯文档修订做静态检查。源码批次通过必要验证后继续交付；仅新变化、失败或未解决
风险触发重跑。日志/地图/截图写到 build-release-codex/codex-logs，记录最终 tested
commit、输入、回执、地图事实与支持范围；它们不进 Git。只提交任务改动，不 push。
