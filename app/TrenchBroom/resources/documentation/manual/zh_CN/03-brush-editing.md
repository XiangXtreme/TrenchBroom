# Brush 编辑与创建 {#brush_editing_and_creation}

## 创建对象 {#creating-objects}

TrenchBroom 提供了多种创建新对象的方式。在接下来的章节中，我们将逐一介绍这些选项。

### 创建简单形状 {#creating-simple-shapes}

创建新 Brush 最简单的方法是使用简单形状工具直接用鼠标绘制。在没有选中任何对象且没有激活其他工具时，该工具默认启用。只需在 3D 视图或任意 2D 视图中按住左键拖动即可。

在 3D 视图中绘制 Brush 时，其形状由最初开始拖动时的鼠标点、当前鼠标光标下的点以及当前网格大小共同控制。在 XY 轴上绘制 Brush 时，Brush 的高度将设置为当前网格大小。拖动时，按住 #key(Shift) 可以强制 X 和 Y 轴尺寸相等，按住 #key(Shift)+#key(Alt) 可以强制 X、Y 和 Z 轴尺寸均相等；在绘制 Brush 时按住 #key(Alt) 也可以仅更改高度。

![在 3D 视图中创建长方体](images/DrawBrush.gif)

在 2D 视图中绘制 Brush 时，你只能控制该 2D 视图所显示轴向上的范围。例如，如果在 XZ 视图中绘制 Brush，你可以用鼠标控制 X/Z 范围，但无法直接更改 Y 轴范围，Y 轴范围始终固定为最近选中对象的 Y 轴范围。这同样适用于所有不同的 2D 视图。

无论哪种情况，分配给新创建 Brush 的材质都是 _当前材质_。当前材质可以通过在[材质浏览器](#material_browser)中选择材质或选择已具有材质的面来设置。这一概念同样适用于创建新 Brush 的其他方式。

这种创建 Brush 的方式仅允许你创建下表中列出的简单形状。在下一节中，你将学习如何使用复杂形状工具创建更复杂的 Brush 形状。

形状                   说明
-----                  -----------
长方体                 创建长方体形状
楼梯                   创建楼梯
拱形                   创建半圆形拱门
圆柱体                 创建边数可变的圆柱体；可设为空心
圆锥体                 创建边数可变的圆锥体
球体 (UV)              创建由三角形和四边形组成的双极点球体形状
球体 (二十面体)        基于二十面体创建由三角形组成的球体形状
岩石                   创建由一个或多个 Brush 组成的随机岩石地貌

请注意，圆柱体、圆锥体、UV 球体和拱形都具有类似的选项，即边数和圆形模式。
通过在不同形状间为这些选项使用相同的值，TrenchBroom 可以创建彼此完美贴合的形状。

可以通过相应按钮选择三种圆形模式：

模式                                说明
----                                -----------
![](images/CircleEdgeAligned.png)   创建 4 条边与包围盒对齐的圆
![](images/CircleVertexAligned.png) 创建 4 个顶点与包围盒对齐的圆
![](images/CircleScalable.png)      创建可缩放的圆

最后一种形状需要一些解释。它不是一个完美的圆，而是稍微调整了顶点位置以使其完美对齐到网格上。请看下面的示例。

![可缩放的空心圆柱体](images/ScalableHollowCylinder.png)

这个空心圆柱体是可缩放的，因为它的所有顶点都在网格上对齐。放大或缩小它都能使顶点整齐地保持在整数网格上，这对于类 Quake 地图编译器非常有利，因为当几何体在网格上对齐时，编译器通常能更好地处理几何体。可缩放形状只能具有 12、24、48 或 96 条边。这类曲线也被称为 [CZG 曲线](https://www.quaketerminus.com/hosted/happymaps/curv_tut.htm)。

![非对称可缩放圆柱体](images/ScalableCylinderStretch.gif)

如果创建非对称的可缩放形状，它不会像其他形状那样缩放以适应鼠标绘制的包围盒。相反，只有它的中间部分会被拉长，以便顶点保持在网格上。这甚至适用于圆锥体和 UV 球体，从而使不同的形状仍然可以相互贴合。

拱形是作为空心圆柱体的上半部分创建的。轴向是拱形穿过的方向，类似于隧道。边数、厚度和圆形模式的工作方式与圆柱体完全相同，因此使用相同参数构建的拱形和圆柱体会完美贴合。当使用可缩放圆形模式时，将边界绘制得高于半圆会使两侧垂直向下延伸，作为拱形的“支撑柱”。勾选 _Spandrel（拱肩填充）_ 后，会使用额外的 Brush 填满拱形与包围盒之间的空隙。

岩石形状会创建填满鼠标绘制包围盒的随机岩石地貌。根据类型不同，岩石地貌可以由单个 Brush 或多个重叠的 Brush 组成。支持以下类型：

类型     说明
----     -----------
Boulder  单个带平底的圆巨石
Shelf    单个带有斜切边和不平整顶部的宽扁岩板/岩架
Strata   向上渐缩并略微横向偏移的多层叠层岩
Crag     高耸陡峭的主岩峰，外围环绕小巨石
Crystal  从中心柱向外散射的多棱晶体/水晶簇
Columns  围绕中心高柱分布、高度错落的多棱石柱群
Basalt   高度错落的六边形玄武岩蜂窝柱群
Cluster  散落的小石块集群

_Detail（细节）_ 滑块控制岩石地貌的复杂度。每种类型对细节的解释方式各不相同，例如巨石的面数、叠层岩的层数，或晶体簇的分支碎片数量。在相同的细节设置下，更大的包围盒会生成更复杂的岩石；对于玄武岩和碎石集群，细节设置决定了块体的大小，因此更大的包围盒会被更多的块体填满。

![岩石形状界面](images/RockShapeUi.png)

_Seed（种子）_ 决定了岩石地貌的随机变化。相同的类型、细节、种子和包围盒始终会生成完全相同的岩石地貌，因此你可以更改种子来尝试岩石地貌的不同变体。种子旁边的按钮可以设置一个随机种子。如果切换其右侧的按钮，每次创建岩石或将参数应用于所选内容时，种子都会自动递增，从而使每个新岩石地貌的外观都各不相同。

除了石柱和玄武岩的倾斜顶面外，岩石地貌的顶点都会自动吸附到整数网格上。

### 创建复杂形状 {#creating-complex-shapes}

![绘制矩形并复制](images/CreateBrushByDuplicatingPolygon.gif) 如果你想创建不是简单轴对齐长方体的 Brush，可以使用 Brush 工具。Brush 工具允许你定义一组点并创建这些点的凸包。凸包是包含所有点的最小凸体积。这些点将成为新 Brush 的顶点，除非它们位于 Brush 内部（在这种情况下它们会被丢弃）。相应地，Brush 工具为你提供了几种放置点的方法，但有两个限制：第一，你只能在 3D 视图中放置点；第二，你只能以其他 Brush 作为参考来放置点。

要使用 Brush 工具，首先必须通过选择 #menu(Menu/Edit/Tools/Brush Tool) 激活它。然后，你可以通过左键单击其他 Brush 的面在网格上放置单个点。此外，你可以双击某个面在其所有顶点上放置点。你还可以在现有 Brush 表面上按住左键拖动以绘制矩形形状，从而在该矩形的四个角上放置四个点。最后，如果目前放置的点构成了一个多边形，你可以按住 #key(Shift) 并按住左键拖动，沿其法线方向复制并移动该多边形。放置好所有点后，按 #action(Controls/Map view/Create brush) 即可实际创建 Brush。

点放置后无法单独修改或移除，只能按 #action(Controls/Map view/Cancel) 键丢弃所有点。

### 创建 Patch（仅限 Quake 3） {#creating_patches}

Patch 是从 Brush 面创建的。创建一个 Brush 并选择其一个或多个面，然后选择 #menu(Menu/Edit/Convert Selection to Patches)。TrenchBroom 将为每个选中的面创建一个 Patch，并且该 Brush（或多个 Brush）将从地图中移除。随后你可以使用控制点工具来细化 Patch。

![正方形面生成单个 Patch](images/CreatePatches_Square.gif)

请注意，选中的面不一定是矩形。TrenchBroom 会创建多个与该面形状完全匹配的 Patch。在此过程中，它会将面细分为四边形，并为每个四边形创建一个 Patch。TrenchBroom 会优先采用能生成对称 Patch 的细分方式。

![八边形面生成三个 Patch](images/CreatePatches_Octagon.gif)

如果面的顶点数为奇数，则会创建一个退化的三角形 Patch，其中多个控制点重合。

![五顶点面生成三角形 Patch](images/CreatePatches_Corner.gif)

### 编辑 Patch（仅限 Quake 3） {#editing_patches}

可以使用控制点工具编辑 Patch。

![编辑控制点](images/ControlPointTool.png)

调整控制点的工作方式与[编辑顶点](#vertex_editing)相同，因此我们在此不再重复所有细节。只需选择并拖动控制点即可调整 Patch 的形状。与顶点工具类似，位于相同位置的控制点会汇聚在一起，以便你可以一起编辑相邻 Patch 的控制点。

当该工具处于激活状态且选中了一个 Patch 时，可以使用地图视图顶部的两个微调框更改控制点网格的行数或列数：

![编辑控制点网格](images/ControlPointToolSpinBoxes.png)

只有当所有选中的 Patch 具有相同数量的控制点行数和列数时，这些微调框才会启用。当你调整其中一个微调框时，Patch 的新形状将尽可能逼近先前的形状：当控制点数量增加时，新形状将与先前的形状完全相同；但当控制点数量减少时，由于信息丢失，新形状无法完美再现先前的形状。

### 创建实体 {#creating_entities}

实体分为两类：点实体和 Brush 实体，实体的创建方式取决于其类型。在接下来的小节中，我们将介绍创建点实体的三种方法和创建 Brush 实体的两种方法。

#### 点实体 {#point_entities}

创建新点实体有三种方法。首先，你可以使用[地图视图上下文菜单](#map_view_context_menu)在 3D 和 2D 视图中放置新实体。要打开上下文菜单，请在视图中右键单击。要创建拾取武器或怪物等点实体，请打开“Create Point Entity”子菜单并从子菜单中选择正确的实体定义。

![使用上下文菜单放置实体](images/CreateEntityContextMenu.png)

新创建实体的位置取决于你是在 3D 视图还是 2D 视图中单击。如果你在 3D 视图中单击，实体将放置在鼠标下方的 Brush 上。如果鼠标下方没有 Brush，实体将放置在默认距离处。请注意，实体的包围盒会吸附到网格上。如果你在 2D 视图中单击，实体的位置同样取决于单击时鼠标下方的内容。如果鼠标下方有选中的 Brush，新实体将放置在该 Brush 上。如果鼠标下方没有选中的 Brush，实体将放置在最近选中对象包围盒的远端。同样，新建实体的包围盒会吸附到网格上。

![实体浏览器](images/EntityBrowser.png) 其次，你可以通过从实体浏览器中拖拽来创建新的点实体。实体浏览器位于实体检查器页面中。在实体浏览器底部，你可以找到许多用于更改排序顺序和过滤浏览器中显示实体的控件。

最左侧的下拉列表允许你更改排序顺序。实体可以按名称或使用次数排序（最常用的实体排在最上方）。“Group”按钮切换按分类对实体进行分组，分类由实体名称的前缀派生。例如，所有以 "key_" 开头的实体都会放入名为 "key" 的分类中。标记为“Used”的按钮切换所有未使用的实体，按下时浏览器中仅显示地图中已使用的实体。要按名称过滤实体，请在右侧的搜索框中输入文本，浏览器中将仅显示包含搜索文本的实体。

要创建新实体，只需将其从浏览器中拖出并放到 3D 或 2D 视图中。如果拖到 3D 视图中，实体将定位在鼠标下方的 Brush 上，其包围盒会吸附到网格。如果将实体拖到 2D 视图中，其位置由最近选中对象的远端边界决定。

最后，你可以在[偏好设置](#keyboard_shortcuts)中分配键盘快捷键来创建特定实体。这对于经常使用的实体（如光源）非常有用。实体将在鼠标光标下创建；其位置计算方式与使用上下文菜单相同。

#### Brush 实体 {#brush_entities}

![将 Brush 移动到 Brush 实体](images/MoveBrushesToEntity.png) 创建 Brush 实体同样使用上下文菜单完成。选择几个 Brush 并在其上右键单击，然后从菜单中选择所需的 Brush 实体。要将 Brush 从一个 Brush 实体移动到另一个 Brush 实体，请选择要移动的 Brush，并在属于目标 Brush 实体的 Brush 上右键单击，然后选择“Move brushes to Entity ENTITY”，其中“ENTITY”是目标 Brush 实体的名称（例如左图中的 "func_door"）。如果包含待移动 Brush 的原 Brush 实体变空，它将被自动删除。要将 Brush 从 Brush 实体移回 world 实体并清除内容标志，请选择这些 Brush，右键单击并选择“Make Structural”。

此外，你还可以在[偏好设置](#keyboard_shortcuts)中分配键盘快捷键来创建特定的 Brush 实体。

通常，通过复制现有对象来创建新对象要快得多。可以使用 TrenchBroom 中的专用功能复制对象，或者直接复制并粘贴它们。

### 复制对象 {#duplicating_objects}

当前选中的对象可以通过选择 #menu(Menu/Edit/Duplicate) 进行复制。这将在原地复制对象，即副本保留原始对象的确切位置。为了提供视觉反馈，复制的对象会快速闪烁白色。在下面的短视频中，你可以看到选中的 Brush 被复制，随后复制的 Brush 被向上移动。

![原地复制 Brush](images/DuplicateInPlace.gif)

在很多情况下，你会希望在复制对象后立即将其移动到不同位置，因为让副本保留在与原始对象相同的位置很少有用。因此你也可以一次性复制并移动对象，而无需执行两个独立的操作。要复制并移动对象，可以使用以下键盘快捷键：

方向          快捷键 (2D)                                                                                          快捷键 (3D)
---------     -------------                                                                                        -------------
左            #action(Controls/Map view/Duplicate and move objects left)                                           #action(Controls/Map view/Duplicate and move objects left)
右            #action(Controls/Map view/Duplicate and move objects right)                                          #action(Controls/Map view/Duplicate and move objects right)
上            #action(Controls/Map view/Duplicate and move objects up; Duplicate and move objects forward)         #action(Controls/Map view/Duplicate and move objects backward; Duplicate and move objects up)
下            #action(Controls/Map view/Duplicate and move objects down; Duplicate and move objects backward)      #action(Controls/Map view/Duplicate and move objects forward; Duplicate and move objects down)
前            #action(Controls/Map view/Duplicate and move objects forward; Duplicate and move objects down)       #action(Controls/Map view/Duplicate and move objects up; Duplicate and move objects forward)
后            #action(Controls/Map view/Duplicate and move objects backward; Duplicate and move objects up)        #action(Controls/Map view/Duplicate and move objects down; Duplicate and move objects backward)

本质上，这些快捷键与在 3D 和 2D 视图中[移动对象](#moving_objects)所使用的快捷键相同，只不过需要同时按住 #key(Ctrl)。同理，你可以在按住 #key(Ctrl) 的同时左键拖动选中的对象，以复制并移动所有选中的对象。

![复制并移动 Brush](images/DuplicateAndMove.gif)

请注意，在上图中，选中的 Brush 在向右移动的同时会闪烁。这表明在这种情况下，复制和平移是同时发生的，而不是像前一个示例那样先后进行。

### 复制和粘贴 {#copy-and-paste}

你可以通过选中对象并选择 #menu(Menu/Edit/Copy) 来复制对象。TrenchBroom 会创建选中对象的文本表示（就像保存到地图文件中一样），并将该文本表示放入剪贴板。这使你能够将它们粘贴到地图文件中，也可以直接从地图文件中复制对象并粘贴到 TrenchBroom 中。请注意，你也可以复制 Brush 面，这同样会将该 Brush 面的文本表示放入剪贴板。复制 Brush 面后，你可以将该面的属性（材质、偏移、缩放等）粘贴到其他选中的 Brush 面上。

有两个菜单命令可用于将剪贴板中的对象粘贴到地图中。其中较简单的是 #menu(Menu/Edit/Paste at Original Position)，它直接从剪贴板粘贴对象而不改变其位置。另一个命令位于 #menu(Menu/Edit/Paste)，它不会在原始位置粘贴对象，而是尝试使用当前鼠标位置来定位它们。如果粘贴到 3D 视图中，粘贴的对象将放置在鼠标下方的 Brush 顶部。如果鼠标下方没有 Brush，对象将放置在默认距离处。粘贴对象的包围盒会吸附到网格，TrenchBroom 会尝试将粘贴对象的包围盒中心保持在鼠标光标附近。以下视频演示了这些概念：复制灯具后多次粘贴。

![在 3D 视图中粘贴对象](images/PastePositioning3D.gif)

粘贴到 2D 视图中的对象定位尝试实现类似的效果：将粘贴的对象定位为与最近选中对象的边界远端对齐，同时保持在鼠标下方，且中心吸附到网格。

## 编辑对象 {#editing-objects}

以下各节首先介绍适用于所有对象的编辑操作，例如移动、旋转、缩放和删除，然后说明本章涉及的 Brush 塑形工具：挤出、裁剪、路径放样和倒角。后续章节将分别介绍顶点编辑与 CSG、材质以及实体组织。本章最后说明 TrenchBroom 的撤销与重做行为。

### 移动对象 {#moving_objects}

你可以使用鼠标或键盘快捷键来移动对象。在选中的对象上按住左键拖动即可移动它（以及所有其他选中的对象）。在 3D 视图中，对象默认在 XY 平面上移动。按住 #key(Alt) 可以沿 Z 轴垂直移动对象。在 2D 视图中，对象在视图的视平面上移动。在 2D 视图中无法使用鼠标更改对象与摄像机的距离。如果启用了网格吸附，移动距离将按分量吸附到网格，也就是说，如果网格设置为 16 个单位，你可以沿任一方向按 16 个单位移动对象。

你也可以使用键盘来移动对象。每次按下下表中的快捷键时，对象将沿相应方向移动当前网格大小的距离。还请记住，你可以按住 #key(Ctrl) 并按下下面列出的键盘快捷键之一，在单次操作中[复制对象并移动它们](#duplicating_objects)。

方向          快捷键 (2D)                                                              快捷键 (3D)
---------     -------------                                                            -------------
左            #action(Controls/Map view/Move objects left)                             #action(Controls/Map view/Move objects left)
右            #action(Controls/Map view/Move objects right)                            #action(Controls/Map view/Move objects right)
上            #action(Controls/Map view/Move objects up; Move objects forward)         #action(Controls/Map view/Move objects backward; Move objects up)
下            #action(Controls/Map view/Move objects down; Move objects backward)      #action(Controls/Map view/Move objects forward; Move objects down)
前            #action(Controls/Map view/Move objects forward; Move objects down)       #action(Controls/Map view/Move objects up; Move objects forward)
后            #action(Controls/Map view/Move objects backward; Move objects up)        #action(Controls/Map view/Move objects down; Move objects backward)

请注意，键盘快捷键的含义取决于使用它们的视图。在 2D 视图中使用 #action(Controls/Map view/Move objects up; Move objects forward) 会沿向上轴方向移动选中的对象；而在 3D 视图中使用时，它会在编辑平面上（大致）沿摄像机视线方向（即向前）移动对象。同样，在 2D 视图中使用 #action(Controls/Map view/Move objects forward; Move objects down) 会沿法线轴方向（即向前）移动选中的对象；而在 3D 视图中使用时，它会沿 Z 轴负方向移动对象。

![移动对象](images/MoveObjectsByOffset.png)

要按指定偏移量移动对象，请选择 #menu(Menu/Edit/Move objects) 调出可以输入向量的窗口。点击“OK”，当前选中的对象将按该向量移动。

### 旋转对象 {#rotating_objects}

在 TrenchBroom 中旋转对象最简单的方法是使用以下键盘快捷键：

快捷键                                                          类型     旋转 (3D)                             旋转 (2D)
--------                                                        ----     -------------                         -------------
#action(Controls/Map view/Roll objects clockwise)               滚转     顺时针绕视轴                          顺时针绕法线轴
#action(Controls/Map view/Roll objects counter-clockwise)       滚转     逆时针绕视轴                          逆时针绕法线轴
#action(Controls/Map view/Pitch objects clockwise)              俯仰     顺时针绕右轴                          顺时针绕右轴
#action(Controls/Map view/Pitch objects counter-clockwise)      俯仰     逆时针绕右轴                          逆时针绕右轴
#action(Controls/Map view/Yaw objects clockwise)                偏航     顺时针绕 Z 轴                         顺时针绕向上轴
#action(Controls/Map view/Yaw objects counter-clockwise)        偏航     逆时针绕 Z 轴                         逆时针绕向上轴

如果旋转工具处于激活状态，这些键盘快捷键将使用该工具的旋转手柄和视图上方输入控件设置的旋转中心与角度来旋转选中的对象。如果旋转工具未激活，旋转中心为当前选中对象包围盒的中心（吸附到网格），旋转角度固定为 90°。

![3D 旋转手柄](images/RotateHandle3D.png) 旋转工具比键盘快捷键提供了对旋转更多的控制。按 #menu(Menu/Edit/Tools/Rotate Tool) 激活旋转工具，视图中将出现一个旋转手柄。旋转手柄允许你设置旋转中心，并执行选中对象绕 X、Y 或 Z 轴的实际旋转。在 3D 视图中，你可以通过左键拖动旋转手柄的相应部分来使对象绕上述任意轴旋转；但在 2D 视图中，你只能使对象绕该视图的法线轴旋转。旋转角度默认设置为 15 度，但在激活旋转工具时，可以在编辑视图上方显示的控件中进行更改。旋转过程中，当前旋转角度会显示在旋转手柄的中心。

在 3D 视图中，旋转手柄如左图所示。它有三个轴，采用常规的颜色编码：X 轴为红色，Y 轴为绿色，Z 轴为蓝色。除各轴外，它还有三个四分之一圆弧（同样采用颜色编码）以及位于中心的一个小球形手柄。如果用鼠标左键拖动中心手柄（黄色球体），即可更改旋转中心。移动旋转中心的工作方式与[移动对象](#moving_objects)完全相同。如果将鼠标悬停在中心手柄上，你会注意到旋转中心的坐标显示在中心手柄上方，并且该手柄会以红色轮廓高亮显示。要执行旋转，必须拖动三个带颜色编码的四分之一圆弧之一。当你悬停在其中一个圆弧上时，它会高亮显示以指示你可以开始拖动。用鼠标左键单击并拖动蓝色四分之一圆弧会使对象绕 Z 轴旋转，红色和绿色手柄同理（见下方短片）。

![2D 旋转手柄](images/RotateHandle2D.png) 在 2D 视图中，旋转手柄仅显示为一个圆圈，中心有一个较小的圆形手柄。中心手柄允许你在该视图的视平面上移动旋转中心，外圈则允许你执行旋转。在 2D 视图中，手柄同样采用颜色编码，外圈的颜色以类似于 3D 旋转手柄的方式反映旋转轴。要开始旋转，请以圆形轨迹拖动外圈。与 3D 视图一样，旋转角度将吸附到编辑视图上方角度控件中输入的任何值，并且在旋转过程中，角度会指示在旋转手柄的中心。

![旋转工具控件](images/RotateToolControls.png)

与移动工具一样，旋转工具在视图上方放置了一些控件。最左侧是一个组合框，显示旋转中心的坐标。如果你在 2D 或 3D 视图中移动旋转手柄，此组合框会自动更新。如果你想手动设置旋转中心，可以在此处输入三个坐标并按 #key(Return)。或者，你可以点击标记为“Reset”的按钮将旋转中心设置到当前选中对象包围盒的中心（吸附到网格）。最后，你可以使用组合框将旋转中心恢复为先前使用的值。其余控件允许你通过在文本框中输入角度、从下拉列表中选择旋转轴并点击“Apply”按钮来执行旋转。

![在 3D 视图中绕 Z 轴旋转对象](images/RotateTool.gif)

如果仔细观察上面的视频，你会注意到图中的实体（绿色护甲）随同其所在的 Brush 一起平滑地旋转。首先，它相对于 Brush 的位置似乎没有改变；其次，它的旋转角度也根据用户执行的旋转进行了相应的更改。TrenchBroom 是否以及如何调整实体的旋转角度取决于以下规则。

- "angles" 被解释为 "pitch yaw roll"（如果实体模型是 Quake MDL，则 pitch 反转）
- 如果实体 classname 以 "light" 开头，"mangle" 被解释为 "yaw pitch roll"，否则它是 "angles" 的同义词
- "angle" 被解释为绕 Z 轴的旋转角度
- 如果点实体的包围盒未在 XY 平面居中（例如 Quake 的 misc_explobox），TrenchBroom 中旋转该实体的尝试将被阻止。这样做是为了防止模型旋转超出碰撞盒（碰撞盒在 Quake 中不旋转）。

最后，如果 TrenchBroom 找到了包含实体旋转角度的属性，它会根据用户执行的旋转来调整该属性的值。这些规则相当复杂，因为遗憾的是，实体定义中并未包含应如何对实体应用旋转的信息。但在实际操作中，当你在编辑器中使用旋转工具时，它们通常能如预期般正常工作。

![复选框](images/UpdateAnglePropertyAfterTransform.png)

当旋转工具激活时，可以通过切换“Apply”按钮右侧的复选框来暂时禁用此行为。如果取消勾选，无论是通过旋转工具还是快捷键旋转实体时，TrenchBroom 都不会更新除 origin 之外的任何实体属性。

### 翻转对象 {#flipping_objects}

翻转的效果是镜像选中的对象，镜像平面由选中对象包围盒的中心（吸附到网格）和法向量定义。平面的法向量取决于具体的翻转命令以及 3D 视图中摄像机的视线方向或当前获得焦点的 2D 视图的视平面。下表说明了如何从这些信息中推导法向量。

快捷键                                                    方向          法线 (2D)     法线 (3D)
--------                                                  ---------     -----------   -----------
#action(Controls/Map view/Flip objects horizontally)      水平          右轴          轴对齐右轴
#action(Controls/Map view/Flip objects vertically)        垂直          向上轴        Z 轴

在 3D 视图的情况下，镜像平面的法线是与摄像机右轴最接近的坐标系轴。这意味着如果摄像机大致指向 Y 轴方向，因此其右轴大致指向 X 轴方向，则镜像平面的法线将是 X 轴。有时，由于右轴同时接近两个坐标系轴，你可能无法确定哪个坐标系轴与摄像机右轴最接近。为避免这种混淆，最好在 2D 视图中执行翻转。

### 缩放对象 {#scaling_objects}

按 #menu(Menu/Edit/Tools/Scale Tool) 激活缩放工具。如果你知道确切的 X/Y/Z 缩放比例因子，可以在工具栏中输入它们并点击“Apply”。选中的对象将相对于其包围盒中心进行缩放。

![缩放工具工具栏](images/ScaleToolToolbar.png)

此外，还可以通过多种方式以交互方式缩放选中的对象。

在 3D 视图中：

- 拖动包围盒的一个面仅拉伸该轴。

    ![Dragging a side of the bounding box](images/Scale3DSide.gif)

- 拖动一条边按比例拉伸包围盒的两个相邻面。

    ![Dragging an edge of the bounding box](images/Scale3DEdge.gif)

- 拖动一个角按比例调整所有 3 个轴的大小。

    ![Dragging a corner of the bounding box](images/Scale3DCorner.gif)

在 2D 视图中：

- 角允许沿 2 个轴进行无约束缩放。
- 边仅拉伸单个轴，与 3D 视图中相同。

在 2D 和 3D 视图中均可使用两个修饰键：

- 按住 #key(Shift) 可在 3D 视图中按比例缩放所有三个轴，或在 2D 视图中仅按比例缩放垂直于摄像机的两个轴。你可以在拖动过程中按下/松开 #key(Shift)。（在 3D 中拖动角时 #key(Shift) 没有效果，因为所有 3 个轴已经按比例缩放。）

- 按住 #key(Alt) 可将缩放锚点移动到包围盒的中心。否则，锚点位于被拖动手柄的对侧。

    ![Dragging a side of the bounding box](images/Scale3DSideCenter.gif)


### 切变对象 {#shearing_objects}

按 #menu(Menu/Edit/Tools/Shear Tool) 激活切变工具。拖动包围盒的一个面可使对象沿该平面发生切变。如果切变的不是包围盒顶面或底面，可以同时按住 #key(Alt) 进行垂直拖动。

切变工具中的对齐锁定仅在 Valve 220 格式的地图中有效。

![在 3D 视图中的垂直切变](images/Shear3DVertical.gif)

### 删除对象 {#deleting-objects}

删除对象非常简单，只需选中它们并选择 #menu(Menu/Edit/Delete)。请注意，如果删除了 Brush 实体的所有剩余 Brush，该实体将自动被删除。同样，如果删除了组的所有剩余对象，该组也会被自动删除。

## 塑造 Brush {#shaping-brushes}

TrenchBroom 提供了多种改变 Brush 形状的工具。本章先介绍如何通过挤出来延伸 Brush、冲压新 Brush 或移动面，然后介绍裁剪、沿生成路径对面进行放样，以及对边或顶角进行倒角。更通用的顶点编辑与 CSG 操作将在下一章介绍。

### 挤出 {#extrusion}

使用挤出工具，可以通过用鼠标沿各面的法向量移动面来挤出 Brush。要挤出选中的 Brush，请按住 #key(Shift) 并将鼠标指针移动到要移动的面或其附近。你会注意到 Brush 的一个面会以黄色轮廓高亮显示。在仍然按住 #key(Shift) 的同时使用鼠标左键拖动，即可沿其法线移动高亮显示的面。请注意，只要位于 Brush 后方的面具有从摄像机可见的边，你也可以移动这些面。

![在 3D 视图中挤出 Brush](images/ExtrudeTool3D.gif)

请注意，你不能使用挤出工具更改 Brush 的面数。这意味着你无法无限期地将面推回 Brush 内部。一旦该移动会使其他面消失，TrenchBroom 就会拒绝进一步移动它。同样，从 Brush 中拉出面（这可能会使该面消失）也是不允许的。

如果在开始拖动时按住 #key(Ctrl)，Brush 将不会被挤出，而是会创建一个新的 Brush。原始 Brush 与新 Brush 合并在一起的形状，与未按住 #key(Ctrl) 直接挤出时的形状相同，并且两个 Brush 在你最初拖动的面所在的位置被分割开来。

![在 3D 视图中分割 Brush](images/ExtrudeTool3DSplitMode.gif)

按住 #key(Ctrl) 开始拖动时，你也可以向内拖动以分割原始 Brush：

![在 3D 视图中向内分割 Brush](images/ExtrudeTool3DSplitInwardMode.gif)

你还可以使用挤出工具通过移动多个 Brush 的面来同时挤出它们，但前提是这些面必须完美对齐。正如以下动画所示，仅面相互平行是不够的 —— 它们必须完全相同。不过请注意，它们的法线可以是相反的，因此你也可以调整两个 Brush 接触处面的大小。如果两个面具有完全相同的顶点，你可以通过悬停在共享边上来选择共享面进行挤出。挤出相反面时禁用分割，以避免创建重叠的 Brush。

![挤出多个 Brush](images/ExtrudeTool3DMultipleBrushes.gif)

挤出工具当然也适用于 2D 视图，但在 2D 视图中无法移动选中 Brush 后方的面。在这两种情况下，TrenchBroom 使用两种方法来确定如何吸附拖动面的距离：

- 距离吸附到当前网格大小，例如，如果当前网格大小为 16，沿法线拖动面 17.5 个单位时，它将移动 16.0 个单位。如果正在调整作为曲线一部分的 Brush 大小，这非常有用，因为拖动后它们的面仍会对齐。
- 被拖动面的顶点吸附到网格平面，即每当至少一个顶点分量（X、Y 或 Z）是当前网格大小的倍数时，该面就会吸附到该顶点。这使得将一个面对齐到其他相邻面变得非常容易。

两种吸附模式同时生效。在某些情况下，你可能需要将摄像机移近某个面，以便在拖动面时获得足够的精度。

#### 冲压 Brush {#stamping-brushes}

通常，挤出的 Brush 会延续原始 Brush 的形状，但这并不总是符合预期。

![挤出与冲压对比](images/ExtrudeToolStamping.png)

在图中，左侧选中的 Brush 是从其下方 Brush 的顶面挤出的，它延续了该 Brush 的平截头体形状。右侧选中的 Brush 是从其下方 Brush 的顶面冲压出来的。冲压不会延续原始 Brush 的形状，而是直接复制选中的面并沿其法线移动。新 Brush 随后成为原始面及其副本顶点的凸包。

要冲压 Brush，在按住 #key(Shift) 的同时额外按住 #key(Ctrl) 和 #key(Alt)，然后拖动选中 Brush 的一个面。

#### 移动面而不是挤出 {#moving_faces}

Brush 挤出工具提供了一种快速移动 Brush 单个面的方法。开始拖动面时，在按住 #key(Shift) 的同时按住 #key(Alt) 即可启用此模式。你会注意到面像往常一样高亮显示，但当你开始拖动鼠标时，面将仅沿拖动方向移动。在 2D 视图中，移动不受面法线限制，其他面也会受到影响。在 3D 视图中，移动受面法线限制。

![移动面 (2D 视图)](images/ExtrudeTool2DFaceMoving.gif)

距离吸附到当前网格大小。如果多个面位于同一平面上，则可以同时移动多个面。[UV 锁定](#uv_lock)设置控制使用此模式拖动面时是否使用对齐锁定。

### 裁剪 {#clipping}

裁剪是 Quake 地图中最基础的操作，这是由 Brush [由平面构建而成](#brush_geometry)的特性决定的。本质上，裁剪所做的就是在 Brush 上添加一个新平面，并根据 Brush 的形状移除变得多余的其他平面。在 TrenchBroom 中，裁剪是通过裁剪工具完成的，你可以通过选择 #menu(Menu/Edit/Tools/Clip Tool) 来激活该工具。裁剪工具允许你以多种方式定义裁剪平面，并允许你将该平面应用于选中的 Brush。

应用裁剪平面有三种不同的结果：丢弃选中 Brush 位于（有向）裁剪平面前方的所有部分、丢弃选中 Brush 位于裁剪平面后方的所有部分，或者将每个选中的 Brush 切割成两块。下图说明了这三种模式：

![三种裁剪模式](images/ClipModes.png)

在所有三张图中，都有一个由两点定义的裁剪平面。该裁剪平面将图中的单个 Brush 切割成两部分，其中左侧部分位于平面下方，右侧部分位于平面上方。在第一张图中，裁剪模式设置为保留 Brush 位于裁剪平面下方的部分，并丢弃位于裁剪平面上方的部分。生成的 Brush 形状如图像中 Brush 的红色部分所示。在第二张图中，裁剪模式设置为保留 Brush 的两部分，此裁剪操作的结果将是两个 Brush。在第三张图中，裁剪模式设置为保留 Brush 位于裁剪平面上方的部分，并丢弃另一部分。这与第一种情况相反。在裁剪工具中，你可以通过按 #action(Controls/Map view/Toggle clip side) 在这三种模式之间循环切换。定义裁剪平面有两种方法：更常见的方法是在 3D 或 2D 视图中放置至少两个且最多三个点（在此上下文中，这些点称为裁剪点）。另一种方法是使用现有的 Brush 面来定义裁剪平面。

#### 裁剪点 {#clip-points}

要放置裁剪点，只需在裁剪工具激活时在视图中单击左键即可。或者，你也可以通过按住鼠标左键拖动一次添加两个裁剪点。在这种情况下，第一个裁剪点放置在拖动的起点，第二个裁剪点放置在拖动的终点。在 3D 视图中，你只能在已有的 Brush 上放置裁剪点，而在 2D 视图中，你可以放置在任意位置。裁剪点会吸附到网格，但在 3D 视图中有一个注意事项，我们将在下面进行说明。当裁剪工具激活时，它会以鼠标指针附近显示的橙色球体形式向你提供一些反馈。该球体指示裁剪点在吸附到网格后将被放置的位置。仅当裁剪点实际可以放置在鼠标下方的点或其附近时，才会显示此反馈球体。

放置好两个裁剪点后，TrenchBroom 将尝试推测裁剪平面，尽管此时约束不足：只用两个点无法唯一定义一个平面。如果你对 TrenchBroom 确定的裁剪平面感到满意，则可以通过按 #action(Controls/Map view/Perform clip) 应用裁剪操作。否则，你可以放置第三个点来完整定义裁剪平面，或者可以修改已放置的裁剪点。要修改裁剪点，只需用鼠标左键单击并拖动它即可。要移除最近放置的裁剪点，可以选择 #menu(Menu/Edit/Delete)。

#### 裁剪点吸附 {#clip-point-snapping}

在 3D 视图中，裁剪点只能放置在已存在 Brush 的面上。这样的裁剪点会吸附到投影在该 Brush 面上的网格。因此它看起来吸附到了投影网格上，但同时也保持粘附在 Brush 表面上。如果该点在所有维度上都被吸附，则它要么沉入要么脱离其放置所在的 Brush 面。TrenchBroom 通过将裁剪点粘附到用户放置它们的 Brush 面上来避免这种情况。这意味着如果你尝试在 3D 视图中移动已放置的裁剪点，该点将被移动到鼠标下方 Brush 表面上最近的吸附点。

在 2D 视图中，裁剪点仅吸附到可见网格，因此不受限于粘附在 Brush 表面上。你可以在任何想要的视图中放置裁剪点，也可以在任何其他视图中移动在一个视图中放置的裁剪点，但网格吸附将遵循你用于移动裁剪点的视图的网格。这意味着如果你使用 2D 视图移动在 3D 视图中放置的裁剪点，则该点可以被拖离其放置所在的 Brush 面并进入空白处。相反，如果你使用 3D 视图移动在 2D 视图中放置的裁剪点，该裁剪点将吸附到鼠标下方的 Brush 表面上，或者如果鼠标下方没有 Brush 表面，则根本不会移动。

#### 匹配裁剪平面 {#matching-clip-plane}

![匹配裁剪平面](images/MatchingClipPlane.gif) 裁剪平面也可以通过与现有 Brush 面对齐来定义。要在 3D 视图中使裁剪平面与现有 Brush 面对齐，必须双击该面。这样该 Brush 面将显示橙色轮廓，并定义一个与该面的平面完全匹配的裁剪平面。在使几何体与其他几何体贴合时，这非常有用。请注意，裁剪平面的平面点就是所匹配 Brush 面的平面点，因此使用此特定功能时不会出现微泄漏 (microleak) 问题。

### 路径放样 {#sweeping}

路径放样工具会在选中的 Brush 面与这些面的副本（称为目标端面）之间生成一系列 Brush 来填充空隙。根据目标端面的放置位置和所选路径，它可以沿直线对面进行放样、绕轴旋转以构建拱门或管道，或沿 S 形曲线生成路径，并可同时产生扭转和渐扩或渐缩效果。对于 Valve 风格的地图格式，UV 设置既可以保留源面的投影，也可以在相连的边界面和各个放样分段之间连续旋转纹理对齐。连续对齐会保留纹理缩放，并在闭合轮廓上留下一条接缝。要使用路径放样工具，请选择一个或多个 Brush 面，然后选择 #menu(Menu/Edit/Tools/Sweep Tool)。

桥接模式连接不同 Brush 上两个互不相连的选中面组件。每个组件可以包含一个或多个边相连的面，并且两个组件必须具有匹配的面、顶点和共享边拓扑。第一个组件提供入口几何体和侧面材质；第二个组件是精确且锁定的目标端。使用 **Swap ends** 可反转该方向。桥接模式会自动匹配循环或反向的顶点顺序，并使用原始端点顶点，使生成的 Brush 与两个选中组件无缝衔接而不会产生坐标接缝。如果某个插值分段无法形成有效的凸 Brush，预览会报告受影响的分段，而不会应用不完整的几何体。

![使用路径放样工具旋转面形成弯头](images/SweepTool.gif)

路径放样工具激活时，虚影轮廓会显示目标端面的最终位置，手柄可用于调整它：

- 拖动手柄中心可移动目标端面。
- 拖动其中一个圆环可使其绕相应轴旋转。
- 拖动绿色手柄可对目标端面进行均匀缩放，使放样形状渐扩或渐缩。
- 按 #action(Controls/Map view/Move objects up; Move objects forward) 和其他移动快捷键可将目标端面移动一个网格步长。
- 按 #action(Controls/Map view/Roll objects clockwise) 和其他旋转快捷键可将目标端面旋转一个角度吸附步长。
- 按 #action(Controls/Map view/Increase sweep scale) 或 #action(Controls/Map view/Decrease sweep scale) 可将缩放手柄向外或向内移动一个网格步长，从而放大或缩小目标端面。

调整目标端面时，生成的 Brush 会以预览形式显示在视图中。在停用该工具之前，作用于选区的快捷键（包括 UV 编辑）均不可用。编辑视图上方的控件决定如何填充空隙：

- **Segments**（分段数）是选中面与目标端面之间创建的 Brush 数量。
- **Path**（路径）选择 Brush 的布局方式：Arc（圆弧）绕根据旋转推导出的轴旋转面，Straight（直线）沿直线对面进行放样，S-bend（S 弯）使其沿 S 形曲线延伸。三种模式下的目标端面最终都位于相同位置。
- **Iterations**（迭代次数）重复执行路径放样，并从上一个目标端面继续延伸。例如，一段在转弯时升高的圆弧经过多次迭代后会形成螺旋楼梯。
- **Snap to integer grid**（吸附到整数网格）将生成 Brush 的顶点舍入到整数坐标。
- **Reset**（重置）将目标端面移回选中的面上。

按 #action(Controls/Map view/Perform sweep) 可用 Brush 填充空隙并选中生成结果。按 #action(Controls/Map view/Cancel) 会将目标端面恢复到起始位置；再次按下该快捷键会停用路径放样工具。

### 倒角 {#chamfering}

选择 #menu(Menu/Edit/Tools/Chamfer Tool) 可对选中的 Brush 边进行倒角，或切去选中的 Brush 顶角。目标选择器用于在边手柄和顶点手柄之间切换。选择一个或多个手柄以预览结果，然后调整倒角距离并应用操作。边倒角还支持多个分段，以生成圆滑轮廓。UV Lock 决定如何保留受影响面的投影。

## 撤销与重做 {#undo_redo}

在 TrenchBroom 中执行的几乎所有操作都可以通过选择 #menu(Menu/Edit/Undo) 撤销。这不仅适用于移动对象等修改地图文件的操作，也适用于选择、隐藏和锁定等不改变地图文件的操作。可撤销的操作次数没有限制；撤销操作后，可以选择 #menu(Menu/Edit/Redo) 将其重做。

### 撤销合并与事务 {#undo-collation-and-transactions}

TrenchBroom 会将某些连续操作组合为一个事务，以便作为整体撤销和重做。例如，选中若干对象后将其隐藏时，这些对象会自动取消选中。取消选择与隐藏对象会被组合到同一个事务中，因此撤销时，对象会同时恢复显示并重新被选中。

如果相同操作发生在一次鼠标拖动中或较短时间内，TrenchBroom 还会将它们合并。例如，拖动 Brush 时，整个移动过程会合并为一个操作；短时间内连续按移动快捷键时，这些移动也会合并。这样既节省内存，也能用一次撤销还原整个操作序列。
