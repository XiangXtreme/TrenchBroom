# MCP Python 迁移场景

本文件定义 capability-map 中 `S-*` 场景的最低验收事实。`planned` 表示
G3 尚未实现，不能被实现 Agent 当作通过；场景在对应 API、目录和真实 Release
进程都留下回执、地图和原始日志后才可改为 `passed`。

| 场景 | 覆盖领域 | 最低验收事实 |
| --- | --- | --- |
| S-inspect | 状态与 API 发现 | 以 Off、ReadOnly、Edit 分别查询状态和 API，确认无 Python 求值且结果受 16 KiB 预算约束。 |
| S-documents | 文档生命周期 | 在隔离配置中打开、绑定、保存、导出并关闭一次性地图；错误 fingerprint 或 path 不得改变任何文档。 |
| S-objects | 查询、选择、对象和分组 | 用 selector、bounds 和属性查询定位对象，执行选择、变换、删除和 undo/redo，核对稳定对象身份。 |
| S-viewport | 视口与 overlay | 查询和设置受支持视口状态，验证 overlay 不改变地图 dirty 状态，也不残留到下一次 MCP 执行。 |
| S-review | 截图与 Review | 生成 selection、object、module 与 operation 隔离 Review，检查 manifest、资源 URI、silhouette/all 两种边线模式。 |
| S-actions | 原生动作 | 仅以 action 模式执行有明确文档绑定的 action；失败回执必须列出已完成动作和 `partialMutation`。 |
| S-modules | 模块与 selector | 创建带 moduleId 的内容，精确 preview guard 替换，验证过期 guard 拒绝和父操作撤销。 |
| S-entities | 实体与 FGD | 批量创建、更新、删除、绑定/解绑实体 brush，并验证 FGD schema 和失败输入不会提交。 |
| S-brushes | 原语与批量几何 | 创建 box、polygon、wedge、cylinder 和 planes brush，验证几何事实、对象身份和一次父事务。 |
| S-history | 历史恢复 | 交错插入人工编辑、控制台和 Python 操作，查询对应关系并验证不会越过人工编辑静默恢复。 |
| S-assets | GoldSrc 资产 | 搜索并放置 model、sprite、sound，验证资源识别、实体属性与未找到资源的错误边界。 |
| S-materials | 材质与 UV | 搜索/替换材质并设置 face UV，随后重载材质、undo/redo，确认所有句柄生命周期检查有效。 |
| S-faces | Face 选择与纹理 | 选择 face、读取属性、写入纹理属性和 UV，验证不匹配面与非法 UV 的回滚。 |
| S-validation | 地图问题与泄漏 | 检查并修复安全问题、加载 pointfile，验证问题增量而非仅截图或通过布尔值。 |
| S-compile | 编译 | 在测试环境保存并运行指定 profile，读取日志尾部，记录输出文件和 crash log 清单。 |
| S-ir | IR、blockout 与 recipe | 验证/预览/应用文件 IR，执行模块精确替换；确认 Recipe 仅产出 IR，未直接编辑地图。 |
| S-heightmaps | Heightmap | 预览并导入灰度图，验证 brush 数、bounds、材质和失败路径不会写入地图。 |
| S-geometry | CSG、坡度与路线 | 执行 CSG，检查 slope、route continuity、shell seam 和 spiral stair 的几何事实，不把 Review 作为几何验收。 |

每个场景的最终记录应至少包含输入、目标文档引用、执行回执、前后问题摘要、几何或
文件事实、输出资源和运行前后的 crash log 清单。测试 handler 或可读截图不能替代该记录。
