# GoldSrc 地形材质工具与资产库流水线设计

更新日期：2026-11-05 · 状态：自定义材质主流程、材质包预生成、预算与切分分析已接通；共享纹理会按源贴图和笔迹选择生成方式，完整人工验收仍待完成。

本文定义基于「资产库（`AssetLibrary`）→ 工程快照（`.tbproj`）→ 右侧地形面板（`TerrainInspector`）→ 编译快照（`TerrainCompile`）」的完整地形材质编辑、预览与编译流程。当前实现支持 Half-Life Valve 220 工程地图，后续章节中的设计目标不等同于已完成的功能。

### 当前实现与未完成项

- 已支持在资产库中创建三槽材质，也可多选三张贴图后从右键菜单创建；按资产 ID 捕获贴图依赖，在工程中绑定并涂刷，按捕获的贴图生成统一调色板、底纹与局部混合图集。贴图使用 64、128 或 256 像素的正方形不透明图像。
- 已支持笔刷半径、强度和软硬度，按源贴图平均颜色显示权重预览；素材或依赖关系变更会使相关地图的编译缓存失效。资源审计识别地图中的材质引用并阻止误删。
- 资产库创建材质时会保存包含三张归一化底图、共享调色板、三张底纹和三张固定双材质过渡纹理的 `compatible` 预生成包，每张输出纹理均包含四级 MIP。旧版只含底图与调色板的包仍可读取；工程烘焙时核对包完整性及当前捕获贴图的像素，缺失、损坏或不匹配时重新计算。
- 在 100% 密度下，当过渡两侧的源图均为单色、笔迹及其过滤范围内满足连续权重约束、UV 质量达标时，可复用固定过渡纹理。带纹理细节的源图采用按地形区域坐标对齐的方案，以保持图案方向。
- 自定义材质在 100% 烘焙密度下，还可按实际笔迹烘焙与地形区域坐标对齐的 `128 × 128` 共享混合块，相同内容跨周期复用，同一周期内相邻切面可合并；这种块在周期边缘留有 8 像素的 MIP 缓冲。三种材质同时参与的三角面、不能采用共享方案的切面以及 50% 和 200% 密度使用局部图集。只有实际地形 WAD 小于局部方案，且预算和生成 Brush 校验均通过时，才选用共享方案。
- 面板可配置每个区域的新增顶面数、地形 WAD 大小上限、最小顶面宽度和局部烘焙密度（50%、100%、200%）；可分析源顶面、生成顶面、贴图数、该区域单独打包时的 WAD 大小和最窄面位置，并在视口显示切分线框与过窄面。面宽按生成顶面在世界水平面的最窄宽度计算，适用于全部输出面，原始地形过窄也会报告；共享方案不满足约束时回退至局部方案，最终方案超限会阻止生成该区域的编译快照。WAD 上限不包含其他区域或地图资源。
- §5 的实际地图、游戏内人工验收仍未完成；带纹理细节的源图尚不能任意旋转复用固定过渡纹理。

---

## 1. 现状与演进目标

### 1.1 原生首版已实现操作（MVP）

1. 将地形 Brush 编成一组，选中该组，在「地形」面板点击「创建地形区域」。区域以创建时的组内坐标为基准，最大覆盖 1280 × 640 地图单位。每个 Brush 直接属于该组，且只有一个朝上的顶面。顶面在 XY 上不能重叠，Brush 必须位于自己的顶面投影内。
2. 选择草、泥或岩，调整笔刷半径，开启「涂刷地形」，在 3D 视图用左键拖动。视口显示权重色块，一次拖动对应一次撤销。按 Escape 可取消正在进行的笔划。
3. 进入地形组后用原生裁剪工具修改 Brush。原地保留的笔迹会投到当前顶面，裁掉的部分由地形边界遮住。保存工程时，组内权重随地图修订写入 `.tbproj`。
4. 保存工程并运行工程编译配置，地形结果会自动用于编译。也可在命令行直接运行 `hltools compile --project <工程.tbproj> --map <地图名> --output <地图.bsp>`。面板中的「导出编译快照」用于需要保留编译输入或排查问题时。

保存时只为发生变化的地图重新生成地形编译数据，并将结果连同源地图修订和生成器版本保存在 `.tbproj` 中。`hltools` 读取同一份结果，临时准备编译输入并默认把使用的纹理嵌入 BSP。地形结果缺失、过期或生成失败时直接报错；用新版 TB 打开并保存工程可重新生成。包含地形编译数据的工程使用格式版本 4，需使用支持该版本的 TB 与 `hltools`。

默认材质使用内置示例草、泥、岩底纹；也可以选择工程捕获的自定义三槽材质。下文保留完整设计，具体完成情况以上面的状态为准。

---

## 2. 资产库与工程化材质流水线架构

为了避免传统 GoldSrc 散装 `.wad` 路径依赖，地形材质完全复用现有的 `AssetLibrary`（`.tbassets`）与 `ProjectStore`（`.tbproj`）生态，通过 `project::AssetType::Material` 将「底材挑选与混合」同「地图地形涂刷」衔接起来：

```
[1. 资产库 AssetLibrary]
  挑选 3 张匹配的 Texture 资产
  ──(合成并校验)──> 生成地形混合材质 (AssetType::Material)
                    + 记录 3 条 AssetDependency 依赖边

[2. 工程捕获 ProjectSession::buildCapturePlan]
  捕获该 Material 资产进入 .tbproj
  ──(依赖闭包事务)──> 自动将 Material 及其依赖的 3 张 Texture 一并拷入工程快照

[3. 地图地形面板 TerrainInspector]
  从当前工程已捕获的 Material 列表中选择地形混合材质
  ──(绑定并涂刷)──> TerrainRegion 记录 materialAssetId 与 3 通道顶点权重

[4. 原生 C++ 烘焙与编译快照 TerrainExport / TerrainCompile]
  读取工程内 Material 的 3 张源贴图与共享调色板
  ──(自动切分与烘焙)──> 输出凸子 Brush + 共享过渡带 + 局部烘焙图集 (WAD)
```

### 2.1 在资产库中合成地形混合材质（`AssetType::Material`）

在 GoldSrc 中，3 张贴图要实现无缝过渡，必须满足**尺寸周期对齐**与**256 色共享调色板**两个硬约束。这一步放在资产库中完成：

- **入口与分类**：
  - 在 [`AssetLibraryWindow`](../lib/TbUiLib/src/AssetLibraryWindow.cpp) 左侧分类栏启用 `Material`（地形材质）分类。
  - 支持点击「新建地形混合材质…」或在 `Texture` 视图下多选 3 张贴图右键选择「合成为地形材质…」。
- **匹配校验与归一化**：
  - 基准纹素周期为 `128 × 128`（对应 `TerrainRegion` 的标准平铺周期）。
  - 若所选 `Texture` 尺寸为 `64 × 64` 或 `256 × 256`，合成器在生成兼容内容时自动平铺（tile）或双线性重采样至 `128 × 128`，并在合成对话框中提示尺寸归一化策略。
- **数据存储与预烘焙产物**：
  - **`nativeFormat`**：标记为 `"goldsrc-terrain-material"`。
  - **`recipe`（JSON）**：记录 3 个材质槽位的显示名称、引用的源 `libraryAssetId`、主色调（用于 3D 视口快速着色预览）及采样参数。
  - **`compatible`（预生成包）**：保存 3 张归一化底图、**256 色混合调色板（Palette）**、3 张输出底纹（`base[0..2]`）和 3 张固定的双材质共享过渡纹理（`pair[0..2]`）的四级 MIP。版本 2 包含全部数据，版本 1 按需生成缺失的 MIP；两种版本均校验完整性和当前捕获的源图。源图或调色板改变时，不复用旧的预生成结果。六张纹理的 MIP 索引共增加 130,560 字节的材质包体积。
  - **依赖关系（`AssetDependency`）**：在 `.tbassets` 中写入 3 条依赖边（`kind = "terrain-slot-0"`, `"terrain-slot-1"`, `"terrain-slot-2"`），指向所选的 3 张 `Texture` 资产。
  - **缩略图**：在资产库卡片上直接渲染三材质过渡预览图，方便直观检索。

### 2.2 捕获到工程（`.tbproj` 依赖闭包）

- 当用户在工程资源窗口或资产库窗口将某个 `AssetType::Material` 捕获（Capture）到 `.tbproj` 时，[`ProjectSession::buildCapturePlan`](../lib/TbUiLib/include/ui/ProjectSession.h#L276) 会沿着 `AssetDependency` 依赖边，**在同一个数据库事务中将该 `Material` 及其依赖的 3 张 `Texture` 一并捕获进工程**（放入 `"materials/..."` 与 `"textures/..."` 虚拟目录）。
- 工程保存后完全自包含：即便脱离外部资产库目录，工程依然拥有完整的混合材质定义、底图快照与编译能力。
- 若后续资产库中的底图更新，工程可通过现有的 `checkLibraryUpdates` / `updateLibraryAssets` 机制同步更新，并自动触发关联地图的地形编译缓存重算。

### 2.3 右侧「地形」面板绑定与涂刷

面板与 Outliner、插件面板同级。选择已登记的地形组时显示区域参数，选择普通 Brush 组时提供「创建地形区域」。

| 区块 | 内容 |
| --- | --- |
| 区域与材质包 | 地形组状态、**工程地形混合材质下拉框（`AssetType::Material`）**、可刷面范围 |
| 涂刷 | 3 个动态材质槽（名称与颜色随所选 `Material` 资产变化）、笔刷半径、强度和边缘软硬 |
| 切割预算 | 最大新增顶面数、最小面宽、WAD 预算、局部烘焙纹素密度 |
| 预览与应用 | 源顶面、预计新增顶面、预计纹理体积、异常位置和导出编译快照按钮 |

- **向后兼容默认材质**：下拉框默认提供内置的 `Default (Grass / Dirt / Rock)` 选项（对应 `materialAssetId = ""`），无需预先创建材质资产即可直接体验涂刷并兼容已有测试与旧地图。
- **绑定工程材质**：当选择工程内的某个 `AssetType::Material` 时，[`TerrainRegion`](../lib/TbUiLib/include/ui/TerrainRegion.h) 将该资产的工程 ID 记入 `_tb_terrain_paint`（版本 2 编码格式：`2 <originX> <originY> <materialAssetId|-> <hexWeights>`，同时兼容解码版本 1）。设置面数或 WAD 预算时使用版本 3：`3 <originX> <originY> <materialAssetId|-> <maxAddedFaces> <maxWadKiB> <hexWeights>`，预算 `-1` 表示不限；启用最小面宽或修改局部密度时使用版本 4：`4 <originX> <originY> <materialAssetId|-> <maxAddedFaces> <maxWadKiB> <minFaceWidth> <densityPercent> <hexWeights>`。版本 4 的面宽 `0` 表示关闭，密度为 50、100 或 200。旧版地形工具需要支持相应版本才能编辑带新设置的区域；不使用新设置时仍写入原有格式。
- **视口预览**：拖动涂刷时，3D 视口根据所绑定 `Material` 的 3 个槽位主色调实时混合显示顶点权重；点击「分析地形」后可叠加显示切分线框（底材、共享过渡带、局部烘焙块及过窄面），区域内容或参数变化后需要重新分析。

---

## 3. 编辑与生成契约

### 3.1 职责划分

| 数据 | 谁负责 | 保存什么 |
| --- | --- | --- |
| 地形混合材质 | 作者在资产库挑选 3 张贴图合成并捕获入工程 | `AssetType::Material` 快照、3 张源贴图依赖、共享调色板与预生成纹理 |
| 源地形 | 作者用普通 Brush 工具制作和裁剪 | 地图中的 Brush 组和可涂刷面范围 |
| 材质笔迹 | 作者在 3D 视图刷 3 个槽位材质 | 区域局部坐标里的权重、绑定的工程 `materialAssetId` |
| 生成方案 | TB 在保存工程或导出快照时计算 | 经确认的源地形、权重、生成切面与烘焙 WAD、源修订与生成器版本标识 |

创建区域时选一个普通 Brush 组，确认可刷顶面。首版支持在区域局部 XY 平面上，每个位置最多对应一个可刷表面的地形。坡折和外轮廓来自源 Brush。作者可以另外用普通贴图工具处理侧壁、台阶立面及悬挑。

刷材质只改权重，切换混合材质只改绑定的 `materialAssetId`。用裁剪工具时，选中和裁剪的是源 Brush。裁掉的笔迹由当前地形范围遮住，剩余区域保留原位置的权重。新裁出的侧面沿用普通贴图流程。撤销裁剪时，几何恢复后原笔迹重新显现。移动整组时，局部坐标随组移动。改高程时，重叠范围内的权重投到新顶面，新增区域使用槽位 0 默认底材。

### 3.2 原生 C++ 生成与保存流程

1. **提取与校验**：从源 Brush 提取可刷平面和权重覆盖范围。每个源 Brush 独立裁切，所有子 Brush 必须是有效凸体，覆盖源 Brush 体积并共享边界。编译快照只输出其中一套地形实体。
2. **纯材质与共享过渡带**：单一材质保留原面（若所绑定 `Material` 的对应槽位具备独立工程贴图，也可直接复用，否则输出 `tbt<group>_<mat>`）。内置材质使用按过渡方向映射的共享带及 UV 奇异值校验。自定义材质可对两张单色源图复用固定过渡纹理，条件是笔迹在周围两格过滤范围内与同一渐变一致且 UV 质量达标；其他情况在满足周期内边距和密度条件时，按实际笔迹生成与地形区域坐标对齐的共享块，保留源纹理方向。比较完整共享和局部方案的实际地形 WAD，并检查新增面数、最小面宽、WAD 预算和 Brush 有效性后选择方案。
3. **局部烘焙与图集打包**：三材质交汇（`junction`）、急弯和未通过 UV 约束的候选（`fit`）使用局部烘焙。优先合并 `2 × 2` 繁忙块以减少碎面。导出时基于所绑定 `Material` 的 3 张源贴图像素与共享调色板进行加权采样，100% 密度下的烘焙块为 `64 × 64`（含 8px gutter）；50% 和 200% 密度分别使用 `32 × 32`（4px gutter）和 `128 × 128`（16px gutter）。同一调色板的块打包到最大 `256 × 256` 图集，各级 MIP 分别保留块边缘；引用面的 UV 随密度缩放并按图集位置平移。
4. **缓存失效与资源审计联动**：
   - `.tbproj` 按地图资源保存编译快照 [`TerrainCompile`](../lib/TbProjectLib/include/project/ProjectStore.h#L45)。
   - 地图自身的 `sourceRevision` 与 `TerrainGeneratorVersion` 由 [`ProjectSession::terrainCompileNeedsSave`](../lib/TbUiLib/src/ProjectSession.cpp#L1059) 检查；材质、源贴图或依赖关系变化时，工程存储会直接清除相关缓存，以便外部编译器也能发现需要重算。
   - [`ProjectResourceAudit`](../lib/TbUiLib/src/ProjectResourceAudit.cpp) 识别地图 `_tb_terrain_paint` 对工程 `Material` 资产的引用，纳入工程引用统计与防误删保护。

现有接入点包括 [Brush 裁切](../lib/TbMdlLib/include/mdl/Brush.h#L110)、[地形区域模型](../lib/TbUiLib/include/ui/TerrainRegion.h)、[地形面板](../lib/TbUiLib/include/ui/TerrainInspector.h)、[地形烘焙导出](../lib/TbUiLib/include/ui/TerrainExport.h)、[工程编译快照](../lib/TbUiLib/src/ProjectSession.cpp#L731)和 [WAD 写入](../lib/TbProjectLib/include/project/GoldSrcWad.h#L77)。

---

## 4. 分阶段实施路线图

### 阶段一：原生 C++ 烘焙器下沉与 `TerrainRegion` 协议扩展
- **目标**：移除 [`TerrainExport.cpp`](../lib/TbUiLib/src/TerrainExport.cpp) 对 `QJSEngine` 和 `terrain_baker.qrc` 的运行时依赖，将 `multi.js` 的网格分类、UV 奇异值校验、MIP 采样与图集打包重写为原生 C++；扩展 `TerrainRegion` 编码格式支持可选的 `materialAssetId`（向后兼容版本 `1`）。
- **涉及文件**：
  - `lib/TbUiLib/include/ui/TerrainRegion.h` / `lib/TbUiLib/src/TerrainRegion.cpp`
  - `lib/TbUiLib/include/ui/TerrainExport.h` / `lib/TbUiLib/src/TerrainExport.cpp`
  - `lib/TbUiLib/CMakeLists.txt`
- **验证**：运行 `TbUiLibTest` 中的 `"Project terrain*"` 测试用例，确认切面生成、面积覆盖校验与 WAD 输出完全一致。

### 阶段二：资产库合成地形混合材质（`AssetType::Material`）与工程捕获
- **目标**：在资产库中支持挑选 3 张 `Texture` 合成为 `AssetType::Material`，自动归一化尺寸、生成共享调色板与过渡预览图、写入 `AssetDependency` 依赖边，并支持一键连同依赖捕获进 `.tbproj`。
- **涉及文件**：
  - `lib/TbProjectLib/include/project/AssetLibrary.h` / `lib/TbProjectLib/src/AssetLibrary.cpp`
  - `lib/TbUiLib/src/AssetLibraryWindow.cpp`（启用 `material` 分类过滤、新增合成地形材质对话框与缩略图渲染）
  - `lib/TbUiLib/src/ProjectSession.cpp`（完善 `AssetType::Material` 的捕获、落盘与依赖同步）
- **验证**：在 `TbProjectLibTest` 与 `TbUiLibTest` 中新增测试，验证合成 `Material` 资产、写入 3 条贴图依赖边、并通过 `buildCapturePlan` 一次性捕获到 `.tbproj`。

### 阶段三：右侧 `TerrainInspector` 接入工程材质、预算控制与审计闭环
- **目标**：在 `TerrainInspector` 中提供工程 `Material` 选择器、笔刷强度/软硬调节与统计信息展示；将 `bakeTerrainSnapshot` 与工程内的 `Material` 像素数据打通；在 `ProjectResourceAudit` 与 `terrainCompileNeedsSave` 中接入 `materialAssetId` 追踪。
- **涉及文件**：
  - `lib/TbUiLib/include/ui/TerrainInspector.h` / `lib/TbUiLib/src/TerrainInspector.cpp`
  - `lib/TbUiLib/src/ProjectSession.cpp` / `lib/TbUiLib/src/ProjectResourceAudit.cpp`
- **验证**：运行 `scripts\check-ui-style-governance.ps1`、`TbUiLibTest` 全套工程与地形测试，并编译 Release `TrenchBroom` 目标验证完整闭环。

---

## 5. 可复现的裁剪样例

在仓库根目录运行以下命令。样例把一段泥路刷过将要裁剪的位置，再用同一份权重和 WAD 生成斜坡与削平后的平台。

```powershell
$terrainExample = Join-Path $env:TEMP 'tb-terrain-clip-example'
node tools/map/terrain-material-demo/clip-example.cjs $terrainExample
Set-Location $terrainExample
hltools compile -threads 2 -noembedsource terrain_painted_before.map
hltools compile -threads 2 -noembedsource terrain_painted_after.map
```

| 在 TB 中依次打开 | 观察内容 | 地形顶面 |
| --- | --- | ---: |
| `terrain_source_before.map` | 整块原始斜坡 | 1 |
| `terrain_source_after.map` | 高处削成平台，坡折在 X=640、Z=40 | 2 |
| `terrain_painted_before.map` | 草、泥、岩笔迹与裁剪前的切面 | 105 |
| `terrain_painted_after.map` | 同一笔迹跨过坡折，生成面沿坡折分开 | 106 |

如果 TB 里还没显示纹理，把同目录的 `terrain_clip.wad` 加入地图的 WAD 列表。已用 `hltools` 编译四张地图。两张带笔迹的 BSP 分别是 137 和 144 个面，共用一份 0.486 MB 的 WAD，包含 3 张基础纹理、1 张共享过渡和 5 张图集。样例的裁剪线放在涂刷网格线上，生成切面与贴图使用 Web Demo 的算法。

原生 C++ 自定义材质的封闭测试工程可由回归测试导出，再交给支持工程地形快照的 `hltools` 编译：

```powershell
$env:TB_TERRAIN_ACCEPTANCE_PROJECT = Join-Path $env:TEMP 'tb-terrain-material-acceptance.tbproj'
build-release-codex\lib\TbUiLib\test\TbUiLibTest.exe 'Project terrain bakes captured material and invalidates changed textures'
hltools compile --project $env:TB_TERRAIN_ACCEPTANCE_PROJECT --map Terrain --output (Join-Path $env:TEMP 'tb-terrain-material-acceptance.bsp') -threads 2
Remove-Item Env:TB_TERRAIN_ACCEPTANCE_PROJECT
```

2026-11-04 已用本机 `hltools 1.24.0` 编译该自定义材质工程，得到 BSP 30、28 个可见面及 4 种 hull；检查确认两张生成的地形贴图和一张源贴图已嵌入 BSP。该检查不替代游戏内验证。

共享混合纹理的封闭测试工程也可由回归测试导出。以下命令每次使用新的文件名：

```powershell
$env:TB_TERRAIN_SHARED_ACCEPTANCE_PROJECT = Join-Path $env:TEMP ("tb-terrain-shared-" + [guid]::NewGuid().ToString('N') + '.tbproj')
build-release-codex\lib\TbUiLib\test\TbUiLibTest.exe 'Project terrain chooses shared tiles only within budget and with WAD savings'
hltools compile --project $env:TB_TERRAIN_SHARED_ACCEPTANCE_PROJECT --map Shared --output ([System.IO.Path]::ChangeExtension($env:TB_TERRAIN_SHARED_ACCEPTANCE_PROJECT, '.bsp')) -threads 2 -noembedsource
Remove-Item Env:TB_TERRAIN_SHARED_ACCEPTANCE_PROJECT
```

2026-11-05 的该测试样例中，共享方案将区域 WAD 从 1,098,984 字节降至 857,936 字节；本机 `hltools` 生成的 BSP 30 含 268 个面、4 种 hull 和 12 张嵌入纹理，编译无泄漏或警告。实际节省取决于笔迹和地形，游戏内效果仍需人工验收。

如需验证固定过渡纹理，可用同一回归测试生成另一张地图：

```powershell
$env:TB_TERRAIN_PAIR_ACCEPTANCE_PROJECT = Join-Path $env:TEMP ("tb-terrain-pair-" + [guid]::NewGuid().ToString('N') + '.tbproj')
build-release-codex\lib\TbUiLib\test\TbUiLibTest.exe 'Project terrain chooses shared tiles only within budget and with WAD savings'
hltools compile --project $env:TB_TERRAIN_PAIR_ACCEPTANCE_PROJECT --map Bands --output ([System.IO.Path]::ChangeExtension($env:TB_TERRAIN_PAIR_ACCEPTANCE_PROJECT, '.bsp')) -threads 2 -noembedsource
Remove-Item Env:TB_TERRAIN_PAIR_ACCEPTANCE_PROJECT
```

2026-11-05 已用本机 `hltools 1.24.0` 编译此样例；BSP 30 含 104 个面、4 种 hull 和 10 张嵌入纹理。回归测试核对了固定过渡纹理的四级 MIP 确实出现在导出快照中。

原生版本人工验收时，在同一个 `.tbproj` 内完成「从资产库合成地形材质、捕获入工程、绑定并刷材质、裁剪、撤销、重做、保存重开、重编」。检查坡折两侧的贴图接缝、输出 Brush 的体积与边界、CS 1.6 内的行走碰撞，以及同一输入重复应用的面数和纹理一致性。
