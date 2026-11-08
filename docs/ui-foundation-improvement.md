# UI 公共组件与布局改进

日期：2026-11-08。状态：已实施并验证。本文记录本轮范围、扩展接口和验收结果。

## 目标

普通业务面板通过添加字段、区块和操作完成界面构建，复用公共层提供的尺寸、
间距、折叠、滚动及状态表现。保留现有地图功能、文档生命周期、撤销、快捷键、
插件注册和 Python UI 接口。

## 现状与问题

- `ThemeTokens`、`ApplicationStyle` 和 `base.qss` 已承担主题和基础控件外观。
- `CollapsibleTitledPanel` 已统一折叠行为，但业务代码仍自行组装内容与标题操作。
- Terrain、PluginInspector 和 Outliner 各自维护滚动容器、区块列表及底部留白。
  Terrain 创建后隐藏按钮，暴露出默认拉伸规则导致的内部空白。
- Terrain、插件和 Transform/Entity Properties 使用重复的页面名称选择器定义卡片。
- 表单、弹出筛选器、设置和编译任务分散设置标签、字段增长和行间距。
- 静态组件截图无法单独证明业务动态显隐、窄栏及反复展开的布局正确。

## 责任划分

| 层 | 所有者 | 责任 |
| --- | --- | --- |
| 主题 | ThemeTokens / ThemeRegistry | 可分发主题的颜色和明暗；主题不改变几何 |
| 基础控件 | ApplicationStyle / base.qss | 控件几何、图元、hover/focus/disabled 等状态 |
| 内容布局 | PanelLayout | 表单、内容列、操作行及滚动区的稳定默认规则 |
| 面板列表 | PanelList | 区块插入、移除、排列及多余空间分配 |
| 折叠区 | CollapsibleTitledPanel | 展开状态、内容增长策略、公共卡片和标题操作插槽 |
| 业务 | 各 Inspector / 编辑器 / 插件 | 字段、数据、命令、可用状态及领域内容 |

布局间距和内容留白在 `PanelLayout` 定义；QSS 管理边框、圆角和控件高度。
同一参数只由一层负责。已有基础控件角色继续使用 `tbControlRole`。

## 公共接口与扩展规则

### PanelLayout

提供创建标准表单、内容列和操作行的工厂。所有结果仍是 Qt 标准布局，允许添加
任意 QWidget 和嵌套布局。表单默认顶部对齐、字段按可用宽度增长、长行换行。
嵌入式与带留白的内容由明确枚举区分。弹出筛选器可以保留自身固定尺寸约束。

标准滚动区工厂统一 frame、widgetResizable 和滚动策略；图像平移、颜色历史等
具有特殊滚动行为的领域组件继续由各自所有者管理。

标准表单嵌入区块的示例：

```cpp
auto* section = new CollapsibleTitledPanel{tr("Properties")};
section->setPresentation(CollapsibleTitledPanel::Presentation::Card);
auto* form = createPanelForm();
form->addRow(tr("Name"), new QLineEdit{});
section->createContentLayout()->addLayout(form);
list->addWidget(section); // list 是 PanelList，自动管理尾部空间
```

独立表单使用 `createPanelForm(parent, PanelInsets::Content)`；已带留白的父布局
使用默认 `None`。已有 QFormLayout 子类可调用 `configurePanelForm` 复用同一规则。
`createContentLayout` 重复调用返回同一个标准内容列，运行时切换 Section/Card 会
同步该列留白；自行设置布局的插件继续拥有其布局配置。

### PanelList

列表持有内容 QWidget、QVBoxLayout 和底部拉伸项。调用者通过添加、插入和移除
组件管理内容，不直接维护底部 spacer。移除条目不销毁 QWidget；业务所有者明确
决定复用或删除，插件关闭和 Outliner 保留 Transform 实例的语义继续成立。

### CollapsibleTitledPanel

在现有类上提供标题操作插槽、标准内容列和明确内容尺寸策略。普通表单使用
按内容排列；树、列表和可视化可以选择增长。嵌入式 section 和独立 card 是按
容器用途选择的公共表现，使用语义属性选取全局 QSS，页面 objectName 用于查找、
测试和持久化。

切换展开或表现时更新实际受影响节点的样式与几何，覆盖展开、折叠、焦点和禁用。
标题操作不触发折叠；空间键与回车继续控制标题；已有 saveState 格式保持兼容。

### 扩展边界

- 使用普通 Qt 控件添加新字段，自动继承全局主题。
- 优先组合标准表单、区块和操作位置；业务页面不复制卡片绘制。
- 新语义角色需有实际交互或密度用途，并加入组件展示和状态测试。
- 地图、模型、图像、波形等领域可视化保留原渲染器，周围使用公共容器。
- 不把业务状态转换成组件库配置，也不为每个 Qt 控件增加转发包装类。

## 本轮迁移范围

1. 文档、责任划分、公共布局接口及其尺寸回归测试。
2. Terrain、插件列表和 Outliner 属性列表统一使用 PanelList。
3. 全部 CollapsibleTitledPanel 使用点检查尺寸用途；独立卡片统一外观，清理
   Terrain/Plugin/Transform/Entity Properties 的重复卡片选择器。
4. 统一普通表单的默认规则，覆盖 Terrain、资源库详情、浏览器筛选弹窗、
   编译任务与运行配置、设置项和插件表单。保留专用表格、向量和领域编辑器。
5. 组件展示、主题矩阵、业务回归和静态治理；记录交付结果。

本轮完成以所有步骤交付并验证为准。其他已有工具栏、基础控件和对话框继续使用
已有全局样式；改进公共默认值时通过真实页面矩阵共同验证。

## 验收

- 新面板的标准示例不需要手动设置标题高度、卡片圆角或底部拉伸项。
- 重复的按页面命名卡片规则已删除，objectName 不决定这些卡片的外观。
- 内容增长是明确策略；隐藏字段、操作后内容可以正确收缩；列表空白位于底部。
- 覆盖窄/宽、矮/高、中英文长标签、重复展开、动态隐藏、移除和重新添加。
- 验证标题操作、键盘折叠、禁用及焦点状态；验证插件面板生命周期。
- 构建 TbUiLibTest 和 Release TrenchBroom，执行相关测试和最终本地 CI preflight。
- `components` 和代表性 Terrain、插件、Outliner、资源库、浏览器及设置页覆盖
  Light/Dark/Blender；公共布局在 1x、1.5x、2x 下通过截图验证。
- 保留所有原有业务能力；迁移和清理在同一轮完成，提交中不包含生成截图或日志。

## 交付记录

### 实际迁移

- `PanelLayout.h/.cpp` 提供标准 Qt 布局工厂和 `PanelList`；全库普通表单创建已
  集中到此处。`FormWithSectionsLayout` 继承这些默认值，保留设置分组缩进和对话框
  外留白。设置页面清除了重复的行间距与字段增长覆盖。
- TerrainInspector、PluginInspector、OutlinerEntityPropertyEditor 共用面板列表。
  Outliner 保留 Transform、属性卡及添加栏实例；插件保留注册、用户关闭和会话
  清理的生命周期。重建时嵌套 QLayout 的重复释放已修正。
- `CollapsibleTitledPanel` 提供 Section/Card、FitContents/Expanding、内容列与标题
  操作接口。所有使用点已按用途迁移；树、资源详情和插件可增长，普通表单按内容
  排列。Terrain、Plugin、Transform、Entity Properties 的重复 QSS 已合并。
- 资源库详情、资源浏览器筛选弹窗、六类编译任务、编译/引擎配置、地形材质对话框、
  MCP 设置及 Python 插件表单接入标准布局。Python UI 的公开方法与参数保持一致。
- 专用滚动组件保留在 ViewEditor、SmartFlagsEditor、SmartColorEditor；它们分别
  管理勾选树、标志位和颜色历史。各领域渲染器继续负责自身内容。
- `components` 展示公共卡片、嵌入区块及标题操作。静态治理新增已迁移卡片的页面
  名称选择器检查，防止重复外观再次出现。严格编译检出的未使用 `assetTypeName`
  内部辅助函数已清理。

### 验证记录

2026-11-08，Windows Release，Qt 6.11.1：

- `TbUiLibTest` 与 `TrenchBroom` 构建通过；29 个改动编译单元通过 clang-cl 严格编译。
- `ci-preflight.ps1 -BaseRef 0dd0a3fd9 -AllTests -TestJobs 1` 通过：177 项中
  175 项通过，2 项按环境条件跳过，零失败。完整 UI 回归串行运行，避免地形交互
  综合测试受并发时序影响。
- AssetInspection 已执行的 1511 项断言通过；其真实 GoldSrc 模型段因未配置
  `TB_GOLDSRC_MODEL` 跳过。原生鼠标捕获测试需要 `TB_TEST_SHOW_WINDOWS=1`，本轮
  背景验证按既有规则跳过该项。
- 新增 PanelList 顺序、移除复用与删除测试；PanelLayout 覆盖长中英文标签、窄宽
  布局、字段及操作显隐、短窗口滚动。折叠测试覆盖三主题反复切换、标题操作、
  禁用操作、尺寸策略和表现切换；Outliner 新增嵌套布局清理及永久控件保留测试。
- `check-ui-style-governance.ps1` 与 `git diff --check` 通过。

- 19 个组件及真实页面目标、Light/Dark/Blender 和 1x/1.5x/2x 共 171 张截图通过
  `ui-theme-acceptance.ps1` 检查。范围包括 Terrain 展开/折叠、插件、Outliner、
  资源库宽窄视图、实体浏览器、设置、工作区和使用 GPU-ready 材质的面检查器。
  人工检查了组件、Terrain、插件、Outliner、资源库和设置页代表图。

截图是本机 Qt、字体和渲染环境下的布局证据；本机 Qt 6.11.1 与 CI 的 6.10.3
存在小版本差异，其他平台仍由对应 CI 和平台验收覆盖。
