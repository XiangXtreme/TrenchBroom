# Getting Started {#getting_started}

This section starts with a brief introduction to the most important technical terms related to mapping for brush-based engines. We also introduce some concepts on which TrenchBroom is built. Afterwards, we introduce the welcome window and game selection dialog, give an overview of the main window, and explain camera navigation in the 3D and 2D views.

## Preliminaries {#preliminaries}

In this section we introduce some technical terms related to mapping in Quake-engine-based games. You do not need to understand every detail, but to understand how TrenchBroom works, you should have a general idea of how maps are structured and how TrenchBroom views and manages that structure. In particular, this section introduces concepts that we added to map structures (without changing the file format, of course). Knowing these concepts will help you understand several important aspects of editing levels in TrenchBroom.

### Map Definitions {#map-definitions}

In Quake-engine based games, levels are usually called maps. The following simple specification of the structure of a map is written in extended Backus-Naur-Form (EBNF), a simple syntax to define hierarchical structures that is widely used in computer science to define the syntax of computer languages. Don't worry, you don't have to understand EBNF as we will explain the definitions line by line.

    1. `Map      = Entity {Entity}`
    2. `Entity   = {Property} {Brush}`
    3. `Property = Key Value`
    4. `Brush    = {Face}`
    5. `Face     = Plane Texture Offset Scale Rotation ...`

The first line specifies that a **map** contains an entity followed by zero or more entities. In EBNF, the braces surrounding the word "Entity" indicate that it stands for a possibly empty sequence of entities. Altogether, line one means that a map is just a sequence of one or more entities. An **entity** is a possibly empty sequence of properties followed by zero or more brushes. A **property** is just a key-value pair, and both the key and the value are strings (this information was omitted from the EBNF).

Line four defines a **Brush** as a possibly empty sequence of faces (but usually it will have at least four, otherwise the brush would be invalid). And in line five, we finally define that a **Face** has a plane, a material, the X and Y offsets and scales, the rotation angle, and possibly other attributes depending on the game.

In this document, we use the term _object_ to refer to entities and brushes.

### TrenchBroom's View of Maps {#trenchbrooms-view-of-maps}

TrenchBroom organizes the objects of a map a bit differently than how they are organized in the map files. Firstly, TrenchBroom introduces two additional concepts: [Layers](#layers) and [Groups](#groups). Secondly, the worldspawn entity is hidden from you and its properties are associated with the map. To differentiate TrenchBroom's view from the view that other tools and compilers have, we use the term "world" instead of "map" here. In the remainder of this document, we will use the term "map" again.

    1. `World        = {Property} DefaultLayer {Layer}`
    2. `DefaultLayer = Layer`
    3. `Layer        = Name {Group} {Entity} {Brush}`
    4. `Group        = Name {Group} {Entity} {Brush}`

The first line defines the structure of a map as TrenchBroom sees it. To TrenchBroom, a **World** consists of zero or more properties, a default layer, and zero or more additional layers. The second line specifies that the **DefaultLayer** is just a layer. Then, the third line defines what a **Layer** is: A layer has a name (just a string), and it contains zero or more groups, zero or more entities, and zero or more brushes. In TrenchBroom, layers are used to partition a map into several large areas in order to reduce visual clutter by hiding them. In contrast, groups are used to merge a small number of objects into one object so that they can all be edited as one. Like layers, a **Group** is composed of a name, zero or more groups, zero or more entities, and zero or more brushes. In case you didn't notice, groups induce a hierarchy, that is, a group can contain other sub-groups. All other definitions are exactly the same as in the previous section.

To summarize, TrenchBroom sees a map as a hierarchy (a tree). The root of the hierarchy is called world and represents the entire map. The world consists first of layers, then groups, entities, and brushes, whereby groups can contain more groups, entities, and brushes, and entities can again contain brushes. Because groups can contain other groups, the hierarchy can be arbitrarily deep, although in practice groups will rarely contain more than one additional level of sub groups.

### Brush Geometry {#brush_geometry}

In standard map files, the geometry of a brush is defined by a set of faces. Apart from the attributes defining the UV mapping, a face also specifies a plane using three plane points. The plane is oriented by the order in which the three points are written in the face specification in the map file. Here is an example:

    ( 32 32 -3824  ) ( -32 32 -3824  ) ( -32 96 -3824 ) mmetal1_2 0 0 0 1 1

The plane points are given as three groups of three numbers. Each group is made up of three numbers that specify the X,Y and Z coordinates of the respective plane point. The three points, p1, p2 and p3, define two vectors, v1 and v2, as follows:

      *p2
     /|\
      |
      v2
      |
    p1*--v1--->*p3

The normal of the plane is in the direction of the cross product of v1 and v2. In the diagram above, the plane normal points towards you. Together with its normal, the plane divides three dimensional space into two half spaces: The upper half space above the plane, and the lower half space below the plane. In this interpretation, the volume of the brush is the intersection of the lower half spaces defined by the face planes. This way of defining brushes has the advantage that all brushes are automatically convex. However, this representation of brushes does not directly contain any other geometric information of the brush, particularly its vertices, edges, and facets, which must be computed from the plane representation. In TrenchBroom, the vertices, edges, and facets are called the **brush geometry**. TrenchBroom uses the same method as the BSP compilers to compute the brush geometry. Having the brush geometry is necessary mainly for two things: Rendering and vertex editing.

### Entity Definitions {#entity_definitions}

To TrenchBroom, entities are just a sequence of properties (key value pairs), most of which are without any special meaning. In particular, the entity properties do not specify which model TrenchBroom should display for the entity in its viewports. Additionally, some property values have specific types, such as color, angle, or position. But these types cannot be hardcoded into the editor, because depending on the game, mod, or entity, a property called "angle" may have a different meaning. To give TrenchBroom more information on how to interpret a particular entity and its properties, an entity definition is necessary. Entity definitions specify the spawnflags of an entity, the names and types of its properties, the model to display in the editor, and other things. They are usually specified in a separate file that you can [load into the editor](#entity_definition_setup).

### Mods {#mods}

A mod (short for game modification) is a way of extending a Quake-engine based game with custom gameplay and assets. Custom assets include models, sounds, and materials. From the perspective of the game, a mod is a subdirectory in the game directory where the additional assets reside in the form of loose files or archives such as pak files. As far as TrenchBroom is concerned, a mod just provides assets, some of which replace existing assets from the game and some of which are new. For example, a mod might provide a new model for an entity, or it provides an entirely new entity. In order to make these new entities usable in TrenchBroom, two things are required: First, TrenchBroom needs an entity definition for these entities, and TrenchBroom needs to know where it should look for the models to display in the viewports. The first issue can be addressed by pointing TrenchBroom to an alternate [entity definition file](#entity_definitions), and the second issue can be addressed by [adding a mod directory](#mod_setup) for the current map.

Every game has a default mod which is always loaded by TrenchBroom. As an example, the default mod for Quake is _id1_, which is the directory that contains all game content. TrenchBroom supports multiple mods, and in the case of multiple mods, there has to be a policy for resolving name conflicts between resources. For example, a mod might provide an entity model for an entity defined in the default mod in order to replace the default model with a more detailed one. To do this, the mod provides an entity model with the same name as the one it wants to replace. To resolve such name conflicts, TrenchBroom assigns priorities for mods, and if a name conflict occurs between different mods, the mod with the highest priority wins. Note that the default mod always has the lowest priority.

### Materials and Material Collections {#materials}

A material defines how a surface is rendered and how it interacts with light. In TrenchBroom, materials are usually closely related to textures. When TrenchBroom finds a texture on the filesystem, it automatically creates a default material for it. For Quake 3, additional material properties are read from shader files. Materials are usually not provided individually, but as material collections. A material collection can be a directory containing loose image files, or it can be an archive such as a wad file. Some games such as Quake 2 come with textures that are readily loadable by TrenchBroom. Such textures are called _internal_. _External_ textures on the other hand are textures that you provide by loading a wad file. Since some games such as Quake don't come with their own textures readily available, you have to obtain the textures you wish to use and add them to TrenchBroom manually by [loading a wad file](#material_management).

Multiple material collections may contain a material with the same name, resulting in a name conflict. Such a conflict is resolved by observing the order in which the material collections were loaded - the material that was loaded most recently wins the conflict. You cannot control the load order unless you are using wad files.

## Startup {#startup}

The first thing you will see when TrenchBroom starts is the welcome window. This window allows you to open one of your most recently edited maps, to create a new map or to browse your computer for an existing map you wish to open in TrenchBroom.

![TrenchBroom's welcome window](images/WelcomeWindow.png)

You can click the button labeled "New map..." to create a new map or you can click the button labeled "Browse..." to find a map file on your computer. Double click one of the documents in the list on the right of the window to open it. The light gray text on the left gives you some information about which version of TrenchBroom you are currently running. The version information is useful if you wish to report a problem with the editor (see [here](#reporting_bugs) for more information).

If you choose to create a new map, TrenchBroom needs to know which game the map should be for, and will show a dialog in which you can select a game and, if applicable, a map format. This dialog may also be shown when you open an existing map. TrenchBroom will try to detect this information from the map file, but if that fails, you need to select the game and map format.

![The game selection dialog (Mac OS X)](images/GameSelectionDialog.png)

The list of supported games is shown on the right side of the dialog. Below the game list, there is a dropdown menu for choosing a map format; this is only shown if the game supports more than one map format. One example for this is Quake, which supports both the standard format and the Valve 220 format for map files. In the screenshot above, none of the games in the list were actually found on the hard disk. This is because the respective game paths have not been configured yet. TrenchBroom allows you to create maps for missing games, but you will not be able to see the entity models in the editor and other resources such as materials might be missing as well. To open the game configuration preferences, you can click the button labeled "Open preferences...". Click [here](#game_configuration) to find out how to configure the supported games.

Once you have selected a game and a map format, TrenchBroom will open the main editing window with a new map. The following section gives an overview of this window and its main elements. If you want to find out how to [work with mods](#mods) and how to [add materials](#material_management), you can skip ahead to the respective sections.

## Main Window {#main_window}

The main window consists of a menu bar, a toolbar, the editing area, an inspector on the right and an info panel at the bottom. In the screenshot below, there are three editing areas: one 3D viewport and two orthographic 2D editing areas.

![The main editing window](images/MainWindow.png)

The sizes of the editing area, the inspector and the info bar can be changed by dragging the dividers with the mouse. This applies to some of the dividers in the inspector as well. If a divider is 2 pixels thick, it can be dragged with the mouse. The positions of the dividers and the size of the editing window are saved when you close a window. The following subsections introduce the most important parts of the main window: the editing area, the inspector, and the info bar. The toolbar and the menu will be explained in more detail in later sections.

### The Editing Area {#the-editing-area}

The editing area is divided in two sections: The context sensitive info bar at the top and the viewports below. The info bar contains different controls depending on which tool is currently activated. You can switch between tools such as the rotate tool and the vertex tool using the toolbar buttons, the menu or with the respective keyboard shortcuts. The context sensitive controls allow you to perform certain actions that are relevant to the current tool such as setting the rotation center when in the rotate tool or moving objects by a given delta when in the default move tool. Additionally, there is a button labeled "View Options" on the right of the info bar. Clicking on this button unfolds a dropdown containing controls to [filter out](#filtering_rendering_options) certain objects in the viewports or to change how the viewport [renders its contents](#filtering_rendering_options).

![The info bar with view dropdown (Windows 10)](images/ViewDropdown.png)

There are two types of viewports: 3D viewports and 2D viewports. TrenchBroom gives you some control over the layout of the viewports: You can have one, two, three, or four viewports. See section [View Layout and Rendering](#view_layout_and_rendering) to find out how to change the layout of the viewports. If you have fewer than four viewports, then one of the viewports can be cycled by hitting #action(Controls/Map view/Cycle map view). Which of the viewports can be cycled and the order of cycling the viewports is given in the following table:

No. of Viewports    Cycling View         Cycling Order
----------------    ------------         -------------
1                   Single view          3D > XY > XZ > YZ
2                   Right view           XY > XZ > YZ
3                   Bottom right view    XZ > YZ
4                   None

There are three types of 2D viewports: the XY, the XZ, and the YZ viewport. You can also think of these viewports as the top, the front, and the side view, respectively. The following table summarizes the properties of the three 2D viewports. Remember that Quake-based engines use a right handed coordinate system.

Viewport    Right Axis    Up Axis    Normal Axis    Name
--------    ----------    -------    -----------    ----
XY          +X            +Y         +Z             Top
XZ          +X            +Z         -Y             Front
YZ          +Y            +Z         +X             Side

The normal axis is the axis that would be protruding from the screen when looking at the respective 2D viewport. In the case of the XY viewport, the normal axis is the positive Z axis, but in the case of the XZ viewport, the normal axis is the negative Y axis. For the mathematically inclined, the normal axis is the cross product of the right axis and the up axis. Sometimes, we will also refer to the inverted normal axis as the depth axis. So, the depth axis of the XY viewport is the negative Z axis. We also refer to the plane that is spanned by the first two axes as the view plane of a 2D viewport. Accordingly, the view plane of the XZ viewport is the X/Z plane.

![The compass](images/Compass3D.png) In the bottom left of each viewport, there is a compass that indicates the orientation of the camera of that viewport. In the 3D viewport, you can see how the compass rotates when you rotate the camera. In the 2D viewport, the compass axes are fixed, but they indicate which of the coordinate system axes are the right and the up axis for that viewport. The colors of the compass hands represent the axes: Red is the X axis, green is the Y axis, and blue is the Z axis (RGB vs. XYZ).

At most one of the viewports can have focus, that is, only one of them can receive mouse and keyboard events. Focus is indicated by a highlight rectangle at the border of the viewport. If no viewport is focused, you have to click on one of them to give it focus. Once a viewport has focus, the focus follows the mouse pointer, that is, to move focus from one viewport to another, simply move the mouse to the other viewport. The focused viewport can also be maximized by choosing #menu(Menu/View/Maximize Current View) from the menu. Hit the same keyboard shortcut again to restore the previous view layout.

### Map Bounds {#map_bounds}

![McKinley Base and Quake map bounds](images/CTF1Bounds.png)

Game engines often impose a limit on the usable/playable volume of space. TrenchBroom can display this limit as a guide to use when creating your map. Such bounds will appear as an orange square or rectangle in the 2D viewports. For example, the image above shows the Quake map McKinley Base (ctf1 from Threewave CTF) within the normal bounds for a Quake map.

![Map Properties (Ubuntu Linux)](images/SoftBounds.png) If bounds are configured for a game, they will usually represent the limits observed by the last official release or patch of a particular game engine. Those bounds may not be correct for your situation, so you can disable or modify the displayed bounds using **Map Settings…** (#key(Ctrl)#key(1)), available from the Scene toolbar or View menu.

In Quake-engine games, these bounds represent a limit on the volume that can contain players, items, or other entities. Static geometry (plain brushes) can extend further, which is why these limits are often called "soft bounds". As far as TrenchBroom is concerned however, it is just drawing orange lines at whatever coordinates are indicated in the [game configuration file](#game_configuration_files)... if this is a non-Quake-engine game then the bounds could represent something else.

In any case, keep in mind that the displayed bounds are just a guide that you should use to help yourself stay within the limits of your target game engine. Changing the bounds in TrenchBroom will not change the behavior in the game!

### The Inspector {#the-inspector}

The inspector is located at the right of the main window and contains various controls, distributed to several pages, to change certain properties of the currently selected objects. You can show or hide the inspector by choosing #menu(Menu/View/Toggle Inspector). Open **Map Settings…** with #key(Ctrl)#key(1), Scene and properties with #key(Ctrl)#key(2), or the face inspector with #key(Ctrl)#key(3).

![Outliner, Entity, and Face inspectors](images/Inspector.png)

The **Scene** page combines [layers](#layers), the object hierarchy and entity [properties](#entity_properties). The bottom Content Browser's Entities folder lets you [create point entities](#creating_entities) and [configure entity definitions](#entity_definitions). **Map Settings…** contains [map bounds](#map_bounds) and [mods](#mods). The face inspector retains its graphical [UV editor](#uv_editor), face attributes and [material browser](#material_browser).

### The Info Bar {#the-info-bar}

You can show or hide the info bar by choosing #menu(Menu/View/Toggle Info Panel). It contains the console where TrenchBroom prints out messages, warnings and errors, and the live [issue browser](#issue_browser) where you can view, filter and resolve issues with your map.

## Camera Navigation {#camera_navigation}

Navigation in TrenchBroom is quite simple and straightforward. You will mostly use the mouse to move and pan the camera and to look around. There are also some keyboard shortcuts to help you position the camera with more precision. Just like in Quake, the camera cannot be rolled - in other words, up is always in the positive Z direction and down is always in the negative Z direction. The behavior of the camera can be controlled in the [preferences](#mouse_input).

### Looking and Moving Around {#looking-and-moving-around}

In the 3D viewport, you can look around by holding the right mouse button and dragging the mouse around. There are several ways to move the camera around. First and foremost, you can move the camera forward and backward in the viewing direction by spinning the scroll wheel. If you prefer to have the scroll wheel move the camera in the direction where the mouse cursor is pointing, you can check the "Move camera towards cursor" option in the preferences. To move the camera sideways and up / down, hold the middle mouse button and drag the mouse in any direction. For tablet users, there is an option in the preferences that will enable you to move the camera horizontally by holding #key(Alt). Additionally, you can use the following keyboard shortcuts:

Direction    Key
---------    ---
Forward      #action(Controls/Camera/Move forward)
Backward     #action(Controls/Camera/Move backward)
Left         #action(Controls/Camera/Move left)
Right        #action(Controls/Camera/Move right)
Up           #action(Controls/Camera/Move up)
Down         #action(Controls/Camera/Move down)

To adjust the movement speed of these keyboard shortcuts, you can either go the [preferences](#mouse_input) and adjust the corresponding slider, or you can turn the mouse wheel while holding the right mouse button in the 3D view.

### Orbiting {#orbiting}

The camera orbit mode allows you to rotate the camera about a selectable point. To get an idea as to what this means, imagine that you define a point in the map by clicking on a brush. The point where you clicked will be the center of your camera orbit. Now imagine a sphere whose center is the point where you just clicked and whose radius is the distance between the camera and the point. Orbiting will move the camera on the surface of that sphere while adjusting the camera's direction so that you keep looking at the same point. Visually, this is the same as rotating the entire map about the orbit center. Of course, you are not actually rotating anything - only the camera's position and direction are modified. Note that, since up and down are always fixed, you cannot cross the north and south poles of the orbit sphere.

Camera orbit mode is very useful if you are editing a brush because it allows you to view this brush from all sides quickly. Its best to try it and see for yourself how useful it is. To invoke the orbit mode, click and drag with the right mouse button while holding #key(Alt). The orbit center is the point in the map which you initially clicked. Dragging sideways will orbit the camera horizontally and dragging up and down will orbit the camera vertically. You can change the orbit radius during the orbit with the scroll wheel.

### Automatic Navigation {#automatic-navigation}

You can center the camera on the current selection by choosing #menu(Menu/View/Camera/Focus on Selection) from the menu. This will position the camera so that all selected objects become visible. The camera will not be rotated at all; only its position will be changed. Note that this action will also adjust the 2D viewports so that the selection becomes visible there as well.

Finally, you can move the camera to a particular position. To do this, choose #menu(Menu/View/Camera/Move Camera to...) and enter the position into the dialog that pops up. This does not affect the 2D views.

## Navigating the 2D Viewports {#navigating-the-2d-viewports}

![Linked YZ and XZ viewports](images/Linked2DViewports.gif)

Navigating the 2D viewports is naturally a lot simpler than navigating the 3D viewport. You can pan the viewport by holding and dragging either the middle or the right mouse button, and you can use the scroll wheel to adjust the zoom. Note that if you have set your viewport layout to show more than one 2D viewport, then the 2D viewports are linked. Specifically, the zoom factor is linked so that you always have a consistent zoom level across all 2D viewports, and the viewports are panned simultaneously along the same axes. This means that if you pan the XY viewport along the X axis, the XZ viewport gets panned along the X axis, too, so that you don't have to pan both viewports manually. Also note that zooming always keeps the coordinates under the mouse pointer invariant, that is, you can focus on a particular object or area by hovering the mouse over it and zooming in or out.

## FOV and Zoom {#fov-and-zoom}

For the 3D view, the camera FOV (field of vision) can be adjusted in the [view preferences](#view_layout_and_rendering) by dragging the slider. This is a permanent setting.

To temporarily adjust the camera's zoom, hold #key(Shift) and scroll the mouse wheel. To reset the zoom factor, hit #action(Controls/Map view/Reset camera zoom).
