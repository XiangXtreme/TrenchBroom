# GoldSrc 灯光方向标记

状态：已实现；2026-11-10。

2D／3D 视图中的灯光箭头表示编译器实际使用的照射方向。规则仅在加载 GoldSrc
纹理的游戏中启用，与已有 GoldSrc 渲染的游戏识别一致。

- `light_spot`、`light_environment`、`info_sunlight` 使用灯光方向规则。
- `light*` 指定非空 `target` 后也按定向灯处理。有效目标优先于全部角度，使用
  `目标 origin - 灯光 origin`；同名目标取地图遍历顺序中的第一个。
- 找不到目标时回退到角度。非零 `angle` 覆盖 `angles` 的 yaw；非零 `pitch`
  覆盖 `angles` 的 pitch。零值表示回退，而非强制水平或朝东。
- 无目标时，方向为 `(cos(pitch) cos(yaw), cos(pitch) sin(yaw), sin(pitch))`。
  因此负 pitch 向下、正 pitch 向上；roll 不改变照射方向。
- 没有目标的普通全向点光源、纹理灯光控制器不显示方向箭头。
  `light_shadow`／`light_bounce` 按 `convertto` 转换后的类型处理。
- 目标与灯光重合或编译器得到零方向时不画箭头。

实现依据是用户本地 hltools 的 `src/rad/lightmap.cpp:create_direct_lights` 和
`src/csg/entity_output_fixups.cpp:apply_entity_output_fixups`。RAD 对旧式
`angle=-1/-2` 先清空水平分量，随后仍用 `sin(pitch)` 覆盖 Z；pitch 为零时
会得到零向量。方向标记保留该实际行为，不把它解释成普通实体的上下方向。

方向解析独立于实体模型旋转，不写入地图属性。FGD 的默认值不会作为隐式编译输入；
标记读取地图实际存储的键值。目标查询不依赖 FGD 的链接字段声明，也不受编辑器
隐藏状态影响。每轮箭头渲染按需建立一次目标索引，避免为每个灯光遍历地图；下一轮
重新读取，以便移动目标、修改属性及撤销／重做后即时更新。
标记为不参与导出的图层不会提供编译用目标。

`EntityDirectionResolver` 回归覆盖参数优先级、俯仰符号、目标和同名目标、目标移动／
重命名、全向灯和纹理灯、其他游戏和普通实体，以及 2D／3D 投影方向。
