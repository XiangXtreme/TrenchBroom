# Entities, Outliner, and Layers {#entities_outliner_layers}

## Entity Browser {#entity_browser}

The entity browser is in the bottom Content Browser. Select the **Entities** folder in the source tree, or use #menu(Menu/View/Show Entity Browser). The content area displays a 3D thumbnail grid using the existing entity model and bounds renderer. The Point and Brush subfolders narrow the types. Use the shared search field to match names or descriptions, **Filters** to show only used types, and **View** to change thumbnail size, grouping or name/usage order. Selecting a thumbnail shows its description and creation action in the compact bottom bar without changing the map selection. The shared breadcrumbs and back/forward buttons navigate entity and file folders while preserving their search state.

Drag a point type into a 2D or 3D viewport to place it, or double-click it, press Enter, or click **Create in current view** to create it at the view's default position. To create a brush entity, select brushes, choose a brush type, then click **Turn selection into entity**. Creation uses normal map undo and redo. Without an open map you can browse definitions, but creation and definition settings are disabled.

The browser uses the entity definition files configured for the current map. If an expected class is missing, verify the active FGD, ENT, or DEF configuration before creating an unconfigured entity manually.

## Entity Properties {#entity_properties}

An entity is, essentially, a collection of properties, and a property is a key value pair where both the key and the value is a string. Some values have a special format, such as colors, points, or angles. But in general, if you are editing an entity, you will be working with strings. In TrenchBroom, you can add, remove, and edit entity properties using the entity property editor, which is located below the scene tree in the right-hand Scene page. Press #key(Ctrl)#key(2) to show the scene and expand its properties. Use the properties table for the complete editor, including default values, smart editors, flags, help, and linked-group protection.

![Entity Property Editor](images/EntityPropertyEditor.png)

The entity property editor is split into two separate areas. At the top, there is a tabular representation of the properties of the currently selected entities and, if applicable, the defaults for those properties which are not present in the selected entities.

### Default Entity Properties {#entity_properties_defaults}

The default properties are shown in _italics_ below the actual properties of the selected entities. To hide the default properties, you can uncheck the checkbox at the bottom of the table. Default entity properties are defined in an entity definition file such as an FGD file, and their meaning depends on the game. Some games, like Quake, have builtin default values for entity properties, and the default entity properties reflect these defaults (if set up correctly in the entity definition file).

Other games such as Half Life don't provide default values for entity properties, and expect the defaults to be set explicitly for every entity. If configured in the [game configuration](#game_configuration_files_entities), then TrenchBroom will automatically instantiate the default entity properties (with default values) when a new entity is created. Note that not every default property will be instantiated by TrenchBroom - only those properties which have a default value configured in the entity definition file can be instantiated by TrenchBroom.

![Setting Default Entity Properties](images/SetDefaultPropertiesMenu.png)

Below the entity property editor, there is a small button which pops up a menu when pressed. This menu has three entries:

- **Set existing default properties**: This resets all entity properties to their default values if they have one. No entity property is added or removed, and no entity property is changed unless it has a default value.
- **Set missing default properties**: This adds all default entity properties that aren't set. Only adds new entity properties. No entity property is removed and no existing entity property is changed.
- **Set all default properties**: This is a combination of the above. Every entity property having a default value is set to its default, regardless of its current value. Missing default properties are added. No entity properties are removed, and only default entity properties are changed.

### Editing Properties {#editing-properties}

To select an entity property, just click in the row that represents that property in the table. The clicked field will be highlighted, indicating that it has focus. The highlight indicates that you can change the field by entering text. In the screenshot above, the "mangle" property has been selected, and its value has focus, indicating that it is ready to be changed.

If you are changing a lot of properties, you may wish to navigate quickly through the table. You can use the cursor keys to move focus around in the table. Alternatively, you can hit #key(Tab) to move field by field. If the focus is on a key of some property, hitting tab will move the cursor the value field of that property, and hitting tab again will move it to the key field of the next property, and so on until you reach the end of the table. You can also move in the opposite direction by hitting #key(Shift)#key(Tab). Hitting #key(Return) starts editing the field that has focus, and hitting #key(Return) again applies your change and keeps the focus on that field. This also applies if you pick one of the suggestions that pop up while you type a key or a value.

To change the key or the value of a property, set the focus to the appropriate field in the table. If you enter some text now, that text will replace the content of the field. An alternative way to change a field is to click on it while its property is already selected. This will show an actual text field in which you can enter the text.

There are several ways to add a property to an entity. First, you can click on the button with the "+" label at the bottom of the table. This will insert a new property with a default name and no value into the table. Second, you can hit #key(Ctrl)#key(Return) to add a new property. In both cases, the new property will be selected so that you can start editing its key and value right away as described above. Finally, you can add a property by changing the value of a default property. This will promote the default property to an actual property of the entity.

To remove entity properties, you should click the rows in the table that represent them and hit the button labeled "-" at the bottom of the table.

### Multiple Entity Selections {#multiple-entity-selections}

![Multiple Entity Selections](images/EntityPropertyEditorMultiSelection.png) If multiple entities are selected, the table will show the union of all their properties and not just those properties which all of the selected entities have in common. Properties which are not present in all of the selected entities have their names grayed out, and properties which have different values in those entities that actually have those properties are displayed with an empty value. In the screenshot, there are three light entities selected. Consequently, the "classname" property is present in all of them and has the same value everywhere. Likewise, the "origin" property is present in all of these entities, but it has different values in each of them, so it is shown without a value. The "light", "wait", "angle", and "mangle" properties, on the other hand, are only present in some of the selected entities, but they do have the same values in each of the entities that have them.

If you change an entity property when multiple entities are selected, the change gets applied to all of the selected entities, even if that requires adding that property. So if you were to change the value of the "light" property in the example above to 200, each of the selected entities will subsequently have a "light" property with the value 200, even if only a subset of the selected entities had that property before.

### Smart Entity Property Editors {#smart-entity-property-editors}

TrenchBroom provides special editors for the following entity properties: spawnflags, colors, and choices. These special editors are called _smart property editors_ and are displayed below the entity property table if you select an entity property for which such an editor exists.

Type             Editor                                                         Description
----             ------                                                         -----------
Spawnflags       ![Smart Spawnflags Editor](images/SmartSpawnflagsEditor.png)   A table of checkboxes which allow you to toggle the individual spawnflag values.
Color            ![Smart Spawnflags Editor](images/SmartColorEditor.png)        A color chooser control that allows you to convert between byte and float color values, and provides a list of all colors found in the map.
Choice           ![Smart Spawnflags Editor](images/SmartChoiceEditor.png)       A dropdown list of values. You can also enter any text into the text box.

### Linking Entities {#linking-entities}

Entities can be linked using special link properties. Each link has a source and a target entity. The target entity has a property called "targetname", and the value of that property is some arbitrary string. The source entity has a "target" or a "killtarget" property, and the value of that property is the value of the target entity's "targetname" property. To create an entity link, you have to manually set these properties to the proper values. Currently, the names of the link properties are hardcoded into TrenchBroom, but in the future they will be read from the FGD file if appropriate. The following section explains how entity links are visualized in the editor.

### Entity Link Visualization {#entity-link-visualization}

Entity Links are rendered as lines in the 3D and 2D viewports. TrenchBroom provides you with four modes for entity link visualization. You can switch between these modes in the dropdown menu that is displayed when you click on the "View" button at the right of the info bar. The following table explains the four different modes.

Mode                   Description
----                   -----------
All                    Always show all entity links.
Transitive selected    Show all entity links connected to the selected entities, and any link that is reachable from the selected entities, too.
Direct selected        Show all entity links connected to the selected entities.
No                     Don't show any entity links.

An entity link that is connected to a currently selected entity is rendered as a red line that connects the selected entity with the source or target of that link. Other entity links are colored green.

![Entity Link Visualization](images/EntityLinkVisualization.png)

In the screenshot above, the link between the two info_null entities is rendered in green because neither of the entities is selected.

## Keeping an Overview {#keeping_an_overview}

If you are working on large maps, it can become cumbersome to manage the objects in the map and to keep an overview over them. Some areas may be crowded with a lot of brushes and entities so that it becomes difficult to edit a particular object that is occluded by other things. TrenchBroom provides you with a number of tools to easily keep an overview over your map and to remove clutter in crowded areas.

## Outliner {#outliner}

The Outliner page in the inspector displays layers, groups, entities, and brushes as one hierarchy. Selecting an item selects the corresponding map object, and expanding groups or entities exposes their children. It is particularly useful when geometry overlaps in the viewports or when you need to understand nested groups.

![Layers, groups, entities, and brushes in the Outliner](images/OutlinerHierarchy.png)

### Navigating and Selecting {#outliner_navigation}

Each layer is a top-level row. Groups and entities appear beneath the layer or group that contains them. A `worldspawn` branch collects the ordinary brushes in each layer, while a brush entity such as `func_door` exposes its own brushes as children. The Info column shows useful counts such as the number of objects in a layer or brushes in an entity.

Selection is synchronized in both directions: selecting a row highlights the object in the viewports, and selecting an object in a viewport reveals and selects its row. Use #key(Ctrl) or #key(Shift) for multiple rows. #key(Esc) clears the selection. #key(Del) and #key(Backspace) delete selected groups, entities, or brushes, but do not delete layer rows.

The two state columns on the right show locking and visibility. Click an icon in either column to toggle that state for the row and its contents. The active layer, open group, linked groups, locked rows, and hidden rows use distinct visual states so that editing context remains visible while navigating the tree.

The toolbar provides these controls:

- The search field filters the hierarchy after a short delay. Clear it to restore the full tree.
- **Default** sorts siblings alphabetically, **Type** groups comparable object types and then sorts by name, and **File Order** follows object order in the map file. The chosen mode is remembered.
- The plus button creates a named layer and reveals it in the tree.
- The properties button opens a resizable entity property panel below the tree. Toggle it off to devote the full inspector height to the hierarchy.

Layer visibility, locking, current-layer state, and group nesting remain visible in the scene tree while the properties below follow the same map selection. Browse entity types in the bottom Content Browser; edit existing objects in the Scene page.

### Filtering and Sorting {#outliner_filtering}

Enter one or more words to match row names without regard to case. All plain words must match. The Outliner keeps the ancestors of every result visible, so a matching object still shows which group and layer contain it.

The search field also recognizes these filters, which can be combined with each other and with plain words:

Filter                                      Result
------                                      ------
`type:group`                                Show groups. Other types are `layer`, `entity`, `brush`, `patch`, `worldspawn`, `world`, and `other`.
`type:group,entity`                         Show any of the comma-separated types.
`vis:visible` or `vis:hidden`               Show rows by visibility.
`lock:locked` or `lock:unlocked`            Show rows by lock state.
`selected`                                  Show selected rows and the ancestors needed to locate them.

For example, the following view uses `type:group` and **Type** sorting. Only the three matching groups and their layer ancestors remain visible.

![Filtering the Outliner to show groups](images/OutlinerFilter.png)

Clearing the field restores both the complete tree and the expansion state from before filtering. Filtering changes only what the tree displays; it does not hide objects in the map viewports.

### Editing Entity Properties {#outliner_entity_properties}

Click the properties button at the right of the toolbar to open the inline property editor. Select a point entity, brush entity, or `worldspawn` row to edit it without switching inspector pages. Drag the divider between the tree and editor to allocate more space to either area.

![Editing a light entity in the Outliner](images/OutlinerEntityProperties.png)

The inline editor supports the same property operations and definition-aware controls described in [Entity Properties](#entity_properties), including adding and removing keys, editing choices and spawnflags, and using smart color or resource controls where available. Changes apply to the map immediately and can be undone normally.

### Reorganizing Objects by Dragging {#outliner_dragging}

Dragging a row onto another container changes the object's parent in the map hierarchy. It does not perform a CSG merge or change the brush geometry. Each successful drop is one undoable transaction.

To move objects to another layer, select their rows and drag them onto the destination layer row. The first image shows the selected world brush in `Gameplay`; the second shows the same brush beneath the `Architecture` layer.

![A world brush before moving it to another layer](images/OutlinerMoveToLayerBefore.png)

![The world brush after moving it to the Architecture layer](images/OutlinerMoveToLayerAfter.png)

Dropping a child brush from a brush entity onto a layer row moves the entire brush entity to that layer. To detach individual brushes instead, drop their brush rows onto the destination layer's `worldspawn` row or one of its child brushes.

To add ordinary brushes to an existing brush entity, select only brush rows and drag them onto the brush entity row. The first image shows a brush beneath the Default Layer's `worldspawn`; after the drop, `func_door` contains two brushes.

![A world brush before adding it to a brush entity](images/OutlinerBrushEntityBefore.png)

![The func_door brush entity after receiving the brush](images/OutlinerBrushEntityAfter.png)

If moving brushes leaves their former brush entity empty, the empty entity is removed automatically. To move one or more brushes back out, drag their rows onto the `worldspawn` branch of the desired layer. Dragging layer rows does not reorder layers; the sort dropdown changes only how the existing hierarchy is displayed.

## Filtering {#filtering_rendering_options}

To filter out certain types of objects, you can open the view drop-down window by clicking the "View" button at the right of the info bar above the editing area.

![The info bar with view dropdown (Windows 10)](images/ViewDropdown.png)

On the left side of the view drop-down, there is a list of checkboxes that allows you to hide all entities that share the same entity definition (that is, the same classname). Uncheck an entity definition (or a group thereof) to hide the respective entities. To quickly hide and show all entities, click one of the two buttons below the list.

The right half of the view dropdown has several options, partitioned into three groups.

* **Entities** - here you can configure how entities are rendered in the editor.
* **Brushes** - allows you to toggle on or off certain special brush or face types. These types are game specific and are read from the [game configuration file](#game_configuration_files).
* **Renderer** - various options on how other objects are rendered.

Note that it is possible to add keyboard shortcuts to toggle every option in the view dropdown in the [preferences](#keyboard_shortcuts).

## Hiding and Isolation {#hiding-and-isolation}

If you are working on a crowded area, it can be useful to hide certain objects, or to hide everything but the objects of interest. To hide the selected objects, choose #menu(Menu/View/Hide), and to isolate the selected objects, choose #menu(Menu/View/Isolate). To show all hidden objects, choose #menu(Menu/View/Show All). All of these actions can be undone.

## Locking {#locking}

Locking prevents objects from being selected or edited in anyway. Locked objects are rendered with blue edges and their faces are tinted in blue, as shown in the following screenshot.

![Locked Objects](images/Locking.png)

Objects can be locked either if you are editing an open group or if you set a layer to locked (see below). You cannot lock objects individually.

## Groups {#groups}

Groups allow you to treat several objects as one and to give them a name. A group can contain the following types of objects: entities, brushes, and more groups. The fact that a group can contain groups induces a hierarchy - but in practice, you will rarely create such nested groups. In the viewports, groups have their bounding box rendered in blue, and their name is displayed above them.

To create a group, make sure that no tool is currently active and select some objects and choose #menu(Menu/Edit/Group). The editor will ask you for a name. Group names need not be unique, so you can have several groups with the same name. To select a group, you can click on any of the objects contained in it. This will not select the individual object, but the entire group, which is why you can only edit all objects within a group as one. If you want to edit individual objects in a group, you have to open the group by double clicking on it with the left mouse button. This will lock every other object in the map (locked objects are not editable and rendered in blue). Once the group is opened, you can edit the individual objects in it, or you can create new objects within the group in the usual ways. Once you are done editing the group, you can close it again by left double clicking anywhere outside of the group. Finally, you can remove a group by selecting it and choosing #menu(Menu/Edit/Ungroup). Note that removing a group does not remove the objects in the group from the map, the objects are merely ungrouped.

To add objects to an existing group, select the objects you wish to add to the group, then right click on an object already existing to that group and select "Add Objects to GROUPNAME", where GROUPNAME is the name of the group. Likewise, you can remove objects from a group by opening that group, selecting the objects you wish to remove from the group, and selecting "Remove Objects from GROUPNAME" from the [map view context menu](#map_view_context_menu). The removed objects are added to the current layer. If you remove all objects from a group, the group is deleted automatically.

## Linked Groups {#linked_groups}

Groups can also be linked together to allow a form of instancing. Linked groups contain the same objects, but can be transformed into different positions and shapes as a whole. Changing one of the linked groups will update all the other linked groups. Linked groups are useful to build reusable structures such as doorways that you want to keep in sync. The workflow for linked groups is always the same:

- Create some objects that form a reusable structure, e.g. a doorway.
- Group the objects.
- Select the group and create a linked duplicate via the context menu or by choosing #menu(Menu/Edit/Create Linked Duplicate) from the menu.
- Move the duplicate to its intended position and apply further transformations to it (e.g. rotation).
- Create more linked duplicates by duplicating a linked group in the usual way.
- At any time, open any of the linked groups and change its contents. These changes will then be replicated into the other linked groups.

You can apply various transformations to linked groups such as translation, rotation, scaling, or flipping. Groups and linked groups can be nested arbitrarily, so a linked group can contain a group, or a group can contain a linked group, and linked groups can even contain linked groups.

It is important not to think of linked groups as instancing. In TrenchBroom, there is no fixed "primary" version of the linked group that you create instances of. Indeed, linked groups are much simpler under the hood: When you change a linked group, that group will temporarily become the "primary" version and all of its contents are copied into all of its linked siblings, independent of whether or not the contained objects were changed. With this in mind, you may think of TrenchBroom performing a manual updating process automatically for you.

You can add objects to linked groups or remove objects from linked groups in the usual way, and the change is reflected in the linked groups immediately. To edit an object in a linked group, open the group as usual and perform your changes. Again, the changes are reflected in the linked groups immediately.

Consider the following example where you have two linked groups, each containing a brush and an entity.

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

`Group B` is structurally identical to `Group A`, but it's translated by 128 units on the X axis. Suppose you change `Brush A` by moving one of its vertices. Then all contents of `Group A` are copied, translated by 128 on the X axis, and added to `Group B`, replacing its existing content. Or let's say you set `Entity B`'s spawnflags to `1`, then the same process happens, but this time `Group B`s content is copied, translated by -128 on the X axis, and finally `Group A`'s contents are replaced by the copies. The result would look as follows:

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

There are some situations in which you might not want all of your changes to be reflected in all linked groups. For example, when building a door, you will usually hook the door brushes to a trigger brush using `target` and `targetname` properties. But of course, you want to use different names for different doors so that all doors don't open at once when one of them opens in the game. To allow these properties to have different values in different linked groups, you can protect entity properties against changes from their counterparts in a linked group.

### Protected Entity Properties {#protected_entity_properties}

Marking an entity property as protected blocks any changes to this property from the corresponding entity in a linked group. Furthermore, any changes to a protected entity property are not reflected in the corresponding entities in linked groups. Let's consider an example again.

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

Let's assume you want to change `Entity B`'s angle, but you don't want this change to affect `Entity A`. In this case, you can set the `angle` property of `Entity B` to protected before changing its value to `180`. The result will look as follows.

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

Note that `Entity A`'s `angle` property still has a value of 90. If you now change `Entity A`'s `angle` property, this change will not be reflected in `Entity B` either.

You can use the properties table in the Scene page to protect entity properties. When editing an entity inside of a linked group, a new column with checkboxes appears like in the following screenshot.

![Protected Entity Properties (macOS)](images/ProtectedProperties.png)

To set a property to protected, click on its checkbox. To remove the protection, click on the checkbox again. When you set a property to unprotected, its value will be reset to the value of the corresponding unprotected properties in the other entities. In our example from above, Setting `Entity B`'s `angle` property to unprotected will reset its value to `90`, which is the value from `Entity A`'s unprotected `angle` property.

To set all properties of one or multiple entities to unprotected, select the entities (or their containing groups) and choose #menu(Menu/Edit/Clear Protected Properties).

Since all changes you make to a linked group are immediately replicated into the other linked groups, newly added properties show up in the linked groups right away. If you want to add a property without replicating it, you can add it as protected by clicking on the shielded `+` icon in the toolbar below the entity property editor (see previous screenshot). Conversely, if you want to suppress a property in a linked group, that is, you don't want it to be created when adding it to another linked group, you can add it as a protected property and immediately delete it again. It will still be shown in the property editor until you remove its protected checkmark, but the name will be in italics, so it will look like a default property.

To illustrate the value of these deleted protected properties, consider the following example.

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

Suppose you want to set an angle for all of the `monster_army` entities except for `Entity A`. In this case, you would first add the `angle` property to `Entity A` as a protected property, and then delete it again from `Entity A`. Then you would add the `angle` property to `Entity B` and give it a value. This property would be replicated into `Entity C`, but not `Entity A` because there it is protected, even though the property isn't even present. In the following screenshot, the `angle` property has been set to protected and was then subsequently deleted. If you click its checkbox to remove the protection, the property will no longer show up in the entity property editor.

![Protected Deleted Entity Properties (macOS)](images/ProtectedProperties.png)

### Unlinking and Separating Linked Groups {#separating_linked_groups}

To unlink a linked group, select the group and choose #menu(Menu/Edit/Separate Linked Groups). This will turn the linked group into a regular group again. If you select multiple linked groups from a set of mutually linked groups, the selected groups will not be turned into regular groups, but rather they will become a separate set of linked groups. This separate set of linked groups is still mutually linked to each other, but they are no longer linked to the other, unselected members of the set.

Note that if you remove all members of a set of linked groups, either by separation or by deleting them, the single remaining member of the set will become a regular group.

### Extracting Objects Into New Linked Groups {#extracting_linked_groups}

When a linked group is opened, select a few (but not all) objects in that group and choose #menu(Menu/Edit/Extract Linked Groups) to extract the selected object into a new, separate linked group. The objects are removed from the currently opened linked group and added to a new linked group. The same will happen to the linked copies of these objects in the other linked groups. Consider the following example:

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

With group A open and Entity 1 and Brush 2 selected, extracting these objects will result in the following structure:

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

Thereby, groups A and B remain linked and groups X and Y are also linked.

### Visualization {#linked_group_visualization}

![Linked Groups in 3D view (macOS)](images/LinkedGroups.png)

Linked groups are rendered with a different color than regular groups. If you select a linked group, the editor will render arrows emanating from the selected group and ending in the other linked groups to indicate which groups will be updated when the selected group changes. These arrows are still shown if you open a linked group.

### Linked Groups in the Map File {#linked_groups_map_file}

Like regular groups, linked groups are stored in the map file using `func_group` entities with additional, TrenchBroom specific properties. If you edit a map file with linked groups in another editor than TrenchBroom and you change objects belonging to a linked group, then that linked group is out of sync with its linked counterparts. TrenchBroom will load such groups without issue and you can keep editing them as usual. However, if you change one of the linked groups, then this group will overwrite the contents of all other linked groups, so afterwards they will be in sync again. So if you purposefully changed one of the linked groups in an external editor, and want to replicate these changes into the linked groups, just open this specific group in the editor, make change to it, and close the group again. This will update all of the linked groups and they will be in sync again.

Consider the following linked groups:

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

In the map file, these groups would be stored as follows. Refer to the comments for information about the TrenchBroom specific properties for linked groups.

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

## Layers {#layers}

Layers divide your map into several parts. For example, you might create a layer for separate rooms or areas. Layers can contain groups, entities, or brushes, and each of these objects can belong to one layer only. Each layer has a name and can be set to hidden or locked, or omitted from exported maps. Every map contains a "Default Layer" that cannot be removed.

![Layer Editor](images/LayerEditor.png)

The layer editor in the map inspector and the [Outliner](#outliner) both display all layers in your map. The Outliner plus button is the quickest way to create and reveal a new layer without leaving the hierarchy.

- Omit a layer from export by clicking the hollow circle icon (the "X" indicates the layer is omitted from export)
- Hide or show a layer by clicking on the eye icon
- Lock or unlock a layer by clicking on the lock icon
- Create a new layer by clicking the plus button at the bottom of the layer list
- Remove one or more layers by selecting them and clicking the minus button

New objects created from scratch or pasted from the clipboard are inserted into the current layer (unless you are working in a group). Objects created from other objects (e.g. by duplicating or extrusion) are inserted into the layer of the source object.

The current layer is indicated in the layer list by a radio button and by having its name in bold, and you can set the current layer by double-clicking a layer in the layer list.

Right-click a layer in the layer editor to open its context menu:

- Make active layer
- Move selection to layer
- Select all in layer
- Hide layer
- Isolate layer
- Lock layer
- Omit from export
- Show all Layers
- Hide all Layers
- Unlock All Layers
- Lock All Layers
- Rename Layer
- Remove Layer

The [map view context menu](#map_view_context_menu) also has some layer related shortcuts.

## Entity Templates {#entity_templates}

When building complex level mechanics with recurring custom entities (such as functional doors, triggers, buttons, or monster configurations), you can capture any existing entity as a template and apply it across multiple brushes in a single step:

1. **Set as Entity Template**: Right-click an entity in the viewport (or any brush belonging to that entity) and select **Set CLASSNAME as Entity Template**. TrenchBroom stores the entity classname and all of its properties as the active template.
2. **Apply Entity Template**: Select one or more geometry brushes, right-click, and select **Apply Entity Template (CLASSNAME)**.

TrenchBroom will automatically instantiate a separate entity clone from the template for each selected brush, reparent each brush into its new entity, and keep all brushes selected. The entire multi-entity creation and reparenting operation is executed in a single atomic transaction and can be undone or redone with a single #key(Ctrl)+#key(Z).

## 3D Skybox Rendering {#3d_sky_rendering}

For GoldSrc and supported game configurations, setting the `skyname` property on `worldspawn` enables real-time 3D skybox rendering in the 3D viewport. The 6-sided environment cubemap is rendered at infinite depth, allowing you to preview the environment backdrop, horizon line, and atmospheric context directly while sculpting map geometry.
