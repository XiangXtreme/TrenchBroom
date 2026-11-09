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


## 全局视觉收口（2026-11-09）

方向：精确、紧凑、沉静的关卡工作台。地图、材质、模型和地形预览承担色彩与
细节，周围控件使用现有主题的中性色。沿用字体、TB 标识、功能入口和工作区组织。

本轮按公共契约调整，而不是为每个页面另做皮肤：

- 间距：面板工具栏统一使用 `PanelInsets::Toolbar`，表单列距统一为 12 px；
  Content Browser 预览间距为 8 px，资源库标题与侧栏使用同一组间距。资源库标题与
  管理操作分两行，网格筛选与显示控制分两行，长库名和路径使用共享 ElidedLabel，
  保持搜索、转换、预览同时可见。
- 色彩：辅助文字使用 secondary，禁用状态使用 disabled；验证错误、输出日志和
  Python Console 使用当前安装主题的 error/warning/success。卡片标题减少重复底色。
  修正浅色主题失焦选中行的文字对比度，普通列表与自绘资源分类共同使用对应 palette group。
- 图标：设置导航与资产库操作统一使用 16 px，保留具有领域含义的地图工具图标。
- 文案与字重：区域标题取消强制全大写，分类取消额外字距；标题使用 600，资产名
  使用 500，元数据继承字体后小一号且不低于 8 pt。地形创建分组使用简短字段名。
- 导航与操作：公共 navigation 状态同时覆盖右栏、底栏与设置列表；设置行最小
  高度为 32 px，短窗口可滚动。欢迎页明确新建地图为默认操作。
- 扩展：新增面板继续使用公共 role、布局和主题接口；组件快照覆盖导航列表及
  secondary/error/disabled 反馈，避免页面私有视觉实现再次分叉。

此轮使用 Unslop 的审查方法进行现有界面打磨；方法作者为
[Damir Sokolovsky (@heydamir)](https://heydamir.design/unslop)，CC BY 4.0。


### 本轮验证

Windows Release / Qt 6.11.1：

- `TbUiLibTest`、`TrenchBroom` 构建通过；23 个改动编译单元通过 clang-cl 严格检查。
- 串行执行 183 项 UI 回归，并对修正测试样本后的 `AssetInspection` 专项复验：
  181 项通过，2 项受环境条件限制。`AssetInspection` 已执行的 1560 项断言全部通过，
  真实 GoldSrc 模型段因未设置 `TB_GOLDSRC_MODEL` 跳过；原生鼠标捕获按背景测试策略跳过。
  最终 `ci-preflight.ps1 -BaseRef HEAD -TestFilter '^AssetInspection$' -TestJobs 1` 通过。
- 22 个真实页面及组件目标通过 Light/Dark/Blender 的 66 张 1x 截图检查；最终组件、
  窄栏属性、窄资产库及设置页覆盖三主题和 1x/1.5x/2x，共 36 张截图通过。
  人工核对标题、导航、辅助文字、失焦选中行、预览与滚动区域。
- 资产库窄窗口在所有缩放下保持 900 逻辑像素宽；验收脚本新增宽度上限，防止
  后续修改把窄窗口自动撑大而误判为通过。面检查器的 fixture 材质全部达到 GPU-ready。
- 样式治理与 `git diff --check` 通过。验收文件保存在本机 `codex-logs`，不纳入源码。

## 属性区嵌入列表（2026-11-09）

WAD 编辑器原先使用普通 QListWidget，在场景属性卡片和外层滚动区中会被压到
两行左右；完整路径又占满行宽，水平滚动条进一步挤占内容。此次增加共享的
`PanelItemList`，根据当前字体、样式、网格和行高计算所需空间：WAD 保留四至六行，
更多文件在列表内滚动；短侧栏由外层属性区滚动。天空选择器接入同一组件，移除
210 px 固定总高度，保留二至三行完整缩略图。组件继承既有主题外观。

WAD 行显示文件名，完整路径保存在数据与提示中，文件顺序、路径值和撤销行为
保持原有语义。同名文件仍按完整路径区分。相同内容刷新时复用列表，保留选中和
滚动位置；载入、清空及重建后同步更新操作按钮。标题和操作说明补齐中文翻译。

同时检查了标志位、颜色与模型属性区。场景页的标志位直接使用内容布局，颜色与
模型操作也未发现同类列表压缩原因，本轮不增加独立的尺寸补丁。

`outliner-properties-wad` 快照目标使用内存中的 GoldSrc 样本，检查窄栏展开状态、
六个完整文件行及无横向滚动；不改动用户工程。回归覆盖短窗口、列表增长与清空、
字体变化、同名不同路径、撤销、相同内容刷新，以及属性区展开、折叠和再次展开。

本轮验证：Windows Release / Qt 6.11.1，`TbUiLibTest` 与 `TrenchBroom` 构建通过，
10 个改动编译单元通过严格编译，11 项相关 UI 回归全部通过，最终本地 CI preflight
通过。组件、WAD 窄栏、普通属性窄栏、属性表格和支持面板共五个目标，通过
Light/Dark/Blender 与 1x/1.5x/2x 的 45 张截图检查；人工复核了组件、WAD 列表和
整体布局。样式治理与补丁空白检查通过。截图和日志仅保存在本机 `codex-logs`。

### 嵌入区展开与折叠

嵌入区显隐使用 `PanelList::setContentVisible`：暂缓滚动区重绘，自内向外更新
卡片布局约束，再同步更新滚动区布局和范围，最后恢复原有重绘状态。WAD、天空
和标志位使用同一入口。此过程仅发送指定滚动区的布局事件，不运行应用事件循环。
属性行在同行/堆叠模式不变时，也会立即应用随滚动条显隐变化的标签宽度。

动态回归比较点击返回时与后续事件处理后的内容尺寸、可用宽度、滚动范围、滚动
位置、按钮位置和输入框尺寸；覆盖短表单、长表单以及三种窗口尺寸下的反复点击。

修复验证：Release 构建、3 个改动编译单元严格检查、11 项相关回归及本地 CI
preflight 通过；原生窗口下的属性编辑器动态测试通过。组件、WAD 窄栏和普通属性
窄栏通过三主题、三缩放共 27 张截图检查。与前次基线相比，WAD 样本的自动滚动
位置因同步布局而改变，已人工核对文件行及工具栏；组件基线比较通过。样式治理
及补丁空白检查通过。
