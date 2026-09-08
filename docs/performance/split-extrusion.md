# 分割挤出拖动性能定位

后续修复及复测见 [分割预览节点复用优化](split-extrusion-optimization.md)。本文保留优化前的定位数据与分析；复现脚本及探针已适配当前实现，重测优化前版本需检出 `46a74bc41`。

测量日期：2026-09-08。被测代码：`9a8e40f2f826c7fe3c8630b3deaf33ea17b0a0f5`。

## 结论

分割挤出的主要额外成本来自每次有效拖动撤销并重建节点、改变选区后触发的 UI 工作：Outliner 反复同步选区、重建父节点子树，属性编辑器重建 QWidget 行，随后 Qt 处理这些变化产生的控件绘制、样式及事件。简单 brush 的复制、边界移动和 clip 合计不足 0.1 ms。

普通挤出也会回滚事务。因此根因不是“分割才使用 undo”，而是分割的 undo/重新执行不断跨越节点生命周期和选区状态，普通挤出主要交换已有节点内容。

单 brush 向外分割的平均工作时间为 **12.74 ms**，普通挤出为 **1.95 ms**，约 **6.5 倍**。分割时间中，Outliner、属性面板和其余 Qt 事件/绘制合计 **78.1%**。地图含 64 个 brush、仍只拖一个六面 brush 时，分割平均 **24.26 ms**，P95 **50.92 ms**。

这复现了显著的交互成本差距及约 50 ms 的慢样本。这里测的是无节拍的控制器/事件处理工作时间，不是用户原地图的实际屏幕 FPS；没有将 `1000 / 耗时` 伪装成实测帧率，也没有声称精确重现用户的平均 100/20 FPS。

## 实验方法与复现

- Windows，i9-12900HX（16 核、24 线程），RTX 4060 Laptop GPU，MSVC Release，Qt 6.11.1。
- 正式 `installApplicationStyle`，Dark 主题，100% 缩放，1440×900 MapWindow，Outliner 页；四个视图实例接收通知，一个可见视图绘制。窗口使用 `WA_DontShowOnScreen`。
- `AppControllerFixture` + Quake/Valve 文档；清空初始地图后创建 1 或 64 个 64×64×64 的六面 brush。同层排列，始终只选择第一个 brush、命中其顶面。材质使用空占位；不加载用户地图、外部资产或用户插件。QSettings 使用临时目录。
- 实际调用 `ExtrudeToolController3D::acceptMouseDrag`，按模式设置 Shift 或 Shift+Ctrl，之后每步重新 picking 并执行 `GestureTracker::update`。射线指向顶面初始位置加绝对位移；位移循环为 8～23 单位，向内模式取负。关闭网格吸附，使每步均为有效位置变化。
- 每模式预热 20 次，再记录 120 次；五种环境 × 两种地图大小 × 三轮独立进程，每个模式/环境累计 360 个样本，共 10,800 次计时更新。每步断言位置确实改变，取消拖动后断言原始边界和节点数恢复。
- 每步先执行 picking/controller，再 `QApplication::processEvents()` 和清理 deferred-delete。正式结果不调用 `grabFramebuffer`，每个样本实际记录一次 MapView 绘制。探针以 `steady_clock` 记录 wall time，收集调用次数、inclusive 和 exclusive 时间；循环中不写日志，结束后写 CSV。
- 保留 Qt/GPU/系统调度等待，不把这些时间说成纯 CPU 指令时间。探针和断言有开销；未单独校准。三轮存在调度波动，因此同时报告均值、中位数和 P95。没有覆盖 OS 鼠标事件采集/合并、ToolBox 的其他工具遍历、真实地图资产、复杂斜面/多面、多 brush 或 linked group。

从仓库根目录运行（复用现有 Release 树，期间不要并行使用该构建树）：

```powershell
python scripts/profile-extrude.py run --repeat 3 --brushes 1 64
```

脚本应用 [临时探针补丁](../../scripts/performance/extrude-profile.patch)，构建 `TbUiLibTest`，复制所需 shader/font/stylesheet 资源，运行实验；最后反向应用补丁并重建普通测试程序。补丁不应用时生产代码没有计时或旁路逻辑。`git apply --check` 会拒绝不兼容的源码；不强行覆盖冲突。异常退出若未完成恢复，可运行 `python scripts/profile-extrude.py restore` 后重建。`apply` / `sample` 支持手动调试；`sample` 要求已构建带探针版本。

本次原始逐帧数据、每轮 scope CSV 和测试输出在：

```text
build-release-codex/codex-logs/extrude-profile/final/
```

可随仓库检查的结果：

- [所有环境的统计结果](split-extrusion-results.csv)
- [基线每个探针的调用次数和耗时](split-extrusion-scopes.csv)
- [复现脚本](../../scripts/profile-extrude.py)

## 工作时间

单位均为 ms/有效拖动更新。同步阶段包括 picking/controller、几何、命令和同步通知；事件阶段包括已排队 UI 更新及绘制。

| 地图 brush 数 | 模式 | 平均 | 中位数 | P95 | 同步阶段 | 事件与绘制 |
|---:|---|---:|---:|---:|---:|---:|
| 1 | 普通 | 1.953 | 1.530 | 4.210 | 0.850 | 1.103 |
| 1 | 向外分割 | 12.740 | 10.102 | 29.061 | 4.511 | 8.229 |
| 1 | 向内分割 | 10.770 | 9.847 | 16.431 | 3.880 | 6.890 |
| 64 | 普通 | 1.713 | 1.513 | 3.104 | 0.753 | 0.961 |
| 64 | 向外分割 | 24.261 | 19.301 | 50.919 | 7.766 | 16.495 |
| 64 | 向内分割 | 24.798 | 20.516 | 51.170 | 9.510 | 15.287 |

不构造 MapWindow，仅保留文档、命令与文档持有的 MapRenderer、执行相同 controller 的对照：

| 地图 brush 数 | 普通 | 向外分割 | 向内分割 |
|---:|---:|---:|---:|
| 1 | 0.045 | 0.097 | 0.143 |
| 64 | 0.058 | 0.122 | 0.145 |

## 各主要阶段的成本

以下表格按 **exclusive** 时间分组，父探针扣除所有已计时子探针，列间可相加。属性行的 Qt polish、后续 QWidget 绘制归入 Qt 事件；因此不能把“属性面板”一行理解为该面板引发的全部最终成本。

| 阶段 | 普通，1 brush | 向外分割，1 brush | 分割占比 | 向外分割，64 brushes | 分割占比 |
|---|---:|---:|---:|---:|---:|
| Brush 复制、moveBoundary、clip | 0.047 | 0.078 | 0.61% | 0.076 | 0.31% |
| 命令/节点操作及其余同步通知 | 0.480 | 1.490 | 11.70% | 1.390 | 5.73% |
| Outliner 子树与选区同步 | 0.000 | 2.746 | 21.55% | 5.883 | 24.25% |
| 属性面板更新 | 0.455 | 1.978 | 15.53% | 1.964 | 8.09% |
| Renderer 缓存失效与重建 | 0.017 | 0.055 | 0.43% | 0.210 | 0.87% |
| picking | 0.025 | 0.061 | 0.48% | 0.235 | 0.97% |
| 视图状态及动作更新 | 0.016 | 0.114 | 0.89% | 0.121 | 0.50% |
| 视口绘制，扣除缓存阶段 | 0.438 | 0.579 | 4.54% | 0.605 | 2.49% |
| 其余 Qt 事件/控件绘制 | 0.301 | 5.228 | 41.04% | 13.305 | 54.84% |
| 测量循环及未细分事件开销 | 0.174 | 0.411 | 3.23% | 0.470 | 1.94% |
| **合计** | **1.953** | **12.740** | **100%** | **24.261** | **100%** |

下表用于追踪调用链，采用 **inclusive** 时间，包含同步通知和子调用，**不可相加**，也不可与上表再相加。单位 ms/更新，括号为调用次数。

| 入口/阶段 | 普通，1 brush | 向外分割，1 brush | 向内分割，1 brush |
|---|---:|---:|---:|
| 复制 faces + geometry | 0.012（1 份） | 0.015（1 份） | 0.032（3 份） |
| `Brush::moveBoundary` | 0.035（1） | 0.039（1） | 0 |
| `Brush::clip` | 0 | 0.023（1） | 0.056（2） |
| `Map::rollbackTransaction` | 0.437（1） | 2.550（1） | 2.338（1） |
| `CommandProcessor::undoCommand` | 0.274（1） | 2.361（3） | 2.194（3） |
| `deselectAll` | 0 | 0.414（1） | 0 |
| `addNodes` | 0 | 0.424（1） | 0.831（1） |
| `selectNodes` | 0 | 1.007（1） | 0.326（1） |
| `addNodesAndNotify` | 0 | 0.134（1） | 0.621（1） |
| `removeNodesAndNotify` | 0 | 0.128（1） | 0.599（1） |
| `doSwapNodeContents` | 0.300（2） | 0 | 0.265（2） |
| `selectionDidChangeNotifier` 全部回调 | 0 | 3.201（4） | 1.520（3） |
| Outliner `refreshTreeParents` | 0 | 0.227（2） | 1.187（2） |
| Outliner `syncSelectionFromDocument` | 0 | 2.529（6） | 2.062（5） |
| Outliner 属性 `rebuildPropertyRows` | 0 | 1.949（1） | 1.733（1） |
| `BrushRenderer::validate` | 0.011（1） | 0.029（2） | 0.022（1） |

向内分割直接创建 front/back 两份 brush；第三次复制出现在内容更新链路中。表中没有把初始拖动状态的快照复制计入每步，因为它发生在预热之前。

## 每步调用链及放大机制

```text
ToolBoxConnector::processDrag
  mouseMoved + updatePickResult
  ToolBox::mouseDrag → HandleDragTracker → ExtrudeDragDelegate::update
    ExtrudeTool::extrude
      普通：rollback → extrudeBrushes → applyAndSwap / updateNodeContents
      向外：copy → moveBoundary → clip → rollback → deselectAll → addNodes → selectNodes
      向内：copy front/back → clip 两次 → rollback → updateNodeContents → addNodes → selectNodes
  Qt event loop → 排队的属性更新 / QWidget 绘制 / MapView 绘制及缓存重建
```

测量驱动从同样的 picking/controller/tracker 进入；`ToolBoxConnector::processDrag` 上层鼠标记录/分发由源码追踪，没有将未执行的上层分发算作实测覆盖。tracker 会跳过吸附后未变化的位置；此处测的是实际产生预览修改的每一步。

### 事务、节点与通知

源码入口：[ExtrudeTool.cpp](../../lib/TbAppLib/src/ExtrudeTool.cpp)，`splitBrushesOutward`（362）、`splitBrushesInward`（440）、`extrude`（978）。

- 普通模式每步回滚一次旧内容交换，再执行一次新内容交换，节点 identity 和选区保持稳定；每步两次内容通知，没有节点增删及选区通知。
- 向外模式回滚会撤销选中新预览节点、移除旧预览节点、恢复原选择；随后再次取消原选择、添加新预览节点并选中。稳定拖动每步产生一次节点新增、一次移除、四次选区通知。
- 向内模式在原节点上更新切割的一部分，并添加另一部分，保留原 brush 的选择；每步一次新增、一次移除、两次内容交换、三次选区通知。
- `CommandProcessor::rollbackTransaction` 逆序执行当前事务命令并清空它们。这里没有调用用户 redo 栈；下一位置由新的命令重新执行。`MapDocument::transactionDone/transactionUndone` 还会传播可观察的 `documentDidChangeNotifier`。
- 项目 [Notifier](../../lib/TbBaseLib/include/base/Notifier.h) 直接迭代回调。上表 rollback 的 2.55 ms 包含同步 UI 工作，不能解释为 undo 数据结构自身花了 2.55 ms。单 brush 文档对照中，包含 renderer 通知的整个向外分割仅 0.097 ms。

### Outliner 和属性面板

[OutlinerTreeWidget.cpp](../../lib/TbUiLib/src/outliner/OutlinerTreeWidget.cpp)：增删通知立即调用 `refreshTreeParents`（978）。它保存展开状态、删除父节点下的整批 QTreeWidgetItem、重新添加子树、恢复展开并同步选区。对世界 brush，父节点为 layer，所以一次单 brush 修改仍会重建同层条目。

`syncSelectionFromDocument`（1624）先清空树选区，再设置选中项/current item、展开其祖先；无选中子项时还会折叠 worldspawn。向外分割的四次文档选区通知，再加两次子树刷新内的同步，合计六次。节点数从 1 增至 64 后，每步 `setupTreeItem` 从 5 次增至 131 次。这是确实存在的按地图规模放大的工作。

[OutlinerEntityPropertyEditor.cpp](../../lib/TbUiLib/src/outliner/OutlinerEntityPropertyEditor.cpp)：`scheduleUpdate`（564）用零延迟 timer 合并本轮请求，但 `updateFromSelection` 仍每轮调用 `rebuildPropertyRows`（707）。该函数清空布局、移除并删除 QWidget，再创建全部属性行和嵌入编辑器。虽然选中的 brush 在切换，归属的 worldspawn 实体未变，也会重建。普通内容变更则走现有行的 `refreshVisiblePropertyValues/refreshEmbeddedEditors`。

Qt 延迟合并绘制请求不能消除上述已经发生的同步树操作，也不能避免下一轮属性行重建及 polish。单 brush 向外分割每步记录 19 次 QWidget Paint 投递，普通只有 1 次；视口 paintGL 都只有 1 次。64 brush 分割的 Qt Paint exclusive 达 8.30 ms/步。这里是多个控件绘制事件，不能解读为视口画了 19 帧。

Qt 行为通过 AnySearch 核对 [官方 QWidget 文档](https://doc.qt.io/qt-6.8/qwidget.html#paintEvent)，并以本地 `QApplication::notify` 的 Paint/UpdateRequest/Polish/MetaCall/Timer 计时交叉验证。Qt 分类仍包含底层 Qt/驱动/等待，没有把每一笔都归给某一个 panel。

### Renderer、picking 和重绘

[MapRenderer.cpp](../../lib/TbRenderLib/src/MapRenderer.cpp) 对节点新增/移除、选区变更更新 default/selection renderer，并使 link 等缓存失效。普通 `nodesDidChange` 更新受影响节点和祖先，不递归使整张地图失效。

还有一个明确的次级放大点：[BrushRenderer.cpp](../../lib/TbRenderLib/src/BrushRenderer.cpp) 在 default renderer 启用 `UseReadable2DBrushOutlines` 时，add/remove 会调用 `invalidate()`，移除该 renderer 全部 brush 的 VBO 范围并将它们标为 invalid。selection renderer 关闭此选项。分割的反复取消选择/选择使 brush 在两类 renderer 间迁移；64 brush 向外分割每步实际 validate 65 个 brush，普通只 validate 1 个。此次失效和重建合计约 0.210 ms，占 0.87%，不是简单场景的主要卡顿来源，但更大的地图值得单独关注。

[MapViewBase.cpp](../../lib/TbUiLib/src/MapViewBase.cpp) 的 selection/document 通知更新 picking、动作启用状态并调用 `update()`。普通每步合计 8 次视图 repick，分割合计 24 次，包含未显示的其他视图实例；并非每步重建快捷键。单 brush 分割全部已计时 picking 约 0.061 ms，64 brush 约 0.235 ms。视口实际绘制 exclusive 约 0.579/0.605 ms，均远小于控件更新成本。

## 诊断性旁路对照

保持相同几何、命令、节点和文档选区逻辑；只在计时区间临时跳过指定 UI 函数。这些旁路会使树选中状态或属性显示暂时不同，**是定位实验，不是可直接发布的修复**。跳过树选区同步仍保留子树刷新及节点映射，避免让树保留失效的节点引用。

| 环境 | 1 brush，向外 | 1 brush，向内 | 64 brushes，向外 | 64 brushes，向内 |
|---|---:|---:|---:|---:|
| 完整基线 | 12.740 | 10.770 | 24.261 | 24.798 |
| 跳过 Outliner 选区同步 | 9.411 | 9.467 | 15.313 | 18.016 |
| 跳过属性行重建 | 9.353 | 10.044 | 16.210 | 17.938 |
| 同时跳过两项 | 5.570 | 6.123 | 13.975 | 15.110 |

单 brush 向外分割减少 56.3%。64 brush 同时旁路仍有 13.98 ms，说明父节点子树刷新、其后控件绘制和其余观察者成本仍在。各项旁路影响共享 Qt 事件和布局工作，不能把不同对照的差值线性相加；跨进程调度波动也影响细小差值。

## 建议的修复方向

1. 在长事务预览期间合并面向 UI 的刷新，只消费最终节点/选区状态；保留节点生命周期、有效引用、原生命令和必要的 renderer 通知。不能全局屏蔽所有文档通知。
2. 属性面板比较有效实体集合及属性结构。归属实体和结构不变时更新值，复用 QWidget 行；有效实体或定义结构变化时才重建。
3. Outliner 对简单 brush 的增删维护条目，避免删除重建整个 layer 子树；选区同步做差量更新，避免空选区中间态反复折叠/展开 worldspawn。
4. 若上述优化后仍需进一步降低成本，再设计拖动期间稳定的预览节点 identity，避免每步 remove/add；必须覆盖向内/向外切换、无效几何恢复、取消、undo/redo、不同父节点及 linked groups。
5. 对 readable 2D outlines 的全 renderer 失效独立设定大地图验证目标；本次数据不支持把它列为简单 brush 首要瓶颈。

本次交付为定位结果和可复现实验；生产挤出行为未修改。

## 交付验证

- 30 个实验进程全部通过位置与取消恢复断言；检查了 90 组逐帧数据，共 10,800 个样本。所有基线每步一次 controller picking、一次视口绘制；exclusive 时间总和与 frame inclusive 一致。
- 补丁正向 applicability 和反向恢复检查通过；恢复后被测生产/测试源码与原始 HEAD 内容一致。
- 恢复后重新构建 `TbAppLibTest`，`ExtrudeTool*`：2 个用例、404 个断言通过。
- 恢复后重新构建 `TbUiLibTest`，`MapWindow,OutlinerTreeWidget,OutlinerEntityPropertyEditor`：3 个用例、410 个断言通过。
- Release `TrenchBroom` 构建通过；UI style governance 通过。
- `ci-preflight.ps1 -BaseRef HEAD` 对本次交付通过。最终提交仅含诊断脚本、补丁和报告/数据，因此没有改动的生产 C++ 单元；Qt 检查提示本地 6.11.1 与 CI 6.10.3 的次版本差异。
