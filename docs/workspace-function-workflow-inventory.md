# 工作区功能与工作流清单

日期：2026-11-08
状态：整合前代码走读基线。后续实施与验证进度见[整合方案交付记录](workspace-unification-proposal.md#第一二轮交付记录2026-11-08)。

本文覆盖地图编辑器右侧 Inspector、底部工具区、Content Browser 和资源库。
结论来自当前代码与现有项目文档，尚未逐项进行人工操作或行为对等测试。
统一的目标是减少重复的操作路径和实现，同时保留现有功能、地图格式和工作流。

## 当前入口

| 入口 | 主要内容和操作 | 上下文与实现 |
| --- | --- | --- |
| Map | 图层管理、地图边界、Mods | 当前地图；`MapInspector`、`LayerEditor`、`ModEditor` |
| Entity | 实体属性、实体定义浏览和定义文件设置 | 当前地图与选区；`EntityPropertyEditor`、`EntityBrowser` |
| Face | 面属性及 UV、材质浏览、材质集合设置；工程地图另有资源贴图选择 | 当前地图与面选区；`FaceAttribsEditor`、`MaterialBrowser` |
| Outliner | 层级树、搜索排序、图层和节点操作、可选属性面板 | 当前地图与选区；`OutlinerTreeWidget`、`OutlinerEntityPropertyEditor` |
| Terrain | 创建、转换、绘制、雕刻、顶点编辑；材质、编译预算、分析和导出 | 受支持的 Half-Life Valve 220 工程地图；`TerrainInspector` |
| Plugin | 展示和关闭插件提供的面板 | 插件注册的 UI；`PluginInspector` |
| 底部工具区 | Content Browser、输出、Python 控制台、Issues，及 Extensions 跳转；支持 Overlay/Push、保持打开和高度调整 | `WorkspaceToolPanel`；扩展入口跳到右侧插件页 |
| Content Browser | 按来源和类型浏览、搜索、预览、应用及管理工程资源 | 工程使用 `ProjectAssetPanel` + `ModelBrowser` + `AssetInspector`；独立地图通过 `InfoPanel` 的 `ModelInspector` 路径 |
| Asset Library 窗口 | 库浏览、导入、转换、地形材质创建、重命名、删除及预览 | 独立库管理；可从欢迎页、Terrain 和浏览器等入口打开 |

依据：[Inspector](../lib/TbUiLib/src/Inspector.cpp)、[InfoPanel](../lib/TbUiLib/src/InfoPanel.cpp)、[ProjectAssetPanel](../lib/TbUiLib/src/ProjectAssetPanel.cpp)、[WorkspaceToolPanel](../lib/TbUiLib/src/WorkspaceToolPanel.cpp)。

## 重叠与合并候选

| 主题 | 已确认的现状 | 建议与边界 | 优先级 |
| --- | --- | --- | --- |
| 图层 | `LayerEditor` 与 Outliner 图层右键菜单均实现激活、移动选区、全选、显示、锁定、隔离、导出、重命名和删除等操作。两处新建均调用 `createLayer` 并激活新图层；Layer Editor 的按钮提示说从选区创建，但当前实现没有将选区移动进去。 | 先统一命令、启用条件和撤销语义，再比较两个视图是否仍各有价值；不能只删 Map 页。保留默认图层、批量状态、顺序和选区相关行为；修正提示或单独设计从选区创建的操作。 | 高 |
| 实体属性 | Entity 和 Outliner 使用不同属性编辑器。Outliner 另有 Transform、友好名/原始键、添加删除、混合值及部分专用编辑器；Entity 还与实体定义浏览及设置相邻。 | 对照单选、多选、混合类型、worldspawn、智能编辑器和键操作，逐步共享属性编辑能力；入口可继续不同。兼容原有键盘操作、选择与撤销。 | 高 |
| 材质选择 | Face 原生浏览器按材质集合浏览并支持名称/使用量排序、分组和已使用过滤；工程地图另提供资源选择对话框。Content Browser 也能对选中面应用纹理。 | 先统一“选择资源后应用”的目标和反馈，比较原生材质集合、WAD、非 GoldSrc 格式与资源快照的覆盖，再决定界面是否合并。保留 UV、选中材质定位和材质集合设置。 | 高 |
| 资源浏览和管理 | Content Browser 与库窗口均有搜索、分类、预览；`AssetInspector` 已在多个入口复用，但列表和管理界面独立。 | 共享资源身份、预览、筛选语义及定位；库窗口仍承担导入、转换、删除等库管理任务。评估共享浏览组件时保留批量转换和库管理能力。 | 高 |
| Terrain 材质 | Terrain 使用工程材质下拉选择并能打开资源库；库中创建复合地形材质，Content Browser 也能筛选地形材质。 | 补齐从浏览到定位、捕获和绑定的连续路径；保留默认三通道材质、缺失提示及编辑中的材质/槽位状态。具体交互待验证。 | 中 |
| 地图配置与场景组织 | Map 页包含 Layers、边界和 Mods；Outliner 是完整层级视图。功能存在交集，但边界和 Mods 并非层级编辑。 | 可考虑将场景组织收敛到 Outliner，把地图级设置单独组织；先比较操作密度与兼容性，不预先删除入口。 | 中 |
| 插件导航 | 底部 Extensions 跳转右侧 Plugin 页，两个入口指向同一类任务。 | 明确一个主要位置并保留便捷跳转；不能破坏插件面板注册、关闭及生命周期。 | 低 |
| 实体定义与资源 | Entity Browser 浏览实体定义，Content Browser 浏览文件与项目资产。名字相似，但对象和使用方式不同。 | 统一搜索和视觉交互即可；实体定义、FGD 设置及创建能力不应直接当作文件资产合并。 | 中 |

图层证据：[LayerEditor](../lib/TbUiLib/src/LayerEditor.cpp)、[OutlinerTreeWidget](../lib/TbUiLib/src/outliner/OutlinerTreeWidget.cpp)。属性证据：[EntityInspector](../lib/TbUiLib/src/EntityInspector.cpp)、[OutlinerEntityPropertyEditor](../lib/TbUiLib/src/outliner/OutlinerEntityPropertyEditor.cpp)。

## 工作流与必须保留的行为

| 工作流 | 当前路径 | 统一时的验收重点 |
| --- | --- | --- |
| 管理图层 | Map 的 Layers 或 Outliner 的图层操作 | 激活层、新建层、移动/选择、显隐锁定、隔离、排序、导出设置、删除后的对象归属；撤销和选区同步。单独验证是否增加从选区创建 |
| 查找与编辑实体 | 视口或 Outliner 选择；Entity 或 Outliner 属性编辑；Entity Browser 查定义 | 单选、多选及混合类型、原始键和友好名称、专用编辑器、FGD 切换、撤销重做、选区同步 |
| 编辑面与选择材质 | 选中面，在 Face 调整属性或选择原生材质；工程地图也可打开资源选择器；Content Browser 可应用纹理 | 无面/多面、当前材质定位、UV 与面标志、集合过滤、不同游戏格式；项目资源的捕获和目标变化检查 |
| 使用资源 | Content Browser 选择来源、类型与资源；预览、拖放或执行上下文操作 | 项目地图、独立地图及无活动地图分别验证；模型、sprite、声音、天空盒、纹理、prefab 和地图的行为按类型验证，不假设全部支持相同操作 |
| 管理资源库 | 进入 Asset Library，导入、转换或创建地形材质；浏览器可定位到库 | 外部库、工程快照和游戏文件身份独立；取消、冲突、缺失库、重连、更新、删除与引用查询需要明确反馈 |
| 编辑地形 | 创建或转换区域，选择材质和槽位，绘制/雕刻/编辑顶点，分析预算并导出 | 分块、边缘锁定、撤销、材质缺失、编译实体设置、预算及编译快照；切换面板不丢工具状态 |
| 插件和辅助工具 | 打开 Plugin、输出、Python 控制台或 Issues | 插件面板生命周期、当前地图目标、快捷键、工具区布局和焦点切换 |

资源选择器只在确认时捕获库资源，并在提交前检查编辑目标是否仍有效。
Content Browser 的纹理或天空盒应用先处理项目捕获，再执行地图命令；地图撤销
撤回地图编辑，已捕获资源仍是工程快照。统一时应保留并清晰表达这一边界。
依据：[ModelBrowser](../lib/TbUiLib/src/ModelBrowser.cpp)、[FaceInspector](../lib/TbUiLib/src/FaceInspector.cpp)。

## 建议推进顺序

1. 建立操作对照和验收样例：记录两套图层菜单、两套实体属性编辑器、两种材质浏览的差异，覆盖工程和独立地图。后续整合方向见[工作区整合方案](workspace-unification-proposal.md)。
2. 提取共享命令和编辑能力，先保证两处入口的结果与撤销一致，再考虑合并视图。
3. 统一资源选择上下文、资源身份和定位；打通 Terrain 材质选择，保留库管理和工程捕获的生命周期。
4. 基于实际操作与窄面板截图调整导航和布局，迁移时保留快捷键、面板状态与插件扩展点。

验收至少包括相关 `TbUiLibTest`、Release 构建、三个主题的代表页面截图，以及
工程/独立地图的人工工作流检查。原生焦点、拖放和目标切换需单独验证；截图
只能证明界面呈现，不能证明操作语义等价。

## 待确认

- Layer Editor 与 Outliner 的全部菜单、排序和快捷键是否逐项等价；是否增加真正的从选区创建图层操作。
- 两套实体属性编辑器对所有智能编辑器、混合选择和 FGD 特性的覆盖差异。
- 原生材质浏览器与 Content Browser 对所有支持游戏、材质集合和使用量排序的差异。
- 独立地图与工程地图在资源选择、拖放及预览上的实际差异，以及切换地图后的焦点和选区行为。
- Terrain 材质从库创建到工程捕获和绑定的实际操作步骤、失败反馈及适合的主要入口。

相关设计：[工作区 UI 架构](workspace-ui-architecture.md)、[资源库总体设计](asset-library-master-plan.md)、[地形材质设计](terrain-material-authoring-design.md)。
