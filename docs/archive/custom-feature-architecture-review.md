# 自定义功能架构审查

本文档审查当前分支相对 upstream `master` 叠加的自定义提交。重点不是
界面细节或单个 bug，而是从框架层面判断哪些功能容易成为长期维护负担，
以及哪些优化投入产出比最高。

当前审查基线：

- 分支：`feature/latest-upstream-merge`
- 已检查的 upstream 基点：`b8c14a93c`
- 已检查的当前 HEAD：`be0ca0be5`
- 主要新增功能范围：命令面板、统一 GoldSrc 资产浏览器、声音试听预览、
  action 勾选状态同步、FPS overlay、GoldSrc FGD/资源扩展，以及本地
  Windows Release 构建脚本。

## 总结

最高收益的下一步不是立刻继续增加更多资产类型，而是先加固资产浏览器的
架构。它正在变成模型、Sprite、声音、未来 WAD 纹理、实体模板、拖放放置
和智能属性编辑器的共同入口。如果现在不先拆好边界，后续每加一个功能都会
继续压到同一个大 widget 上。

当前最大的框架风险是：

- `ModelBrowserView` 仍然是一个多职责的大型 OpenGL / Qt / 音频 / 控制器
  widget。它已经同时负责渲染、命中测试、选择状态、拖放、右键菜单、
  GoldSrc MDL 缩放、SPR 纹理缓存和 WAV 播放。
- 资产扫描已经比旧版清晰，但仍然默认资产都是 GoldSrc 文件系统里的磁盘
  文件。WAD 纹理和未来实体模板不应该硬塞进同一套递归文件扫描器。
- 命令面板很有价值，但目前逻辑仍然以 widget 为中心。插件命令接入前，
  搜索、排序和命令收集应该先抽成可测试的模型。
- checkable action 的同步已经在 `MapWindow` 层修复，但责任仍然容易在
  未来新增开关时被遗漏。
- Release 构建脚本对本机很有价值，但硬编码了机器路径。除非参数化，
  否则它应该被视为本地分支辅助脚本。

## 高优先级

### 统一资产浏览器：拆分视图、预览和播放

证据：

- `AssetBrowserModel` 已经负责资产分类、支持的根目录/扩展名、扫描、变更路径检测和 entry 构建。
- `AssetPreviewProvider` 已经负责 SPR 解码和 WAV 可播放路径检查。
- `ModelBrowserView` 仍然负责渲染、选择、声音播放、Sprite 预览 GL 纹理生命周期、右键菜单、拖放 payload 和 MDL 缩放操作。

风险：

- 增加 WAD 纹理、实体模板、波形预览或更复杂的卡片 UI 时，`ModelBrowserView` 会继续膨胀。
- `QMediaPlayer` 状态和 OpenGL 纹理状态生命周期不同，却由同一个 widget 管理，刷新、关闭和测试隔离都更脆弱。
- 声音按钮和占位符的手写 OpenGL 渲染把 UI 语义散落在绘制代码里，而不是集中到一个轻量的 card/view-state 层。

建议重构：

- 增加 `AssetBrowserViewModel` 或 `AssetGridModel`，负责可见条目、选择、hover、按钮命中区域、拖放 payload 和右键菜单动作。
- 将 WAV 播放移到独立的 `AssetAudioPreviewController`，由浏览器 widget 持有，而不是由 OpenGL 网格视图直接持有。
- 将 SPR GL 纹理缓存移到专门的 `AssetPreviewTextureCache`，提供明确的 `clear`、`invalidate(path)` 和 `uploadIfNeeded` API。
- 让 `ModelBrowserView` 只负责绘制当前 view model，并转发输入事件。

收益：

- WAD 纹理和实体模板会更容易接入。
- 降低 Release-only 的 OpenGL / Qt Multimedia 生命周期问题。
- UI 行为可以不依赖 OpenGL widget 进行测试。

### WAD 支持前先引入资产来源注册表

证据：

- 当前资产根目录硬编码为 `models`、`sprites` 和 `sound`。
- 当前支持扩展名硬编码为 `.mdl`、`.spr` 和 `.wav`。
- roadmap 已经明确需要 WAD 纹理和实体模板。

风险：

- WAD 纹理并不是和 `.mdl/.spr/.wav` 同类的普通磁盘文件。把它硬塞进 `collectBrowserAssets` 会混淆来源语义，也会让刷新和失效逻辑变复杂。
- 实体模板可能是项目本地 JSON 或 map 片段，并不一定来自游戏文件系统。

建议重构：

- 引入 `AssetSource` 接口，例如 `FileAssetSource`、`MaterialAssetSource`、`TemplateAssetSource`。
- 用一个小型 `AssetRegistry` 合并各来源结果，形成统一可搜索列表。
- 保留 `FileAssetSource` 作为当前 GoldSrc 文件系统扫描器。
- 后续通过 `MaterialAssetSource` 接入 WAD/material 枚举，数据来源是当前 map 的 material collections。

收益：

- 避免统一资产浏览器退化成另一个堆满特殊分支的 Model Browser。
- 每类资产都可以清楚定义自己的刷新、预览和放置行为。

### Python API 绑定文件仍是大风险

证据：

- 现有风险文档已经指出 `PythonApiModule.cpp` 是一个混合了大量职责的大绑定文件。
- 当前分支增加了大量 Python API 示例，并继续把 Python 插件作为重要自定义方向。

风险：

- 每增加一个 API，`PythonApiModule.cpp` 在生命周期、transaction、handle invalidation 和 GIL 行为上都更难审查。
- 插件 UI 和资产浏览器最终很可能需要 Python 扩展点，不应该继续叠在一个单体绑定文件上。

建议重构：

- 在暴露资产浏览器扩展 API 前，先按领域拆分绑定：`document`、`selection`、`entity`、`brush`、`face`、`material`、`ui`、`actions`、`events`、`transactions`。
- 保持 session-owned callbacks/timers/panels 作为唯一资源所有权模型。
- 增加针对文档 reload、节点删除、brush geometry replacement 的 handle invalidation focused tests。

收益：

- 在插件 API 面继续扩大前，先降低崩溃和 stale object 风险。
- Python 插件后续更容易做成可维护的生产级架构。

## 中优先级

### 命令面板需要可测试的命令模型

证据：

- `CommandPaletteDialog` 直接收集 `ActionManager::actionsMap()`，过滤 enabled actions，构造显示字符串，并持有搜索逻辑。
- 当前测试只验证菜单 action 和默认快捷键存在。

风险：

- 插件命令、最近命令、收藏、别名或评分排序一旦加入，排序逻辑会继续被推进 dialog。
- 当前搜索质量只是 label/path/shortcut 上的简单 term contains。

建议重构：

- 抽出 `CommandPaletteModel`，负责命令收集、过滤、评分、display path 格式化和当前选中命令 id/path。
- 增加单元测试覆盖过滤、排序、禁用 action 排除、重复 label、快捷键匹配和插件命令元数据。
- 保持 dialog 只是薄 Qt adapter。

收益：

- 命令面板可以继续成长，而不会变成纯 UI 逻辑。
- 可以快速获得更好的排序和插件命令支持。

### Checkable action 状态应归 action 系统管理

证据：

- `MapWindow::updateActionState()` 现在会从 `Action::checked(context)` 同步 `QAction::checked`。
- `MapWindow::preferenceDidChange()` 对所有 preference 变化调用 `updateActionStateDelayed()`。

风险：

- 当前修复有效，但 `MapWindow` 之外的 checkable UI 未来仍可能忘记同步 checked 状态。
- preference、document、tool 状态变化都走比较宽泛的延迟刷新路径。

建议重构：

- 增加一个 action-state bridge，统一负责所有已创建 `QAction` 的 enabled、checked 和 shortcut 同步。
- 让 `ActionManager` 或 bridge 暴露明确刷新触发：preferences、context、tool state、document state。
- 增加测试覆盖 preference 改变后 checkable action 状态同步。

收益：

- 避免纹理锁定这类 UI 图标不同步问题再次出现。
- 未来新增 view/tool toggle 更安全。

### GoldSrc FGD 增量需要来源和分层策略

证据：

- Half-Life 配置现在引用 `CounterStrike16.fgd`。
- 旧的未引用 `combined.fgd`、`models.fgd`、`sprites.fgd` 已清理。

风险：

- 大型 vendored FGD 文件在后续 upstream 合并时很难维护。
- 当前不够清楚哪些 definition 是原始来源、生成结果、局部 patch，或本分支便利扩展。
- 后续修改可能不小心把自定义默认值和 upstream 游戏定义混在一起。

建议重构：

- 在 `app/TrenchBroom/resources/games/Halflife/` 下补一个简短 README，记录 FGD 来源、合并策略和本地修改点。
- 自定义增量优先放在小 overlay FGD 中，不直接编辑大型导入定义。
- 增加 smoke test 或脚本，验证配置引用的 FGD 文件存在且可解析。

收益：

- 降低后续更新 GoldSrc definitions 的风险。
- 让 entity browser 和 asset drop 行为更容易追溯。

### 本地构建脚本共享前需要参数化

证据：

- `scripts/build_release_codex.cmd` 固定了 VS、Qt、`rc.exe` 和 `mt.exe` 的绝对路径。
- 这个脚本解决了当前机器上的 Release 构建环境不稳定问题。

风险：

- 对当前工作站很有价值，但不可移植。
- 后续 agent 或其他机器可能直接照搬到 CI 或其他环境，却没注意到硬编码路径。

建议重构：

- 保留当前脚本作为本地分支辅助工具，但增加环境变量覆盖：`VSDEVCMD`、`QT_PREFIX`、`WINDOWS_KITS_BIN`、`BUILD_DIR`。
- 删除构建目录前先打印实际解析到的工具路径。
- 如果它继续是工作站专用脚本，可以考虑改名为 `build-release-local.cmd`。

收益：

- 保留可靠构建链路，同时减少本地路径带来的意外。

## 低优先级

### FPS Overlay 应纳入统一 View Overlay 模型

证据：

- FPS、sky、2D readable outlines、fog、grid、entity overlays 现在都是分散的 view preferences。

风险：

- 每增加一个显示功能，都会多一条独立 toggle 路径和刷新规则。

建议重构：

- 引入轻量的 `ViewOverlaySettings` 分组，让 `ViewEditor` 继续作为 UI adapter。
- 在 overlay 控件继续增多前，不需要过度设计。

收益：

- View Options 更清晰，刷新路径更集中。

### 资产浏览器 UI 美化应等模型拆分后再做

证据：

- 当前资产 UI 已经可用，但视觉上仍比较朴素。
- 渲染仍然是 `ModelBrowserView` 内的自定义 OpenGL 绘制。

风险：

- 如果先在当前 widget 上投入大量 UI 美化，可能会加深耦合，让后续架构拆分更贵。

建议重构：

- 先拆 view state、播放控制器和纹理缓存。
- 再在更干净的模型上做卡片布局、密度模式、类型筛选、排序控件和详情面板。

收益：

- 避免把设计工作投入到很快会被重排的结构上。

## 建议推进顺序

1. 先抽出 `AssetBrowserViewModel` 和 `AssetAudioPreviewController`。
2. 在接 WAD 纹理前增加 `AssetSource` / `AssetRegistry`。
3. 在增加资产/插件扩展 API 前，按领域拆分 `PythonApiModule.cpp`。
4. 抽出 `CommandPaletteModel` 并补 focused tests。
5. 给 FGD 增量补来源说明和解析 smoke check。
6. 将 `scripts/build_release_codex.cmd` 参数化，减少本机路径绑定。

这个顺序能保留当前已经可用的功能，同时优先降低最可能拖慢下一轮
GoldSrc 专项开发的结构风险。

## 可能失效或有误导风险的新增文件

以下判断基于当前代码、CMake、资源复制规则和搜索引用关系，不以旧计划文档
或提交作者作为依据。这些文件不一定都会导致运行时错误，但已经满足至少一个
条件：当前未被配置引用、与当前架构方向冲突、属于生成物，或带有明显本机
环境假设。

### 建议删除或改名

| 文件 | 当前证据 | 可能影响 | 建议 |
|---|---|---|---|
| `python/src/trenchbroom_api.egg-info/PKG-INFO` | `*.egg-info` 是 Python 打包生成物，只被自身 `SOURCES.txt` 引用 | 污染源码树；后续打包时可能带入过期 metadata | 已删除，并加入 `.gitignore` |
| `python/src/trenchbroom_api.egg-info/SOURCES.txt` | 同上 | 记录的文件列表会随本地构建变化，容易产生无意义 diff | 已删除，并加入 `.gitignore` |
| `python/src/trenchbroom_api.egg-info/dependency_links.txt` | 同上 | 生成物，无源码价值 | 已删除，并加入 `.gitignore` |
| `python/src/trenchbroom_api.egg-info/top_level.txt` | 同上 | 生成物，无源码价值 | 已删除，并加入 `.gitignore` |
| `python/src/tb/__init__.py` | 当前运行时入口已统一为 `trenchbroom`，代码和测试都导入 `trenchbroom`；此文件仍描述 legacy `tb` | 如果被安装到编辑器环境，会让补全和示例继续指向已移除的 `tb` API | 已删除；后续如需类型包，应新建 `trenchbroom` stub |
| `python/src/tb/py.typed` | 只服务上面的 `tb` stub 包 | 和当前 `trenchbroom` 入口不一致 | 已删除 |
| `python/pyproject.toml` | 包名为 `trenchbroom-api`，描述仍是 embedded module `tb` 的 type stubs | 发布或本地安装后会继续宣传错误入口 | 已删除；等 `trenchbroom` stub 成熟后重新建立 |
| `python/upload_pypi.bat` | 直接执行 `twine upload dist/*`，没有和当前 `trenchbroom` stub 策略同步 | 误触会发布过期 `tb` 类型包 | 已删除 |
| `app/TrenchBroom/resources/graphics/images/LanguagePreferences.svg` | 当前没有代码引用；但 `graphics/images` 会整体复制到 Release 包 | 增加无用资源；容易误导后续认为存在独立语言偏好页 | 已删除 |

### 建议归档或重写说明

| 文件 | 当前证据 | 可能影响 | 建议 |
|---|---|---|---|
| `app/TrenchBroom/resources/games/Halflife/combined.fgd` | `GameConfig.cfg` 只加载正式 CS1.6 FGD；`combined.fgd` 没被引用 | 如果手动改成它，可能引入重复/冲突实体定义 | 已删除 |
| `app/TrenchBroom/resources/games/Halflife/models.fgd` | 当前没有被 `GameConfig.cfg` 引用 | 大型生成式 entity 列表会误导维护者，以为模型资产仍靠 FGD 暴露 | 已删除，模型浏览交给统一资产浏览器 |
| `app/TrenchBroom/resources/games/Halflife/sprites.fgd` | 当前没有被 `GameConfig.cfg` 引用 | 同上；可能与资产浏览器的 sprite 扫描职责重复 | 已删除，sprite 浏览交给统一资产浏览器 |
| `docs/Outliner_Implementation_Plan.md` | 功能已经实现，文件仍是实现计划口吻 | 后续接手时可能把历史计划当作当前设计 | 已删除 |
| `docs/python_api_improvement_plan.md` | 当前 Python 已全面切到 `trenchbroom`，但文件仍混合计划、状态和迁移说明 | 容易和实际 API 实现状态不一致 | 已删除；后续单独写当前 `trenchbroom` API 文档 |
| `docs/custom-feature-refactor-notes.md` | 文件包含多阶段历史记录和旧结论 | 作为“当前事实”阅读时容易误导 | 已删除，保留当前架构审查和维护风险文档 |
| `lib_docs/index.md` | 未被 CMake/docs 系统引用，内部含本机 `file:///d:/...` 路径 | 对其他机器不可用，且可能和当前源码不再同步 | 已删除 |
| `lib_docs/kdl.md` | 同上 | 同上 | 已删除 |
| `lib_docs/stackwalker.md` | 同上 | 同上 | 已删除 |
| `lib_docs/upd.md` | 同上 | 同上 | 已删除 |
| `lib_docs/vm.md` | 同上 | 同上 | 已删除 |
| `python/examples/legacy_removed_tb/README.md` | 目录只剩一个迁移说明，没有可运行示例 | 不影响运行，但会让示例目录继续暴露 legacy `tb` 名称 | 已删除 |

### 保留但需要标注环境风险

| 文件 | 当前证据 | 可能影响 | 建议 |
|---|---|---|---|
| `scripts/build_release_codex.cmd` | 当前机器 Release 构建依赖它；但硬编码 VS、Qt、Windows Kits 路径 | 其他机器直接运行会失败；脚本删除构建目录，误传参有破坏性 | 保留，但参数化 `VSDEVCMD`、`QT_PREFIX`、`WINDOWS_KITS_BIN`，并在执行前打印目标目录 |
| `scripts/build-filtered.ps1` | 用于降低编译输出噪声，依赖已有 build tree 和默认 VS 路径 | 有用但偏本机；过滤规则可能隐藏少量有价值 warning 上下文 | 保留为开发工具，补 README 或脚本注释说明完整日志位置 |

### 不建议列为无效的新增文件

下面这些文件虽然也是自定义分支新增，但当前有明确引用或运行价值，不应按
“无效文件”处理：

- `app/TrenchBroom/resources/shader/Sky.vertsh`、`Sky.fragsh`：已由
  `gl::Shaders` 引用。
- `app/TrenchBroom/resources/translations/trenchbroom_zh_CN.ts` 和 `.qm`：
  `.qm` 已进入 qrc，`Main.cpp` 会加载中文翻译。
- `app/TrenchBroom/resources/graphics/images/Map_*.svg`、
  `object_show.svg`、`object_hidden.svg`、`Path.svg`：当前 Outliner、
  Model Browser 或 Path Tool 有明确引用。
- `lib/TbUiLib/include/ui/Asset*`、`lib/TbUiLib/src/Asset*`、
  `GoldSrcSpritePreview*` 及其测试：统一资产浏览器当前正在使用。
- `lib/TbRenderLib/*SkyRenderer*`、`*BrushOutlineColor*` 及测试：当前
  skybox 和 2D readable outlines 功能正在使用。
