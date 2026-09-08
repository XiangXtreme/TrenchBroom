# MCP Python 执行层验收

按 [交付规范](development.md) 验证新架构。场景使用实际绑定的 trenchbroom API，
验证通用 Python 组合、原生编辑正确性和桥接可靠性。

| 场景 | 必需事实 |
| --- | --- |
| C1 入口与权限 | Edit 仅 tb_inspect/tb_api/tb_execute_python/tb_capture；ReadOnly 三项，Off 零项；发现、查找、分发集合相同；旧名称调用失败；requestedMode 只能降权；没有 profile 或隐藏别名恢复旧目录；实测发现载荷。 |
| C2 文档与对象 | 打开一次性地图，绑定 fingerprint/path，读取并选择对象；跨文档和失效句柄拒绝；action 模式保存/另存/关闭；未保存修改不能被静默丢弃。 |
| C3 Python 组合 | 一段脚本用循环/函数创建重复盒体、凸棱柱和实体，修改属性、材质和位置；检查实际 counts/bounds/属性；无需 IR、模块或专用生成工具。 |
| C4 编辑与原生历史 | 查询、变换、删除、face UV 和一次选择 CSG；通过 tb.history.undo/redo 验证原生父事务；人工编辑混入后不错误撤销其他内容；不依赖 MCP operationId。 |
| C5 失败与恢复 | 覆盖异常、SystemExit/KeyboardInterrupt、协作超时、非法/超限结果、提交失败、请求重放/冲突和断连；检查地图、dirty、选择、句柄与必要回执。原生事务回滚，无旧模块或历史元数据同步要求。 |
| C6 检查与截图 | Python validation.check 返回原生问题；ReadOnly 的基本问题摘要无需 Python；截图取得可读图像与输出路径；区分地图验证、保存、截图和未运行的 BSP/碰撞检查；没有新增 crash log。 |
| C7 存续调用方 | 保留的控制台/插件运行机制和原生 UI 正常；仓库内脚本改用当前 API；测试实际保留的能力，不为旧 Python 名称或退役工具恢复别名。 |

C5 的故障注入和完整错误矩阵主要通过自动化测试；真实 Release 至少复现成功组合、
异常回滚、错误目标拒绝、原生 undo/redo 和截图。使用隔离配置与一次性地图。

记录最终 tested commit、输入脚本、目标身份、回执、前后地图/文件事实、图像路径及
crash log 检查。源码存在、截图非空和旧门禁 passed 均不能代替行为验证。

旧 S-*、T3-* 和 capability-map 只是历史索引，可引用其中仍验证原生行为的有效测试。
IR 替换、模块恢复、专用路线、资产平台、heightmap、编译自动化、隔离 Review 等旧
全功能场景不属于本次发布门禁。对仍由原生 UI 使用的算法保留相应原生测试即可。

四入口与旧名称拒绝可通过注册集合断言和参数化测试完成，不需要重新编写逐工具迁移
对照或 140 项功能验收。最终检查源码依赖，确保被删目录没有经 automation/Python
包装重新进入桥接路径。
