# GoldSrc 实体运动预览

## 使用

在场景树或地图视图中选中一个 `func_door`、`func_button` 或
`func_door_rotating`，右侧“属性”标题下方显示运动预览。选中该实体的一个或多个
子刷子、刷子面也可以使用。混合选择多个实体或世界刷子时隐藏此区域。

- 拖动滑杆，检查游戏初始位置到触发后位置之间的姿态。
- “初始位置”和“触发后”直接定位到两端。
- “重置”清除虚影；原刷子始终显示在编辑位置。
- 双向旋转门可以切换默认方向与反向。实际游戏中由触发者的站位和朝向决定方向。
- 表单、表格共用此区域，切换显示方式不改变进度。

3D 视图绘制半透明虚影和轮廓，2D 视图绘制轮廓。预览不写入几何体、实体属性、
保存状态或撤销历史。修改实体属性时保留进度并重新计算；更换选择、删除实体、
加载地图或进入 Play 模式时清除预览。退出 Play 后手动重新预览。

## 行为依据

计算依据 Valve Half-Life SDK 的 `dlls/subs.cpp`（SetMovedir、AxisDir）、
`dlls/doors.cpp`（Spawn、DoorGoUp）、`dlls/buttons.cpp`（Spawn）及
`pm_shared/pm_math.c`（AngleMatrix），并核对本地 hltools 的
`src/csg/map_post.cpp` 和 `src/csg/brush.cpp`。

- 平移方向优先使用 `angles`，否则使用旧式 `angle`。`-1` 向上、`-2` 向下。
- 平移距离为刷子模型尺寸在方向上的投影减去 `lip`。SDK 的 `size - 2` 是补偿
  引擎包围盒每侧扩大 1 单位，编辑器的原始刷子尺寸不能再减 2。
- `func_button` 的缺省或显式零 `lip` 都按 4 处理；Dont Move 标志或绝对行程
  小于 1 单位时不移动。门的负行程仍按反方向位移处理。
- Start Open 使用“初始打开 → 触发关闭”的端点顺序；不改变原始编辑姿态。
- 旋转使用 GoldSrc 的 Euler 分量及顺序。标志 64 选择 roll（世界 X），128
  选择 pitch（世界 Y），缺省 yaw（世界 Z）。同时设置时 64 优先。
- 旋转支点取 `origin`，或按 hltools 从 ORIGIN 刷子的包围盒中心计算；没有二者
  时为世界原点，不擅自改为实体中心。ORIGIN 不参与实体行程尺寸或虚影。
- hltools 将 ORIGIN 中心向零截断为整数后写入 origin，并在建模前重新读取此键；
  预览使用最终整数支点。实测含半单位中心的 ORIGIN 刷子，BSP 模型与实体原点一致。
- 双向 yaw 门的反向目标按 SDK 的目标 Euler 角取负处理，保留初始角度的影响。

## 范围与限制

只在使用 GoldSrc `.C` 纹理格式的游戏配置中启用。无有效模型几何体、参数无效、
ORIGIN 冲突、BOUNDINGBOX 或 `zhlt_usemodel` / `zhlt_minsmaxs` /
`zhlt_transform` 等编译器覆盖时显示原因并禁用滑杆。

这是实体姿态的手动检查工具。自动播放、触发链、玩家推挤、运动碰撞和运行中的
平台模拟不在本版范围内。自定义游戏 DLL 改写实体运动规则时，以实际游戏为准。

## 代码与验证

`GoldSrcEntityMotion` 提供无状态计算和复制的面顶点；`EntityMotionPreview`
由每个 `MapViewToolBox` 持有，集中管理选择、进度和所有地图视图的渲染。
属性面板只绑定该状态，不拥有第二套计算或文档注册表。滑杆更新仅变换缓存顶点，
不扫描整张地图或重建属性控件。

聚焦测试：`GoldSrcEntityMotion`、`EntityMotionPreview`；回归覆盖属性面板与
工具箱。主题验收目标：`outliner-motion`、`outliner-motion-table`、
`outliner-motion-narrow`。渲染测试同时检查 2D、3D 帧缓冲及重置后的恢复。
