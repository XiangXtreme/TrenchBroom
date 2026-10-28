# Transform 数值框交互对照

核对基于本地 Blender 源码版本 `fbe6228777e7d9afefcd61a413844e790ae75db7`。
这里讨论 Location、Rotation、Scale 数值框的状态转换；底层地图编辑仍由 TB 的原生命令执行。

## Blender 源码依据

下列路径均相对于 Blender 源码根目录。

| 入口 | 已核对的细节 |
| --- | --- |
| `source/blender/editors/interface/interface_handlers.cc`：`do_but_NUM`、`do_but_SLI` | 按下先进入数值手势，点击松手才进入文本编辑；Ctrl+点击直接编辑；拖动松手提交；Esc/右键取消 |
| 同文件：`but_dragedit_update_mval`、`multibut_drag_wait` | 拖动阈值以内不改值；跨过阈值后重设拖动基准；纵向手势期间等待多字段选择 |
| 同文件：`do_button` 的 `USE_DRAG_MULTINUM` 分支 | 累积横纵移动距离判断方向；纵向归一化分量大于 0.75 才开始连选；横向转折后固定字段集合 |
| 同文件：`multibut_states_tag` | 以按下点到当前点的线段与字段矩形相交来选中；每次重算，往回拖可缩小范围 |
| `source/blender/editors/interface/interface.cc`：`button_is_compatible` | 只连选可交互、类型和属性语义兼容的字段 |
| `interface_handlers.cc`：`multibut_states_create`、`multibut_states_apply` | 文本输入应用同一值；拖动应用原值加增量，比例属性按原值乘比例；主字段为零时不启用比例 |
| 同文件：`textedit_begin`、`do_but_textedit`、`do_but_textedit_select` | 开始文本编辑时全选；再次点击定位光标，拖动选择文本，双击选择词；Enter 确认，Esc/右键取消；Tab/Shift+Tab 切换字段 |
| 同文件：`event_to_snap`、`numedit_apply_snapf`、`numedit_but_NUM` | Shift 精调，Ctrl 吸附，Ctrl+Shift 使用较小吸附间隔；拖动过程中修饰键变化重新计算吸附 |
| 同文件：`do_button`、`handle_button_event` | 悬停复制/粘贴、属性上下文菜单、空文本恢复 RNA 默认值属于属性系统的通用处理 |
| `source/blender/makesrna/intern/rna_object.cc`：`rna_def_object` | Scale 标记 `PROP_PROPORTIONAL`；Location、Euler Rotation 与 Scale 有各自的单位、精度、范围和步长 |

官方手册补充：[Input Fields](https://docs.blender.org/manual/en/latest/interface/controls/buttons/fields.html)。
行为发生分歧时，以以上本地源码版本为核对依据。

## TB 数值框契约

| 操作 | TB 行为 |
| --- | --- |
| 鼠标导致 FocusIn，随后按下 | 焦点本身不进入文本编辑；按下仍然启动手势识别 |
| 初次单击松手 | 进入文本编辑，全选当前数字；角度后缀不属于待替换数字 |
| 编辑中再次点击或拖动 | 交给原生文本编辑器定位光标或选择文本 |
| Ctrl+单击 | 立即全选数字并进入文本编辑，松手不清除选区 |
| 横向拖动 | 超过阈值后建立基准，随后实时修改地图；松手形成一个撤销步骤 |
| 纵向划过 X/Y/Z | 高亮同一向量组中被线段经过的可编辑轴；锁定轴跳过；往回划可缩小范围 |
| 连选后松手 | 保持高亮，主字段全选数字；键入值后确认，所有选中字段取同值 |
| 连选后转为横拖 | 固定选中轴集合；Location/Rotation 保留各轴差值，Scale 保留各轴比例 |
| 连选 Scale 时主轴原值为零 | 使用加法增量，避免除零 |
| 显式连选与比例链同时启用 | 显式连选决定修改的轴和值；比例链用于单轴编辑时联动未锁轴 |
| 拖动中 Shift / Ctrl / Ctrl+Shift | 精调 / 吸附 / 精细吸附；主轴吸附后其它连选轴继承增量或比例 |
| Enter | 提交并退出编辑，焦点返回上次使用的地图视图 |
| Esc / 编辑中的右键 | 取消未确认文本或整次实时拖动，清理高亮及鼠标捕获，返回地图视图 |
| Tab / Shift+Tab | 提交当前输入并继续 Qt 焦点导航；键盘进入数值框时全选数字 |
| 编辑中的上/下方向键 | 移到数字开头/末尾；Shift 扩选，不步进修改地图 |
| 编辑中的 Ctrl+A/C/X/V/Z | 使用 Qt 文本编辑器的全选、复制、剪切、粘贴和文本撤销 |
| 点击其它控件 | 提交当前文本；普通滚轮继续用于面板滚动；未进入编辑状态时 Ctrl+滚轮可步进 |
| 面板销毁 | 关闭观察者和输入回调；丢弃尚未提交的文本，取消进行中的长事务 |

## TB 的适配边界

- 手势阈值使用控件逻辑高度的四分之一，随 Qt 缩放；精调系数为 0.1。
  Location、Rotation、Scale 分别保留 TB 当前的每像素步长；Ctrl 吸附使用当前地图网格、5° 和 0.1，
  Ctrl+Shift 将对应吸附间隔再缩小到十分之一。
- Blender 的 RNA 单位系统会影响长度属性的比例拖动、步长和吸附。
  TB Location 按地图坐标作加法偏移，Scale 连选作比例调整；三种向量之间不混合连选。
- Scale 数值范围暂沿用 TB 的正数限制；比例计算为主轴零值保留加法回退，面板尚不提供负缩放或零缩放。
- 文本输入目前使用 Qt 数值校验。Blender 的 Python 表达式、单位表达式、RNA 默认值重置、
  悬停属性快捷键、驱动器、关键帧和属性右键菜单尚未接入此面板。
- 普通 Brush 的独立旋转、缩放持久化见 `custom-feature-roadmap.md` 的专有项目格式待办。

## 验证

`tst_TransformCard.cpp` 覆盖鼠标 FocusIn 先于 Press 的事件顺序、点击替换数字、纵向连选、
回划和锁定轴、横向转折、比例拖动、吸附、文本确认/取消与撤销事务。
`tst_MapWindow.cpp` 覆盖编辑结束后返回原地图视图的焦点路由。
后台测试确认 Qt 事件和焦点状态；真实窗口的激活、鼠标捕获及操作手感仍需人工试用。
