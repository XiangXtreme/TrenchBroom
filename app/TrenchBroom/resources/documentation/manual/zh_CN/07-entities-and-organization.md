# 实体、Outliner 与图层 {#entities_outliner_layers}

## 实体浏览器 {#entity_browser}

实体浏览器位于底部内容浏览器。在来源树中选择**实体**文件夹，或使用 #menu(Menu/View/Show Entity Browser)。内容区以 3D 缩略图网格展示点实体类型，复用已有的实体模型和包围盒渲染。共用搜索框按名称或说明查找，**筛选**中可以只显示已使用类型，**视图**中可以调整缩略图尺寸、分类分组和名称/使用量排序。选中缩略图后，底部紧凑信息栏显示说明和创建操作，地图选区保持不变。共用面包屑与前进/后退按钮在实体和文件资源文件夹间导航，并保留各自搜索状态。

将点实体类型拖入 2D 或 3D 视口可以定位放置，也可以双击缩略图、按 Enter 或点击**在当前视图创建**，按视图的默认位置创建。创建刷子实体时，在地图中选中刷子，再从视口右键菜单的**创建刷子实体**子菜单中选择类型。创建操作支持地图的撤销和重做。未打开地图时可以浏览定义，但不能创建实体或修改定义设置。

浏览器使用为当前地图配置的实体定义文件。如果缺少预期的类，请在手动创建未配置的实体之前先检查活动的 FGD、ENT 或 DEF 配置。

## 实体属性 {#entity_properties}

从本质上讲，实体是属性的集合，而属性是键值对，其中键和值都是字符串。某些值具有特殊格式，例如颜色、点或角度。但一般来说，在编辑实体时，你将与字符串打交道。在 TrenchBroom 中，你可以使用右侧“场景”页中场景树下方的实体属性编辑器来添加、移除和编辑实体属性。按 #key(Ctrl)#key(2) 可打开场景并展开属性区。属性表格保留完整编辑能力，包括默认值、智能编辑器、标志、帮助和链接组属性保护。

![实体属性编辑器](images/EntityPropertyEditor.png)

实体属性编辑器分为两个独立的区域。顶部是以表格形式展示当前所选实体的属性，如果适用，还会展示所选实体中未包含属性的默认值。

### 默认实体属性 {#entity_properties_defaults}

默认属性以*斜体*显示在所选实体的实际属性下方。要隐藏默认属性，可以取消勾选表格底部的复选框。默认实体属性在实体定义文件（例如 FGD 文件）中定义，其含义取决于具体游戏。某些游戏（如 Quake）为实体属性内置了默认值，而默认实体属性会反映这些默认值（前提是在实体定义文件中正确设置）。

其他游戏（例如 Half Life）不为实体属性提供内置默认值，并期望为每个实体显式设置默认值。如果在[游戏配置](#game_configuration_files_entities)中进行了配置，那么当创建新实体时，TrenchBroom 将自动实例化默认实体属性（带有默认值）。请注意，并非所有默认属性都会被 TrenchBroom 实例化——只有那些在实体定义文件中配置了默认值的属性才会被 TrenchBroom 实例化。

![设置默认实体属性](images/SetDefaultPropertiesMenu.png)

在实体属性编辑器下方，有一个小按钮，按下时会弹出一个菜单。该菜单有三个条目：

- **设置已有默认属性（Set existing default properties）**：将所有具有默认值的实体属性重置为其默认值。不会添加或移除任何实体属性，且除非具有默认值，否则不会更改任何实体属性。
- **设置缺失默认属性（Set missing default properties）**：添加所有尚未设置的默认实体属性。仅添加新的实体属性。不会移除任何实体属性，也不会更改任何已有实体属性。
- **设置所有默认属性（Set all default properties）**：上述两者的组合。无论当前值如何，每个具有默认值的实体属性都会被设置为默认值，并添加缺失的默认属性。不会移除任何实体属性，且仅更改默认实体属性。

### 编辑属性 {#editing-properties}

要选择某个实体属性，只需在表格中点击代表该属性的行。被点击的字段将高亮显示，表示它已获得焦点。高亮表示可以通过输入文本来更改该字段。在上面的截图中，选中了“mangle”属性，其值字段获得了焦点，表示可以进行更改。

如果你需要更改大量属性，可能希望在表格中快速导航。可以使用光标键在表格中移动焦点。或者，可以按 #key(Tab) 逐字段移动。如果焦点位于某个属性的键上，按 Tab 键会将光标移动到该属性的值字段，再次按 Tab 键会将其移动到下一个属性的键字段，依此类推，直到到达表格末尾。你也可以按 #key(Shift)#key(Tab) 反向移动。按 #key(Return) 开始编辑当前字段，再按 #key(Return) 会应用更改并将焦点保留在该字段。输入键或值时，从弹出的建议中选择补全项也遵循此规则。

要更改属性的键或值，请将焦点设置到表格中的相应字段。如果此时输入文本，该文本将替换该字段的内容。更改字段的另一种方法是在属性已被选中的情况下点击该字段。这将显示一个实际的文本字段，你可以在其中输入文本。

有几种方法可以向实体添加属性。首先，可以点击表格底部标有“+”的按钮。这将在表格中插入一个带有默认名称且无值的新属性。其次，可以按 #key(Ctrl)#key(Return) 添加新属性。在这两种情况下，新属性都会被选中，以便你可以立即按上述方式开始编辑其键和值。最后，可以通过更改默认属性的值来添加属性。这会将默认属性提升为实体的实际属性。

要移除实体属性，应在表格中点击代表它们的行，然后点击表格底部标有“-”的按钮。

### 多实体选择 {#multiple-entity-selections}

![多实体选择](images/EntityPropertyEditorMultiSelection.png) 如果选中了多个实体，表格将显示其所有属性的并集，而不仅仅是所有所选实体共有的属性。并非在所有所选实体中都存在的属性，其名称将显示为灰色；而在实际具有这些属性的实体中具有不同值的属性，将显示为空值。在截图中，选中了三个 light 实体。因此，“classname”属性在所有实体中均存在且处处具有相同的值。同样，“origin”属性在所有这些实体中均存在，但在每个实体中的值不同，因此显示时不带值。另一方面，“light”、“wait”、“angle”和“mangle”属性仅存在于部分所选实体中，但在包含它们的各个实体中确实具有相同的值。

如果在选中多个实体时更改实体属性，该更改将应用于所有选中的实体，即使这需要添加该属性也是如此。因此，如果在上面的示例中将“light”属性的值更改为 200，则每个选中的实体随后都将具有值为 200 的“light”属性，即使之前只有一部分选中的实体具有该属性。

### 智能实体属性编辑器 {#smart-entity-property-editors}

TrenchBroom 为以下实体属性提供了专用编辑器：spawnflags、颜色和 choices（选项）。这些专用编辑器称为*智能属性编辑器*，如果选中的实体属性存在此类编辑器，它们就会显示在实体属性表格下方。

类型             编辑器                                                         描述
----             ------                                                         -----------
Spawnflags       ![智能 Spawnflags 编辑器](images/SmartSpawnflagsEditor.png)   复选框表格，允许切换各个 spawnflag 值。
Color            ![智能颜色编辑器](images/SmartColorEditor.png)                 颜色选择器控件，允许在字节和浮点颜色值之间转换，并提供地图中找到的所有颜色的列表。
Choice           ![智能选项编辑器](images/SmartChoiceEditor.png)               值的下拉列表。你也可以在文本框中输入任意文本。

### 链接实体 {#linking-entities}

可以使用特殊的链接属性将实体链接在一起。每个链接都有一个源实体和一个目标实体。目标实体具有一个名为“targetname”的属性，该属性的值是任意字符串。源实体具有“target”或“killtarget”属性，该属性的值是目标实体“targetname”属性的值。要创建实体链接，必须手动将这些属性设置为正确的值。目前，链接属性的名称硬编码在 TrenchBroom 中，但将来会在适当时从 FGD 文件中读取。下一节将解释实体链接在编辑器中是如何可视化的。

### 实体链接可视化 {#entity-link-visualization}

实体链接在 3D 和 2D 视口中渲染为线条。TrenchBroom 为实体链接可视化提供了四种模式。点击信息栏右侧的“View”按钮弹出的下拉菜单中可以在这些模式之间切换。下表解释了这四种不同的模式。

模式                   描述
----                   -----------
All                    始终显示所有实体链接。
Transitive selected    显示连接到所选实体的所有实体链接，以及从所选实体可到达的任何链接。
Direct selected        显示连接到所选实体的所有实体链接。
No                     不显示任何实体链接。

连接到当前所选实体的实体链接渲染为红线，将所选实体与该链接的源或目标相连。其他实体链接显示为绿色。

![实体链接可视化](images/EntityLinkVisualization.png)

在上面的截图中，两个 info_null 实体之间的链接呈现为绿色，因为这两个实体都未被选中。

## 保持全局概览 {#keeping_an_overview}

如果你在处理大型地图，管理地图中的对象并保持对它们的全局了解可能会变得很繁琐。某些区域可能挤满了大量 Brush 和实体，从而难以编辑被其他物体遮挡的特定对象。TrenchBroom 为你提供了许多工具，可让你轻松掌握地图的全局概览，并清理拥挤区域中的视觉干扰。

## Outliner {#outliner}

检查器中的 Outliner 页面将图层、组、实体和 Brush 显示为一个层级树。选择某个项目会选中对应的地图对象，展开组或实体则会显示其子对象。当视口中的几何体发生重叠，或者你需要理清嵌套组时，它特别有用。

![Outliner 中的图层、组、实体和 Brush](images/OutlinerHierarchy.png)

### 浏览与选择 {#outliner_navigation}

每个图层都是一个顶层行。组和实体显示在包含它们的图层或组下方。每个图层中的普通 Brush 汇总在 `worldspawn` 分支中，而 `func_door` 之类的 Brush Entity 会将自己的 Brush 显示为子项。信息（Info）列会显示图层中的对象数量、实体中的 Brush 数量等信息。

选择会双向同步：选择树中的行会在视口中高亮对应对象，在视口中选择对象也会定位并选中对应行。使用 #key(Ctrl) 或 #key(Shift) 可以选择多行。#key(Esc) 清除选择。#key(Del) 和 #key(Backspace) 可以删除选中的组、实体或 Brush，但不会删除图层行。

右侧两个状态列分别显示锁定和可见性。点击其中的图标可以切换该行及其内容的对应状态。当前图层、已打开的组、链接组、锁定行和隐藏行采用不同的视觉状态，因此在浏览树时仍能看清当前编辑上下文。

工具栏提供了以下控件：

- 搜索字段在短暂延迟后过滤层级结构。清空搜索框可恢复完整树形结构。
- **默认（Default）**按名称对同级项目排序，**类型（Type）**先将可比较的对象类型分组再按名称排序，**文件顺序（File Order）**遵循地图文件中的对象顺序。所选模式会被记住。
- 加号按钮创建一个具名图层并在树中显示它。
- 属性按钮在树下方打开一个可调整大小的实体属性面板。将其关闭可将检查器的全部高度用于层级树。

图层可见性、锁定状态、当前图层状态以及组嵌套在场景树中保持可见，下方属性区跟随同一地图选区。在底部内容浏览器浏览实体类型，在“场景”页编辑已有对象。

### 过滤与排序 {#outliner_filtering}

输入一个或多个单词可以按名称过滤行，不区分大小写。所有普通单词都必须匹配。Outliner 会保留每个结果的祖先，因此仍能看出匹配对象位于哪个组和图层中。

搜索字段还支持以下过滤器。它们可以相互组合，也可以与普通单词组合：

过滤器                                    结果
------                                    ------
`type:group`                              显示组。其他类型包括 `layer`、`entity`、`brush`、`patch`、`worldspawn`、`world` 和 `other`。
`type:group,entity`                       显示逗号分隔类型中的任意一种。
`vis:visible` 或 `vis:hidden`             按可见性显示行。
`lock:locked` 或 `lock:unlocked`          按锁定状态显示行。
`selected`                                显示选中行，以及定位它们所需的祖先行。

例如，下图使用 `type:group` 和**类型（Type）**排序。树中只保留三个匹配的组及其图层祖先。

![过滤 Outliner 以显示组](images/OutlinerFilter.png)

清空搜索字段会恢复完整树，以及过滤前的展开状态。过滤只会改变树中显示的内容，不会在地图视口中隐藏对象。

### 编辑实体属性 {#outliner_entity_properties}

点击工具栏右侧的属性按钮可打开内嵌属性编辑器。选择点实体、Brush Entity 或 `worldspawn` 行，即可在不切换检查器页面的情况下编辑它。拖动树与编辑器之间的分隔条，可以为任一区域分配更多空间。

![在 Outliner 中编辑 light 实体](images/OutlinerEntityProperties.png)

内嵌编辑器支持[实体属性](#entity_properties)中介绍的相同属性操作和基于实体定义的控件，包括添加和删除键、编辑 choice 与 spawnflags，以及在可用时使用智能颜色或资源控件。更改会立即应用到地图，并可按常规方式撤销。

### 通过拖放重组对象 {#outliner_dragging}

将一行拖到另一个容器上会更改对象在地图层级中的父级。它不会执行 CSG 合并，也不会改变 Brush 几何体。每次成功的拖放都是一个可撤销的事务。

要将对象移动到另一个图层，请选中它们的行并拖到目标图层行。第一张图显示 `Gameplay` 中选中的 world Brush；第二张图显示同一个 Brush 已位于 `Architecture` 图层下。

![将 world Brush 移动到另一个图层之前](images/OutlinerMoveToLayerBefore.png)

![world Brush 移动到 Architecture 图层之后](images/OutlinerMoveToLayerAfter.png)

如果将 Brush Entity 的子 Brush 拖到图层行，整个 Brush Entity 都会移动到该图层。若要拆出单独的 Brush，应将其 Brush 行拖到目标图层的 `worldspawn` 行或该分支中的任意子 Brush 上。

要将普通 Brush 添加到已有 Brush Entity，请只选择 Brush 行，然后将它们拖到 Brush Entity 行。第一张图显示 Default Layer 的 `worldspawn` 下有一个 Brush；拖放完成后，`func_door` 中包含两个 Brush。

![将 world Brush 添加到 Brush Entity 之前](images/OutlinerBrushEntityBefore.png)

![func_door 接收 Brush 之后](images/OutlinerBrushEntityAfter.png)

如果移动 Brush 后原 Brush Entity 变空，该空实体会自动删除。要将一个或多个 Brush 移回 world，请将它们的行拖到所需图层的 `worldspawn` 分支。拖动图层行不会重新排列图层；排序下拉框只会改变现有层级的显示方式。

## 过滤 {#filtering_rendering_options}

要过滤掉某些类型的对象，可以点击编辑区上方信息栏右侧的“View”按钮打开视图下拉窗口。

![带视图下拉菜单的信息栏 (Windows 10)](images/ViewDropdown.png)

在视图下拉窗口的左侧，有一个复选框列表，允许隐藏共享相同实体定义（即相同 classname）的所有实体。取消勾选某个实体定义（或其分组）即可隐藏对应的实体。要快速隐藏和显示所有实体，请点击列表下方的两个按钮之一。

视图下拉窗口的右半部分有若干选项，分为三组：

* **实体（Entities）** - 在这里可以配置实体在编辑器中的渲染方式。
* **Brushes** - 允许开启或关闭某些特殊的 Brush 或面类型。这些类型因游戏而异，并从[游戏配置文件](#game_configuration_files)中读取。
* **渲染器（Renderer）** - 关于其他对象渲染方式的各种选项。

请注意，可以在[首选项](#keyboard_shortcuts)中添加键盘快捷键来切换视图下拉菜单中的每个选项。

## 隐藏与隔离 {#hiding-and-isolation}

如果你在拥挤的区域中工作，隐藏某些对象，或者隐藏除感兴趣的对象之外的所有内容，会非常有用。要隐藏所选对象，请选择 #menu(Menu/View/Hide)；要隔离所选对象，请选择 #menu(Menu/View/Isolate)。要显示所有隐藏的对象，请选择 #menu(Menu/View/Show All)。所有这些操作都可以撤销。

## 锁定 {#locking}

锁定可防止对象以任何方式被选中或编辑。锁定对象的边缘呈蓝色渲染，其面带有蓝色色调，如下方截图所示。

![锁定对象](images/Locking.png)

对象可以在编辑打开的组时被锁定，也可以在将图层设置为锁定状态时被锁定（见下文）。你不能单独锁定单个对象。

## 组 {#groups}

组允许你将多个对象作为一个整体对待并为其命名。组可以包含以下类型的对象：实体、Brush 和其他组。组可以包含组这一事实构成了层级结构——但在实践中，你很少会创建这种深层嵌套的组。在视口中，组的边界框以蓝色渲染，并且其名称显示在上方。

要创建组，请确保当前没有激活任何工具，选中某些对象并选择 #menu(Menu/Edit/Group)。编辑器会提示输入名称。组名称不必唯一，因此可以有多个同名的组。要选择组，可以点击其中包含的任何对象。这不会选中单个对象，而是选中整个组，这就是为什么你只能将组内的所有对象作为一个整体进行编辑。如果你想编辑组内的单个对象，必须通过鼠标左键双击该组来将其打开。这会锁定地图中的所有其他对象（锁定对象不可编辑且以蓝色渲染）。打开组后，你可以编辑其中的各个对象，也可以按常规方式在组内创建新对象。完成对组的编辑后，可以通过在组外的任意位置双击鼠标左键将其再次关闭。最后，你可以通过选中组并选择 #menu(Menu/Edit/Ungroup) 来移除组。请注意，移除组并不会从地图中移除组内的对象，这些对象只是被解散了组合关系。

要将对象添加到已有组中，请选中要添加到组的对象，然后右键点击该组中已有的某个对象，并选择“Add Objects to GROUPNAME”，其中 GROUPNAME 是组的名称。同样，你可以通过打开该组、选中要从组中移除的对象，然后从[地图视图上下文菜单](#map_view_context_menu)中选择“Remove Objects from GROUPNAME”来从组中移除对象。被移除的对象将添加到当前图层中。如果从组中移除了所有对象，该组将被自动删除。

## 链接组 {#linked_groups}

组还可以链接在一起以实现某种形式的实例化。链接组包含相同的对象，但可以作为一个整体变换为不同的位置和形状。更改其中一个链接组将更新所有其他链接组。链接组对于构建需要保持同步的可复用结构（例如门道）非常有用。链接组的工作流程始终相同：

- 创建一些构成可复用结构的对象，例如门道。
- 将这些对象编组。
- 选中该组并通过上下文菜单或从菜单中选择 #menu(Menu/Edit/Create Linked Duplicate) 来创建链接副本。
- 将副本移动到其目标位置并对其应用进一步的变换（例如旋转）。
- 通过以常规方式复制链接组来创建更多链接副本。
- 随时打开任何一个链接组并更改其内容。这些更改随后将被复制到其他链接组中。

你可以对链接组应用各种变换，例如平移、旋转、缩放或翻转。组和链接组可以任意嵌套，因此链接组可以包含组，组可以包含链接组，链接组甚至可以包含链接组。

重要的是不要将链接组等同于实例化。在 TrenchBroom 中，不存在一个固定的、供你创建实例的“主”版本链接组。实际上，链接组底层的运作方式简单得多：当你更改某个链接组时，该组将暂时成为“主”版本，并且其所有内容都会复制到其所有链接同级组中，无论所包含的对象是否被更改。从这个角度来看，你可以认为 TrenchBroom 只是自动为你执行了手动更新过程。

你可以按常规方式向链接组添加对象或从链接组中移除对象，并且更改会立即反映在各个链接组中。要编辑链接组中的对象，请照常打开该组并进行修改。同样，修改会立即反映在链接组中。

考虑以下示例，其中有两个链接组，每个链接组包含一个 Brush 和一个实体。

```
Group A
- Brush A
- Entity A
  - "classname" "monster_army"
  - "angle" "90"
  - "origin" "0 0 0"

Group B (translated by 128 0 0)
- Brush B
- Entity B
  - "classname" "monster_army"
  - "angle" "90"
  - "origin" "128 0 0"
```

`Group B` 在结构上与 `Group A` 完全相同，但它在 X 轴上平移了 128 个单位。假设你通过移动 `Brush A` 的一个顶点来修改它。那么 `Group A` 的所有内容都将被复制，在 X 轴上平移 128，并添加到 `Group B` 中，替换其现有内容。或者假设你将 `Entity B` 的 spawnflags 设置为 `1`，那么会发生相同的过程，但这次会复制 `Group B` 的内容，在 X 轴上平移 -128，最后用这些副本替换 `Group A` 的内容。结果如下所示：

```
Group A
- Brush A
- Entity A
  - "classname" "monster_army"
  - "angle" "90"
  - "origin" "0 0 0"
  - "spawnflags" "1"

Group B (translated by 128 0 0)
- Brush B
- Entity B
  - "classname" "monster_army"
  - "angle" "90"
  - "origin" "128 0 0"
  - "spawnflags" "1"
```

在某些情况下，你可能不希望所有更改都反映在所有链接组中。例如，在制作门时，通常会使用 `target` 和 `targetname` 属性将门的 Brush 关联到触发器 Brush。但当然，你希望为不同的门使用不同的名称，这样在游戏中打开其中一扇门时就不会同时打开所有门。为了允许这些属性在不同的链接组中具有不同的值，你可以保护实体属性不受链接组中对应实体的更改影响。

### 受保护的实体属性 {#protected_entity_properties}

将实体属性标记为受保护会阻止来自链接组中对应实体的任何更改同步到此属性。此外，对受保护实体属性的任何更改都不会反映在链接组中的对应实体中。我们再来看一个示例。

```
Group A
- Entity A
  - "classname" "monster_army"
  - "angle" "90"
  - "origin" "0 0 0"
  - "spawnflags" "1"

Group B (translated by 128 0 0)
- Entity B
  - "classname" "monster_army"
  - "angle" "90"
  - "origin" "128 0 0"
  - "spawnflags" "1"
```

假设你想更改 `Entity B` 的 angle，但你不希望此更改影响 `Entity A`。在这种情况下，你可以在将其值更改为 `180` 之前，将 `Entity B` 的 `angle` 属性设置为受保护。结果将如下所示。

```
Group A
- Entity A
  - "classname" "monster_army"
  - "angle" "90"
  - "origin" "0 0 0"
  - "spawnflags" "1"

Group B (translated by 128 0 0)
- Entity B
  - "classname" "monster_army"
  - "angle" "180" (protected)
  - "origin" "128 0 0"
  - "spawnflags" "1"
```

请注意，`Entity A` 的 `angle` 属性的值仍为 90。如果你现在更改 `Entity A` 的 `angle` 属性，此更改也不会反映在 `Entity B` 中。

你可以使用“场景”页中的属性表格来保护实体属性。在链接组内部编辑实体时，会出现一个带复选框的新列，如下方截图所示。

![受保护的实体属性 (macOS)](images/ProtectedProperties.png)

要将属性设置为受保护，请点击其复选框。要解除保护，请再次点击该复选框。当将属性设置为未受保护时，其值将重置为其他实体中对应未受保护属性的值。在上面的示例中，将 `Entity B` 的 `angle` 属性设置为未受保护会将其值重置为 `90`，即来自 `Entity A` 的未受保护 `angle` 属性的值。

要将一个或多个实体的所有属性设置为未受保护，请选中这些实体（或包含它们的组）并选择 #menu(Menu/Edit/Clear Protected Properties)。

由于对链接组所做的所有更改都会立即复制到其他链接组中，因此新添加的属性会立即显示在链接组中。如果你希望添加属性而不进行复制同步，可以通过点击实体属性编辑器下方工具栏中带盾牌的 `+` 图标将其添加为受保护属性（见上一张截图）。反之，如果你希望在某个链接组中抑制某个属性，即不希望在将其添加到另一个链接组时在当前组中创建它，你可以将其添加为受保护属性并立即将其删除。在取消勾选其受保护复选框之前，它仍会显示在属性编辑器中，但名称将显示为斜体，看起来就像默认属性一样。

为了说明这些已删除的受保护属性的作用，请考虑以下示例。

```
Group A
- Entity A
  - "classname" "monster_army"
  - "origin" "0 0 0"

Group B (translated by 128 0 0)
- Entity B
  - "classname" "monster_army"
  - "origin" "128 0 0"

Group C (translated by 0 64 0)
- Entity C
  - "classname" "monster_army"
  - "origin" "0 64 0"
```

假设你想为除 `Entity A` 之外的所有 `monster_army` 实体设置 angle。在这种情况下，你首先将 `angle` 属性作为受保护属性添加到 `Entity A` 中，然后再次从 `Entity A` 中将其删除。接着你将 `angle` 属性添加到 `Entity B` 并为其赋值。该属性将被复制到 `Entity C` 中，但不会复制到 `Entity A`，因为在该实体中它是受保护的，即使该属性甚至并不存在。在下面的截图中，`angle` 属性已被设置为受保护并随后被删除。如果你点击其复选框以解除保护，该属性将不再显示在实体属性编辑器中。

![受保护的已删除实体属性 (macOS)](images/ProtectedProperties.png)

### 取消链接与分离链接组 {#separating_linked_groups}

要取消链接组的链接关系，请选中该组并选择 #menu(Menu/Edit/Separate Linked Groups)。这会将该链接组恢复为普通组。如果你从一组相互链接的组中选中多个链接组，选中的组不会变成普通组，而是会成为一个单独的链接组集合。这组单独的链接组之间仍然相互链接，但不再与该集合中其他未选中的成员链接。

请注意，如果通过分离或删除移除了某套链接组中的所有其他成员，则该集合中仅剩的单个成员将变成普通组。

### 将对象提取到新链接组 {#extracting_linked_groups}

当打开一个链接组时，选中该组中的部分（但非全部）对象，并选择 #menu(Menu/Edit/Extract Linked Groups)，即可将所选对象提取到一个新的独立链接组中。这些对象将从当前打开的链接组中移除并添加到新的链接组中。其他链接组中这些对象的链接副本也会发生同样的变化。考虑以下示例：

```
Linked Group A
- Entity 1
- Brush 2
- Brush 3
Linked Group B
- Entity 1
- Brush 2
- Brush 3
```

在打开组 A 并选中 Entity 1 和 Brush 2 的情况下，提取这些对象将产生以下结构：

```
Linked Group A
- Brush 3
Linked Group B
- Brush 3
Linked Group X
- Entity 1
- Brush 2
Linked Group Y
- Entity 1
- Brush 2
```

由此，组 A 和组 B 保持链接，组 X 和组 Y 也相互链接。

### 可视化 {#linked_group_visualization}

![3D 视图中的链接组 (macOS)](images/LinkedGroups.png)

链接组使用与普通组不同的颜色渲染。如果选中一个链接组，编辑器将渲染从所选组发出并指向其他链接组的箭头，以指示在所选组更改时哪些组将被更新。如果打开链接组，这些箭头仍然会显示。

### 地图文件中的链接组 {#linked_groups_map_file}

与普通组一样，链接组使用具有 TrenchBroom 特有属性的 `func_group` 实体存储在地图文件中。如果你在 TrenchBroom 以外的编辑器中编辑带有链接组的地图文件，并修改了属于链接组的对象，那么该链接组与其链接的同级组就会失去同步。TrenchBroom 可以正常加载此类组，你可以像往常一样继续编辑它们。但是，如果你修改了其中一个链接组，那么该组将覆盖所有其他链接组的内容，使其之后再次恢复同步。因此，如果你特意在外部编辑器中修改了某个链接组，并希望将这些更改同步到其他链接组中，只需在编辑器中打开该特定组，对其进行修改，然后再次关闭该组即可。这将更新所有链接组，使它们重新保持同步。

考虑以下链接组：

```
Group A
- Brush A
- Entity A
  - "classname" "monster_army"
  - "origin" "0 0 0"

Group B (translated by 128 0 0)
- Brush B
- Entity B
  - "classname" "monster_army"
  - "origin" "128 0 0"
```

在地图文件中，这些组将按如下方式存储。有关链接组特有的 TrenchBroom 属性，请参阅注释说明。

```
// entity 0
{
"classname" "func_group"
"_tb_type" "_tb_group"
"_tb_name" "group"
"_tb_id" "1"

// The following property is the ID of a set of linked groups.
// All groups with this linked group ID will be mutually linked.
"_tb_linked_group_id" "{38b3b39d-a165-4999-985d-d40563ce51c1}"

// The transformation that has been applied to the group as a whole.
// This will get updated when you transform a group by moving, rotating or scaling it.
"_tb_transformation" "1 0 0 128 0 1 0 0 0 0 1 0 0 0 0 1"

// brush 0
{
// faces omitted
}
}
// entity 1
{
"classname" "monster_army"
"origin" "128 0 0"
"_tb_group" "1"
}
// entity 2
{
"classname" "func_group"
"_tb_type" "_tb_group"
"_tb_name" "group"
"_tb_id" "2"

// This group entity has the same linked group ID as the previous one,
// so they will be linked.
"_tb_linked_group_id" "{38b3b39d-a165-4999-985d-d40563ce51c1}"

"_tb_transformation" "1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1"
// brush 0
{
// faces omitted
}
}
// entity 3
{
"classname" "monster_army"
"origin" "0 0 0"
"angle" "90"
"_tb_group" "2"
"_tb_protected_properties" "angle"
}
```

## 图层 {#layers}

图层将地图划分为若干部分。例如，你可以为不同的房间或区域创建单独的图层。图层可以包含组、实体或 Brush，并且每个对象只能属于一个图层。每个图层都有一个名称，可以设置为隐藏或锁定，或者在导出地图时被忽略。每张地图都包含一个不可移除的“Default Layer”。

![图层编辑器](images/LayerEditor.png)

地图检查器中的图层编辑器和 [Outliner](#outliner) 都会显示地图中的所有图层。Outliner 的加号按钮是在不离开层级树的情况下创建并定位到新图层的最快捷方式。

- 点击空心圆图标可在导出时忽略图层（“X”表示该图层在导出时被忽略）
- 点击眼睛图标可隐藏或显示图层
- 点击锁图标可锁定或解锁图层
- 点击图层列表底部的加号按钮可创建新图层
- 选中一个或多个图层并点击减号按钮可将其移除

从头创建或从剪贴板粘贴的新对象将插入到当前图层中（除非你正在组内工作）。从其他对象创建的对象（例如通过复制或挤出）将插入到源对象所在的图层中。

当前图层在图层列表中由单选按钮指示，并且其名称以粗体显示；你可以通过在图层列表中双击图层来设置当前图层。

右键点击图层编辑器中的图层可打开其上下文菜单：

- 设为活动图层
- 将所选内容移至图层
- 选择图层中的所有内容
- 隐藏图层
- 隔离图层
- 锁定图层
- 导出时忽略
- 显示所有图层
- 隐藏所有图层
- 解锁所有图层
- 锁定所有图层
- 重命名图层
- 移除图层

[地图视图上下文菜单](#map_view_context_menu)中也包含一些与图层相关的快捷方式。

## 实体模板 {#entity_templates}

在构建具有复杂机关逻辑与复用属性的关卡时（例如特定门、触发器、按钮或预设怪物），你可以将任意现有实体快速暂存为模板，并一键批量应用到地图中的多个 Brush 上：

1. **设置实体模板（Set as Entity Template）**：在视口中右键点击任意实体（或属于该实体的某个 Brush），选择 **Set CLASSNAME as Entity Template**。TrenchBroom 会将该实体的类名以及所有键值参数保存为活动模板。
2. **应用实体模板（Apply Entity Template）**：在地图中选择一个或多个几何 Brush，右键打开上下文菜单并选择 **Apply Entity Template (CLASSNAME)**。

TrenchBroom 会自动为每一个选中的 Brush 分别克隆创建一个独立的实体实例，将 Brush 归属（Reparent）至该实体下，并保持所有选区状态。整个批量创建和层级重归属操作封装在单次原子事务中，支持使用 #key(Ctrl)+#key(Z) 一键撤销与重做。

## 3D 天空盒渲染 {#3d_sky_rendering}

对于 GoldSrc 及支持的游戏引擎配置，在 `worldspawn` 实体上设置 `skyname` 属性会在 3D 视口中启用实时的 3D 天空盒渲染。6 面的环境天空盒立方体纹理会以无限远景深进行无缝拼接渲染，便于你在搭建几何场景时即时预览天空背景、地平线与光照氛围。
