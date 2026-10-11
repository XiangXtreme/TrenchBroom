# 首选项与编译运行 {#preferences_and_compilation}

## 游戏配置 {#game_configuration}

![游戏配置对话框（macOS）](images/GamePreferences.png)

游戏配置首选项窗格用于设置 TrenchBroom 所支持游戏的路径。对于每个游戏，你可以通过点击“...”按钮并选择游戏在硬盘中的存储文件夹来设置游戏路径。或者，你也可以在文本框中手动输入路径，但必须按下 #key(Return) 才能应用更改。

此外，你还可以通过点击“Configure engines...”按钮来配置所选游戏的游戏引擎。

点击游戏列表下方的文件夹图标，可以在文件浏览器中打开包含自定义游戏配置的文件夹。

![游戏引擎配置对话框（macOS）](images/GameEngineDialog.png)

在此对话框中，你可以通过点击左侧配置文件列表下方的“+”按钮添加游戏引擎配置文件，并通过点击“-”按钮删除所选配置文件。在列表右侧，你可以编辑所选游戏引擎配置文件的详细信息，具体包括其名称和路径。与游戏路径类似，如果你手动编辑引擎路径，必须在路径文本框中按下 #key(Return) 才能应用更改。点击[此处](#launching_game_engines)了解如何从 TrenchBroom 内部启动游戏引擎。

对于某些游戏配置（如上图所示的 Quake），你还可以选择输入一组编译工具的路径。如果不确定此处应该指定什么路径，将鼠标悬停在路径输入框上可能会显示包含该编译工具附加信息的工具提示。

如果你在此处输入了路径，那么路径左侧显示的名称可以用作该游戏[编译配置文件](#compiling_maps)中的变量。无论该变量出现在何处，都会使用此处指定的路径。例如，如果你的 `qbsp` 工具路径是 `C:\mapping\ericw-tools-v0.18.1-win64\bin\qbsp.exe`，并且你在此处设置了该路径……那么在你的编译配置文件中，凡是需要引用该 qbsp.exe 完整路径的地方都可以输入 `${qbsp}`。

在此处指定工具路径（如果游戏配置允许）的好处在于：

- 更易于创建、编辑和共享你的编译配置文件。
- 如果需要更改工具路径，只需在此处修改即可。

因此在上述示例中，如果你想尝试位于其他文件夹（如 `C:\mapping\ericw-tools-v0.19-win64\bin`）中的更新版 ericw-tools，你只需在此对话框中更改路径即可，而无需编辑所有的编译配置文件。

你还可以添加[自定义游戏配置](#game_configuration_files)以适应特定的环境设置（例如某个引擎支持 TrenchBroom 所支持的格式，但在该游戏中未默认预设）。

## 视图布局与渲染 {#view_layout_and_rendering}

![视图首选项](images/ViewPreferences.png)

在此首选项窗格中，你可以选择编辑区域的布局。共有四种可用布局：

布局        说明
------      -----------
单窗格      一个可循环切换的 3D / XY / XZ / YZ 视口
双窗格      一个 3D 视口和一个可循环切换的 XY / XZ / YZ 视口
三窗格      一个 3D 视口、一个 XY 视口和一个可循环切换的 XZ / YZ 2D 视口
四窗格      一个 3D 视口、一个 XY 视口、一个 XZ 视口和一个 YZ 视口

按下 #action(Controls/Map view/Cycle map view) 可以循环切换可循环的 2D 视口。

其余部分用于控制用户界面、地图视图渲染、材质浏览器以及字体。

设置                          说明
-------                       -----------
Theme                         选择 System、Light、Dark、Blender 或已安装的用户主题。需要重启。
Brightness                    3D 视口中材质和模型皮肤的亮度。
Grid                          3D 和 2D 视口中网格线的不透明度。
FOV                           3D 摄像机的视场角。
Show axes                     在 3D 和 2D 视口中显示坐标轴。
Filter mode                   编辑视图中的纹理过滤模式。
Enable multisampling          对视口渲染进行抗锯齿处理。
Material Browser Icon Size    缩略图缩放比例，范围从 100% 到 500%。
Renderer Font Size            在地图视图内渲染标签的文本大小。
Python Console font and size  用于控制台输入和输出的等宽字体系列及字号。

### 主题 {#themes}

内置主题 ID 包括 `builtin.system`、`builtin.light`、`builtin.dark` 和 `builtin.blender`。System 从操作系统调色板衍生其颜色。Light 和 Dark 提供稳定的 TrenchBroom 调色板，而 Blender 使用基于 Blender 5.2 的紧凑暗色调色板。主题更改将在重启 TrenchBroom 后生效。

第三方主题是扩展名为 `.tbtheme` 的可分发 UTF-8 JSON 文件。将它们放入 TrenchBroom 用户数据目录内的 `themes` 目录中，重启软件，然后在 **Preferences > View > Theme** 下选择新名称：

- Windows：`%APPDATA%\TrenchBroom\themes`
- macOS：`~/Library/Application Support/TrenchBroom/themes`
- Linux 与 FreeBSD：`~/.TrenchBroom/themes`
- 便携模式：`<TrenchBroom directory>/config/themes`

主题可以继承已安装的主题，并仅覆盖选定的颜色标记：

```json
{
  "schemaVersion": 1,
  "id": "example.midnight",
  "name": "Midnight",
  "author": "Example Author",
  "appearance": "dark",
  "inherits": "builtin.dark",
  "colors": {
    "accent": "#4f9bd8",
    "focusBorder": "#4f9bd8",
    "selectionBackground": "#24577a"
  }
}
```

主题 ID 使用小写字母、数字、点号、短横线和下划线；`builtin.` 为保留前缀。颜色使用 `#RRGGBB` 格式。主题文件不能注入样式表、更改控件几何尺寸或运行代码。无效文件将被跳过并在 TrenchBroom 日志中记录。

## 颜色首选项 {#color_preferences}

![颜色首选项](images/ColorPreferences.png)

**Colors** 页面控制编辑器特有的颜色，例如网格、选择、控制柄、叠加层和浏览器状态。这些颜色独立于控件主题：**View > Theme** 用于应用程序外观界面，而 **Colors** 用于地图和工具可视化。色块显示待定的颜色，并在点击时打开颜色选择器。

## 鼠标输入 {#mouse_input}

![鼠标配置对话框](images/MousePreferences.png)

鼠标输入首选项窗格允许你更改 TrenchBroom 解释鼠标移动的方式。

设置        说明
-------     -----------
Mouse Look  视角观察和环绕轨道旋转（右键点击并拖动）的灵敏度与坐标轴反转
Mouse Pan   平移视图（中键点击并拖动）的灵敏度与坐标轴反转
Mouse Move  使用鼠标移动摄像机的灵敏度与设置。如果你使用数位板，“Alt+MMB drag to move camera”设置可能会让导航更轻松。
Fly Mode    控制飞行模式速度的滑块。键盘快捷键可以在键盘首选项中进行调整。

## 键盘快捷键 {#keyboard_shortcuts}

![键盘配置对话框](images/KeyboardPreferences.png)

在此首选项窗格中，你可以更改 TrenchBroom 中使用的键盘快捷键。表格列出了所有可用的快捷键、其上下文和说明。要更改键盘快捷键，在表格第一列的快捷键上单击两次（不要双击）并输入新快捷键。上下文决定了该快捷键在何时可用，例如，根据旋转工具是否处于激活状态，PgDn 键会触发不同的操作。最后，说明列解释了快捷键在特定上下文中的作用。有时，快捷键触发的操作取决于使用该快捷键的视口是 3D 还是 2D 视口。例如，PgDn 键在 2D 视口中可以将对象向后移动（远离摄像机），而在 3D 视口中则可以沿 Z 轴向下移动对象。这些不同的操作会在说明列中列在一起，但用分号分隔。

如果在当前已打开地图时打开首选项对话框，快捷键列表将根据已加载的实体配置文件和游戏配置文件包含附加条目。对于每个实体以及特殊的 Brush 或面类型，以下键盘快捷键可用。

* **实体**
  - `View Filter > Toggle CLASSNAME visible` 切换具有此 classname 的实体的可见与不可见状态（[更多信息](#filtering_rendering_options)）
  - `Create CLASSNAME` 创建具有此 classname 的实体（[更多信息](#creating_entities)）
* **Brush / 面类型**
  - `View Filter > Toggle TYPE visible` 切换具有此类型的 Brush 或面的可见与不可见状态（[更多信息](#filtering_rendering_options)）
  - `Turn selection into TYPE` 将选中的 Brush 或面设置为该类型
  - `Turn selection into non-TYPE` 从选中的 Brush 或面中取消设置该类型

请注意，如果你在同一上下文中将某个键盘快捷键分配给不同的操作，该快捷键将产生冲突，并且在你解决冲突之前无法退出首选项窗格或关闭对话框。冲突的快捷键会以红色高亮显示。

## 杂项与扩展 {#misc_preferences}

**Misc** 页面包含语言、编辑器行为、工具集成和 MCP 设置。更改显示语言需要重启应用程序。编辑器选项包括复制 `worldspawn` 头部以及启用通过 #key(Ctrl)+拖动进行 2D 框选。

**Tools** 部分用于配置预制体目录，并打开[饼状菜单](#pie_menu)与 Python 插件管理器对话框。

## 自动更新 {#automatic_updates}

TrenchBroom 可以检查更新。如果有可用更新，可以直接在 TrenchBroom 中下载并安装。如果在偏好设置中启用了“启动时检查更新”，TrenchBroom 会在启动时检查更新。

TrenchBroom 会在以下位置通知你有新更新：

- 欢迎窗口
- “关于 TrenchBroom”对话框
- 更新偏好设置
- 状态栏

在这些位置，更新器的状态会以文字显示。如果需要用户操作，还会显示可点击的链接。例如，有可用更新时会显示“Update available”链接。点击该链接会打开对话框，你可以在其中下载并安装更新。

![更新指示器（macOS）](images/UpdateIndicator.png)

在上图中，更新器尚未执行检查，因此链接显示为“Check for updates”。点击该链接会开始检查更新。

![更新偏好设置](images/UpdatePreferences.png)

可以在偏好设置中配置更新器。可用设置如下：

- 启动时检查更新：选中后，TrenchBroom 会在启动时自动检查更新。
- 包含预发布版本：选中后，TrenchBroom 会在检查更新时包含预发布版本。预发布版本尚未被视为稳定版本，可能包含尚未进入稳定版本的新功能或错误修复。

请注意，TrenchBroom 检查更新时不会发送任何关于你或计算机的隐私信息，我们也不会收集你的任何数据。检查更新时，TrenchBroom 会通过 HTTPS 向 GitHub 发送一个请求；下载更新时，还会向更新文件所在的位置发送另一个 HTTPS 请求（目前这些文件也都托管在 GitHub 上）。

## 命令重复 {#command-repetition}

编辑 Brush 结构通常需要一遍又一遍地重复相同的步骤。例如在建造螺旋楼梯时，你首先切割出一个代表楼梯一级的 Brush。然后复制该 Brush，将其向上移动，并绕楼梯的中心轴旋转。对楼梯的每一级都要重复这些操作。TrenchBroom 提供了一项名为*命令重复*（command repetition）的功能，旨在为你自动完成该过程的一部分。

重复命令类似于拥有一个自动宏录制器。请记住，TrenchBroom 已经记录了你的所有操作以提供[撤销和重做](#undo_redo)功能。除了撤销操作之外，TrenchBroom 还会利用这些记录的信息让你重复一些最近执行的操作。在楼梯井的例子中，你希望仅通过按键就能一次又一次地重复复制、平移和旋转操作。唯一的问题是你需要确定应该重复哪些最近执行的操作。这可以通过两种方式实现：首先，当选择发生变化时，TrenchBroom 会自动遗忘所有可重复的操作。因此，如果你选中某些对象并在选中后立即选择 #menu(Menu/Edit/Repeat)，将不会发生任何事情，因为所有可重复的操作都已被丢弃。其次，你可以通过选择 #menu(Menu/Edit/Clear Repeatable Commands) 让 TrenchBroom 丢弃所有可重复的操作。你可以将其视为让 TrenchBroom 开始录制一个新宏。

因此在螺旋楼梯井的情况下，你首先创建代表一级台阶的 Brush。由于你不希望重复创建该 Brush 所执行的任何操作，你需要让 TrenchBroom 丢弃所有可重复的命令——可以通过取消选择再重新选择该 Brush，或者从菜单中选择相应的命令来实现。之后，你复制该 Brush，将其向上移动并旋转。接着，你可以通过按需多次选择 #menu(Menu/Edit/Repeat) 来重复这些步骤。

总而言之，你可以将命令重复视为一个非常简单的宏系统，它只包含最近执行的操作构成的单一宏。尽管其功能相当有限，但一旦习惯使用，它能让你的工作变得轻松许多。

## 问题浏览器 {#issue_browser}

问题浏览器位于窗口底部，显示 TrenchBroom 在地图中检测到的问题列表。地图发生变化时，编辑器会自动更新该列表。请注意，TrenchBroom 无法检测所有可能导致编译错误、警告或游戏中异常行为的问题，但它可以检测其中一部分。保持地图没有这些问题，可以避免地图变复杂后花费大量时间修复错误。要查看 TrenchBroom 可以检测和修复的问题类型，请点击问题浏览器右上角的“Filter”按钮，在打开的下拉列表中切换需要检查的问题类型。默认启用所有问题。

![带过滤下拉菜单的问题浏览器](images/IssueBrowserFilter.png)

问题列表中的每一项提供两类信息：问题对象在当前地图文件中的行号（如果适用）以及问题描述。如果想查找导致问题的对象，可以在浏览器中选择该问题，让编辑器选中相应对象，然后选择 #menu(Menu/View/Camera/Focus on Selection)，使其在 3D 和 2D 视口中可见。

![带上下文菜单的问题浏览器](images/IssueBrowserContextMenu.png)

除了提示问题，TrenchBroom 还可以帮助你修复问题。要修复问题，请右键点击该问题，并从“Fix”上下文菜单中选择相应的修复操作。若要忽略某个问题，可以在上下文菜单中选择“Hide”将其隐藏。若要查看所有隐藏的问题，请勾选问题列表上方相应的复选框。要重新显示隐藏的问题，先显示所有隐藏问题，然后右键点击该问题并从上下文菜单中选择“Show”。

## 编译地图 {#compiling_maps}

TrenchBroom 支持直接在编辑器中编译地图。你可以创建编译配置，并配置这些配置来运行外部编译工具。不过，TrenchBroom 不附带预打包的编译工具，你需要自行下载和安装。选择 #menu(Menu/Run/Compile...) 后会打开如下编译对话框。

![编译对话框（macOS）](images/CompilationDialog.png)

你可以在此对话框中创建编译配置，配置列表位于左侧。每个编译配置都有名称、工作目录和任务列表。点击配置列表下方的“+”按钮创建新配置，点击“-”按钮删除选中的配置。要复制配置，请右键点击它并从菜单中选择“Duplicate”。选中配置后，可以在对话框右侧编辑其名称、工作目录和任务。

名称
:    编译配置的名称。不必唯一，也可以为空。

工作目录
:   编译配置的工作目录。该项可选，但非常有用，因为指定任务参数时可以将其作为变量引用（见下文）。支持使用变量（见下文）。此外，相对路径会被解释为相对于此目录的路径。

任务
:   运行编译配置时按顺序执行的任务列表。

每个任务旁的复选框可以让你在运行编译配置时选择性地排除该任务。

任务有以下类型，每种类型都有不同的参数：

### 导出地图 {#export-map}

将地图导出到文件。导出文件应与实际存储地图的文件不同。

标记为“Omit From Export”的图层不会出现在导出的地图中。

#### 参数 {#parameters}

目标
:    导出文件的路径。支持使用变量。相对路径默认相对于工作目录。

移除实体
:    设置 GLOB 模式，移除 classname 匹配的实体。例如使用 'info_player_*' 移除所有 info_player_start 和 info_player_deatchmatch 实体。

添加实体
:    设置要添加到导出地图中的实体 classname。例如 'info_player_start'。该实体的 'origin' 属性会设置为 3D 摄像机的位置，'angle' 属性会设置为摄像机的偏航角。适合测试地图。

移除 TB 专用实体属性
:    从导出的地图文件中移除所有以 _tb_ 开头的实体属性。某些编译器无法处理这些属性。

### 运行工具 {#run-tool}

运行外部工具并捕获其输出。注意，Tool 参数可以使用[游戏配置](#game_configuration)中定义的编译工具变量，详见下文。

#### 参数 {#parameters-1}

工具
:    要运行的工具可执行文件的绝对路径。如果配置了工作目录，则使用配置的工作目录。支持使用变量。

参数
:    运行工具时传递给工具的参数。支持使用变量。

非零错误码时停止
:    如果工具返回错误，则停止编译过程。

### 启动引擎 {#launch-engine}

启动当前游戏中配置的一个 Launch Engine 配置。

适合在编译配置的最后执行：先导出地图、运行编译工具并将输出文件复制到游戏目录，然后启动引擎。

#### 参数 {#parameters-2}

引擎配置
:    要启动的 Launch Engine 配置。

启动失败时停止
:    如果无法启动引擎，则停止编译过程。如果未勾选，系统会报告失败并继续执行剩余任务。

### 复制文件 {#copy-files}

复制一个或多个文件。相对路径默认相对于工作目录。

#### 参数 {#parameters-3}

源
:    要复制的文件。要指定多个文件，可以在文件名中使用通配符（*、?）。支持使用变量。

目标
:    文件复制到的目录。如果目录不存在，会递归创建目录。已有文件会直接覆盖，不会提示。支持使用变量。

### 重命名文件 {#rename-file}

重命名或移动一个文件。相对路径默认相对于工作目录。

#### 参数 {#parameters-4}

源
:    要重命名或移动的文件。不支持通配符。支持使用变量。

目标
:    文件的新路径。路径必须以文件名结尾。如果所在目录不存在，会递归创建目录。已有文件会直接覆盖，不会提示。支持使用变量。

### 删除文件 {#delete-files}

删除一个或多个文件。相对路径默认相对于工作目录。

#### 参数 {#parameters-5}

目标
:    要删除的文件。要指定多个文件，可以在文件名中使用通配符（*、?）。支持使用变量。

### 使用表达式 {#using-expressions}

指定配置的工作目录和任务参数时，可以使用[表达式](#expression_language)。下表列出可用变量、作用域及含义。“Tool”作用域表示指定工具参数时可用；“Workdir”作用域表示仅指定工作目录时可用。输入变量时，TrenchBroom 会弹出自动补全列表帮助你完成输入。

变量             作用域             说明
--------         -----             -----------
`WORK_DIR_PATH`  Tool              工作目录的完整路径。
`MAP_DIR_PATH`   Tool, Workdir     当前编辑地图所在目录的完整路径。
`MAP_BASE_NAME`  Tool, Workdir     当前编辑地图的基本名称（不含扩展名）。
`MAP_FULL_NAME`  Tool, Workdir     当前编辑地图的完整名称（含扩展名）。
`GAME_DIR_PATH`  Tool, Workdir     游戏偏好设置中指定的当前游戏的完整路径。
`MODS`           Tool, Workdir     包含当前地图所有启用模组的数组。
`APP_DIR_PATH`   Tool, Workdir     包含 TrenchBroom 应用程序二进制文件的目录的完整路径。
`CPU_COUNT`      Tool              当前计算机中的 CPU 数量。

如果当前游戏的[游戏配置](#game_configuration)包含编译工具，这些工具的名称也会作为 Tool 作用域中的变量使用。下图展示了编译配置中使用此类变量的部分内容。

![包含工具变量的编译对话框部分（Linux）](images/CompilationDialogToolVars.png)

建议按照以下通用流程编译地图，并根据需要进行调整：

1. 将工作目录设置为 `${MAP_DIR_PATH}`。
2. 添加 *Export Map* 任务，并将目标设置为 `${MAP_BASE_NAME}-compile.map`。
3. 为需要运行的编译工具添加 *Run Tool* 任务。使用 `${MAP_BASE_NAME}-compile.map` 和 `${MAP_BASE_NAME}.bsp` 表达式指定工具的输入和输出文件。由于已经设置了工作目录，此处不需要指定绝对路径。
4. 最后添加 *Copy Files* 任务，将源设置为 `${MAP_BASE_NAME}.bsp`，目标设置为 `${GAME_DIR_PATH}/${MODS[-1]}/maps`。这会将文件复制到最后一个启用模组中的 maps 目录。
5. 可以选择在末尾添加 *Launch Engine* 任务，用最新编译的文件启动游戏。

最后一步会将 bsp 文件复制到游戏路径中的适当目录。如果编译产生的不只是 bsp 文件（例如光照贴图文件），可以添加更多 *Copy Files* 任务。也可以使用 `${MAP_BASE_NAME}.*` 这样的通配表达式复制相关文件。如果添加了 *Launch Engine* 任务，请将它放在所有文件复制任务之后，以便游戏使用最新的编译输出启动。

要运行编译配置，请点击编译对话框中的 'Compile' 按钮。编译配置运行后，可以点击 'Stop' 按钮终止当前运行的工具。如果关闭编译对话框或主窗口，正在运行的编译也会终止，但 TrenchBroom 会在此之前询问你。请注意，编译工具在后台运行，你可以继续编辑地图。

如果想在不实际运行的情况下测试编译配置，请点击 'Test' 按钮。测试运行只会打印每个任务将执行的操作，而不会真正执行任务。

编译完成后，可以启动游戏引擎并在游戏中查看地图。下一节将介绍如何配置游戏引擎，以及如何在编辑器中启动它们。

## 启动游戏引擎 {#launching_game_engines}

在 TrenchBroom 中启动游戏引擎之前，必须先将引擎添加到 TrenchBroom。可以从启动对话框（见下文）或[游戏配置](#game_configuration)打开游戏引擎配置对话框来完成此操作。

可以点击编译对话框中的 'Launch' 按钮，或选择 #menu(Menu/Run/Launch...) 手动启动游戏引擎。这会打开下图所示的启动对话框。

![启动对话框（Linux）](images/LaunchGameEngineDialog.png)

在此对话框中，你可以选择所需的游戏引擎，编辑其参数并启动引擎。要选择引擎，请在对话框右侧列表中点击该引擎。如果想编辑引擎列表，可以点击“Configure engines...”按钮打开游戏引擎配置对话框。然后可以在对话框左侧底部的文本框中编辑其参数。请注意，你可以在该文本框中使用以下变量：

变量             说明
--------         -----------
`MAP_BASE_NAME`  当前编辑地图的基本名称（不含扩展名）。
`GAME_DIR_PATH`  游戏偏好设置中指定的当前游戏的完整路径。
`MODS`           包含当前地图所有启用模组的数组。

`MODS` 变量可用于向引擎传递选择模组的参数。通常会使用当前地图模组列表中的最后一个模组。由于 `MODS` 变量是包含地图所有模组的数组，可以使用下标运算符访问其中的单个元素（见下文）。要访问数组的最后一个元素，可以使用表达式 `$MODS[-1]`。

请注意，这些参数会与游戏引擎配置一起保存。

## 解决问题 {#solving-problems}

本节包含一些关于在使用 TrenchBroom 遇到问题时该如何处理的信息。

### 自动备份 {#automatic-backups}

TrenchBroom 会自动为你的工作创建备份。作为先决条件，你必须在一个已保存的文件上进行工作，即该文件存在于你计算机上的某个位置。因此，当你创建新文件时，一旦决定保留它，就应该立即保存。此时，TrenchBroom 将开始创建其自动备份。这些备份存储在地图文件所在文件夹内名为“autosave”的文件夹中。在上一次备份后，每隔十分钟它就会创建一个新备份，除非自那时起地图文件没有发生任何更改。为了防止自动保存打断你的工作流程，TrenchBroom 仅会在你未与其进行交互时创建自动保存。TrenchBroom 总共最多会创建 50 个备份。此后，在创建新备份时它会删除最旧的备份，以确保备份总数不超过 50 个。备份文件与你正在编辑的地图文件同名，但在扩展名之前添加了备份编号。

在出现问题时，你可以使用这些备份恢复到地图的先前版本。这在修复错误或地图文件意外损坏时可能会对你有所帮助。

## 实体显示模型 {#display-models-for-entities}

TrenchBroom 可以在 3D 和 2D 视口中显示点实体的模型。为了使其正常工作，必须在[实体定义](#entity_definitions)文件中设置显示模型，并且在[游戏配置](#game_configuration)中正确设置游戏路径。对于大多数附带的实体定义文件，模型已经为你设置完毕；但如果你希望为某个 Mod 创建能在 TrenchBroom 中良好运行的实体定义文件，则需要自行添加这些模型定义。在本节中，你将学习如何在 FGD 和 DEF 文件中进行此项设置。

### 通用模型语法 {#general-model-syntax}

在所有实体定义文件中，添加显示模型的语法都是相同的，只有将模型定义插入实体定义中的位置有所不同。我们首先在此解释通用语法。DEF 或 FGD 中的每个模型定义均采用以下形式：

    model(...)

在 ENT 文件中，模型定义作为 `<point />` 元素的 XML 属性值给出，例如：

    <point model="..." />

其中，省略号包含要显示的模型实际信息。你可以使用 TrenchBroom 的[表达式语言](#expression_language)来定义实际模型。每个实体定义应仅包含一个模型定义，并且模型定义中的表达式应求值为字符串类型的值或映射类型的值。如果表达式求值为映射，则它必须具有以下结构：

    {
      "path" : MODEL,
      "skin" : SKIN,
      "frame": FRAME,
        "scale": SCALE_EXPRESSION
    }

占位符 `MODEL`、`SKIN`、`FRAME` 和 `SCALE_EXPRESSION` 具有以下含义：

占位符              说明
-----------         -----------
`MODEL`             相对于游戏路径的模型文件路径，开头可带有可选的冒号。必填。
`SKIN`              要显示的皮肤的从 0 开始的索引。可选，默认为 0。
`FRAME`             要显示的帧的从 0 开始的索引。可选，默认为 0。
`SCALE_EXPRESSION`  根据实体属性求值以确定模型缩放比例的表达式。

如果表达式求值为字符串类型的值，则会被解释为仅包含 `path` 键（以该字符串为值）的映射。换句话说，如果表达式求值为字符串，则该值会被解释为模型的路径。可以将此类表达式视为简写形式，允许你像这样定义简单模型：

    model("path/to/model")

而无需书写：

    model({ "path": "path/to/model" })

如果模型表达式包含缩放表达式，则其结果将用作模型的缩放值。如果表达式无法求值，或者未提供此类表达式，则将对游戏配置中的默认缩放表达式进行求值。有关 `SCALE_EXPRESSION` 和默认缩放表达式的更多信息，请参阅[本节](#game_configuration_files_entities)。

#### 基本示例 {#basic-examples}

因此，有效的模型定义可能如下所示：

    // use the model found at the given path with skin 0 and frame 0
    model("progs/armor")

    // use the model found at the given path with skin 1 and frame 0
    model({
      "path": "progs/armor",
      "skin": 1
    })

    // use the model found at the given path with skin 1 and frame 3
    model({
      "path" : "progs/armor",
      "skin" : 1,
      "frame": 3
    })

    // set a fixed uniform model scale factor 2
    model({
      "path" : "progs/armor",
      "scale" : 2
    })

有时，游戏中显示的实际模型取决于实体属性的值。TrenchBroom 允许你通过使用 switch 和 case 运算符构成的条件表达式，并在表达式中将实体属性作为变量引用来模拟此行为。让我们来看一个使用字面量值组合多个模型定义的示例。

    model({{
      dangle == "1" -> { "path": "progs/voreling.mdl", "skin": 0, "frame": 13 },
                      { "path": "progs/voreling.mdl" }
    }})

Voreling 有两种状态：作为普通怪物站立在地面上，或者倒挂在天花板上。模型表达式包含一个分支表达式（注意双大括号），该分支表达式由一个条件表达式（注意箭头运算符）和一个字面量映射表达式组成。你可以将此表达式解释如下：

    dangle == "1"                                             // If the value of property 'dangle' equals "1"
    ->                                                        // then
    { "path": "progs/voreling.mdl", "skin": 0, "frame": 13 }  // use this as the model.
    ,                                                         // Otherwise,
    { "path": "progs/voreling.mdl" }                          // use this as the model.

如果你在理解此语法时遇到困难，应阅读有关 TrenchBroom [表达式语言](#expression_language)的章节。

以下示例展示了使用标志值组合模型定义的情况。

    model({{
      spawnflags == 2 -> "maps/b_bh100.bsp",
      spawnflags == 1 -> "maps/b_bh10.bsp",
                         "maps/b_bh25.bsp"
    }})

如你所见，生命值包附加了三个模型：`maps/b_bh25.bsp, maps/b_bh10.bsp` 和 `maps/b_bh100.bsp`。这是因为生命值包根据勾选的 spawnflags 使用三种不同的模型。如果勾选了 `ROTTEN`，它使用 `maps/b_bh10.bsp`（即腐烂的生命值包）；如果勾选了 `MEGAHEALTH`，它使用 maps/b_bh100.bsp（即超级生命值强化道具）。如果两者均未勾选，它使用标准生命值包。

相应地，嵌套的条件表达式检查 `spawnflags` 属性的值以确定正确的模型。由于无需为这些模型指定皮肤或帧，表达式仅返回字符串作为简写。

在前面的示例中请注意，如果同时勾选了 `ROTTEN` 和 `MEGAHEALTH`，它将显示 megahealth 模型。请记住，分支运算符会返回第一个求值结果不为 undefined 的表达式的值。因此，你必须将没有条件限制的模型定义作为分支中的最后一项，因为那样会覆盖其他所有项！

#### 高级示例 {#advanced-examples}

到目前为止你所看到的基本表达式允许你高度灵活地根据实体属性的值自定义 TrenchBroom 显示的模型、皮肤和帧，但实际的路径、皮肤索引和帧索引在实体定义文件中是硬编码的。然而，有时即使这种灵活性也不够，特别是在允许将任意模型放置到地图中的实体上。在此类情况下，实体定义文件无法包含实际的模型路径等内容。相反，模型路径、皮肤索引和帧索引是由制作者使用实体属性指定的。由于 TrenchBroom 将实体属性的值作为变量提供给模型表达式，你也可以轻松应对此类情况。

回想一下模型定义映射的结构：

    {
      "path" : MODEL,
      "skin" : SKIN,
      "frame": FRAME
    }

到目前为止，我们一直在映射条目的值中使用硬编码的字面量，如下所示：

    model({ "path" : "progs/armor", "skin" : 1, "frame": 3 })

然而，没有什么能阻止我们使用变量代替硬编码字面量，从而引用实体属性。

    model({
      "path" : PATHKEY,
      "skin" : SKINKEY,
      "frame": FRAMEKEY
    })

占位符 `PATHKEY`、`SKINKEY` 和 `FRAMEKEY` 具有以下含义：

占位符       说明
-----------  -----------
`PATHKEY`    存储模型路径的实体属性键名。
`SKINKEY`    存储模型皮肤索引的实体属性键名。可选。
`FRAMEKEY`   存储模型帧索引的实体属性键名。可选。

一个有效的动态模型定义可能如下所示：

    model({
      "path" : mdl,
      "skin" : skin,
      "frame": frame
    })

然后，如果你使用相应的 classname 创建实体并指定如下三个属性：

    {
      "classname" "mydynamicmodelentity"
      "mdl" "progs/armor.mdl"
      "skin" "2"
      "frame" "1"
    }

TrenchBroom 将使用其第三个皮肤显示 `progs/armor.mdl` 模型的第二帧。如果你更改这些值，3D 和 2D 视口中的模型将相应更新。

#### DEF、FGD 与 ENT 文件的区别 {#differences-between-def-fgd-and-ent-files}

在这两种文件中，模型定义均直接与其他实体属性定义一起指定（注意模型定义后面的分号——这仅在 DEF 文件中是必需的）。来自 DEF 文件的示例如下：

    /*QUAKED item_health (.3 .3 1) (0 0 0) (32 32 32) ROTTEN MEGAHEALTH
    {
      model({{ spawnflags == 2 -> "maps/b_bh100.bsp", spawnflags == 1 -> "maps/b_bh10.bsp", "maps/b_bh25.bsp" }});
    }
    Health box. Normally gives 25 points.

    Flags:
    "rotten"
    gives 15 points
    "megahealth"
    will add 100 health, then rot you down to your maximum health limit
    one point per second
    */

来自 FGD 文件的示例如下：

    @PointClass base(Monster) size(-32 -32 -24, 32 32 64)
                model({{ perch == "1" -> "progs/gaunt.mdl", { "path": "progs/gaunt.mdl", "skin": 0, "frame": 24 } }})
                = monster_gaunt : "Gaunt"
    [
      perch(choices) : "Starting pose" : 0 =
      [
        0 : "Flying"
        1 : "On ground"
      ]
    ]

为了提高与其他编辑器的兼容性，FGD 文件中的模型定义也可以命名为 _studio_ 或 _studioprop_。

在 ENT 文件中，相同的模型规范可能如下所示：

    <point name="ammo_bfg" color=".3 .3 1"
           box="-16 -16 -16 16 16 16"
           model="{{ perch == '1' -> 'progs/gaunt.mdl', { 'path': 'progs/gaunt.mdl', 'skin': 0, 'frame': 24 } }}"
    />

## 点文件与门户文件 {#point-files-and-portal-files}

TrenchBroom 可以加载由 QBSP 生成的点文件（PTS），用于帮助定位泄漏。使用 #menu(Menu/File/Load Point File...) 打开点文件后，它将呈现为一系列连接地图内部与虚空的绿色线段。点击 #menu(Menu/View/Camera/Move to Next Point) 将摄像机移动到第一个点，并继续点击 #menu(Menu/View/Camera/Move to Next Point) 沿着路径飞行，即可向你显示泄漏所在的位置。

由 QBSP 生成的门户文件（PRT）可让你可视化 BSP 叶节点之间的门户。可以通过 #menu(Menu/File/Load Portal File...) 加载它们，并呈现为半透明的红色多边形。

## 更新首选项 {#update_preferences}

**Update** 页面控制自动更新检查以及是否包含预发布版本。它还会显示当前的更新状态，并提供与欢迎窗口和“关于”窗口中相同的检查或下载操作。详情请参阅[自动更新](#automatic_updates)。
