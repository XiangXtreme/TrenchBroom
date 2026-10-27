# Brush Editing and Creation {#brush_editing_and_creation}

## Creating Objects {#creating-objects}

TrenchBroom gives you various options on how to create new objects. In the following sections, we will introduce these options one by one.

### Creating Simple Shapes {#creating-simple-shapes}

The easiest way to create a new brush is to just draw it out with the mouse using the Simple Shape Tool. The tool is enabled by default when nothing else is selected or any other tool active. Left drag in the 3D viewport or any of the 2D viewports.

When drawing a brush in the 3D viewport, its shape is controlled from the point under the mouse where you initially started your drag, to the point currently under the mouse cursor, and the current grid size. When drawing your brush on the XY axes, the height of the brush will be set to the current grid size. While dragging you can force X and Y axes to be equal by holding #key(Shift), or force X, Y and Z axes to be equal by holding #key(Shift)+#key(Alt), and its also possible to change just the height while drawing a brush by holding #key(Alt).

![Creating a cuboid in the 3D viewport](images/DrawBrush.gif)

When drawing a brush in a 2D viewport, you only control its extents on whatever axes the 2D view is set to display. So if you are drawing a brush in the XZ view, you control the X/Z extents with the mouse, and there's no way to change the Y extents directly, which is always fixed to the Y extents of the most recently selected objects. This of course applies to all of the different 2D viewports in the same way.

In either case, the material assigned to the newly created brush is the _current material_. The current material is set by choosing a material in the [material browser](#material_browser) or by selecting a face that already has a material. This concept applies to other ways of creating new brushes, too.

This way of creating brushes only allows you to create the simple shapes listed in the following table. In the next section, you will learn how to create more complex brush shapes with the complex shape tool.

Shape                  Description
-----                  -----------
Cuboid                 Creates a cuboid shape
Stairs                 Creates stairs
Arch                   Creates a semicircular arch
Cylinder               Creates a cylinder with a variable number of sides; potentially hollow
Cone                   Creates a cone with a variable number of sides
Spheroid (UV)          Creates a spheroid shape made up of triangles and quads with two poles
Spheroid (Icosahedron) Creates a spheroid shape made up of triangles, based on an icosahedron
Rock                   Creates a randomized rock formation made up of one or more brushes

Note that the cylinder, cone, UV sphere and arch shapes all have similar options, namely the number of sides and a circle mode.
By using the same values for these options across different shapes, TrenchBroom will create shapes that fit onto each other perfectly.

There are three circle modes that can be selected via the corresponding buttons:

Mode                                Description
----                                -----------
![](images/CircleEdgeAligned.png)   Creates a circle with 4 edges aligned to the bounding box
![](images/CircleVertexAligned.png) Creates a circle with 4 vertices aligned to the bounding box
![](images/CircleScalable.png)      Creates a scalable circle

The last shape requires some explanation. It is not a perfect circle, rather, its vertices are slightly displaced so as to be perfectly aligned on the grid. Consider the following example.

![A scalable hollow cylinder](images/ScalableHollowCylinder.png)

This hollow cylinder is scalable because its vertices are all aligned on the grid. Scaling it larger or smaller will keep the vertices neatly on an integer grid, and this can be beneficial for Quake-like map compilers because they usually handle geometry better when it is aligned on the grid. Scalable shapes can only have 12, 24, 48 or 96 sides. These types of curves are also called [CZG curves](https://www.quaketerminus.com/hosted/happymaps/curv_tut.htm).

![Asymmetric scalable cylinder](images/ScalableCylinderStretch.gif)

If you create an asymmetric scalable shape, it will not be scaled to fit the bounding box drawn with the mouse like the other shapes. Rather, only the middle portion of it will be elongated so that the vertices remain on the grid. This even applies to cones and UV spheres so that the different shapes still fit together.

The arch is created as the top half of a hollow cylinder. The axis is the direction the arch runs through, like a tunnel. Sides, thickness and circle mode work just like the cylinder, so an arch and a cylinder built with the same values will line up. When using scalable circle mode, drawing the bounds taller than a semicircle will extend the sides straight down, acting as "supports" for the arch. Checking _Spandrel_ fills the gaps between the arch and its bounding box with extra brushes.

The rock shape creates a randomized rock formation that fills the bounding box drawn with the mouse. Depending on its type, a rock formation consists of a single brush or of several overlapping brushes. The following types are available:

Type     Description
----     -----------
Boulder  A single rounded boulder with a flat base
Shelf    A single wide slab with sloping sides and an uneven top
Strata   A stack of layers that taper towards the top and drift sideways
Crag     A tall, jagged rock surrounded by smaller boulders
Crystal  A cluster of pointed hexagonal shards that fan out from a central shard
Columns  A group of tapered columns of varying heights around a tall central column
Basalt   A honeycomb of hexagonal columns of varying heights
Cluster  A scattering of small boulders

The _Detail_ slider controls how complex the rock formation is. Each type interprets it in its own way, for example as the number of faces of a boulder, the number of layers of strata, or the number of shards of a crystal. Larger bounds make a rock formation more complex at the same detail setting, and for basalt and clusters, the detail setting determines the size of the pieces, so that larger bounds are filled with more of them.

![Rock Shape UI](images/RockShapeUi.png)

The _Seed_ determines the random variations of the rock formation. The same type, detail, seed and bounds always produce the same rock formation, so you can change the seed to try different variations of a rock formation. The button next to the seed sets a random seed. If you toggle the button to the right of it, the seed is incremented every time you create a rock or apply the parameters to the selection, so that every new rock formation looks different.

The vertices of a rock formation are snapped to the integer grid, except for the slanted tops of columns and basalt.

### Creating Complex Shapes {#creating-complex-shapes}

![Drawing a rectangle and duplicating it](images/CreateBrushByDuplicatingPolygon.gif) If you want to create a brush that is not a simple, axis-aligned cuboid, you can use the brush tool. The brush tool allows you to define a set of points and create the convex hull of these points. A convex hull is the smallest convex volume that contains all the points. The points become the vertices of the new brush, unless they are placed within the brush, in which case they are discarded. Accordingly, the brush tool gives you several ways to place points, but there are two limitations: First, you can only place points in the 3D viewport, and second, you can only place points by using other brushes as reference.

To use the brush tool, you first have to activate it by choosing #menu(Menu/Edit/Tools/Brush Tool). Then, you can place single points onto the grid by left clicking on the faces of other brushes. Additionally, you can left double click on a face to place points on all of its vertices. You can also draw a rectangular shape by left dragging on an existing brush face and thereby place four points at the corners of that rectangle. Finally, if the points you have placed so far form a polygon, you can duplicate and move that polygon along its normal by left dragging it while holding #key(Shift). Once you have placed all points, hit #action(Controls/Map view/Create brush) to actually create the brush.

It is not possible to modify or remove points after they have been placed, except discarding all of them by hitting the #action(Controls/Map view/Cancel) key.

### Creating Patches (Quake 3 only) {#creating_patches}

Patches are created from brush faces. Create a brush and select one or more of its faces, then select #menu(Menu/Edit/Convert Selection to Patches). TrenchBroom will create one patch for each selected face, and the brush (or brushes) will be removed from the map. You can then use the control point tool to refine the patch.

![Square face results in single patch](images/CreatePatches_Square.gif)

Note that the selected faces don't have to be rectangular. TrenchBroom will create multiple patches that match the face's shape exactly. Thereby, it will subdivide the face into quads, and create a patch for each quad. TrenchBroom will prefer a subdivision that results in symmetric patches.

![Octagonal face results in three patches](images/CreatePatches_Octagon.gif)

If the face has an odd number of vertices, one degenerate triangular patch will be created where multiple control points coincide.

![Face with 5 vertices results in a triangular patch](images/CreatePatches_Corner.gif)

### Editing Patches (Quake 3 only) {#editing_patches}

Patches can be edited using the Control Point Tool.

![Editing the Control Points](images/ControlPointTool.png)

Adjusting the control points works in the same way as [editing vertices](#vertex_editing), so we won't repeat all the details here. Just select and drag the control points to adjust the shape of the patch. Like in the vertex tool, control points at the same positions are clumped together so that you can edit the control points of adjacent patches together.

When the tool is active and a patch is selected, you can change the number of rows or columns of the control point grid with the two spin boxes at the top of the map view:

![Editing the Control Point Grid](images/ControlPointToolSpinBoxes.png)

These spin boxes are only enabled if all of the selected patches have the same number of rows and columns of control points. When you adjust one of the two boxes, the patches new shape will approximate the previous shape as closely as possible: When the number of control points increases, the new shape will be exactly the same as the previous shape, but when the number of control points decreases, the new shape cannot represent the previous shape perfectly due to a loss of information.

### Creating Entities {#creating_entities}

There are two types of entities: point entities and brush entities, and it depends on the type how an entity is created. In the following sections, we present three ways of creating point entities and two ways to create brush entities.

#### Point Entities {#point_entities}

There are three ways of creating new point entities. Firstly, you can drop new entities in the 3D and 2D viewports by using the [map view context menu](#map_view_context_menu). To open the context menu, right click into the viewport. To create a point entity such as a pickup weapon or a monster, open the "Create Point Entity" sub menu and select the correct entity definition from the sub menus.

![Dropping an entity with the context menu (Mac OS X)](images/CreateEntityContextMenu.png)

The location of the newly created entity depends on whether you clicked on the 3D viewport or a 2D viewport. If you clicked on the 3D viewport, then the entity will be placed on the brush under the mouse. If there was no brush under the mouse, then the entity will be placed at a default distance. Note that the bounding box of the entity will be snapped to the grid. If you clicked on the 2D view, then the position of the entity depends on what was under the mouse when you clicked, too. If a selected brush was under the mouse, then the new entity will be placed on that brush. If no selected brush was under the mouse, then the entity will be placed at the far end of the bounding box of the most recently selected objects. Again, the bounding box of the newly created entity will be snapped to the grid.

![Entity browser](images/EntityBrowser.png) Secondly, you can create new point entities by dragging them from the entity browser. The entity browser can be found in the entity inspector page. At the bottom of the entity browser, you can find a number of controls to change the sort order and to filter the entities displayed in the browser.

The leftmost dropdown list allows you to change the sort order. Entities can be sorted by name or by their usage count, with the most used entities at the top. The "Group" button toggles grouping the entities by their category, which is derived from the first part of the entity name. For example, all entities starting with "key_" are put into a category called "key". The button labeled "Used" toggles all unused entities, when it is pressed, only those entities which are used in the map are shown in the browser. To filter entities by name, enter some text in the search box on the right. Only entities containing the search text will be shown in the browser.

To create a new entity, simply drag it out of the browser and onto the 3D or a 2D viewport. If you drag it onto the 3D viewport, the entity will be positioned on the brush under the mouse, with its bounding box snapped to the grid. If you drag the entity onto a 2D viewport, its position is determined by the far end of the most recently selected object.

Finally, you can create specific entities by assigning a keyboard shortcut in the [preferences](#keyboard_shortcuts). This is useful for entities that are used very often such as lights. The entity will be created under the mouse cursor; its position will be computed in the same way as if the context menu was used.

#### Brush Entities {#brush_entities}

![Moving brushes to brush entities](images/MoveBrushesToEntity.png) Creating brush entities is also done using the context menu. Select a couple of brushes and right click on them, then select the desired brush entity from the menu. To move brushes from one brush entity to another, select the brushes you wish to move and right click on a brush belonging to the brush entity to which you want to move the brushes, and select "Move brushes to Entity ENTITY", where "ENTITY" is the name of the target brush entity, for example "func_door" in the picture on the left. If the brush entity containing the brushes to be moved becomes empty, it will be automatically deleted. To move brushes from a brush entity back into the world and clear content flags, select the brushes, right click and select "Make Structural".

Additionally, you can also assign a keyboard shortcut to create a specific brush entity in the [preferences](#keyboard_shortcuts).

Often, it is much quicker to create new objects by duplicating existing ones. Objects can be duplicated using dedicated functions in TrenchBroom, or just by copying and pasting them.

### Duplicating Objects {#duplicating_objects}

The currently selected objects can be duplicated by choosing #menu(Menu/Edit/Duplicate). This will duplicate the objects in place, that is, the duplicates retain the exact position of the original objects. To give visual feedback, the duplicated objects are flashed in white really quickly. In the following short clip, you can see that the selected brush gets duplicated. After that, the duplicated brush is moved upwards.

![Duplicating a brush in place](images/DuplicateInPlace.gif)

Very often, you will want to duplicate objects and move them to a different position immediately afterwards, because having duplicates retain the same position as their originals is very seldom useful. That's why you can also duplicate and move objects at once without having to perform two separate actions. To duplicate and move objects, you can use the following keyboard shortcuts:

Direction     Shortcut (2D)                                                                                        Shortcut (3D)
---------     -------------                                                                                        -------------
Left          #action(Controls/Map view/Duplicate and move objects left)                                           #action(Controls/Map view/Duplicate and move objects left)
Right         #action(Controls/Map view/Duplicate and move objects right)                                          #action(Controls/Map view/Duplicate and move objects right)
Up            #action(Controls/Map view/Duplicate and move objects up; Duplicate and move objects forward)         #action(Controls/Map view/Duplicate and move objects backward; Duplicate and move objects up)
Down          #action(Controls/Map view/Duplicate and move objects down; Duplicate and move objects backward)      #action(Controls/Map view/Duplicate and move objects forward; Duplicate and move objects down)
Forward       #action(Controls/Map view/Duplicate and move objects forward; Duplicate and move objects down)       #action(Controls/Map view/Duplicate and move objects up; Duplicate and move objects forward)
Backward      #action(Controls/Map view/Duplicate and move objects backward; Duplicate and move objects up)        #action(Controls/Map view/Duplicate and move objects down; Duplicate and move objects backward)

Essentially, these are the same keyboard shortcuts that you use to [move objects around](#moving_objects) in the 3D and 2D viewports, but while holding #key(Ctrl). In the same vein, you can hold #key(Ctrl) while left dragging a selected object to duplicate and move all selected objects.

![Duplicating and moving a brush](images/DuplicateAndMove.gif)

Note that in the image above, the selected brush flashes while it is moved to the right. This indicates that in this case, the duplication and the translation happened at the same time instead of one after the other as in the previous example.

### Copy and Paste {#copy-and-paste}

You can copy objects by selecting them and choosing #menu(Menu/Edit/Copy). TrenchBroom will create text representations of the selected objects as if they were saved to a map, and put that text representation on the clipboard. This allows you to paste them into map files, and also to directly copy objects from map files and paste them into TrenchBroom. Note that you can also copy brush faces, which will also put a text representation of that brush face on the clipboard. Having copied a brush face, you can paste the attributes of that face (material, offset, scale, etc.) into other selected brush faces.

There are two menu commands to paste objects from the clipboard into the map. The simpler of the two is #menu(Menu/Edit/Paste at Original Position), which will simply paste the objects from the clipboard without changing their position. The other command, available at #menu(Menu/Edit/Paste), does not paste the objects from the clipboard at their original positions, but will try to position them using the current mouse position. If pasted into the 3D viewport, the pasted objects will be placed on top of the brush under the mouse. If no brush is under the mouse, the objects will be placed at a default distance. The bounding box of the pasted objects is snapped to the grid, and TrenchBroom will attempt to keep the center of the bounding box of the pasted objects near the mouse cursor. The following clip illustrates these concepts. The light fixture is copied, then pasted several times.

![Pasting objects in the 3D viewport](images/PastePositioning3D.gif)

Positioning of objects pasted into a 2D viewport attempts to achieve a similar effect by positioning the pasted objects such that they line up with the far end of the bounds of the most recently selected objects while keeping them under the mouse, with their center snapped to the grid.

## Editing Objects {#editing-objects}

The following sections first cover editing operations that apply to all objects, such as moving, rotating, scaling, and deleting. They then introduce the brush-shaping tools covered in this chapter: extrusion, clipping, sweeping, and chamfering. Vertex editing and CSG, materials, and entity organization are covered in the following chapters. This chapter concludes with TrenchBroom's undo and redo behavior.

### Moving Objects {#moving_objects}

You can move objects around by using either the mouse or keyboard shortcuts. Left click and drag on a selected object to move it (and all other selected objects) around. In the 3D viewport, the objects are moved on the XY plane by default. Hold #key(Alt) to move the objects vertically along the Z axis. In a 2D viewport, the objects are moved on the viewport's view plane. There is no way to change an object's distance from the camera using the mouse in a 2D viewport. If grid snapping is enabled, the distances by which you move them are snapped to the grid component-wise, that is, if the grid is set to 16 units, you can move objects by 16 units in either direction.

You can also use the keyboard to move objects. Every time you hit one of the shortcuts in the following table, the object will move in the appropriate direction by the current grid size. Also remember that you can [duplicate objects and move them](#duplicating_objects) in the given direction in one operation by holding #key(Ctrl) and hitting one of the keyboard shortcuts listed below.

Direction     Shortcut (2D)                                                            Shortcut (3D)
---------     -------------                                                            -------------
Left          #action(Controls/Map view/Move objects left)                             #action(Controls/Map view/Move objects left)
Right         #action(Controls/Map view/Move objects right)                            #action(Controls/Map view/Move objects right)
Up            #action(Controls/Map view/Move objects up; Move objects forward)         #action(Controls/Map view/Move objects backward; Move objects up)
Down          #action(Controls/Map view/Move objects down; Move objects backward)      #action(Controls/Map view/Move objects forward; Move objects down)
Forward       #action(Controls/Map view/Move objects forward; Move objects down)       #action(Controls/Map view/Move objects up; Move objects forward)
Backward      #action(Controls/Map view/Move objects backward; Move objects up)        #action(Controls/Map view/Move objects down; Move objects backward)

Note that the meaning of the keyboard shortcuts depends on the viewport in which you use them. While #action(Controls/Map view/Move objects up; Move objects forward) moves the selected objects in the direction of the up axis if used in a 2D viewport, it moves the objects (roughly) in the direction of the camera viewing direction (i.e. forward) on the editing plane if used in the 3D viewport. Likewise, #action(Controls/Map view/Move objects forward; Move objects down) moves the selected objects in the direction of the normal axis (i.e. forward) if used in a 2D viewport and in the direction of the negative Z axis if used in the 3D viewport.

![Moving objects](images/MoveObjectsByOffset.png)

To move objects by a specified offset, select #menu(Menu/Edit/Move objects) to bring up a window where you can enter a vector. Click "OK" and the currently selected objects will be moved by that vector.

### Rotating Objects {#rotating_objects}

The easiest way to rotate objects in TrenchBroom is to use the following keyboard shortcuts:

Shortcut                                                        Type     Rotation (3D)                         Rotation (2D)
--------                                                        ----     -------------                         -------------
#action(Controls/Map view/Roll objects clockwise)               Roll     Clockwise about view axis             Clockwise about normal axis
#action(Controls/Map view/Roll objects counter-clockwise)       Roll     Counter-clockwise about view axis     Counter-clockwise about normal axis
#action(Controls/Map view/Pitch objects clockwise)              Pitch    Clockwise about right axis            Clockwise about right axis
#action(Controls/Map view/Pitch objects counter-clockwise)      Pitch    Counter-Clockwise about right axis    Counter-Clockwise about right axis
#action(Controls/Map view/Yaw objects clockwise)                Yaw      Clockwise about Z axis                Clockwise about up axis
#action(Controls/Map view/Yaw objects counter-clockwise)        Yaw      Counter-clockwise about Z axis        Counter-clockwise about up axis

If the rotate tool is active, these keyboard shortcuts rotate the selected objects using the center of rotation and the angle set using the tool's rotation handle and the input controls above the viewports. If the rotate tool is not active, the center of rotation is the center of the bounding box of the currently selected objects (snapped to the grid), and the rotation angle is fixed to 90°.

![3D rotation handle](images/RotateHandle3D.png) The rotate tool gives you more control over rotation than the keyboard shortcuts do. Hit #menu(Menu/Edit/Tools/Rotate Tool) to activate the rotate tool and a rotation handle will appear in the viewports. The rotation handle allows you to set the center of rotation and to perform the actual rotation of the selected objects about the X, Y, or Z axis. In the 3D viewport, you can rotate the objects about any of those axes by left dragging the appropriate part of the rotate handle, but in a 2D viewport, you can only rotate the objects about the normal axis of that viewport. The angle of rotation defaults to 15 degrees, but it can be changed in the controls that appear above the editing views when you activate the rotate tool. During rotation, the current angle of rotation is shown at the center of the rotation handle.

In the 3D viewport, the rotation handle will appear as in the image on the left. It has three axes, color coded with the X axis in red, the Y axis in green, and the Z axis in blue as usual. In addition to the axes, it has three quarter circles, again color coded, and one small spherical handle at the center. The center handle (the yellow sphere) changes the center of rotation if you drag it with the left mouse button. Moving the center of rotation works exactly as [moving objects with the move tool](#moving_objects) does. If you hover the mouse over the center handle, you will notice that the coordinates of the center of rotation are displayed above the center handle and that the handle is highlighted by a red outline. To perform a rotation, you have to drag one of the three color coded quarter circles. When you hover over one of them, it will be highlighted to indicate that you can start dragging. Clicking and dragging the blue quarter circle with the left mouse button rotates the objects about the Z axis, and likewise for the red and green handles (see the clip below).

![2D rotation handle](images/RotateHandle2D.png) In the 2D viewport, the rotation handle will just appear as a circle with one smaller circular handle at the center. The center handle allows you to move the center of rotation on the view plane of that viewport, and the outer circle allows you to perform the rotation. In the 2D viewports, the handle is also color coded, the colors of the outer circle reflecting the axis of rotation in a similar fashion to the 3D rotate handle. To start a rotation, drag the outer circle in a circular fashion. As in the 3D view, the angle of rotation will be snapped to whatever value is entered in the angle control above the editing views, and during rotation the angle will be indicated at the center of the rotation handle.

![Rotate tool controls](images/RotateToolControls.png)

Like the move tool, the rotate tool places some controls above the viewport. On the very left, there is a combo box that displays the coordinates of the center of rotation. This combo box automatically updates if you move the rotate handle around in the 2D or 3D viewports. If you want to set the center of rotation manually, you can enter three coordinates here and hit #key(Return). Alternatively, you can click the button labeled "Reset" to set the center of rotation to the center of the bounding box of the currently selected objects, snapped to the grid. Finally, you can use the combo box to return the center of rotation to a previously used value. The rest of the controls allow you to perform a rotation by entering an angle in the text box, selecting the rotation axis from the dropdown list, and clicking the "Apply" button.

![Rotating objects about the Z axis in the 3D viewport](images/RotateTool.gif)

If you look closely at the clip above, you will notice that the entity in the picture, a green armor, rotates nicely with the brush it is placed on. Firstly, its position does not seem to change in relation to the brush, and secondly, its angle of rotation is also changed according to the rotation being performed by the user. Whether and how TrenchBroom can adapt the angle of rotation of an entity depends on the following rules.

- "angles" is interpreted as "pitch yaw roll" (if the entity model is a Quake MDL, pitch is inverted)
- "mangle" is interpreted as "yaw pitch roll" if the entity classnames begins with "light", otherwise it's a synonym for "angles"
- "angle" is interpreted as the rotation angle about the Z axis
- If the point entity's bounding box is not centered in the XY plane (e.g. Quake's misc_explobox), attempts to rotate the entity in TrenchBroom will be blocked. This is done to prevent the model from being rotated out of the collision box, which doesn't rotate in Quake.

Finally, if TrenchBroom has found a property that contains the rotation angle of the entity, it adapts the value of that property according to the rotation being performed by the user. These rules are quite complicated because sadly, the entity definitions do not contain information about how rotations should be applied to entities. But in practice, they should just perform as expected when you work with the rotate tool in the editor.

![Checkbox](images/UpdateAnglePropertyAfterTransform.png)

This behavior can be disabled temporarily by toggling the checkbox to the right of the "Apply" button when the rotate tool is active. If the checkmark is removed, TrenchBroom will not update any entity properties except for the origin when an entity is rotated either by the rotate tool or by a shortcut.

### Flipping Objects {#flipping_objects}

Flipping has the effect of mirroring the selected objects, the mirror being a plane which is defined by the center of the bounding box of the selected objects, snapped to the grid, and by a normal vector. The normal vector of the plane depends on the actual flipping command and the viewing direction of the camera in the 3D viewport or the view plane of the focused 2D viewport. The following table explains how the normal vector is derived from this information.

Shortcut                                                  Direction     Normal (2D)   Normal (3D)
--------                                                  ---------     -----------   -----------
#action(Controls/Map view/Flip objects horizontally)      Horizontal    Right axis    Axis-aligned right axis
#action(Controls/Map view/Flip objects vertically)        Vertical      Up axis       Z axis

In the case of the 3D viewport, the normal of the mirror plane is the coordinate system axis that is closest to the right axis of the camera. This means that if the camera is pointing in the general direction of the Y axis, and therefore its right axis points in the general direction of the X axis, the normal of the mirror plane will be the X axis. Sometimes, you will not be able to determine which of the coordinate system axes is closest to the right axis of the camera because the right axis is close to two coordinate system axes. To avoid such confusion, it is best to perform flipping in the 2D viewports.

### Scaling Objects {#scaling_objects}

Hit #menu(Menu/Edit/Tools/Scale Tool) to activate the scale tool. If you know the exact X/Y/Z scale factors you want, you can enter them in the toolbar and click "Apply". The selected objects will be scaled relative to the center of their bounding box.

![Scale Tool Toolbar](images/ScaleToolToolbar.png)

Otherwise, there are various ways to interactively scale your selected objects.

In the 3D view:

- Dragging a side of the bounding box stretches that axis only.

    ![Dragging a side of the bounding box](images/Scale3DSide.gif)

- Dragging an edge stretches the two adjacent sides of the bounding box proportionally.

    ![Dragging an edge of the bounding box](images/Scale3DEdge.gif)

- Dragging a corner resizes all 3 axes proportionally.

    ![Dragging a corner of the bounding box](images/Scale3DCorner.gif)

In 2D views:

- Corners allow unconstrained scaling along 2 axes.
- Sides stretch a single axis only, as in the 3D view.

Two modifiers can be used in both the 2D and 3D views:

- Hold #key(Shift) to scale all three axes to scale proportionally in 3D views, or only the two axes perpendicular to the camera in 2D views. You can press/unpress #key(Shift) during a drag. (#key(Shift) has no effect when dragging corners in 3D, since all 3 axes are already scaled proportionally.)

- Hold #key(Alt) to move the scale anchor point to the center of the bounding box. Otherwise, the anchor point is opposite the handle being dragged.

    ![Dragging a side of the bounding box](images/Scale3DSideCenter.gif)


### Shearing Objects {#shearing_objects}

Hit #menu(Menu/Edit/Tools/Shear Tool) to activate the shear tool. Dragging a side of the bounding box shears along that plane. You can add the #key(Alt) key to drag vertically if you're not shearing the top or bottom of the bounding box.

Alignment lock in the Shear tool only works in Valve 220 format maps.

![Vertical shearing in the 3D viewport](images/Shear3DVertical.gif)

### Deleting Objects {#deleting-objects}

Deleting objects is as simple as selecting them and choosing #menu(Menu/Edit/Delete). Note that if you delete all remaining brushes of a brush entity, that entity gets deleted automatically. Likewise, if you delete all remaining objects of a group, that group also gets deleted.

## Shaping Brushes {#shaping-brushes}

TrenchBroom offers several tools for changing brush shapes. This chapter starts with extrusion for extending brushes, stamping new brushes, or moving faces. It then covers clipping, sweeping faces along generated paths, and chamfering edges or corners. The more general vertex editing and CSG operations follow in the next chapter.

### Extrusion {#extrusion}

Brushes can be extruded using the extrude tool by moving their faces along their respective normal vectors with the mouse. To extrude a selected brush, hold #key(Shift) and move your mouse pointer onto or near the face you wish to move. You will notice that one of the brush's faces is highlighted with a yellow outline. Drag with your left mouse button while still holding #key(Shift) to move the highlighted face along its normal. Note that you can also move brush faces which are behind the brush as long as these faces have an edge that is visible from the camera.

![Extruding a brush in the 3D viewport](images/ExtrudeTool3D.gif)

Note that you cannot change the number of faces of a brush with the extrude tool. This means that you cannot push a face back into a brush indefinitely. TrenchBroom will refuse to move it further as soon as that movement would make other faces disappear. The same applies to pulling a face out of a brush, which can make that face disappear. This is disallowed as well.

If you hold #key(Ctrl) when you start dragging, the brush will not be extruded. Instead, a new brush will be created. Both of the original and the new brush together will have the same shape that the original would have had if you had just extruded it without holding #key(Ctrl), and the two brushes are split where the face you were dragging originally sat.

![Splitting a brush in the 3D viewport](images/ExtrudeTool3DSplitMode.gif)

When starting a drag with #key(Ctrl) you can also drag inward to split the original brush:

![Splitting a brush inward in the 3D viewport](images/ExtrudeTool3DSplitInwardMode.gif)

You can also extrude several brushes at the same time by moving their faces using the extrude tool, but only if these faces line up perfectly. As the following animation illustrates, it's not enough that the faces are parallel - they have to be identical. Note however that their normals can be opposing, so you can also resize the faces where two brushes touch. If the two faces have the exact same vertices, you can pick the shared face(s) for extrusion by hovering over a shared edge. Splitting is disabled when extruding opposing faces to avoid creating overlapping brushes.

![Extruding multiple brushes](images/ExtrudeTool3DMultipleBrushes.gif)

The extrude tool also works in the 2D viewports, of course, but the ability to move faces which are behind the selected brush is absent there. In both cases, TrenchBroom uses two methods to determine how to snap the distance by which you drag the face:

- The distance is snapped to the current grid size, i.e., if you drag a face by 17.5 units along its normal, it will be moved by 16.0 units if the current grid size is 16. This is useful if you are resizing brushes which are part of a curve because their faces will line up after the drag.
- The vertices of the dragged faces are snapped to the grid planes, i.e., whenever at least one vertex component (X,Y, or Z) is a multiple of the current grid size, the face is snapped to that vertex. This makes it easy to align a face to other adjacent faces.

Both snap modes are used simultaneously. There may be situations when you have to move the camera closer to a face in order to have sufficient precision when dragging the face.

#### Stamping Brushes {#stamping-brushes}

Normally, extruded brushes continue the shape of the original brush, but this is not always desirable.

![Extruding vs. stamping](images/ExtrudeToolStamping.png)

In the image, the selected brush on the left has been extruded from the top face of the brush below it. It continues the shape of the brush, a frustum. The selected brush on the right has been stamped from the top face of the brush below it. Stamping does not continue the shape of the original brush, instead, it just duplicates the selected face and moves it along its normal. The new brush then becomes the convex hull of the vertices of the original face and its duplicate.

To stamp a brush, hold #key(Ctrl) and #key(Alt) in addition to #key(Shift) and drag a face of a selected brush.

#### Moving Faces Instead of Extruding {#moving_faces}

The brush extrude tool offers a quick way to move an individual face of a brush. Hold #key(Alt) in addition to #key(Shift) when starting to drag a face to enable this mode. You will notice that a face is highlighted as usual, but when you start dragging the mouse, the face will just be moved in the direction you are dragging. In 2D views, the move is not restricted by the face normal, and other faces will be affected as well. In 3D views, the move is restricted by the face normal.

![Moving faces (2D view)](images/ExtrudeTool2DFaceMoving.gif)

The distance is snapped to the current grid size. Moving multiple faces is possible if the faces lie on the same plane. The [UV Lock](#uv_lock) setting controls whether alignment lock is used when dragging faces using this mode.

### Clipping {#clipping}

Clipping is the most basic operation for Quake maps due to how brushes are [constructed from planes](#brush_geometry). In essence, all that clipping does is adding a new plane to a brush and, depending on the brush's shape, removing other planes from it if they become superfluous. In TrenchBroom, clipping is done using the clip tool, which you can activate by choosing #menu(Menu/Edit/Tools/Clip Tool). The clip tool lets you define a clip plane in various ways, and the lets you apply that plane to the selected brushes.

There are three different outcomes of applying a clip plane: Drop all parts of the selected brushes that are in front of the (oriented) clip plane, drop all parts of the selected brushes that are behind the clip plane, or slice the selected brushes into two pieces each. The following image illustrates these three modes:

![The three clip modes](images/ClipModes.png)

In all three images, there is a clip plane defined by two points. This clip plane slices the single brush in the image into two parts whereby the left part is below the plane and the right part is above it. In the first image, the clip mode is set to retain the part of the brush that is below the clip plane and to discard the part that is above the clip plane. The resulting brush will be shaped like the red part of the brush in the image. In the second image, the clip mode is set to retain both parts of the brush, and the result of this clipping operation will be two brushes. In the third image, the clip mode is set to retain the part of the brush that is above the clip plane and to discard the other part. This is the opposite of the first case. In the clip tool, you can cycle through these three modes by hitting #action(Controls/Map view/Toggle clip side). There are two ways of defining a clip plane: The more common way is to place at least two and and most three points (in this context, these points are called clip points) in either the 3D or a 2D viewport. The other way is to define the clip plane by using an existing brush face.

#### Clip Points {#clip-points}

To place clip points, you simply left click into a viewport when the clip tool is active. Alternatively, you can add two clip points at once by dragging with the left mouse. In that case, the first clip point is placed at the start point of the drag, and the second clip point is placed at the end point of the drag. In the 3D viewport, you can only place clip points on already existing brushes, whereas in the 2D viewports, you can place them anywhere. Clip points are snapped to the grid, however, in the 3D viewport, there is a caveat which we will explain below. When the clip tool is active, it gives you some feedback in the form of an orange sphere that appears close to your mouse pointer. This sphere indicates where a clip point would be placed after being snapped to the grid. This feedback sphere is only shown if a clip point can actually be placed at or close to the point under the mouse.

Once two clip points have been placed, TrenchBroom will attempt to guess a clip plane even though it is underspecified: You cannot define a plane with only two points. If you are happy with the clip plane that TrenchBroom has determined, then you can apply the clipping operation by hitting #action(Controls/Map view/Perform clip). Otherwise, you can place the third point to fully define the clip plane, or you can change the clip points you have already placed. To change a clip point, you can just left click and drag it with the mouse. To remove the most recently place clip point, you can choose #menu(Menu/Edit/Delete).

#### Clip Point Snapping {#clip-point-snapping}

In the 3D viewport, clip points can only be placed on the faces of already existing brushes. Such a clip point is snapped to the grid that has been projected onto the brush face on which you placed that point. So it appears to be snapped to the projected grid, but it is also kept glued onto the brush face. If the point were snapped in all dimensions, then it would either sink into or move away from the brush face it was placed on. TrenchBroom avoids this by glueing clip points to the brush faces on which they were placed by the user. This means that if you attempt to move an already placed clip point around using the 3D viewport, that point will be moved to the closest snapped point on the brush face under the mouse.

In the 2D viewport, clip points are just snapped to the visible grid, so they are not restricted to being glued to brush faces. You can place clip points in any viewport you wish, and you can move clip points that have been placed in one viewport using any other viewport, but the grid snapping will be that of the viewport that you are using to move the clip point. That means if you use a 2D viewport to move a clip point that was placed in the 3D viewport, then that point can be dragged off of the brush face on which it was placed and into the void. Conversely, if you use the 3D viewport to move a clip point that was placed in a 2D viewport, that clip point will snap onto  the brush face under the mouse, or it will not move at all if there is no brush face under the mouse.

#### Matching Clip Plane {#matching-clip-plane}

![Matching a clip plane](images/MatchingClipPlane.gif) The clip plane can also be defined by matching it to an existing brush face. To match a clip plane to an existing brush face, you have to double click that face in the 3D viewport. As a result, the brush face gets an orange outline, and a clip plane is defined to match the face's plane exactly. This can be quite useful when shaping geometry to other geometry. Note that the plane points of the clip plane are the plane points of the brush face to which the clip plane was matched, so there should be no trouble with microleaks when using this particular function.

### Sweeping {#sweeping}

The sweep tool fills the gap between the selected brush faces and a copy of those faces, called the destination cap, with a run of brushes. Depending on where you place the destination cap and which path you choose, this lofts the faces along a straight line, revolves them around an axis to build arches and pipes, or routes them through an S-curve, optionally twisting and tapering along the way. The UV setting can preserve the source projections or continuously rotate the texture alignment across connected boundary faces and sweep segments in Valve-style map formats. Continuous alignment preserves the texture scale and leaves one seam around a closed profile. To use the sweep tool, select one or more brush faces and choose #menu(Menu/Edit/Tools/Sweep Tool).

Bridge mode connects two disconnected selected face components on different brushes. Each component can contain one or more edge-connected faces, and the two components must have matching face, vertex, and shared-edge topology. The first component supplies the entrance geometry and side materials; the second component is the exact, locked destination. Use **Swap ends** to reverse that direction. Bridge mode automatically matches cyclic and reversed vertex order and uses the original endpoint vertices so the generated brushes meet both selected components without a coordinate seam. If an interpolated segment cannot form valid convex brushes, the preview reports the affected segment instead of applying partial geometry.

![Rotating a face to form a bend with the Sweep Tool](images/SweepTool.gif)

When the sweep tool is active, a ghost outline shows where the destination cap will end up, and a handle allows you to place it:

- Dragging the center of the handle moves the destination cap.
- Dragging one of the rings rotates it about the corresponding axis.
- Dragging the green handle scales it uniformly, flaring or tapering the sweep.
- Pressing #action(Controls/Map view/Move objects up; Move objects forward) and the other movement shortcuts moves the destination cap by one grid step.
- Pressing #action(Controls/Map view/Roll objects clockwise) and the other rotation shortcuts rotates the destination cap by one angle snap step.
- Pressing #action(Controls/Map view/Increase sweep scale) or #action(Controls/Map view/Decrease sweep scale) moves the scale handle out or in by one grid step, growing or shrinking the destination cap.

The generated brushes are shown as a preview in the viewports while you place the destination cap. Shortcuts that act on the selection, including UV editing, are unavailable until the tool is deactivated. The controls above the editing views determine how the gap is filled:

- **Segments** is the number of brushes created between the selected faces and the destination cap.
- **Path** selects how the brushes are laid out: Arc revolves the faces around an axis derived from the rotation, Straight lofts them along a line, and S-bend routes them through an S-curve. The destination cap ends up in the same place in each mode.
- **Iterations** repeats the sweep, continuing from the previous destination cap. For example, an arc that rises while turning becomes a spiral staircase when swept for multiple iterations.
- **Snap to integer grid** rounds the vertices of the generated brushes to integer coordinates.
- **Reset** moves the destination cap back onto the selected faces.

Hit #action(Controls/Map view/Perform sweep) to fill the gap with brushes and select them. Hitting #action(Controls/Map view/Cancel) moves the destination cap back onto the selected faces, and hitting it again deactivates the tool.

### Chamfering {#chamfering}

Choose #menu(Menu/Edit/Tools/Chamfer Tool) to bevel selected brush edges or cut selected brush corners. The target selector switches between edge and vertex handles. Select one or more handles to preview the result, then adjust the chamfer distance and apply the operation. Edge chamfers also support multiple segments for a rounded profile. UV Lock controls how the affected face projections are preserved.

## Undo and Redo {#undo_redo}

Almost everything that you do in TrenchBroom can be undone by choosing #menu(Menu/Edit/Undo). This applies to every action that somehow modifies the map file (such as moving objects), but it also applies to some actions that do not change the map file, such as selection, hiding, and locking. There is no limit to how many actions you can undo, and once an action is undone, you can redo it by choosing #menu(Menu/Edit/Redo).

### Undo Collation and Transactions {#undo-collation-and-transactions}

TrenchBroom groups certain sequences of actions into transactions which can be undone and redone as one. For example, if you select a few objects and then hide them, the objects are automatically deselected. Both the action of deselecting the objects to be hidden and hiding them are grouped together into a transaction, so when you undo, the objects will be unhidden and reselected at the same time.

TrenchBroom also merges sequences of the same action if they happen within one mouse drag or within a certain time. If you move a brush around, all steps of the move are merged into one action. If you move a brush by pressing the appropriate keyboard shortcuts several times within a short period, those actions are merged as well. This saves memory and allows the entire sequence to be undone in one step.
