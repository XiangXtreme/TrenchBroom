# 自定义功能路线图

本文档用于记录当前分支值得继续推进的自定义功能候选项。总体原则是：
保持 TrenchBroom 轻量、快速、专注 BSP/关卡编辑，不把它改造成 Blender
或 UE5 那种大而全的编辑器。

## 当前分支已接入的能力基线

这些能力已经在当前 `feature/latest-upstream-merge` 分支中落地，后续 roadmap
应以加固、补齐工作流和降低维护成本为主，而不是从零重做。

- 命令面板
  - 已接入 View 菜单中的 Command Palette，并提供 `Ctrl+Shift+P` 默认快捷键。
  - 当前定位：第一版可搜索并执行现有 `ActionManager` 动作。
  - 后续重点：接入 Python 插件命令、最近使用命令和更细的分类/排序。

- Python 插件 API / `trenchbroom`
  - 已接入 `PythonRuntime`、`trenchbroom` pybind11 模块、manifest 插件目录、
    `PythonPluginManager`、插件 session、timer、event、panel 和示例插件。
  - 当前定位：可用的 Python API 基础架构。
  - 后续重点：拆分大型绑定文件、完善 handle invalidation、补齐 API 文档和端到端测试。

- 插件 UI 与插件管理
  - 已接入 Plugin Inspector、插件面板控件、插件加载状态/错误显示，以及首选项中的插件管理入口。
  - 当前定位：只管理带 UI 的 manifest 插件；普通一次性脚本应继续通过 Run Python Script 执行。
  - 后续重点：让插件安装/刷新/错误信息更稳定、更接近 Blender 的插件管理体验。

- Model Browser 与 SmartModelEditor
  - 已接入模型浏览器、模型属性智能编辑器、GoldSrc MDL 缩放工具，并将默认放置实体调整为更适合 Sprite/模型预览的 `cycler_sprite`。
  - 当前定位：模型浏览与路径编辑已经可用。
  - 后续重点：扩展到 Sprite、声音、WAD 纹理，完善拖放到 2D/3D 视图的放置体验。

- Outliner 与属性编辑
  - 已接入 Outliner 树、Outliner Inspector、实体/层属性编辑，以及部分 smart editor。
  - 当前定位：大型地图对象管理已有基础能力。
  - 后续重点：搜索、过滤、solo、批量编辑、稳定 node id/model-view 架构和大地图性能。

- Path Tool 与 Pie Menu
  - 已接入路径点工具、`path_corner` 链创建、Pie Menu、可配置控制入口和默认快捷键。
  - 当前定位：旧分支的核心入口已恢复。
  - 后续重点：减少多层转发耦合，补齐鼠标点选/网格吸附的自动化测试。

- 2D 可读 Brush 轮廓与视图辅助
  - 已接入 `Readable 2D brush outlines`，普通 world brush 使用稳定 palette，brush entity
    内 brush 使用统一颜色，选中/锁定语义色保持优先。
  - 已接入 `Show FPS` 视图选项。
  - 当前定位：复杂地图 2D 可读性已经明显改善。
  - 后续重点：把 FPS、sky、2D 线条、debug overlay 等统一到更清晰的 View Options / Overlay 管理模型。

- GoldSrc 3D skybox
  - 已接入 `worldspawn.skyname` 六面 skybox 渲染、View Options 开关、缺失资源回退原始 sky 纹理，以及 skybox 方向修正。
  - 当前定位：GoldSrc `gfx/env` 六面 sky 已可在 3D 视图中作为 sky brush 投影显示。
  - 后续重点：缺失面诊断、方向预览、资源选择器和缓存/渲染职责拆分。

- 中文本地化与首选项整理
  - 已同步部分新增功能的中文翻译，并将不稳定的 Misc 大界面回退/整理为更接近原版结构。
  - 当前定位：基础可用。
  - 后续重点：新增功能每次提交时同步中文文案，避免首选项继续成为杂项堆叠区。

## 现代化编辑器待办

- [x] 命令面板
  - 目标：让菜单、工具、视图开关、插件命令更容易被发现，不继续堆工具栏和菜单。
  - 状态：第一版已完成，可从 `ActionManager` 收集当前上下文可用命令并执行。
  - 下一步：接入插件命令、最近命令、收藏命令和更好的匹配排序。

- [ ] 统一资产浏览器
  - 目标：保留现有 Model Browser 的工作成果，并扩展成可放置材质、模型、Sprite、声音和实体模板的资产入口。
  - 状态：Model Browser / SmartModelEditor 已有基础，仍缺 Sprite、声音、WAD 和实体模板的一体化入口。
  - 重点：优先服务 GoldSrc/CS 1.6 的真实资产流。

- [ ] 视图叠加层管理器
  - 目标：统一管理 FPS、sky、2D 可读线条、grid、edge、fog、entity overlay 和后续 debug overlay。
  - 状态：FPS、sky、2D 可读线条已经分别接入 View Options，但还没有统一 overlay 模型。
  - 重点：避免每个显示功能都散落在不同菜单或临时开关里。

- [x] MCP / Agent 白盒生成
  - 状态：MCP 默认关闭，公开入口固定为 `tb_inspect`、`tb_api`、`tb_execute_python` 和 `tb_capture`；ReadOnly 省略执行入口。复杂编辑由受文档和事务保护的 `trenchbroom` Python API 完成，原生 undo/redo、保存、验证和句柄失效检查仍由编辑器 owner 执行。
  - 范围：旧 MCP 工具目录、Blockout IR、profile、operation history、resource、overlay 和兼容包装已退役。截图仅作为当前视口的可读视觉证据，地图事实仍通过检查和 Python API 确认。
  - 后续：新增编辑能力优先扩展通用 Python API；资产和视图功能按各自产品需求推进，不再扩大 MCP 工具目录。

- [ ] Python 插件 API 生产级收尾
  - 目标：让 `trenchbroom` 优先稳定插件生命周期、卸载清理、错误展示和示例文档，再扩展大 API 面。
  - 状态：runtime、manifest、session、timer、panel 和示例已有基础；主要债务在绑定拆分、handle 生命周期和测试夹具。
  - 重点：不要继续扩大 legacy `tb` 兼容负担。

- [ ] Outliner 搜索、过滤、锁定、隐藏、solo 和批量属性编辑
  - 目标：改善大型地图的对象管理，不改动 map 文件格式。
  - 状态：Outliner 树和属性编辑器已接入，后续应补搜索/过滤/solo 和批量编辑。
  - 重点：选择同步、层级显示和属性编辑要保持稳定。

- [ ] Prefab / 实体模板系统
  - 目标：保存并放置常用 entity/brush 组合，例如门、路径链、灯光、Sprite、CS 玩法实体。
  - 重点：先做轻量模板，不做 UE 蓝图式大系统。

- [ ] 更好的崩溃和诊断报告
  - 目标：记录最近操作、地图状态、游戏配置、插件状态和 Python 错误，让崩溃报告更可定位。
  - 重点：开发版优先，减少“崩了但没有线索”的情况。

## GoldSrc / CS 1.6 高价值待办

- [ ] GoldSrc 资产浏览器：MDL、SPR、WAD 纹理和声音
  - 优先级：高。
  - 原因：GoldSrc 教程仍然指出 TrenchBroom 虽然能加载 Sprite 和模型，但没有方便的 Sprite/Model 路径选择器，很多时候只能手填路径。
  - 状态：MDL 浏览和 SmartModelEditor 已有基础；SPR、声音、WAD、拖放放置和路径补全仍需补齐。
  - 第一阶段：可搜索的 model/sprite 列表、预览，以及拖放/放置到 `cycler`、`cycler_sprite`、`env_sprite`、`ambient_generic` 和 model 属性。

- [ ] GoldSrc 智能实体编辑器
  - 优先级：高。
  - 原因：GoldSrc FGD 中的 `studio`、`sprite`、`sound` 等属性有明确语义，但纯文本编辑要求用户记住路径和实体约定。
  - 状态：model/mdl 属性的 SmartModelEditor 已接入；sprite/sound 和 CS 常用实体属性仍需扩展。
  - 第一阶段：为 `studio`、`sprite`、`sound` 和常见 Counter-Strike 实体属性提供路径选择控件。

- [ ] CS 1.6 配置向导
  - 优先级：高。
  - 原因：新用户配置 GoldSrc/CS 1.6 时通常要处理游戏路径、mod 目录、WAD、FGD、VHLT/SDHLT 编译工具和编译 profile。
  - 第一阶段：检测 Half-Life/Counter-Strike 安装目录，配置 game path、mod、编译工具、常用 WAD 和默认 VHLT profile。

- [ ] GoldSrc / VHLT 编译日志分析器
  - 优先级：高。
  - 原因：GoldSrc 编译日志会暴露 leak、missing texture、clipnodes、实体限制、资源限制等高频问题。
  - 第一阶段：解析 `-chart` 输出，显示资源使用进度条，并把常见错误链接到解释或地图视图。

- [ ] GoldSrc 限制预检面板
  - 优先级：中。
  - 原因：mapper 经常在流程后期才发现 clipnodes、entity、texture、lightdata 等限制问题。
  - 第一阶段：编译前做近似警告，编译后结合日志数据修正判断。

- [ ] WAD 工作流增强
  - 优先级：中。
  - 原因：GoldSrc 纹理依赖 WAD，且受顺序、调色板、命名规则和重复纹理影响。当前流程仍然需要用户手动管理很多细节。
  - 第一阶段：显示 WAD 来源和顺序、重复纹理来源、缺失 WAD 警告，以及按 WAD 分组的 used textures 报告。

- [ ] GoldSrc 纹理规则助手
  - 优先级：中。
  - 原因：GoldSrc 纹理名和前缀会影响透明、水、动画、切换动画和滚动等引擎行为。
  - 第一阶段：提示超长名称、空格、非法尺寸，并辅助 `{`、`!`、`+`、`+A`、`scroll` 等命名约定。

- [ ] GoldSrc Skybox 助手
  - 优先级：中。
  - 原因：当前分支已经支持 `skyname`，继续补齐六面 sky 资源检查和预览很自然。
  - 状态：3D skybox 渲染、方向修正和缺失资源回退已接入；还缺可视化选择器、六面完整性检查和方向预览。
  - 第一阶段：从 `gfx/env` 选择 `skyname`，诊断缺失面，并提供方向预览。

- [ ] FGD 兼容助手
  - 优先级：中。
  - 原因：社区 FGD 经常包含 J.A.C.K./Hammer 扩展，TrenchBroom 解析失败时用户需要手动排查和合并 FGD。
  - 第一阶段：更友好的解析诊断、include 链显示，以及可选生成 Half-Life + ZHLT/VHLT combined FGD。

## 调研备注

- TWHL 的 GoldSrc TrenchBroom 设置教程指出：TrenchBroom 可以加载 Half-Life 纹理、Sprite 和模型，但缺少方便的 Sprite/Model 文件浏览器，因此路径常常需要手动输入。
- upstream 长期 GoldSrc issue 表明，完整 GoldSrc 支持不只是 MDL 加载，还包括 Sprite、WAD、skin、bodygroup 和相关资产行为。
- GoldSrc 纹理教程强调 WAD 打包、8-bit indexed 纹理约束、15 字符纹理名限制和特殊纹理前缀。
- GoldSrc 编译教程和限制图表显示，编译日志解析是非常高价值的工具方向。

## 建议近期推进顺序

1. 先做 GoldSrc 资产浏览器第二阶段：Sprite、声音、WAD 纹理和实体属性路径选择。
2. 同步加固 Python API：拆分绑定、稳定插件 session、补齐插件管理 UI 的测试。
3. 做 GoldSrc / VHLT 配置向导和编译日志分析器，因为这两项对 CS 1.6 mapper 的日常收益最高。
4. 保持 MCP 的四入口契约，并在核心工作流需要编辑器内部能力时扩展通用 Python API。
5. 最后整理 View Options / Overlay 管理模型，把 FPS、sky、2D 线条和后续 debug overlay 收到一个清晰入口。
