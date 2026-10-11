# Preferences and Compilation {#preferences_and_compilation}

## Game Configuration {#game_configuration}

![Game Configuration Dialog (macOS)](images/GamePreferences.png)

The game configuration preference pane is where you set up the paths to the games that TrenchBroom supports. For each game, you can set the game path by clicking on the "..." button and selecting the folder in which the game is stored on your hard drive. Alternatively, you can enter a path manually in the text box, but you have to hit #key(Return) to apply the change.

Additionally, you can configure the game engines for the selected game by clicking on the 'Configure engines...' button.

Clicking the folder icon below the game list opens the folder that contains custom game configurations in a file browser.

![Game Engine Configuration Dialog (macOS)](images/GameEngineDialog.png)

In this dialog, you can add a game engine profile by clicking on the '+' button below the profile list on the left, and you can delete the selected profile by clicking on the '-' button. To the right of the list, you can edit the details of the selected game engine profile, specifically its name and path. Similar to the game path, if you edit the engine path manually, you have to apply the changes by pressing #key(Return) while in the path text box. Click [here](#launching_game_engines) to find out how to launch game engines from within TrenchBroom.

For some game configurations (such as for Quake, shown above) you can also optionally enter paths for a set of compilation tools. If it's not clear what you should be specifying a path to here, then hovering over the path entry box may give you a tooltip with additional info about that compilation tool.

If you do enter a path here, then the name shown to the left of the path can be used as a variable in your [compilation profiles](#compiling_maps) for this game. Wherever that variable occurs, the path specified here will be used. For example if your path to the `qbsp` tool is `C:\mapping\ericw-tools-v0.18.1-win64\bin\qbsp.exe`, and you set that path here... then in your compilation profiles you can enter `${qbsp}` wherever you need to refer to that whole qbsp.exe path.

The benefits of specifying your tool paths here (if the game configuration allows) are:

- It will be easier to create, edit, and share your compilation profiles.
- If your tool paths need to be changed, you only have to change them here.

So in the example above, if you wanted to try a later version of ericw-tools that are located in a different folder like `C:\mapping\ericw-tools-v0.19-win64\bin`, then you would only need to change the paths in this dialog. You wouldn't need to edit all of your compilation profiles.

You can also add [custom game configurations](#game_configuration_files) to suit a particular setup (such as an engine supporting formats that TrenchBroom supports, but does not expect with that game).

## View Layout and Rendering {#view_layout_and_rendering}

![View Preferences](images/ViewPreferences.png)

In this preference pane, you can choose the layout of the editing area. There are four layouts available:

Layout      Description
------      -----------
One Pane    One cycleable 3D / XY / XZ / YZ viewport
Two Panes   One 3D and one cycleable XY / XZ / YZ viewport
Three Panes One 3D, one XY viewport, and one cycleable XZ / YZ 2D viewport
Four Panes  One 3D, one XY viewport, one XZ viewport, and one YZ viewport

Cycleable 2D viewports can be cycled by pressing #action(Controls/Map view/Cycle map view).

The remaining sections control the user interface, map view rendering, the material browser, and fonts.

Setting                       Description
-------                       -----------
Theme                         Select System, Light, Dark, Blender, or an installed user theme. A restart is required.
Brightness                    Brightness of materials and model skins in the 3D viewport.
Grid                          Opacity of grid lines in the 3D and 2D viewports.
FOV                           Field of view of the 3D camera.
Show axes                     Show coordinate axes in the 3D and 2D viewports.
Filter mode                   Texture filtering mode in the editing views.
Enable multisampling          Antialias viewport rendering.
Material Browser Icon Size    Thumbnail scale from 100% through 500%.
Renderer Font Size            Text size for labels rendered inside map views.
Python Console font and size  Monospace family and point size used for console input and output.

### Themes {#themes}

The built-in theme IDs are `builtin.system`, `builtin.light`, `builtin.dark`, and `builtin.blender`. System derives its colors from the operating-system palette. Light and Dark provide stable TrenchBroom palettes, while Blender uses a compact dark palette based on Blender 5.2. Theme changes take effect after restarting TrenchBroom.

Third-party themes are distributable UTF-8 JSON files with the `.tbtheme` extension. Put them in the `themes` directory inside the TrenchBroom user data directory, restart, and select the new name under **Preferences > View > Theme**:

- Windows: `%APPDATA%\TrenchBroom\themes`
- macOS: `~/Library/Application Support/TrenchBroom/themes`
- Linux and FreeBSD: `~/.TrenchBroom/themes`
- Portable mode: `<TrenchBroom directory>/config/themes`

A theme may inherit an installed theme and override only selected color tokens:

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

Theme IDs use lowercase letters, digits, dots, dashes, and underscores; `builtin.` is reserved. Colors use `#RRGGBB`. Theme files cannot inject stylesheets, change control geometry, or run code. Invalid files are skipped and reported in the TrenchBroom log.

## Color Preferences {#color_preferences}

![Color Preferences](images/ColorPreferences.png)

The **Colors** page controls editor-specific colors such as the grid, selections, handles, overlays, and browser states. These colors are separate from the widget theme: use **View > Theme** for the application chrome and **Colors** for map and tool visualization. Color swatches show the pending color and open a color chooser when clicked.

## Mouse Input {#mouse_input}

![Mouse Configuration Dialog](images/MousePreferences.png)

The mouse input preference pane allows you to change how TrenchBroom interprets mouse movements.

Setting     Description
-------     -----------
Mouse Look  Sensitivity and axis inversion for mouse look and orbiting (right click and drag)
Mouse Pan   Sensitivity and axis inversion for mouse panning (middle click and drag)
Mouse Move  Sensitivity and settings for moving the camera with the mouse. If you use a tablet, the setting "Alt+MMB drag to move camera" might make navigation easier for you.
Fly Mode    A slider to control the speed in fly mode. The keyboard shortcuts can be adjusted in the Keyboard Preferences.

## Keyboard Shortcuts {#keyboard_shortcuts}

![Keyboard Configuration Dialog](images/KeyboardPreferences.png)

In this preference pane, you can change the keyboard shortcuts used in TrenchBroom. The table lists all available shortcuts, their context, and the description. To change a keyboard shortcut, click twice (do not double click) on the shortcut in the first column of the table and enter the new shortcut. The context determines when this shortcut is available, for example, the PgDn key triggers different actions depending on whether the rotate tool is active or not. Finally, the description column explains what a shortcut does in a particular context. Sometimes a shortcut triggers different actions depending on whether the viewport in which it was used is a 3D or a 2D viewport. For example, the PgDn key can move objects backward (away from the camera) in a 2D viewport or down along the Z axis in the 3D viewport. These different actions are listed together in the description column, but they are separated with a semicolon.

If you open the preference dialog when a map is currently opened, the list of shortcuts will contain additional entries depending on the loaded entity configuration file and the game configuration file. For each entity and special brush or face types, the following keyboard shortcuts are available.

* **Entity**
  - `View Filter > Toggle CLASSNAME visible` to toggle entities with this classname visible and invisible ([more info](#filtering_rendering_options))
  - `Create CLASSNAME` to create entities with this classname ([more info](#creating_entities))
* **Brush / Face Type**
  - `View Filter > Toggle TYPE visible` to toggle brushes or faces with this type visible and invisible ([more info](#filtering_rendering_options))
  - `Turn selection into TYPE` to set this type to the selected brushes or faces
  - `Turn selection into non-TYPE` to unset this type from the selected brushes or faces

Note that if you assign a keyboard shortcut to different actions in the same context, the shortcut creates a conflict and you cannot exit the preference pane or close the dialog until you resolve the conflict. Conflicting shortcuts are highlighted in red.

## Miscellaneous and Extensions {#misc_preferences}

The **Misc** page contains language, editor behavior, tool integration, and MCP settings. Display-language changes require an application restart. Editor options include copying a `worldspawn` header and enabling 2D box selection with #key(Ctrl)+drag.

The **Tools** section configures the prefab directory and opens the [Pie Menu](#pie_menu) and Python Plugin Manager dialogs.

## Automatic Updates {#automatic_updates}

TrenchBroom can check for updates. If an update is available, it can be downloaded and installed from within TrenchBroom. If "Check for updates on startup" is enabled in the preferences, TrenchBroom will perform an update check when it starts.

TrenchBroom will notify you of a new update in the following places:

- The welcome window
- The "About TrenchBroom" dialog
- The update preferences
- The status bar

In each of these places, the status of the updater will be shown as text. If a user action is available, a clickable link will appear. For example, if an update is available, a link labeled "Update available" appears. Clicking the link will bring up a dialog where the update can be downloaded and installed.

![Update Indicator (macOS)](images/UpdateIndicator.png)

In the above screenshot, the updater hasn't performed an update check yet, so the link is labeled "Check for updates". Clicking this link will start an update check.

![Update Preferences](images/UpdatePreferences.png)

The updater can be configured in the Preferences. The following settings are available:

- Check for updates on startup: If this is checked, TrenchBroom will check for updates automatically when it starts.
- Include pre-releases: If this is checked, TrenchBroom will include pre-releases in the update check. Pre-releases are versions of TrenchBroom that are not yet considered stable.
They may contain new features or bug fixes that are not yet part of a stable release.

Note that TrenchBroom doesn't send any private information about you or your computer when it performs an update check. We don't collect any data about you. To perform the update check, TrenchBroom sends one request to GitHub via HTTPS, and to download an update, it sends another HTTPS request to wherever the update file is hosted (currently, these files are all hosted on GitHub, too).

## Command Repetition {#command-repetition}

Editing brushwork often consists of repeating the same steps over and over. As an example, consider building a spiral staircase. You start by cutting out a brush that represents one step of the staircase. Then you duplicate that brush, move it upward, and rotate it about the center axis of the staircase. You repeat these actions for every step of the stair set. TrenchBroom has a feature called *command repetition* that is designed to automate part of this process for you.

Repeating commands is similar to having an automatic macro recorder. Remember that TrenchBroom already records everything you do to provide [undo and redo](#undo_redo). In addition to undoing your actions, TrenchBroom also uses the recorded information to allow you to repeat some of your most recently performed actions. In the example of the stairwell, you would want to repeat the duplication, translation, and rotation actions over and over with a single keystroke. The only problem is that you need to determine which of the most recently performed actions should be repeated. This is done in two ways. First, TrenchBroom automatically forgets all repeatable actions when the selection changes. So if you select some objects and choose #menu(Menu/Edit/Repeat) directly after selecting them, nothing will happen because all repeatable actions have been discarded. Second, you can tell TrenchBroom to discard all repeatable actions by choosing #menu(Menu/Edit/Clear Repeatable Commands). Think of this as telling TrenchBroom to start a new macro.

So in the case of the spiral stairwell, you would first create the brush that represents one stair. Since you don't want to repeat whatever actions you performed to create this brush, you'll have tell TrenchBroom to discard all repeatable commands, either by deselecting and then reselecting the brush, or by choosing the appropriate command from the menu. After that, you duplicate the brush, move it upwards, and rotate it. Then, you can repeat these steps by choosing #menu(Menu/Edit/Repeat) as often as you want.

In summary, you can think of command repetition as a very simple macro system that allows you to have one macro that consists only of the most recently performed actions. Even though it is quite limited, it can make your life a lot easier if you get used to it.

## Issue Browser {#issue_browser}

The issue browser is located at the bottom of the window. It contains a live list of issues that TrenchBroom has detected in your map. The list is live in the sense that the editor updates it automatically whenever the map changes. Be aware that TrenchBroom cannot detect all issues that may lead to compilation errors or warnings, or strange behavior in game. But it can detect some of these issues, and keeping the map free of such issues can protect you from having to spend a lot of time fixing bugs later on when your map becomes more complex. To see which types of issues TrenchBroom can detect and fix for you, click on the "Filter" button at the top right of the issue browser. This opens a dropdown list where you can toggle which types of issues TrenchBroom should check in your map. By default, all issues are enabled.

![Issue Browser with Filter Dropdown](images/IssueBrowserFilter.png)

Every entry in the issue list provides you with two pieces of information: the line number, if applicable, where the problematic object is located in the current map file, and a description. If you wish to find an object that caused an issue, you can select that issue in the browser to have the object(s) selected in the editor. Next you can choose #menu(Menu/View/Camera/Focus on Selection) to make them visible in the 3D and 2D viewports.

![Issue Browser with Context Menu](images/IssueBrowserContextMenu.png)

In addition to making you aware of issues, TrenchBroom can also fix them for you. To fix an issue, right click it and choose the appropriate fix from the "Fix" context menu. If you wish to ignore a particular issue, you can also tell TrenchBroom to hide it by choosing "Hide" in the context menu. If you wish to see all hidden issues, you can check the respective checkbox above the issue list. To make a hidden issue visible again, first show all hidden issues, then right click the issue and choose "Show" from the context menu.

## Compiling Maps {#compiling_maps}

TrenchBroom supports compiling your maps from inside the editor. This means that you can create compilation profiles and configure those profiles to run external compilation tools for you. Note however that TrenchBroom does not come with prepackaged compilation tools - you'll have to download and install those yourself. The following screenshot shows the compilation dialog that comes up when choosing #menu(Menu/Run/Compile...).

![Compilation Dialog (macOS)](images/CompilationDialog.png)

This dialog allows you to create compilation profiles, which are listed on the left of the dialog. Each compilation profile has a name, a working directory, and a list of tasks. Click the '+' button below the profile list to create a new compilation profile, or click the '-' button to delete the selected profile. To duplicate a profile, right click on it and select "Duplicate" from the menu. If you select a profile, you can edit its name, working directory, and tasks on the right side of the dialog.

Name
:    The name of this compilation profile. Need not be unique and can even be empty.

Working directory
:   A working directory for the compilation profile. This is optional, but very useful because it can be referred to as a variable when specifying the parameters of each task (see below). Variables are allowed (see below). Furthermore, relative paths will be interpreted as relative to this directory.

Tasks
:   A list of tasks which are executed sequentially when the compilation profile is run.

The checkbox on each task lets you selectively exclude a task from running when you run the compilation profile.

There are the following types of tasks, each with different parameters:

### Export Map {#export-map}

Exports the map to a file. This file should be different from the actual file where the map is stored.

Layers marked "Omit From Export" will not be present in the exported map.

#### Parameters {#parameters}

Target
:    The path of the exported file. Variables are allowed. Relative paths are implicitly relative to the working directory.

Strip Entities
:    Set a GLOB pattern to strip any entities with a matching classname. Example: 'info_player_*' to strip all info_player_start and all info_player_deatchmatch entities.

Add Entity
:    Set the classname of an entity that will be added to the exported map. Example: 'info_player_start'. This entity will have its 'origin' property set to the position of the 3D camera, and its 'angle' property set to the yaw angle of the camera. Useful when testing maps.

Strip TB specific entity properties
:    Strip any entity properties starting with _tb_ from the exported map file. Some compilers cannot handle these properties.

### Run Tool {#run-tool}

Runs an external tool and captures its output. Note that for the Tool parameter's value, you can use a compilation tool variable defined in the [game configuration](#game_configuration), as discussed below.

#### Parameters {#parameters-1}

Tool
:    The absolute path to the executable of the tool that should be run. The working directory is set to the profile's working directory if configured. Variables are allowed.

Parameters
:    The parameters that should be passed to the tool when it is executed. Variables are allowed.

Stop on nonzero error code
:    Stop the compilation process if this tool returns an error.

### Launch Engine {#launch-engine}

Starts one of the current game's configured Launch Engine profiles.

This is useful as the final task in a compile profile, after exporting the map, running compile tools, and copying the output files into the game directory.

#### Parameters {#parameters-2}

Engine Profile
:    The Launch Engine profile to start.

Stop on launch failure
:    Stop the compilation process if the engine cannot be launched. If this is unchecked, the failure is reported and the remaining tasks continue.

### Copy Files {#copy-files}

Copies one or more files. Relative paths are implicitly relative to the working directory.

#### Parameters {#parameters-3}

Source
:    The file(s) to copy. To specify more than one file, you can use wildcards (*,?) in the filename. Variables are allowed.

Target
:    The directory to copy the files to. The directory is recursively created if it does not exist. Existing files are overwritten without prompt. Variables are allowed.

### Rename File {#rename-file}

Renames or moves one file. Relative paths are implicitly relative to the working directory.

#### Parameters {#parameters-4}

Source
:    The file to rename or move. Wildcards are not supported. Variables are allowed.

Target
:    The new path for the file. The path must end in a filename. The containing directory is recursively created if it does not exist. Existing files are overwritten without prompt. Variables are allowed.

### Delete Files {#delete-files}

Deletes one or more files. Relative paths are implicitly relative to the working directory.

#### Parameters {#parameters-5}

Target
:    The file(s) to delete. To specify more than one file, you can use wildcards (*,?) in the filename. Variables are allowed.

### Using Expressions {#using-expressions}

You can use [expressions](#expression_language) when specifying the working directory of a profile and also for the task parameters. The following table lists the available variables, their scopes, and their meaning. A scope of 'Tool' indicates that the variable is available when specifying tool parameters. A scope of 'Workdir' indicates that the variable is only available when specifying the working directory. Note that TrenchBroom helps you to enter variables by popping up an autocompletion list.

Variable         Scope             Description
--------         -----             -----------
`WORK_DIR_PATH`  Tool              The full path to the working directory.
`MAP_DIR_PATH`   Tool, Workdir     The full path to the directory where the currently edited map is stored.
`MAP_BASE_NAME`  Tool, Workdir     The base name (without extension) of the currently edited map.
`MAP_FULL_NAME`  Tool, Workdir     The full name (with extension) of the currently edited map.
`GAME_DIR_PATH`  Tool, Workdir     The full path to the current game as specified in the game preferences.
`MODS`           Tool, Workdir     An array containing all enabled mods for the current map.
`APP_DIR_PATH`   Tool, Workdir     The full path to the directory containing the TrenchBroom application binary.
`CPU_COUNT`      Tool              The number of CPUs in the current machine.

If the [game configuration](#game_configuration) for the current game includes compilation tools, then the names of those tools are also available as variables in the Tool scope. The following screenshot is a section of a compilation profile showing the use of such variables.

![Compilation Dialog Section, with Tool Variables (Linux)](images/CompilationDialogToolVars.png)

It is recommended to use the following general process for compiling maps and to adapt it to your specified needs:

1. Set the working directory to `${MAP_DIR_PATH}`.
2. Add an *Export Map* task and set its target to `${MAP_BASE_NAME}-compile.map`.
3. Add *Run Tool* tasks for the compilation tools that you wish to run. Use the expressions `${MAP_BASE_NAME}-compile.map` and `${MAP_BASE_NAME}.bsp` to specify the input and output files for the tools. Since you have set a working directory, you don't need to specify absolute paths here.
4. Finally, add a *Copy Files* task and set its source to `${MAP_BASE_NAME}.bsp` and its target to `${GAME_DIR_PATH}/${MODS[-1]}/maps`. This copies the file to the maps directory within the last enabled mod.
5. Optionally add a *Launch Engine* task at the end to start the game with the latest compiled files.

The last step will copy the bsp file to the appropriate directory within the game path. You can add more *Copy Files* tasks if the compilation produces more than just a bsp file (e.g. lightmap files). Alternatively, you can use a wildcard expression such as `${MAP_BASE_NAME}.*` to copy related files. If you add a *Launch Engine* task, place it after any file copying tasks so the game starts with the latest compiled output.

To run a compilation profile, click the 'Compile' button in the compilation dialog. Once the compilation profile is running, you can click the 'Stop' button to terminate the currently running tool. A running compilation will also be terminated if you close the compilation dialog or if you close the main window, but TrenchBroom will ask you before this happens. Note that the compilation tools are run in the background. You can keep working on your map if you wish.

If you want to test your compilation profile without actually running it, click the 'Test' button. A test run will only print what each task will do without actually executing it.

Once the compilation is done, you can launch a game engine and check out your map in the game. The following section explains how you can configure game engines and launch them from within the editor.

## Launching Game Engines {#launching_game_engines}

Before you can launch a game engine in TrenchBroom, you have to make your engine(s) known to TrenchBroom. You can do this by bringing up the game engine profile dialog either from the launch dialog (see below) or from the [game configuration](#game_configuration).

You can launch a game engine manually by clicking the 'Launch' button in the compilation dialog or choosing #menu(Menu/Run/Launch...). This brings up the launch dialog shown in the following screenshot.

![Launch Dialog (Linux)](images/LaunchGameEngineDialog.png)

In this dialog, you can select the game engine of your choice, edit its parameters, and launch the engine. To select an engine, click on it in the list on the right-hand side of the dialog. If you wish to edit the list of engines, you can bring up the game engine profile dialog by clicking on the 'Configure engines...' button. You can then edit its parameters in the text box at the bottom of the left-hand side of the dialog. Note that you can use the following variables in this text box:

Variable         Description
--------         -----------
`MAP_BASE_NAME`  The base name (without extension) of the currently edited map.
`GAME_DIR_PATH`  The full path to the current game as specified in the game preferences.
`MODS`           An array containing all enabled mods for the current map.

The `MODS` variable is useful to pass a parameter to the engine to choose a mod. Usually, this will be the last mod in the mods for the current map. Since the `MODS` variable is an array that contains all mods for the map, its individual entries are accessed using the subscript operator (see below). To access the last entry in the array, you can use the expression `$MODS[-1]`.

Note that the parameters are stored with the game engine profile.

## Solving Problems {#solving-problems}

This section contains some information about what you can do if you run into problems when using TrenchBroom.

### Automatic Backups {#automatic-backups}

TrenchBroom automatically creates backups of your work. As a prerequisite, you have to work on a saved file, that is, a file that exists somewhere on your computer. So when you create a new file, you should save it as soon as you decide that you want to keep it. At that point, TrenchBroom will create its automatic backups. These backups are stored in a folder called "autosave" within the folder where your map file is located. It will create a new backup every ten minutes after the last backup, unless the map file has not been changed since then. To prevent the autosaving from interrupting your workflow, TrenchBroom will only create an autosave you are not interacting with it, however. In total, TrenchBroom will create up to 50 backups. After that, it will delete the oldest backup when it creates a new one so that the total number of backups does not exceed 50. The backups have the same name as the map file you are editing, but with the backup number added to the name just before the extension.

You can use these backups to go back to previous versions of your map if problems arise. This may help you when you are fixing bugs or if your map file gets corrupted somehow.

## Display Models for Entities {#display-models-for-entities}

TrenchBroom can show models for point entities in the 3D and 2D viewports. For this to work, the display models have to be set up in the [entity definition](#entity_definitions) file, and the game path has to be set up correctly in the [game configuration](#game_configuration). For most of the included entity definition files, the models have already been set up for you, but if you wish to create an entity definition file for a mod that works well in TrenchBroom, you have to add these model definitions yourself. You will learn how to do this for FGD and DEF files in this section.

### General Model Syntax {#general-model-syntax}

The syntax for adding display models is identical in all entity definition files, only the place where the model definitions have to be inserted into the entity definitions varies. We will first explain the general syntax here. Every model definition in a DEF or an FGD takes the following form:

    model(...)

In ENT files, the model definitions are given as XML attribute values of the `<point />` element, e.g.

    <point model="..." />

Thereby, the ellipsis contains the actual information about the model to display. You can use TrenchBroom's [expression language](#expression_language) to define the actual models. Each entity definition should contain only one model definition, and the expression in the model definition should evaluate either to a value of type string or to a value of type map. If the expression evaluates to a map, it must have the following structure:

    {
      "path" : MODEL,
      "skin" : SKIN,
      "frame": FRAME,
        "scale": SCALE_EXPRESSION
    }

The placeholders `MODEL`, `SKIN`, `FRAME` and `SCALE_EXPRESSION` have the following meaning

Placeholder         Description
-----------         -----------
`MODEL`             The path to the model file relative to the game path, with an optional colon at the beginning. Mandatory.
`SKIN`              The 0-based index of the skin to display. Optional, defaults to 0.
`FRAME`             The 0-based index of the frame to display. Optional, defaults to 0.
`SCALE_EXPRESSION`  An expression that is evaluated against an entities' properties to determine the model scale.

If the expression evaluates to a value of type string, then that is interpreted as a map containing only a `path` key with the string as its value. In other words, if the expression evaluates to a string, then that value is interpreted as the path to a model. Think of such expressions as shorthands that allow you to define a simple model like so:

    model("path/to/model")

instead of having to write

    model({ "path": "path/to/model" })

If the model expression has a scale expression, then its result is used as the scale value for the model. If the expression cannot be evaluated, or if no such expression is given, then the default scale expression from the game configuration is evaluated instead. Refer to [this section](#game_configuration_files_entities) for more information about `SCALE_EXPRESSION` and the default scale expression.

#### Basic Examples {#basic-examples}

So a valid model definitions might look like this:

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

Sometimes, the actual model that is displayed in game depends on the value of an entity property. TrenchBroom allows you to mimic this behavior by using conditional expressions using the switch and case operators and by referring to the entity properties as variables in the expressions. Let's look at an example where we combine several model definitions using a literal value.

    model({{
      dangle == "1" -> { "path": "progs/voreling.mdl", "skin": 0, "frame": 13 },
                      { "path": "progs/voreling.mdl" }
    }})

The voreling has two states, either as a normal monster, standing on the ground, or hanging from the ceiling. The model expression contains a switch expression (note the double braces) that comprises of a case expression (note the arrow operator) and a literal map expression. You can interpret this expression as follows:

    dangle == "1"                                             // If the value of property 'dangle' equals "1"
    ->                                                        // then
    { "path": "progs/voreling.mdl", "skin": 0, "frame": 13 }  // use this as the model.
    ,                                                         // Otherwise,
    { "path": "progs/voreling.mdl" }                          // use this as the model.

If you have problems understanding this syntax, you should read the section on TrenchBroom's [expression language](#expression_language).

The following example shows a combination of model definitions using flag values.

    model({{
      spawnflags == 2 -> "maps/b_bh100.bsp",
      spawnflags == 1 -> "maps/b_bh10.bsp",
                         "maps/b_bh25.bsp"
    }})

As you can see, there are three models attached to the Health kit, `maps/b_bh25.bsp, maps/b_bh10.bsp` and `maps/b_bh100.bsp`. This is because the Health kit uses three different models depending on what spawnflags are checked. If `ROTTEN` is checked, it uses `maps/b_bh10.bsp`, which is the dim (rotten) health kit and if `MEGAHEALTH` is checked, then it uses maps/b_bh100.bsp which is the megahealth powerup. If neither are checked, it uses the standard health kit.

Accordingly, the nested case expressions inspect the value of the `spawnflags` property to determine the correct model. Since there is no need to specify a skin or a frame for these models, the expressions only return strings as a shorthand.

In the previous example, note that if both `ROTTEN` and `MEGAHEALTH` were checked, it would display the megahealth model. Remember that the switch operator returns the value of the first expression that does not evaluate to undefined. For this reason, you must put model definitions with no condition as the last one in the switch because that will override everything else!

#### Advanced Examples {#advanced-examples}

The basic expressions you have seen so far allow you to customize which model, skin and frame TrenchBroom shows depending on the values of an entity's properties with great flexibility, but the actual paths, skin indices and frame indices are hardcoded in the entity definition file. However, sometimes even this flexibility is not enough, in particular with entities that allow you to place arbitrary models into the map. In such cases, the entity definition file cannot contain the actual model paths and so on. Rather, the model path, skin index and frame index are specified by the mapper using entity properties. Since TrenchBroom provides the values of the entity properties to the model expressions as variables, you can easily cover such cases as well.

Remember the structure of the model definition maps:

    {
      "path" : MODEL,
      "skin" : SKIN,
      "frame": FRAME
    }

So far, we have used hardcoded literals for the values of the map entries like so:

    model({ "path" : "progs/armor", "skin" : 1, "frame": 3 })

However, nothing prevents us from using variables instead of hardcoded literals, thereby referring to the entities properties.

    model({
      "path" : PATHKEY,
      "skin" : SKINKEY,
      "frame": FRAMEKEY
    })

The placeholders `PATHKEY`, `SKINKEY` and `FRAMEKEY` have the following meaning

Placeholder  Description
-----------  -----------
`PATHKEY`    The name of the entity property key in which the model path is stored.
`SKINKEY`    The name of the entity property key in which the model skin index is stored. Optional.
`FRAMEKEY`   The name of the entity property key in which the model frame index is stored. Optional.

A valid dynamic model definition might look like this:

    model({
      "path" : mdl,
      "skin" : skin,
      "frame": frame
    })

Then, if you create an entity with the appropriate classname and specify three properties as follows

    {
      "classname" "mydynamicmodelentity"
      "mdl" "progs/armor.mdl"
      "skin" "2"
      "frame" "1"
    }

TrenchBroom will display the second frame of the `progs/armor.mdl` model using its third skin. If you change these values, the model will be updated in the 3D and 2D viewports accordingly.

#### Differences Between DEF, FGD and ENT Files {#differences-between-def-fgd-and-ent-files}

In both files, the model definitions are just specified alongside with other entity property definitions (note the semicolon after the model definition -- this is only necessary in DEF files). An example from a DEF file might look as follows.

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

An example from an FGD file might look as follows.

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

To improve compatibility to other editors, the model definition can also be named _studio_ or _studioprop_ in FGD files.

In an ENT file, the same model specification might look like this.

    <point name="ammo_bfg" color=".3 .3 1"
           box="-16 -16 -16 16 16 16"
           model="{{ perch == '1' -> 'progs/gaunt.mdl', { 'path': 'progs/gaunt.mdl', 'skin': 0, 'frame': 24 } }}"
    />

## Point Files and Portal Files {#point-files-and-portal-files}

TrenchBroom can load point files (PTS) generated by QBSP, which help locate leaks. After you open a point file with #menu(Menu/File/Load Point File...), it's rendered as a sequence of green line segments which will connect the map interior to the void. Hit #menu(Menu/View/Camera/Move to Next Point) to move the camera to the first point, and continue hitting #menu(Menu/View/Camera/Move to Next Point) to fly along the path, which should show you where the leak is.

Portal files (PRT), also generated by QBSP, let you visualize the portals between BSP leafs. They can be loaded with #menu(Menu/File/Load Portal File...) and are rendered as translucent red polygons.

## Update Preferences {#update_preferences}

The **Update** page controls automatic update checks and whether pre-release builds are included. It also shows the current update status and provides the same check or download action shown in the welcome and About windows. See [Automatic Updates](#automatic_updates) for details.
