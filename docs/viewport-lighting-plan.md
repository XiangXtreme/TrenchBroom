# 视口光照预览（GoldSrc lightmap）移植方案

状态：提案，待评审
范围：TrenchBroom 3D 视口的「光照 / 光贴图」显示，数据由 `hltools` 在外部烘焙

## 1. 结论摘要

1. **光照数值一律来自 `hltools`（csg + bsp + rad），TB 自己不算任何光照。** 编辑器只负责采样与显示。
2. **以子进程方式调用，不把 csg/bsp/rad 链接进 `TrenchBroom.exe`。** 依据：`hltools` 的 `err::fatal()` 直接 `std::exit(1)`；而一次烘焙的总时长是秒级，进程启动开销可忽略。
3. **GPU 后端不作为默认。** 实测 `-fast`（bounce 0）时 GPU 反而慢一倍；默认档 GPU 只快 1.22×（24 线程对比）。
4. **TB 侧只做三件事**：加一套顶点 UV 通道、在 `Face.fragsh` 里乘光照、把每个 TB 面重新采样成自己的 atlas 矩形。
5. **不使用 alpha 通道标记有效位，也不做面签名比对。** `dface_t.lightofs == -1` 已经明确标出「无光照」，TB 用「每面一个矩形」自然获得增量失效能力。

## 2. 现状调研

### 2.1 外部工具链的通行做法

- 渲染端只需把 lightmap 采样乘到表面颜色上，并按面计算每顶点的采样位置。
- 光照数值全部由 `csg/bsp/vis/rad` 产生；编辑器不参与求值，只负责显示。
- 光贴图按面打包成一张 atlas，相邻面之间留空隙，边缘做若干轮缝补，避免线性采样越界。

### 2.2 TB 现状

| 能力 | 现状 |
|---|---|
| 光照渲染 | **无**。`lib` 内所有 `lightmap` 字样都在 Quake3 shader 解析器的 `surfaceparm` 测试里 |
| BSP/rad | **无**。`lib/TbCsLib/src/HullCompiler.h` 明示只编 clip hull，跳过 face clipping / portals / vis / **lighting、texinfo** |
| 光源语义 | **无**。`light` / `_sunlight` 只出现在 FGD 的属性类型里 |
| 纹理轴 | 有。`mdl::UvCoordSystem`（scale/offset/rotation）、`BrushFace::uAxis()/vAxis()/textureSize()` |
| 空间索引 | 有 `mdl::octree`，支持 ray 查询（当前用于拾取） |
| 面渲染 | `FaceShader`（GLSL 1.20，固定管线 attribute）、按材质合批、逐面吐出顶点 |
| 动画时间 | 有。`RenderContext::animationTime()` / `requestAnimation()` |

### 2.3 `hltools` 能提供什么

- `csg::run_csg(map_source, csg_options)`、`bsp::run_bsp(format::map_data&, base, options, bsp_input)`、`rad::run_rad(format::map_data&, base, rad_options)` —— 全部内存接力，`hltools compile` 就是这么串的。
- `format::get_face_extents(map, facenum, mins, maxs)`：引擎逐位一致的 luxel 尺寸计算。
- `format::build_lightmap_atlas(map, atlas)`：1024 宽、1 px 间距的确定性装箱。
- `dface_t{ planenum, side, firstedge, numedges, texinfo, styles[4], lightofs }`，`lightofs = -1` 表示无光。
- `texinfo_t.vecs[2][4]` 就是 lightmap 轴。
- `hltools compile --project <x.tbproj>` 能直接读 TB 项目并解出项目拥有的 WAD。
- 许可证：GPL-2.0-**or-later**，与 TB 的 GPL-3.0 兼容。

## 3. 实测数据

环境：Intel i9-12900HX（16C/24T，8P+8E）、Windows 11、RTX 4060 Laptop、`hltools` 1.24.0 Release。
测试图：`!kz_skyline`，**18,337 面 / 65,438 patches / 2.69 MB lightdata**（lightmap atlas 已达 64/64 页上限）。

### 3.1 时间矩阵

| 档位 | 线程 | 墙钟 | 平均并行度 |
|---|---:|---:|---:|
| CPU `-fast` | 8 | 7.20 s | 6.34 |
| CPU `-fast` | 24（auto） | **6.85 s** | 14.40 |
| CPU 默认（8 bounce） | 8 | 94.65 s | — |
| CPU 默认 | 24（auto） | **56.96 s** | 18.47 |
| GPU `-fast` | 8 | 14.43 s | — |
| GPU `-fast` | 24 | 18.62 s | 13.35 |
| GPU 默认 | 8 | 55.00 s | — |
| GPU 默认 | 24 | **46.75 s** | 9.53 |
| GPU `-extra`（12 bounce, 9× 采样） | 8 | 120.84 s | — |

其他：495 面小图 `compile -fast` 全流程 2.47 s；2 MB KZ 图 `csg` 单阶段 11.9 s。

**结论**：`-fast` 是唯一适合「编辑后自动重烘」的档位；默认档 57 s 只能手动触发；GPU 只在默认档有意义（47 s vs 57 s，1.22×），`-fast` 时反而慢（2.0×）。

### 3.2 CPU 利用率

在 24 逻辑核机器上指定 `-threads 8`，峰值只到 27.5%（理论 33%），平均 20.5% —— **看起来「只用 30%」是线程数上限造成的，不是没有并行**。不给 `-threads` 时：`-fast` 平均 53.2% / 峰值 68.6%，默认档平均 76.8% / 峰值 80.1%。`-fast` 上不去的原因是该档工作量小 + setup/装箱/收尾是串行阶段；默认档则接近饱和（24 线程测得 18.47 平均并行度，混合架构 E 核拖低了收益）。

### 3.3 GPU 保真度（CPU vs GPU 逐像素）

| 对比 | 平均绝对差 | 最大差 | 差 >2/255 的像素 |
|---|---:|---:|---:|
| `-fast` | 0.003 | 56 | 0.01% |
| 默认 | 0.011 | 65 | 0.04% |

差异集中在阴影边界。也就是说 `-gpu` 的「approximate」在实测中非常接近，**像素级对比可以作为回归测试手段**。

### 3.4 已探明的边界条件

1. **缺 WAD 会让 csg 硬失败**：`error: texture X not found in wad files`，退出码 1。放在 map 同目录的 WAD 会被自动探测。
2. **atlas 64 页上限**：真实 KZ 图已 100% 占满并告警 `lightmap atlas 100% full`。
3. **不同档位产出的 lit face 集合不同**：`-fast` 15,638 面 vs 默认 16,935 面。**atlas 与每面矩形必须每次烘焙重建，禁止跨档缓存 UV。**
4. `hltools lightmap` 导出的 BMP **不含**每面矩形清单，需要新增 manifest 输出（见 5.3）。

## 4. 总体架构

```
TB（内存中的 MapDocument）
  │ ① 导出临时 .map：复用现有序列化器；把项目 WAD 绝对路径写进 worldspawn.wad
  ▼
hltools.exe compile <tmp>.map -fast -threads N        [子进程，QProcess，可取消]
  │ ② 产出 <tmp>.bsp（lighting lump 已烘好）
  ▼
hltools.exe lightmap <tmp>.bsp atlas.png --manifest <tmp>.json   [需新增 ~80 行]
  │ ③ manifest：每 dface 的 plane / texinfo.vecs / mins / size / tile / lightofs / 多边形
  ▼
TB（渲染层）
  │ ④ 逐 TB 面做 luxel 合成 → 每面一个矩形 → 拼成 TB 自己的 atlas 纹理
  │ ⑤ GL_TEXTURE1 上传；顶点写入 lm UV
  ▼
Face.fragsh： min(albedo × texture2D(uLightmap, vLmUv), 1)
```

### 4.1 为什么是子进程

| 理由 | 说明 |
|---|---|
| 崩溃隔离 | `hltools` 的 `err::fatal()` → `std::exit(1)`；畸形地图不能带走编辑器 |
| 没有延迟收益 | 一次烘焙 7 s 起，进程启动 ~30 ms（0.4%） |
| 增量缓存同样可用 | `rad -incremental` 读写磁盘上的 `<map>.inc`，与进程边界无关 |
| 构建解耦 | `hltools` 有自己的 CMake、sqlite、zlib、可选 Vulkan，不宜并入 TB 的依赖图 |

### 4.2 TB 侧渲染改动（文件级）

| 文件 | 改动 |
|---|---|
| `lib/TbGlLib/include/gl/VertexAttributeType.h` | 新增 `GLVertexAttributeUvCoord1`（`clientActiveTexture(GL_TEXTURE1)`）+ `VertexAttributeTypes::Uv12` |
| `lib/TbGlLib/include/gl/VertexType.h` | 新增 `VertexTypes::P3NT2T2` |
| `lib/TbMdlLib/include/mdl/BrushRendererBrushCache.h:41` | `VertexSpec = P3NT2T2` |
| `lib/TbRenderLib/include/render/BrushRendererArrays.h:364` | `using Vertex = P3NT2T2::Vertex` |
| `lib/TbRenderLib/src/BrushRenderer.cpp`（`validateBrush`） | memcpy 之后按 `cachedFacesSortedByMaterial` 的面区间回填 lm UV |
| `lib/TbRenderLib/src/FaceRenderer.cpp` | 绑 atlas 到 `GL_TEXTURE1`，设 `Lightmapped/LightScale/LightGamma`，画完复位 |
| `app/TrenchBroom/resources/shader/Face.vertsh` | 传 `gl_MultiTexCoord1` 到 `varying vec2 LightmapUv` |
| `app/TrenchBroom/resources/shader/Face.fragsh` | 在 alpha-test 之后、`Brightness` 之前乘光照；`min(rgb,1)` |
| 新增 `lib/TbRenderLib/include/render/LightmapAtlas.h` | atlas 纹理 + 每 `BrushFace*` 绑定 + 版本号 |
| 新增 `lib/TbUiLib/include/ui/LightmapBakeRunner.h` | QProcess 封装、进度解析、取消、防抖 |
| `lib/TbRenderLib/include/render/RenderContext.h` | `showLighting()`、`lightingQuality()`、`lightingBrightness()` |
| Preferences / 菜单 | 视口模式开关 + 质量档 + 自动重烘 |

关键点：

- `P3NT2` 的其他使用者（`PatchRenderer`、`EntityDecalRenderer`、`UvView`）**保持不动**，首版对这些几何不做光照（fullbright）。
- 顶点回填放在 `BrushRenderer::validateBrush`（渲染层），因为 `BrushRendererBrushCache` 属于模型层，不应该知道 atlas 的存在。
- `Face.fragsh` 现有顺序是 `alpha-test → Brightness/2·2x → 灰阶 → tint → 角度调暗 → 雾 → 网格 → soft bounds`。光照乘法插在 `Brightness` **之前**，其余保持。
- **`LightScale` 必须为 1.0**：`rad` 已把 `-scale 2.0` / `-gamma 0.55` / `minlight` 烘进 lightmap，运行时再乘一次会双重曝光。TB 的 `Preferences::Brightness` 在光照模式下要绕开。
- 2D 视图与 3D 共用 `FaceShader`，必须在 2D 显式关闭 lightmap。

### 4.3 逐 luxel 合成算法

CSG 会把 TB 的一个大面切成多个 `dface`，所以不能 1:1 对应。TB 侧自己生成「每面一个矩形」：

对每个 TB 面 `F`（平面 `n,d`、vecs、多边形 `P`）：

1. 从 manifest 里筛候选：`dot(n_i, n) > 0.9999 && |d_i - d| < 0.5`。
2. 用 vecs 把 `P` 的顶点投影并 `/16`，`floor/ceil` 得到 `F` 自己的 `mins` 与 `size`。
3. 对每 luxel 中心求世界点 → 转进各候选 dface 的 (u,v) → 2D 点在多边形内测试 → 命中则从该 dface 的 tile 采样。
4. 未命中的 luxel 做 2–4 轮 4 邻域 bleed；整面都没有候选时该面无光（`light = 1.0`）。

成本估算：10 万 luxel × 3 候选 × 6 边 ≈ 百万级运算，毫秒级。

收益：TB 面 ↔ atlas 矩形是 1:1，改一个 brush 只需重算涉及的面。

### 4.4 线程与调度策略

- 预览：`-fast`，`-threads = min(P 核数, 12)`，给 UI 留余量（本机 8 线程 = 7.2 s）。
- 质量：默认档，`-threads = 逻辑核数 - 2`（本机 57 s），可选 `-gpu`（47 s）。
- 防抖：最后一次编辑后 1.5 s 触发；烘焙期间的新改动只置脏，不排队第二次。
- 单队列 + 可取消：取消通过 `QProcess::kill` + 删除临时目录；不依赖 `hltools` 的取消命令。
- 临时目录：`<临时目录>/tb-lightmap/<文档 hash>/`，进程退出与文档关闭时清理。

### 4.5 manifest 契约

`hltools lightmap <map.bsp> <atlas.bmp> -manifest <atlas.json>` 已实现，输出形如：

```json
{
  "atlas": { "width": 1024, "height": 2368, "faces": 15638, "tiles": 15638 },
  "faces": [
    {
      "face": 1234,
      "plane": [0, 0, 1],
      "dist": -128,
      "vecs": [[1, 0, 0, 0], [0, 1, 0, 0]],
      "texinfo": 42,
      "mins": [16, 175],
      "size": [10, 3],
      "tile": [49, 221],
      "lightofs": 259779,
      "styles": [0, 255, 255, 255],
      "poly": [[16.4375, 175.562], [24.4375, 175.562], [24.4375, 178.562]]
    }
  ]
}
```

- `poly` 用 **luxel 单位**（已按 vecs 投影并 /16），TB 做点在多边形内判断时不需要再做任何换算。
- `lightofs == -1` 或 `styles[0] == 255` 表示该面不受光，TB 直接用 1.0。
- 只导出 style 0（GoldSrc 的 4 个 style 引擎会混合，编辑器预览不需要）。
- 平面已按 `side` 翻转，`vecs` 是该面实际使用的光贴图轴；同一平面上不同 `vecs` 的记录属于互不相干的 luxel 格。

## 5. 分阶段实施

| 阶段 | 内容 | 估时 | 验收标准 |
|---|---|---|---|
| **M0** | 可行性实测（已完成） | — | 见第 3 节 |
| **M1** | `hltools lightmap --manifest` | 0.5 天 | `!kz_skyline.bsp` 导出 16,935 条记录，矩形与 BMP 布局逐块一致 |
| **M2** | TB 渲染骨架：`Uv12` + `P3NT2T2` + `Face.fragsh` 乘法 + 白 1×1 atlas + 开关 | 2–3 天 | 白光下画面与改动前逐像素一致；2D/选中/透明/灰度零回归 |
| **M3** | 数据链路：导出 map → 子进程 compile → 读 manifest+PNG → 逐 luxel 合成 → 上传 | 4–5 天 | 打开真实 .map，光照视图与 `hltools lightmap` 导出图逐像素对拍通过 |
| **M4** | 交互：防抖/后台/进度/取消/失效/质量档/页满告警 | 3–4 天 | 连续编辑 10 个 brush 不阻塞 UI；切档后 UV 全量重建 |

合计约 2 周。

## 6. 风险与对策

| 风险 | 对策 |
|---|---|
| 缺 WAD → csg 硬失败 | 导出时把项目 WAD 绝对路径写进 `worldspawn.wad`；失败时向用户明示「缺少贴图」而不是「光照失败」 |
| atlas 64 页满 | 解析 `Warning: lightmap atlas ... full`，提示并降级（部分面 fullbright） |
| 面匹配不上（几何差异/CSG 剔除） | 匹配失败的面对回退 `light = 1.0`，并在日志里计数；不阻塞渲染 |
| 烘焙期间编辑 | 版本号 + 单队列；烘焙结果带 map 的几何版本，过期直接丢弃 |
| 大图 csg 12 s + rad 7 s | 防抖 1.5 s + 后台；首次进入光照模式先做一次完整烘焙并显示进度 |
| 双重曝光 | `LightScale = 1.0`，运行时不再应用 `Brightness`/gamma |
| 切换到 `-fast` / 默认档 | 两档 lit face 集合不同 → **作废全部 UV 并重建 atlas** |
| GPU 结果与用户最终编译不一致 | GPU 只在「高质量预览」档可选，并在 UI 里标注「近似」 |

## 7. 明确不做

- 不实现 4 个 light style 的混合（只取 style 0）。
- 不给 Bezier patch、贴花、实体模型烘焙光照（首版 fullbright）。
- 不做 BSP/vis，TB 依旧以 .map 为唯一事实来源。
- 不在编辑器里重新打包成第二张 atlas，也不引入面签名失效机制。
- 不支持 Quake1 以外的光照语义（`hltools` 是 GoldSrc 专用，与本 fork 定位一致）。

## 8. 附录：复现方式

```powershell
# 单图烘焙（快速档）
hltools compile <map>.map -fast -threads 8

# 只重烘光照（已有 bsp，贴图放在 bsp 同目录）
hltools rad <map>.bsp -fast -threads 8

# 导出 atlas 用于对拍
hltools lightmap <map>.bsp atlas.bmp -force
```

实测产物（可删）：`%TEMP%\hltools-probe` 下的 `sky`、`gpu`、`road`、`small` 四个子目录。

为构建带 GPU 后端的 `hltools`，把其 `CMakeLists.txt` 中的 Vulkan-Headers `FetchContent` 改为优先使用已安装的 Vulkan SDK 头文件（可离线构建）；该处为本方案对 `hltools` 仓库的唯一改动。
