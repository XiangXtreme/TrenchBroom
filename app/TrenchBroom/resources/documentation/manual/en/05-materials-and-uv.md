# Materials and UV Editing {#materials_and_uv}

## Working with Materials {#working_with_materials}

There are two aspects to working with materials in a level editor: [material management](#material_management) and material application. This section deals with the latter, so you will learn different ways to apply materials to brush faces and manipulate their alignment. Before we dive into that, we cover three general topics: the material browser, how TrenchBroom assigns materials to newly created brush faces, and the different material projection modes in TrenchBroom.

### The Material Browser {#material_browser}

![The material browser](images/TextureBrowser.png) The material browser is part of the face inspector and is used for two purposes: changing the material for the currently selected faces and selecting the _current material_. The name of every material is displayed below its image. Materials that are currently in use have a yellow border, while the current material has a red border. Hover over a thumbnail to see the material name and texture dimensions.

The browser has two compact control rows above the thumbnails. The first is a search field. Enter one or more space-delimited words to show only names containing every term. The second row contains the following controls:

- **Name / Usage** sorts materials alphabetically or by their current use count.
- **Group** groups materials by [material collection](#material_management). Click a group heading or its disclosure indicator to collapse or expand that collection.
- **Used** limits the browser to materials currently used by the map.
- **100%–500%** changes the thumbnail scale. You can also hold #key(Ctrl) and use the mouse wheel over the browser to step through the same scale range.

The selected thumbnail scale is shared with **Preferences > View > Material Browser > Icon size**. Ordinary wheel input continues to scroll the browser.

To select all faces having a certain material, right-click that material in the material browser and click "Select Faces" in the pop-up menu.

To select all brushes having a face with a certain material, right-click that material in the material browser and click "Select Brushes" in the pop-up menu.

### Filtering Material Collections {#filtering-material-collections}

![Filtering material collections](images/TextureCollectionEditor.png) To filter out material collections, click on the "Settings" button in the material browser's title bar. This reveals a UI to enable or disable individual material collections. Select one or more material collections from the "Available" section and click on the "+" icon to enable them. To disable material collections, select them in the "Enabled" section and click on the "-" icon. Click on the circular arrow icon to reload all material collections.

Click on the "Browser" button in the material browser's title bar to return to the material browser.

### Material Projection Modes {#material_projection_modes}

In the original Quake engine, materials are projected onto brush faces along the axes of the coordinate system. In practice, the engine (the compiler, to be precise), uses the normal of a brush face to determine the projection axis - the chose axis is the one that has the smallest angle with the face's normal. Then, the material is projected onto the brush face along that axis. This leads to some distortion (shearing) that is particularly apparent for slanted brush faces where the face's normal is linearly dependent on all three coordinate system axes. However, this type of projection, which we call _paraxial projection_ in TrenchBroom, also has an advantage: If the face's normal is linearly dependent on only two or less coordinate system axes (that is, it lies in the plane defined by two of the axes, e.g., the XY plane), then the paraxial projection ensures that the material still fits the brush faces without having to change the scaling factors.

The main disadvantage of paraxial projection is that it is impossible to do perfect alignment locking. _Alignment locking_ means that the material remains perfectly in place on the brush faces during all transformations of the face. For example, if the brush moves by 16 units along the X axis, then the materials on all faces of the brush do not move relatively to the brush. With paraxial projection, materials may become distorted due to the face normals changing by the transformation, but it is impossible to compensate for that shearing.

This is (probably) one of the reasons why the Valve 220 map format was introduced for Half Life. This map format extends the brush faces with additional information about the UV axes for each brush faces. In principle, this makes it possible to have arbitrary linear transformations for the UV coordinates due to their projection, but in practice, most editors keep the UV axes perpendicular to the face normals. In that case, the material is projected onto the face along the normal of the face (and not a coordinate system axis). In TrenchBroom, this mode of projection is called _parallel projection_, and it is only available in maps that have the Valve 220 map format.

### How TrenchBroom Assigns Materials to New Brushes {#how-trenchbroom-assigns-materials-to-new-brushes}

In TrenchBroom, there is the notion of a current material, which we have already mentioned previous sections. Initially, the current material is unset, and it is changed by two actions: selecting a brush face and selecting the current material by clicking on a material in the material browser. When TrenchBroom creates a new brush or a new brush face, it may consult the current material to determine which material to apply to the newly created brush faces. This is not always the case: Sometimes, TrenchBroom can determine materials for newly created brush faces from the context of the operations. We have discussed this earlier for [CSG operations](#materials_and_csg_operations). In other cases, such as when you create a new brush with the mouse, TrenchBroom will always apply the current material.

### Assigning Materials Manually {#assigning-materials-manually}

To change the material of the currently selected faces, left click on a material in the material browser. This also works if you have selected brushes (and nothing else) - in this case, the new material is applied to all faces of the currently selected brushes.

You can also transfer material and attributes from one face to another. "Attributes" in this context refers to almost any characteristic &mdash; such as offset, scale, or surface flags &mdash; that you can modify through the [face attribute editor](#face_attribute_editor). The sole exception to this is content flags; the content flags on the target face(s) will always be preserved unchanged.

To do this transfer, start by selecting the source face with #key(Shift) + left click. Then, hold one of the following modifier combinations depending on what you want to transfer:

Modifier Keys            Meaning
-------------            -------
#key(Alt)                Transfer material and attributes from selected face (by projecting it on to the target faces)
#key(Alt)#key(Shift)     Transfer material and attributes from selected face (by rotating it on to the target faces, available on Valve format maps only)
#key(Alt)#key(Ctrl)      Transfer material only (attributes of the target are preserved)

and perform one of the following actions, depending on which faces you want to transfer to:

Actions                    Faces to transfer to
-------                    --------------------
Left mouse click           clicked face
Left mouse drag            all faces dragged over (each subsequent face dragged over will transfer from the last)
Left mouse double click    all faces of target brush

To clarify, using the #key(Alt) modifier copies the source face's UV axes to the target face without altering it. Sometimes this is desirable, but it can lead to the target face having a stretched material if the face normals are very different. The #key(Alt)#key(Shift) combination avoids this by rotating the source face's UV axes onto the target face, but it's only available on Valve format maps.

Finally, you can use copy and paste to copy the material and attributes of a selected face onto other faces:

1. Select the face that you wish to copy from and choose #menu(Menu/Edit/Copy)
2. Select the faces that you wish to copy to, and choose #menu(Menu/Edit/Paste)

### Replacing Materials {#replacing-materials}

If you want to replace a particular material with another one, you can choose #menu(Menu/Edit/Replace Material...). This opens a window where you can select the material to be replaced and the replacement material using two material browser. This window is depicted in the following screenshot.

![Material Replace Window (Mac OS X)](images/ReplaceTexture.png)

Select the material you wish to replace in the left material browser. This browser by default only shows you the materials which are currently in use in the map. In the screenshot, the material "b_pv_v1a1" has been selected for replacement and therefore has a red border. Then select the replacement material in the right material browser ("b_sr_20c" in the screenshot). Finally, hit the "Replace" button. The replacement is applied to all brush faces in the map if nothing is currently selected. Otherwise, it is applied to the selected brush faces only. If the replacement succeeded, the faces which have been replaced are subsequently selected. Otherwise, the selection remains unchanged.

### Setting Face Attributes {#setting-face-attributes}

Face attributes control how materials are mapped onto brush faces. At the very least, every face has the attributes offset, scale, and angle. The offset allows you to shift a material on a face, the scale factors stretch the material, and by changing the angle you can rotate the material. Additionally, some engines have further attributes. Quake 2 adds surface flags and a surface value, and additional content flags. All of these values can be changed in different ways: There is a face attribute editor that allows you to enter the values directly, you can use keyboard shortcuts in the 3D viewport, or you can use the UV editor.

#### Aligning, Justifying and Fitting Textures {#align_justify_fit_textures}

To quickly align, justify or fit a texture to a brush face, you can select the face and use the buttons below the UV editor.

Operation    Behavior
---------    --------
Align        Rotate the texture to make it parallel to a face edge.
Justify      Change the offset to justify the texture to the face's bounding box in the selected direction.
Fit          Change the scale to fit the texture (or a multiple of it) onto the face while keeping it justified.


![Align, justify and fit buttons](images/AlignJustifyFit.png) Click one of the four triangle buttons to justify the texture against the face's bounding box. If the texture size in the chosen direction is a multiple of the face size along the same axis, you can press the justify button multiple times to step through different options. This can be helpful to justify a texture from a texture atlas. Hold #key(Shift) when clicking to step through the options in the opposite direction.

The lower three buttons are used to align and fit the texture. Click on the leftmost button to align the texture to the face edges. Click repeatedly to cycle through the face edges. Hold #key(Shift) while clicking to cycle in the opposite direction.

The two remaining buttons fit the texture horizontally and vertically. If the texture is smaller than the face, repeated clicks cycle through integer fit factors, increasing the repeat count one step at a time; hold #key(Shift) to cycle through the options in the opposite direction. If the texture is larger than the face, the fit buttons scale it so that the entire texture is visible on the face. To instead show only a fraction (1/n) of the texture, hold #key(Ctrl) while clicking to cycle through the integer subdivisions, and hold #key(Ctrl) and #key(Shift) to cycle through them in the opposite direction. This mode is useful for trim sheets, where several sub-textures are packed into a single image.

The button in the center of the four justification arrows auto fits the texture, i.e. it aligns, justifies and fits the texture.

#### The Face Attribute Editor {#face_attribute_editor}

The face attribute editor is located in the face inspector, right between the UV editor and the material browser. It contains several controls to edit the face attributes of one or several selected brush faces.

![Face Attribute Editor (Mac OS X)](images/FaceAttribsEditor.png)

Two types of controls are visible in the screenshot above. Numerical input controls consist of a text field and small buttons to increase or decrease the value in the field. The text field will show the value of the respective face attribute, such as "1" for the X Scale in the screenshot. If more than one brush face is selected, the text field will also show the value if all faces have the same value for the respective attribute, or it will show the placeholder word "multi" otherwise. In the screenshot above, the X Offsets of the selected brush faces differ, hence the text field shows "multi". All other values are identical for all selected brush faces, so all other attribute editors show concrete values instead of the placeholder. By entering a number into the text field, the attribute value of all selected brush faces can be set to that value. Consequently, if you were to enter the value "32" into the X Offset editor in our example above, all selected brush faces would have this value as their X Offset afterwards.

The spin button however works differently. By clicking the up- or down arrow button, you can increase or decrease the value of the respective face attribute by a certain delta value, which depends on the grid settings and the currently pressed modifier keys. The following table explains which delta value is chosen in each case.

Attribute    Default      #key(Shift) pressed  #key(Ctrl) pressed
---------    -------      -----------------    -----------------
Offset       Grid size    2 * grid size        1.0
Scale        0.1          0.25                 0.01
Angle        15°          90°                  1°

Note that these deltas are applied to the respective attributes of every selected brush face. So if you have selected two brush faces, one with an X Offset of 0 and one with an X Offset of 8, and your current grid size is 16, clicking on the up arrow button next to the X Offset attribute editor will change the X Offsets to 16 and 24, respectively. Only entering a value in the text field will set the two X Offsets to the same value.

In addition to using the buttons to change the values, you can use the scroll wheel or the arrow keys when the text field has focus. Scrolling and the arrow keys follow the same rules to determine the delta values as described in the table above.

For attributes that represent flag values, such as the surface and content flags for Quake 2, there is a different type of control available in the face attribute editor. This control shows a textual representation of the flag values in a text field, and you can change the flags using a dropdown window that is shown if you click on the button labeled "..." next to the text field. The dropdown window contains one checkbox for each flag, and you can check or uncheck them individually.

The text field for content flags will display "multi" if the currently selected faces have different sets of content flags. Note that for games that support content flags, it is almost always desirable to use identical content flags on all faces of a given brush, to avoid unexpected behavior in-game; therefore if the content flags text field shows "multi" when a single brush is selected, this can be an indicator of an error that should be corrected.

The surface flags text field will also display "multi" if the selected faces have different sets of surface flags, but this is not necessarily a situation that needs to be corrected. It is often valid to have different surface flags on different faces of a brush.

#### Material Alignment Keyboard Shortcuts {#material-alignment-keyboard-shortcuts}

The following shortcuts work in the 3D viewport, and affect all selected brushes or brush faces:

Attribute    Keys                                    Default      #key(Shift) pressed  #key(Ctrl) pressed
---------    ----                                    -------      -------------------  -----------------
Offset       #key(Left)#key(Right)#key(Up)#key(Down) Grid size    2 * grid size        1.0
Angle        #key(PgUp)#key(PgDown)                  15°          90°                  1°

Command                                   Keys
-------                                   ----
Flip Horizontally                         #action(Controls/Map view/Flip textures horizontally)
Flip Vertically                           #action(Controls/Map view/Flip textures vertically)
Reset alignment                           #action(Controls/Map view/Reset texture alignment)
Reset alignment to world aligned          #action(Controls/Map view/Reset texture alignment to world aligned)

These are interpreted relative to the 3D camera (except "Reset"). This means that pressing #key(Up) will move a material roughly in that direction visually, possibly increasing or decreasing the X or Y offset depending on the camera and face orientation. The angle is treated similarly: Pressing #key(PgUp) will rotate the material counterclockwise visually, and pressing #key(PgDown) will rotate it clockwise.

#### The UV Editor {#uv_editor}

The UV editor is located at the top of the face inspector. Using the UV editor, you can adjust the offset, the scale and the angle of the material of the currently selected brush face. Note that the UV editor is only usable if one brush face is selected. If multiple brush faces are selected, the UV editor is empty.

![UV Editor](images/UVEditor.png)

The material of the current face is shown in the background of the UV editor. The material is tiled, and the tiling edges are displayed in gray. These tiling edges are called the _UV grid_. The shape of the currently selected brush face is displayed in white. The yellow filled circle marks the origin of the material, and the two red lines that meet at the origin mark the origin axes of the material - these are used for scaling. The larger yellow circle is a handle used for rotating the material. The UV axes are displayed in red and green at the center of the brush face.

To change the offset of the material in relation to the brush face, you can just click and drag the material anywhere with the left mouse button. Note that the material will snap to the vertices of the brush face to make alignment easier.

You can scale the material by clicking and dragging the gray UV grid lines, which will also snap to the vertices of the brush face. Scaling is relative to the origin of the material, as marked by the yellow circle. To change the scaling origin, you can left click and drag the yellow circle, or you can left click and drag the red lines meeting at the origin. The lines allow you to set the X and Y coordinates of the origin separately. The origin snaps to the vertices of the face and to its center.

To rotate the material about the origin, left click and drag the large yellow circle, or hold #key(Ctrl) and left click and drag anywhere in the UV editor. The angle will snap to the edges of the brush face to make it easier to adjust it to the shape of the face.

Shearing the material is possible if the map uses a [parallel projection](#material_projection_modes). Shear by holding #key(Alt) while left clicking and dragging the gray UV grid lines.

At the bottom of the UV editor are the following controls:

![UV Editor Toolbar](images/UVEditorToolbar.png)

- Reset alignment. All attributes are reset, and in [parallel projection](#material_projection_modes) format maps, the material is projected from the face plane. In standard format maps, acts the same as "Reset alignment to world aligned".
- Reset alignment to world aligned. All attributes are reset and the material is projected from an axial plane.
- Flip material horizontally
- Flip material vertically
- Rotate material 90° counterclockwise
- Rotate material 90° clockwise
- Grid controls for subdividing the UV grid. This can be useful to align part of a larger trim sheet to the face.
