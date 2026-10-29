# TB 工程工作区：需求完成度与验收状态

状态：2026-10-30，基于 `main` @ `9f152d2bf`。本文把「统一 `.tbproj` 流程 + 共享资产库 +
按需嵌入 + 按需打包」这组需求逐条对到实现和验证，并列出必须人工验收的部分。

## 1. 需求范围

1. 用新的工程文件 `.tbproj` 统一流程：一个工程包含多张地图、资源、版本和编译配置，
   日常保存和分享使用一个完整工程文件。
2. 资产库是独立文件夹，与工程解耦；一次配置后所有 `.tbproj` 都可以共享同一个库。
3. 浏览和预览资产库不写入工程；只有在地图里实际使用（拖放或显式 Add to Project）时，
   才把该资源的当前修订嵌入 `.tbproj`。
4. 按需打包：编译只带本轮实际使用的资源，输入在编译开始时冻结成快照。

非目标：BSP 是游戏运行产物而非工程容器；`.tbproj` 不直接供 GoldSrc 运行。

## 2. 需求 → 实现状态

| 需求 | 状态 | 实现位置 | 自动化证据 |
| --- | --- | --- | --- |
| 单文件工程（多地图/资源/版本/编译配置） | 已完成 | `lib/TbProjectLib` 的 `ProjectStore`（SQLite 容器、修订、内容块、检查点） | `TbProjectLibTest` 6 用例 453 断言 |
| 工程地图标签与文档保留 | 已完成 | `ProjectWindow` 标签、`ProjectSession::retainDocument`、`MapEditorView` | `ProjectWindow` 486 断言 |
| 资产库独立、可被多个工程共享 | 已完成 | `project::AssetLibrary`（`.tbassets` 目录文件）、`AssetLibraryWindow` | `AssetLibrary` 测试、库关联用例 |
| 浏览/预览不捕获，使用才嵌入 | 已完成 | `CatalogAssetDrop`、`ProjectSession::buildCapturePlan/applyAssetCapturePlan` | 拖放/捕获用例（`ProjectWindow`、`ProjectSession`） |
| 捕获带依赖闭包（模型配套、天空盒六面等） | 已完成 | `LibrarySync`、依赖图 | 依赖闭包与回滚用例 |
| 库更新提示 + 显式更新/忽略 | 已完成 | `AssetLibraryWatcher`、`ProjectLibraryDialog` | 库更新/忽略用例 |
| 编译使用冻结快照，按需打包 | 已完成 | `ProjectSession::exportCompileSnapshot`、`CompilationContext` | `ProjectCompilation` |
| 版本节点、比较、恢复（含安全版本） | 已完成 | `ProjectStore::checkpoint/compare/restore` | 恢复与回滚用例 |
| 未使用修订的显式清理 | 已完成 | `ProjectStore::unreferencedRevisions/pruneUnreferencedRevisions` | `ProjectStore` 210 断言 |
| 工程级自动恢复（崩溃后） | 已完成 | `ui/ProjectRecovery`、`ProjectWindow` 定时快照、`AppController::openProject` | `ProjectRecovery` 18 断言、`ProjectWindow` 集成用例 |
| MCP 工作区上下文（活动地图/显式目标/文档代次） | 已完成 | `MapEditingContext`、`McpInspect` 的 workspace 分页 | `McpWorkspaceContext` 79 断言、`McpPythonExecution` 867 断言 |
| 图片入库时转换（库内保存兼容纹理） | 已完成 | `AssetLibrary::publishCompatibleContent` + 资产库窗口 **Convert Images…**；捕获直接复用已发布的 WAD | `AssetLibrary` / `LibrarySync` / `ProjectSessionAssetLibrary` 定向用例 |
| 修改发布名而不破坏地图引用 | 已完成 | 工程格式升到 v3，`publishPath` 独立存储；`ProjectSession::renamePublishPath` 先写安全版本，再重写所有已关闭地图中的引用，最后更新发布路径；纹理会把单纹理 WAD 的 miptex 改名并重写面材质/补丁。Asset 菜单 **Rename Published Path…** | `ProjectStore`（`setPublishPath`、v1→v3 迁移）、`ProjectSession`（路径型引用改写、纹理 miptex 改名与面材质改写、审计变 Embedded、冲突回滚、打开地图/多纹理 WAD 拒绝） |
| 同名资源跨库冲突的图形化处理 | 已完成 | `ProjectSession::captureConflicts/captureLibraryAssetResolving` + 捕获时的替换确认框（拖放与 Add to Project 共用） | `ProjectSessionAssetLibrary`、`CatalogAssetDrop` 定向用例（报告冲突、取消不变、替换沿用原 ID） |
| 库目录移动后的重新指向 | 已完成 | `ProjectSession::relocateLibrary` + Library 菜单 **Relink Moved Library…**；校验库身份，已捕获快照不受影响 | `ProjectSessionAssetLibrary` 定向用例（校验同一库、拒绝其他库、保存重开） |
| 地图引用的库内补捕获（Sprite / WAV / 模型） | 已完成 | `ProjectSession::captureMapReferences` + Project 菜单 **Capture Map Resources…**：按引擎纹理名与游戏路径匹配已关联的库，逐项捕获并报告仍缺失的引用 | `ProjectSessionAssetLibrary` 定向用例（先 Missing，捕获后 Embedded，缺失项进 unresolved） |
| 游戏内画面验收（天空盒、发布产物） | **未完成（人工）** | 编译快照已生成 `gfx/env` 六面 TGA | 见第 4 节 |

## 3. 自动化验证现状

- 最后一次**全量**验证在 `50b995b77`：UI 120 项全部通过（1 项原生鼠标捕获按后台策略跳过），
  本地 CI preflight（PowerShell 7）通过，日志 `build-release-codex/codex-logs/cleanup-preflight7.log`。
- 之后的提交只做了定向验证，没有重跑全量：
  - `4b80ba2b8`：`AssetBrowserModel` + `ModelBrowserView` + `ProjectWindow` + `ProjectSession` 共 753 断言。
  - `c73648f23`：`ProjectRecovery` 18、`ProjectWindow` 486、`ProjectSession` 177 断言。
  - `aa187d5d0` / `9f152d2bf`：`ProjectCompilation + ProjectSession + ProjectWindow + McpWorkspaceContext` 779 断言、
    `McpPythonExecution` 867 断言、`TbProjectLibTest` 453 断言。
- 每次改动后都构建了 Release `TrenchBroom`。
- 明确未覆盖：没有用批准基线做像素差异判定；没有重复独立 Release 进程的 MCP 截图验收；
  `aa187d5d0` 之后的提交没有跑完整 preflight。

## 4. 必须人工验收的项目

以下内容无法由现有自动化测试替代，需要在真实桌面/游戏环境里由人确认。

### 4.1 桌面 UI 视觉

- 命令：`powershell -ExecutionPolicy Bypass -File scripts\ui-theme-acceptance.ps1 -Targets workbench,components -SkipBuild`
- 检查点：
  - 内容浏览器顶部的来源筛选与类型筛选在窄宽度下是否挤压面包屑/搜索框；
  - Light / Dark / Blender 三主题、100% / 150% / 200% 缩放下是否可读；
  - 欢迎页、工程窗口、独立资产库窗口、恢复询问对话框的布局。
- 通过标准：无控件重叠/截断，文字对比度可读，筛选框下拉可正常展开。

### 4.2 CS 1.6 端到端（真实游戏）

需要本机安装 CS 1.6 与 hltools。建议用一次性地图走完整链路：

1. 建一个资产库，放入 WAD、模型、Sprite、声音、天空盒六面。
2. 两个不同的 `.tbproj` 共享同一个库；各自把同一张纹理拖进地图，确认两份快照相互独立。
3. 保存工程、关闭重开；移走库目录后再打开工程，确认仍可浏览/编译已嵌入内容。
4. 运行编译，检查 `textures.wad`、模型/Sprite/声音、`gfx/env` 六面是否齐全。
5. 启动游戏跑图，确认材质、模型、声音、天空盒显示正确。
- 通过标准：游戏内无缺失材质/模型，天空盒方向正确，发布产物不依赖库路径。

### 4.3 真实崩溃恢复

1. 打开工程，修改一张地图并新增一个资源捕获，等待自动快照（或手动触发保存前的等待）。
2. 用任务管理器强制结束 `TrenchBroom.exe`。
3. 重新打开同一工程，确认出现恢复询问；选择恢复后检查未保存内容、`autosave/` 备份文件。
- 通过标准：恢复后内容与崩溃前一致，备份文件存在且可单独打开。

### 4.4 大工程性能

- 用几百 MB 资源的工程测量：保存耗时、自动快照期间 UI 是否明显卡顿、打开工程耗时。
- 通过标准：保存与快照不造成可感知的长时间冻结；若冻结明显，需要把自动快照改为可取消/限流。

### 4.5 MCP 独立进程验收（可选）

- 用独立 Release 进程 + 隔离配置 `TB_MCP_CONFIG_PATH` + 一次性地图，跑 `tb_inspect`/`tb_api`/`tb_execute_python`/`tb_capture`，
  确认工具发现载荷、截图与失败回执。历史记录见 `docs/mcp-python-migration/development.md`。

## 5. 已知限制（与设计文档同步）

- 地图仍以纹理名/发布路径引用资源；模型的发布路径、纹理的 miptex 名都可用
  **Rename Published Path…** 重命名，命令会先写安全版本并同步改写已关闭地图中的引用。
  纹理必须是单纹理 WAD（多纹理 WAD 会被拒绝并提示拆分）；打开中的地图也会被拒绝，
  避免与未保存编辑互相覆盖。
- 未转换的图片仍在捕获进工程时转换；库内转换后的结果是新的库修订，转换参数目前只能通过重新
  转换来修改。
- 同名资源跨库冲突会在捕获时弹出替换确认（Replace / Cancel）；工程不会同时保存两个发布名
  相同的资源，替换沿用原资源 ID 并把旧修订留在历史里。
- 库目录移动后可用 Library 菜单的 **Relink Moved Library…** 指向新位置（只接受同一库身份）。
- SPR/WAV 本身是单文件格式，没有可枚举的外部依赖；地图实体引用（model、message 等）可用
  **Capture Map Resources…** 从已关联的库补捕获。纹理按引擎名匹配，库内的多纹理 WAD 会逐张
  成为独立工程 WAD。
- 未实现增量保存、只读工程模式和面向 CS 1.6 的完整发布闭环验收。

## 6. 建议验收顺序

1. 自动：定向测试 + Release 构建（已随每次提交完成）。
2. 视觉：4.1 的快照矩阵（脚本可跑，判定需要人）。
3. 游戏内：4.2 的一次性地图链路。
4. 恢复与性能：4.3 / 4.4。
5. 把结果填进下面的记录表，并把新的限制补回第 5 节。

## 7. 验收记录（待填写）

| 日期 | 验收项 | 提交 | 结果 | 备注 |
| --- | --- | --- | --- | --- |
|  | 4.1 桌面 UI 视觉 |  |  |  |
|  | 4.2 CS 1.6 端到端 |  |  |  |
|  | 4.3 真实崩溃恢复 |  |  |  |
|  | 4.4 大工程性能 |  |  |  |
|  | 4.5 MCP 独立进程 |  |  |  |
