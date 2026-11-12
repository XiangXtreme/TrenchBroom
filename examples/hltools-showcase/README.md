# hltools 交互实验室

这是本分支内置 **hltools** 的可编辑教学工程。打开
[`hltools-showcase.tbproj`](hltools-showcase.tbproj)，从工程内容中依次打开地图。每个实验
都有现场中文说明牌、真实的画刷或实体，以及可重复的观察方法。地图、原创纹理和说明牌
均已打包进工程；打开工程和使用内置编译器无需另外安装 WAD。

建议先复制一份 `.tbproj` 再动手。工程使用 **Half-Life / Valve 220** 地图格式；使用
支持本分支工程格式和 hltools 的 TrenchBroom 打开。需要在 GoldSrc 游戏中检查门、触发器、
最终天空和玩家碰撞时，还需自行准备相应游戏。编辑器的 Play 模式不能代替游戏实体逻辑。

## 从哪里开始

1. 打开 **00 实验室导览**，查看实验分类。随后从工程内容打开编号地图；各地图是独立
   工作台，不需要从导览地图走到其他地图。
2. 在 3D 视口靠近说明牌和展品；选中对象后，在实体属性中查看实际的 `classname` 和键值。
   某些编译器专用键可在属性表中查看或编辑。必要时切换 2D 视图定位下列坐标。
3. 对需要比较编译结果的实验，选择 **Run → Compile Map… → 内置 hltools**，使用相同
   质量、输出路径和参数分别编译。默认输出及日志位于工程旁的 `compiled` 文件夹。
   可以先复制第一次输出再做第二次实验。
4. 光照实验也可在 3D 视口用灯泡按钮烘焙预览；预览用于交互，不生成游戏碰撞 hull。
   检查碰撞、门户、最终光照及游戏实体时，以完整编译及目标游戏为准。

地图中的 A / B 标记和坐标均以源地图为准。观察编译后变化时，不要期待源画刷自动改变；
很多设置只影响 BSP、碰撞树、PVS 或烘焙光照。

## 地图与实验索引

| 地图 | 主题 | 主要对象 / 纹理 | 可验证的结果 |
| --- | --- | --- | --- |
| 00 | 导览 | 分类说明牌 | 选择接下来的工作台 |
| 01 | 灯光 | `light`、`light_spot`、`info_texlights`、`LAMP` | 聚光与贴图光，反弹、AO 和阴影 |
| 02 | 细节 CSG | `func_detail`、`zhlt_detaillevel`、`zhlt_chopdown`、`zhlt_chopup`、`zhlt_coplanarpriority` | 相交及共面画刷的编译差异 |
| 03 | 碰撞 | `zhlt_cliptype`、`zhlt_noclip`、`zhlt_clipnodedetaillevel` | 碰撞模式和可穿越画刷 |
| 04 | 工具纹理 | `HINT`、`SKIP`、`CLIP`、`NULL`、`NOCLIP` | 门户切分、不可见碰撞与隐藏面 |
| 05 | 可见性 | `info_portal`、`info_target` | 双向、单向和断开链接的 PVS |
| 06 | 画刷实体 | `func_door_rotating`、`trigger_multiple`、`func_group`、`func_detail`、`func_wall`、`ORIGIN`、`AAATRIGGER` | 旋转轴、触发关系和编译后的模型 |
| 07 | 天空 | `SKY`、`light_environment` | 太阳方向、遮阳与投影 |
| 08 | 光照配置 | `info_minlights`、`info_compile_parameters` | 按材质设置最低光和参数优先级 |
| 09 | 诊断 | 点线 `.pts` | 找到并修复故意制作的漏空 |

## 01 · 光照与贴图光

左侧天花板使用 `LAMP`。选中附近的 `info_texlights`，其中 `LAMP = 255 205 130 150`
指定该贴图的发光颜色和亮度。移除该键或实体并重新烘焙，可比较贴图光对周围表面的贡献；
发光材质本身不是会移动的游戏实体。

蓝色挡板和柱体用于观察阴影、角落以及反弹光。右侧有一个朝下的 `light_spot`：改变
`_cone`、`_cone2` 或 `_light` 后重新烘焙，比较照亮区域。修改点光源的 `_light` 可
比较颜色和亮度。建议从小变化开始，并保留一张原始结果。

使用内置编译任务的**标准**质量，在附加参数中分别比较：

| 实验 | 参数 A | 参数 B | 观察 |
| --- | --- | --- | --- |
| 反弹光 | `-bounce 0` | `-bounce 8` | 遮挡背面和间接光 |
| 环境遮蔽 | 不添加 | `-ao -aoscale 64 -aostats` | 柱脚、墙角及日志统计 |
| 阴影采样 | 不添加 | `-pcf 2` | 阴影边缘，采样增加也会增加耗时 |
| 模糊约束 | 不添加 | `-blurclamp 0.75` | 明暗边缘的混合 |

视口预览的质量菜单与编译对话框的附加参数是两套入口。AO、`-pcf` 大于 1 或
`-lightcull` 与 GPU 同时启用时，直接光采集会回退到 CPU；可从日志确认实际路径。

## 02 · func_detail 层级、切割和共面

从左到右四站位于 `x=-800、-256、256、800`。A 排中心 `y=-144`，B 排中心 `y=144`；
每站都有蓝色基体和橙色相交体，实体名称如 `02_b_cutter`。选中画刷实体检查：

| 站 | A 排 | B 排 | 比较重点 |
| --- | --- | --- | --- |
| 01 | 两个 `detaillevel=1` | 橙色体为 2 | 不同细节层级 |
| 02 | 橙色体 `chopdown=0` | 橙色体 `chopdown=1` | 降低切割体的有效层级 |
| 03 | 蓝色体 `chopup=0` | 蓝色体 `chopup=1` | 提高被切割体的有效层级 |
| 04 | 蓝色共面优先级为 1 | 橙色共面优先级为 1 | 同层共面重叠时的表面取舍 |

对应的完整键名前缀都是 `zhlt_`。编译器比较“切割体层级 − chopdown”和“被切割体
层级 + chopup”。每次只改一个字段重新编译；在附加参数加入 `-dumpintermediates`
可以保留 `.p0` 等中间文件，辅助比较 CSG 结果。`func_detail` 用于细节，不要用它封闭
地图；效果取决于画刷是否相交和具体编译场景。

## 03 · 碰撞模式与穿越

前四组台阶依次使用 `simple`、`legacy`、`legacytrim`、`normalized` 的
`zhlt_cliptype`。`legacytrim` 是此分支的连续外表面碰撞模式。比较编译日志中的
hull / clipnode 数据，并在目标游戏沿台阶和接缝行走；简单台阶不保证所有模式产生
肉眼可见或手感明显不同的结果。

`x=480` 处 A / B 两块橙色画刷外观一致：A 保留碰撞，B 设置 `zhlt_noclip=1`。
`x=800` 处比较 `zhlt_clipnodedetaillevel` 对相交画刷碰撞树的影响；这个键不是
“可穿越”开关。

## 04 · HINT 与工具纹理

从起点穿过两个错开的门洞。第一道门洞 `x=-800` 有一片薄画刷：一面是 `HINT`，
其余五面是 `SKIP`。`HINT` 提示 BSP 分割平面；`SKIP` 舍弃不需要的工具面，二者
都不能代替封闭地图的实体墙。

1. 标准质量编译，在附加参数填写 `-dumpintermediates`。
2. 在地图窗口选择 **File → Load Portal File…**，打开输出旁的 `04_tool_textures.prt`。
3. 复制 `.prt` 留作对照，把薄片上唯一的 `HINT` 面改为 `SKIP`，以相同参数重新编译，
   加载新 `.prt` 并比较。门户文件显示分割，不直接等于每个叶区最终的 PVS；增加 HINT
   不保证加快 VIS。

其余三个站点对应实际画刷：

| 纹理 | 位置 | 本例中的作用 |
| --- | --- | --- |
| `CLIP` | `x=-264..-248, y=-96..96` | 不绘制的玩家碰撞体；不能用于封闭世界 |
| `NULL` | `x=192..320, y=64..192` 方柱底面 | 省去不应显示的表面；不要贴在玩家可见的面上 |
| `NOCLIP` | `x=736..864, y=64..192` 橙柱底面 | 本编译器把该面按 NULL 处理，并关闭**整个画刷**的碰撞 |

工具纹理在编辑器中是可视辅助，编译结果和游戏中的可见性可能不同。碰撞请用完整编译
与目标游戏检查，而不是仅凭光照预览判断。

## 05 · info_portal 与 PVS

左房 `info_portal` 位于 `(-480, 0, 64)`，目标 `portal_east` 位于 `(480, 0, 64)`。
默认 `oneway=0`，`neighbor=0`，`targetneighbor=0`。两室之间的墙是实心的：这个
示例刻意让 PVS 差异容易检查，链接不会打通墙或创造实际视线。可在编辑器中穿过墙查看
右房，不要把它当成玩家可走通的门。

标准质量编译后，在本目录打开 PowerShell：

```powershell
python .\inspect_pvs.py .\compiled\05_portal.bsp --from -480 0 64 --to 480 0 64
```

如果编辑的是复制到其他目录的工程，请把 BSP 路径改为该副本旁的 `compiled` 文件夹。
脚本只依赖 Python 3。每次改动后都要重新编译：

| 设置 | A → B | B → A |
| --- | --- | --- |
| 默认双向 | visible | visible |
| `oneway=1` | visible | not visible |
| 删除或断开 `info_portal` | not visible | not visible |

`neighbor` 与 `targetneighbor` 控制相邻门户扩展层数，`reverse=1` 用于移除关系。
修改 PVS 可能导致物体消失，应在真实场景中谨慎测量和检查。

## 06 · 画刷实体与特殊纹理

五站从左到右排列；选中对象或查看 Outliner 中的实体名称：

| 站点 | 对象 | 观察 / 操作 |
| --- | --- | --- |
| 旋转门 | `func_door_rotating`、`ORIGIN` | `ORIGIN` 小画刷中心是 `(-720, 88, 64)`。移动它会改变编译后门模型的轴心；它不会作为普通可见实体面留下。 |
| 触发体 | `trigger_multiple`、`AAATRIGGER` | `showcase_trigger` 的 `target` 指向 `showcase_door`。触发与门的运行行为由目标游戏执行。 |
| 编辑归组 | `func_group` | `editor_group` 便于编辑；编译后画刷并入世界。 |
| 细节 | `func_detail` | `world_detail` 是可编辑的细节画刷；编译后并入世界，不能用作封闭墙。 |
| 画刷模型 | `func_wall` | `opaque_wall` 保留为独立画刷模型，`zhlt_lightflags=2` 请求遮光。静态遮光须重新烘焙观察。 |

`ORIGIN` 画刷属于门实体，不能单独当世界封闭画刷使用。`AAATRIGGER` 是编辑器中识别
触发体积的材质，实际触发规则仍由游戏实体及其属性决定。

## 07 · SKY 与环境光

本地图的顶部是 `SKY`，场景包含遮阳板和立柱。选中 `light_environment`，修改
`pitch` 或 `angles` 的偏航分量并重新烘焙，比较遮阳板下方与露天区域的光照、阴影。
天空面允许编译器计算环境光，最终背景还取决于游戏实际可用的天空资源和配置。
不要只靠编辑器预览判断目标游戏的天空效果。

## 08 · 最低光照与编译参数实体

两块柱子使用外观相同的 `MINLIT` 与 `MINBASE` 纹理。`info_minlights` 定义
`MINLIT=0.45`，不为 `MINBASE` 设置最低值。删除该键或实体后重新编译，比较较暗的面；
它设置的是对应材质的最低光照，不是新的光源。

`info_compile_parameters` 使用 `rad_bounce=0` 给 RAD 提供默认反弹次数。标准编译日志
中的 `bounces` 会显示实际值；在编译任务附加参数填写 `-bounce 1` 可覆盖实体设置。
注意质量预设和显式命令行参数也可能提供同名选项，查看日志确认最终值。

## 09 · 故意漏空的地图

该地图缺少东墙。编译应报告 `LEAK`，而不是生成正常 BSP。选择 **File → Load Point
File…** 打开输出旁的 `09_leak.pts`，沿点线查找从室内到虚空的位置。

用普通世界画刷补墙：`x=304..320`、`y=-256..256`、`z=0..256`，再编译至不再漏空。
不要用 `func_detail`、`CLIP`、`HINT` 或 `SKIP` 代替封闭世界的墙体。

## 重建与验证

日常使用只需要 `.tbproj`。源文件在本目录；`showcase-textures.wad` 是工程内纹理的
可重建源，图像由 `generate_textures.py` 原创生成，未使用第三方游戏纹理。重建需要
Python 3、Pillow、支持中文的本机字体，以及 `goldsrc-map-assets` 技能的 WAD 工具。
生成器复用仓库内 `examples/hltools-tutorial/generate.py` 的基础几何和当前 ProjectStore
数据库模式。字体位于非默认路径时，向纹理生成脚本传 `--font <字体文件>`。

在仓库根目录执行（临时目录和 WAD 输出需位于同一盘）：

```powershell
$assetTool = 'C:\Users\Xiang\.codex\skills\goldsrc-map-assets\scripts\goldsrc_asset_tool.py'
$work = 'build-release-codex\codex-logs\hltools-showcase'
python examples\hltools-showcase\generate_textures.py "$work\textures"
python $assetTool texture-audit "$work\textures"
$env:TEMP = (Resolve-Path $work).Path
$env:TMP = $env:TEMP
python $assetTool wad-build "$work\textures" "$work\showcase-textures.wad" --force
python $assetTool wad-inspect "$work\showcase-textures.wad"
python examples\hltools-showcase\generate.py --output "$work\hltools-showcase.tbproj" --wad "$work\showcase-textures.wad" --force
python examples\hltools-showcase\verify.py --worker build-release-codex\app\TrenchBroom\HltoolsLightmapWorker.exe --project "$work\hltools-showcase.tbproj" --wad "$work\showcase-textures.wad"
Copy-Item "$work\showcase-textures.wad" examples\hltools-showcase\showcase-textures.wad
Copy-Item "$work\hltools-showcase.tbproj" examples\hltools-showcase\hltools-showcase.tbproj
```

确认审计、打包、回读和验证均通过后再复制；复制会覆盖目标，请先备份已编辑的工程。
验证脚本检查工程和纹理完整性、编译九张正常地图、确认漏空和修复，并实际比较光照、
细节设置、碰撞、HINT、PVS、门的轴心、天空及编译参数。目标游戏中的显示、声音、实体
运行和手感仍需在对应游戏环境里检查。
