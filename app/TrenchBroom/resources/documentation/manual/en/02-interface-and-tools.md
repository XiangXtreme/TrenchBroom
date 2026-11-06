# Interface and Tools {#interface_and_tools}

## Selection {#selection}

There are two different kinds of things that can be selected in TrenchBroom: objects and brush faces. Most of the time, you will select objects such as entities and brushes. Selecting individual brush faces is only useful for changing their attributes, e.g. the materials. You can only select objects or brush faces, but not both at the same time. However, a selection of brushes (and nothing else) is treated as if all of their brush faces were selected individually.

![Object selection in 3D and 2D viewports](images/ObjectSelection.gif)

In the 3D viewport, selected objects are rendered with red edges and slightly tinted faces to distinguish them visually. The bounding box of the selected objects is rendered in red, with spikes extending from every corner when the mouse hovers over one of the selected objects. These spikes are useful to precisely position objects in relation to other objects. Additionally, the dimensions of the bounding box are displayed in the 3D viewport. In a 2D viewport, selected objects are just rendered with red edges. No spikes or bounding boxes are displayed there since the 2D viewports have a continuous grid anyway.

## Selecting Objects {#selecting-objects}

To select a single object, simply left click it in a viewport. Left clicking anywhere in a viewport deselects all other selected objects, so if you wish to select multiple objects, hold #key(Ctrl) while left clicking the objects to be selected. If you click an already selected object while holding #key(Ctrl), it will be deselected. You can also _paint select_ multiple objects. First, select an object, then hold #key(Ctrl) while dragging over unselected objects. Take care not to begin your dragging over a selected object, as this will [duplicate the selected objects](#duplicating_objects). Note that this even applies to occluded objects, so when you start your paint selection, you must ensure that no object under the mouse is selected.

In the 3D viewport, you can only select the frontmost object with the mouse. To select an object that is obstructed by another object, you can use the mouse wheel. First, select the frontmost object, that is, the object that occludes the object that you really wish to select. Then, hold #key(Ctrl) and scroll up to push the selection away from the camera or scroll down to pull the selection towards the camera. Note that the selection depends on what object is under the mouse, so you need to make sure that the mouse cursor hovers over the object that you wish to select.

![Selection drilling in the 3D viewport](images/DrillSelection.gif)

In a 2D viewport, you can also left click an object to select it. But unlike in the 3D viewport, this will not necessarily select the frontmost object. Instead, TrenchBroom will analyze the objects under the mouse, specifically the faces under the mouse, and it will find the one with the smallest visible area. Having found such a face, it will select the object to which this face belongs. Since entities don't necessarily have faces, the faces of their bounding boxes will be considered instead. The advantage of this technique is that it allows you to easily select occluded objects in the 2D viewports.

You may also think of left click selection like this: In both the 3D viewport or a 2D viewport, TrenchBroom first compiles a set of candidate objects. These are all objects under the mouse. Then, it must choose an object to be selected from these candidates. In the 3D viewport, the frontmost object always wins (unless you're using the scroll wheel to drill the selection), and in a 2D view, the object with the smallest visible area wins. Other than that, selection behaves exactly the same in both viewports, that is, you can hold #key(Ctrl) to select multiple objects and so on.

Sometimes, selecting objects manually is too tedious. To select all currently editable objects, you can choose #menu(Menu/Edit/Select All) from the menu. Note that hidden and locked objects are excluded, so this command is particularly useful in conjunction with those features. Another option to select multiple objects at once is to use _selection brushes_. Just create one or more new brushes that enclose or touch all the objects you wish to select. These brushes are called selection brushes. Select all of these newly created selection brushes, and choose #menu(Menu/Edit/Select Touching) to select every object touched by the selection brushes, or choose #menu(Menu/Edit/Select Inside) to select every object enclosed inside them. Note that selection brushes will disappear after they were made use of.

![Using a selection brush](images/SelectTouching.gif)

A similar operation can be found under #menu(Menu/Edit/Select Tall), but this particular operation is only available when a 2D view has focus. It projects the selection brushes onto the view plane of the currently focused 2D viewport, which results in a 2D polygon, and then selects every object that is contained entirely within that polygon. You can think of this as a cheap lasso selection tool that works like #menu(Menu/Edit/Select Inside), but without having to adjust the distance and thickness of the selection brushes.

If you have selected a single brush that belongs to an entity or group, and you wish to select every other object belonging to that entity or group, you can choose #menu(Menu/Edit/Select Siblings). The same effect can be achieved by left double clicking on a brush that belongs to an entity or group. The menu command #menu(Menu/Edit/Select by Line Number) is useful for diagnostic purposes. If an external program such as a map compiler presents you with an error message and a line number indicating where in the map file that error occurred, you can use this menu command to have TrenchBroom select the offending object for you.

Choose #menu(Menu/Edit/Invert Selection) from the menu to invert the selection, i.e. select everything that is currently unselected (excluding hidden and locked objects).

Finally, you can deselect everything by left clicking in the void, or by choosing #menu(Menu/Edit/Deselect All).

## Selecting Brush Faces {#selecting-brush-faces}

![Selected brush face](images/BrushFaceSelection.png)

To select a brush face, you need to hold #key(Shift) and left click it in the 3D viewport. You can select multiple brush faces by additionally holding #key(Ctrl). To select all faces of a brush, you can left double click that brush while holding #key(Shift). If you additionally hold #key(Ctrl), the faces are added to the current selection. To flood fill a whole coplanar surface at once - even where it spans several touching brushes - left double click a face while holding #key(Shift)#key(Alt). Again, hold #key(Ctrl) to add the faces to the current selection. To paint select brush faces, first select one brush face, then left drag while holding #key(Ctrl) and #key(Shift). To deselect all brush faces, simply click in the void or choose #menu(Menu/Edit/Deselect All).

## Map Setup {#map-setup}

The first step when creating a new map is the setup of mods, entity definitions, and material collections.

### Setting Up Mods {#mod_setup}

![Mod editor](images/ModEditor.png) We explained [previously](#mods) that a mod is just a sub directory in the game directory itself. Every game has a default mod that is always active and that cannot be deactivated - in Quake, this is _id1_, the directory that contains all game resources. TrenchBroom supports an unlimited number of additional mods. Mods can be added, removed, and reordered by using the mod editor, which you can find at the bottom of the map inspector.

When the editor starts, the mod editor is folded, but you can unfold it by clicking on its title bar. You are then presented with a two column view of the available mods (left column) and the enabled mods (right column). The list of available mods contains every sub directory in the game directory in alphabetic order. This list can be filtered using the search field at the bottom. To enable some mods, select them in the list of available mods and click on the plus icon below the list of enabled mods. To disable mods, select them in the list of enabled mods and click on the minus icon.

The order of the enabled mods is important because it defines the [priorities for loading resources](#mods). You can change the priority of enabled mods by reordering them in the list. For this, you can select an enabled mod and use the small triangles below the list of enabled mods to push it up or down.

Mods are stored in a worldspawn property called "_tb_mod".

### Loading Entity Definitions {#entity_definition_setup}

![Entity definition editor](images/EntityDefinitionEditor.png) Entity definitions are text files containing information about [the meaning of entities and their properties](#entity_definitions). Depending on the game and mod you are mapping for, you might want to load different entity definitions into the editor. To load an entity definition file into TrenchBroom, switch to the entity inspector and click on the entity browser title bar where it says "Settings". The entity definition browser is horizontally divided into two areas. The upper area contains a list of _builtin_ entity definition files. These are the entity definition files that came with TrenchBroom for the game you are currently working on. You can select one of these builtin files by clicking on it. TrenchBroom will load the file and update its resources accordingly. Alternatively, you may want to load an external entity definition file of your own. To do this, click the button labeled "Browse" in the lower area of the entity definition browser and choose the file you wish to load. Currently, TrenchBroom supports Radiant DEF files and ENT files as well as [Valve FGD][FGD File Format] files. To reload the entity definitions (as well as the referenced models) from the currently loaded external file, you can click the button labeled "Reload" at the bottom or use #menu(Menu/File/Reload Entity Definitions). This may be useful if you're editing an entity definition file for a mod you're working on.

Return to the entity browser by clicking on the entity browser title bar again where it says "Browser".

Note that FGD and ENT files contain much more information than DEF files and are generally preferable. While TrenchBroom supports all of these file types, its support for FGD and for ENT is better and more comprehensive. Unfortunately, DEF files are still relevant because Radiant style editors require them, so TrenchBroom allows you to use them, too.

The path of an external entity definition file is stored in a worldspawn property called "_tb_def".

### Managing Materials {#material_management}

#### Wad Files {#wad-files}

Wad files are managed via the `wad` worldspawn property. The property contains a semicolon separated list of paths where TrenchBroom will load a wad file from. Map compilers also use this property to find wad files.

![Smart wad editor](images/SmartWadEditor.png) In TrenchBroom, you cannot edit this property directly, so you must use a smart property editor that's available for the `wad` property. This smart editor is available when you select the `wad` worldspawn property in the entity property editor. Click the `+` icon to add a wad file or the `-` icon to remove the selected wad files. The two triangle icons move a selected wad file up and down in the list. This only has an effect on how name conflicts between materials are resolved. The icon with the two circular arrows reloads all texture wads.

Alternatively, you can drag wad files onto the editor window from a file browser such as the Windows explorer. If the map uses wad files, these files will be loaded and their paths will get appended to the `wad` worldspawn property.

#### Materials from directories {#materials-from-directories}

Unless you are using wad files, you don't need to do anything to manage your materials - TrenchBroom will automatically load all available material collections.

If you want to provide your own custom textures, you need to put them in a subdirectory where TrenchBroom can find them. For Quake 2, this means that you need to create a subdirectory called `textures` in the directory of the mod you're mapping for, or in the `baseq2` directory. Then you need to create another subdirectory with a name of your choice. Then you copy your texture files into that directory. TrenchBroom will then find that directory (possibly after restarting the editor) and allow you to load the textures from there. Currently, for a generic game you must create a `textures` folder in the game path directory you set in [game configuration](#game_configuration), or in a directory you loaded as a mod.

You need to place your textures in a subdirectory exactly one level deep in the `textures` folder. Any loose images in `textures` will not be detected, nor will any nested in subdirectories of your collection.

## Interacting with the Editor {#interacting-with-the-editor}

Before we delve into specific editing operations such as creating new objects, you should learn some basics about how to interact with the editor itself. In particular, it is important to understand the concept of tools in TrenchBroom and how mouse input is mapped to 3D coordinates.

### Working with Tools {#working-with-tools}

All editing functionality in TrenchBroom is provided by tools. There are two types of tools in TrenchBroom: Permanently active tools and modal tools. Modal tools are tools which have to be activated or deactivated manually by the user. Permanently active tools are tools which are always available, unless they are deactivated by a modal tool. The following table lists all tools with a short description:

Tool                  Viewports    Type          Purpose
----                  ---------    ----          -----------
Camera Tool           2D, 3D       Permanent     Adjusting the 3D camera and the 2D viewports
Selection Tool        2D, 3D       Permanent     Selecting objects and brush faces
Simple Shape Tool     2D, 3D       Permanent*    Creating simple shapes
Complex Shape Tool    3D           Modal         Creating arbitrarily shaped brushes
Entity Drag Tool      2D, 3D       Permanent     Creating entities by drag and drop
Resize Tool           2D, 3D       Permanent*    Resizing brushes by dragging faces
Move Tool             2D, 3D       Permanent*    Moving objects around
Rotate Tool           2D, 3D       Modal         Rotating objects
Sweep Tool            2D, 3D       Modal         Sweeping brush faces into runs of brushes
Scale Tool            2D, 3D       Modal         Scaling brushes
Shear Tool            2D, 3D       Modal         Shearing brushes
Clip Tool             2D, 3D       Modal         Clipping brushes
Vertex Tool           2D, 3D       Modal         Editing brush vertices, edges, and faces

Tools of the type Permanent* are deactivated whenever a modal tool is active. For example you cannot create cuboid brushes when the vertex tool is active. Additionally, at most one modal tool can be active at a time. You can activate and deactivate modal tools using the menu and keyboard shortcuts listed in the following table:

Tool                  Menu
----                  -----------
Complex Shape Tool    #menu(Menu/Edit/Tools/Brush Tool)
Rotate Tool           #menu(Menu/Edit/Tools/Rotate Tool)
Sweep Tool            #menu(Menu/Edit/Tools/Sweep Tool)
Scale Tool            #menu(Menu/Edit/Tools/Scale Tool)
Shear Tool            #menu(Menu/Edit/Tools/Shear Tool)
Clip Tool             #menu(Menu/Edit/Tools/Clip Tool)
Vertex Tool           #menu(Menu/Edit/Tools/Vertex Tool)
Path Tool             #menu(Menu/Edit/Tools/Path Tool)

![Tool buttons](images/ToolbarTools.png) Additionally, tools can be toggled by using the buttons on the left of the toolbar. In the image, the first button is active, however, this particular button does not represent any of the modal tools listed in the table above. Rather, it indicates that no modal tool is currently active, and therefore all permanent tools are available. The buttons icon indicates that objects can be moved, which is only possible if no modal tool is active. The second button represents the convex brush tool, the third button toggles the clip tool, the fourth button is used to toggle the vertex tool and the fifth button toggles the rotate tool.

You can learn more about these tools in later sections. But before you can learn about the tools in detail, you should understand how TrenchBroom processes mouse input, which is what the following two sections will explain.

### Command Palette {#command_palette}

Choose #menu(Menu/View/Command Palette...) or press <kbd>Ctrl</kbd> + <kbd>P</kbd> (macOS: <kbd>Cmd</kbd> + <kbd>P</kbd>) to open a searchable list of commands. Type part of a command name or menu path, use the arrow keys to change the highlighted result, and press #key(Return) to run it. The palette also shows the current shortcut for each result. Press #key(Esc) to close it without running a command.

![Command Palette](images/CommandPalette.png)

The Command Palette is useful when you know what an operation is called but do not remember its menu location or shortcut. It lists commands that are currently unavailable as disabled, so it also reflects the active document, selection, and tool context.

### Pie Menu {#pie_menu}

Hold the backtick key (`` ` ``) in a map view to open the Pie Menu. Move the pointer toward an enabled action and release the key to run it; release near the center to cancel. The default shortcut can be changed under [Keyboard Shortcuts](#keyboard_shortcuts).

Open **Preferences > Misc > Pie Menu Settings** to choose the actions, remove entries, and reorder them by dragging. The menu uses the same underlying actions as the main menu and Command Palette, so its entries follow the current editor context and custom keyboard configuration.

### Path Tool {#path_tool}

Choose #menu(Menu/Edit/Tools/Path Tool) or press #action(Menu/Edit/Tools/Path Tool) to create a chain of `path_corner` entities. The active game must provide a `path_corner` entity definition.

Left-click brush geometry, or an empty viewport plane, to add grid-snapped path points. The preview draws the points and their connecting segments.

![Four uncommitted points in the Path Tool preview](images/PathToolPreview.png)

Press #key(Left) to remove the last point, #key(Right) to restore the most recently removed point, and #key(Return) or double-click to create the chain. TrenchBroom assigns unique `targetname` values and links each entity to the next with `target`. Press #key(Esc) to leave the tool without creating the remaining preview points.

The Path Tool creates a new linear `path_corner` chain. It does not insert nodes into an existing path, reverse a path, or reconnect existing branches.

### Canceling Operations and Tools {#canceling}

To cancel a mouse drag, hit #action(Controls/Map view/Cancel). The operation will be undone immediately. The same keyboard shortcut can be used to cancel all kinds of things in the editor. The following table lists the effects of canceling depending on the current state of the editor.

State                 Effect
-----                 ------
Complex Shape Tool    Discard all placed points; deactivate tool
Clip Tool             Discard most recently placed clip point; deactivate tool
Sweep Tool            Move the destination cap back onto the selected faces; deactivate tool
Vertex Tool           Discard current vertex selection; deactivate tool
Selection Tool        Discard current selection

For those tools where a second effect is listed (separated by a semicolon), the second effect only takes place if the first effect couldn't be realized. For example, if the clip tool is active but no clip points have been placed, then hitting #action(Controls/Map view/Cancel) will deactivate the clip tool. Hitting #action(Controls/Map view/Cancel) yet again will deselect all selected objects or brush faces.

In addition, you can hit #action(Controls/Map view/Deactivate current tool) to directly deactivate the current tool regardless of what state the tool is in.

### Mouse Input in 3D {#mouse-input-in-3d}

It is very important that you understand how mouse input is mapped to 3D coordinates when editing objects in TrenchBroom's 3D viewport. Since the mouse is a 2D input device, you cannot directly control all three dimensions when you edit objects with the mouse. For example, if you want to move a brush around, you can only move it in two directions by dragging it. Because of this, TrenchBroom maps mouse input to the horizontal XY plane. This means that you can only move things around horizontally by default. To move an object vertically, you need to hold #key(Alt) during editing. This applies to moving objects and vertices, for the most part.

But this is not always true, since some editing operations are spatially restricted. For example, when resizing a brush, you drag one of its faces along its normal, so the editing operation is restricted to that normal vector. In fact, the mouse pointer's position must be mapped to a one-dimensional value that represents the distance by which the brush face has been dragged. Whenever mouse input has to be mapped to one or two dimensions, TrenchBroom does this mapping automatically and no additional thought is required. But if mouse input must be mapped to three dimensions, TrenchBroom does so by employing the editing plane metaphor explained before.

### Mouse Input in 2D {#mouse-input-in-2d}

Mapping mouse input to 3D coordinates is much simpler in the 2D viewports, because the first and second dimension is given by the fixed viewport axes, and the third dimension (the depth) is usually taken from the context of the editing operation. For example, if you move an object by left dragging it in the XY viewport, then the mouse input is mapped to the X and Y axes, and the object's Z coordinates remain unchanged. When creating new objects, the depth is usually computed from the bounds of the most recently selected objects. So if you create a new brush by left dragging in the XY view, its distance and its height is determined by the most recently selected objects, while its X/Y extents are determined by the mouse drag.

### Axis Restriction {#axis_restriction}

To avoid imprecise movements when moving objects in two dimensions, you can limit movement to a single axis when using the mouse. By default, objects are moved on the XY plane in the 3D viewport or on the view plane in the 2D viewports. To move objects vertically in the 3D viewport, you have to hold #key(Alt). This works either when you start moving the objects and also during a drag. Furthermore, when moving objects on the XY plane in the 3D viewport or on the view plane in the 2D viewports, you can restrict the movement to one axis by holding #key(Shift). TrenchBroom will then restrict movement to the axis on which the objects have moved the farthest. So if you are moving objects in the 3D viewport and you want to restrict movement to the X axis, move the objects some distance along the X axis and press #key(Shift) to lock all movement to that axis. When you release #key(Shift), the restriction is lifted again and the objects will move to the position under the mouse. This applies not only to moving objects, but also moving vertices in the vertex tool and moving clip points in the clip tool. In the clip tool, the axis restriction only works in the 2D viewports however.

![Moving a brush in the 3D viewport with trace line](images/MoveTrace.png)

Note that TrenchBroom draws a trace line for you when you move objects with the mouse. The trace line helps to move objects in straight lines and as a visual feedback for your move. When an axis restriction is active, the trace line is rendered thicker.

### The Grid {#the-grid}

TrenchBroom provides you with a static grid to align your objects to each other. The grid size can be 1, 2, 4, 8, 16, etc. up to 256. It is also possible to set the grid size to a value smaller than 1, more precisely to 0.5, 0.25 or 0.125. If grid snapping is enabled, then most editing operations will be snapped to the grid. For example, you can only move objects by the current grid size if grid snapping is enabled. In the 3D viewport, the grid is projected onto the brush faces. Therefore the grid may appear distorted if a brush face is not axis aligned. In the 2D viewports, the grid is just drawn in the background. You can change the brightness of the grid lines in the preferences.

The grid size can be set via the menu, or by scrolling the mouse wheel while holding both #key(Alt) and #key(Ctrl).

### Map View Context Menu {#map_view_context_menu}

Right clicking in a map view gives the following context menu:

Group
:   [Groups](#groups) the selected objects.

Ungroup
:   [Ungroups](#groups) the selected objects.

Merge groups
:   When multiple groups are selected, merges them into a single group.

Rename Groups
:   Rename the selected groups.

Move to Layer
:   Move the selected objects to the chosen [layer](#layers).

Make Layer LAYERNAME Active
:   Changes the [current layer](#layers) to the chosen layer.

Hide Layers
:   Hide all layers which contain selected objects.

Isolate Layers
:   Isolate the layers containing selected objects.

Select All in Layers
:   Select all objects in the layers which contain the selected objects.

Make Structural
:   Moves brushes back into the world and clears any content flags. See [Brush Entities](#brush_entities).

Select All CLASSNAME
:   Select every entity in the map which has the same classname as the entity being hovered.

Reveal MATERIALNAME in Material Browser
:   Switches to the face inspector and scroll to the clicked material in the [Material Browser](#material_browser).

Create Point Entity
:   Create a [Point Entity](#point_entities) of the chosen type.

Create Brush Entity
:   Create a [Brush Entity](#brush_entities) with the selected brushes.

Set CLASSNAME as Entity Template
:   Saves the selected entity's classname and all properties as an active [Entity Template](#entity_templates).

Apply Entity Template (CLASSNAME)
:   Creates a separate entity cloned from the active template for each selected brush and reparents the brush into it.

## 2D Readable Outlines {#readable_outlines_2d}

In complex or densely structured maps, 2D viewports draw high-contrast readable brush outlines around geometry to make individual brushes distinguishable against background grid lines and adjacent surfaces.

## Terrain Sculpting and Materials {#terrain_editing}

Open a Half-Life Valve 220 map in a project and select contiguous ground brushes or a group containing them. In the terrain inspector, choose Sculpt, set the grid spacing, and click Create Sculptable Terrain. The ground must cover a contiguous rectangle with a common flat bottom, and adjacent brush boundaries must align with the selected spacing. A failed conversion leaves the original ground in place.

Alternatively, choose Draw New Terrain in Sculpt mode, set its grid spacing, height, and thickness, and drag a rectangle in the 2D top (XY) view. The rectangle snaps to the terrain grid; press Escape to cancel it. A single terrain region can cover up to 1280 × 640 units.

Choose Raise, Lower, Smooth, or Flatten and adjust the brush radius, strength, and hardness. Click Sculpt Terrain, then hold the left mouse button in the 3D view. Flatten uses the height at the start of the stroke. Lock terrain edges is enabled by default to avoid moving the boundary accidentally. Release the mouse to finish a stroke, or press Escape to cancel it.

Switch to Vertices and click Edit Terrain Vertices. Click a vertex or drag a rectangle to select multiple vertices in a 3D or 2D view, then drag vertically to change their height. Hold Ctrl or Shift to add to the selection. Movement follows the map grid snap. Boundary vertices cannot be selected while terrain edges are locked; unlock them first to adjust the boundary. A drag is one undo step, and Escape cancels a drag in progress.

Switch to Material, choose a terrain material and slot, and click Paint Terrain to paint. You can return to sculpting or vertex editing without losing the painted material. Locked or hidden terrain and its brushes cannot be edited. Directly modifying generated terrain brushes with the ordinary vertex tool can invalidate the terrain data; use the terrain inspector to change its shape.
