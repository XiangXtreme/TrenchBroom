# Assets and Prefabs {#assets_and_prefabs}

## Unified Asset Browser {#asset_browser}

The **Assets** tab in the bottom info panel provides a content browser. In a standalone map, it shows files from enabled game mods and the user prefab directory. For GoldSrc, these include the `models`, `sprites`, `sound`, and `prefabs` roots. In a `.tbproj` project, it shows project resources and the attached asset library; game-directory files are not listed. Import a game file into the project, or capture it from the asset library, before using it as a project resource.

Use the folder tree, the breadcrumb row, or the back and forward arrows to navigate; double-click the breadcrumb row to type a folder path directly. The search field states its scope: **This folder** searches inside the current folder, while **All assets** searches the whole listing. **Filters** holds the source, type, library, sound, tag, and map filters and only shows the ones that can apply to the current folder; **View** holds the thumbnail size and WAD grouping. The reload button refreshes the available sources. In standalone maps, enabled game and mod paths are rescanned, and changes in watched asset directories are picked up automatically.

Model cells render their model preview, sprite cells render the first supported sprite frame, and selected sound cells expose a play/stop control when the platform can decode the file. A placeholder and the asset tooltip still identify files whose preview cannot be rendered.

Drag a model or sprite into a viewport to create an entity using that asset. Drag a sound to assign or create the appropriate sound reference where the current game and editor context support it. Drag a prefab to place its stored map objects.

![Asset Browser](images/AssetBrowser.png)

### Asset Library Folders {#asset_library_folders}

Open **Asset Library…** from the welcome window, or right-click a library asset in a project and choose **Show in Asset Library**. Select a type directory such as Model or Texture, then click **+** beside Folders or choose **New folder…** from the folder context menu. Subfolders can contain more subfolders, and empty folders remain available after reopening the library.

Use the folder context menu to rename a subfolder or delete an empty folder and its empty descendants. Select one or more assets of the same type, then right-click and choose **Move to folder…**. Assets stay under their type directory. Importing uses the selected folder when its type matches the imported file; other file types use their own type roots. Newly created terrain materials use the selected Material folder.

These folders organize the catalog. Moving assets or renaming folders preserves asset IDs, content versions, and existing project references.

### Reusable Prefabs {#reusable_prefabs}

Select map objects and click **Save selection as prefab** in the Assets toolbar. A standalone map creates a `.tbprefab` file and thumbnail in the directory configured under **Preferences > Misc > Tools**; a project stores the prefab in the project. Prefabs appear under the `prefabs` root and can be dragged into the map; right-click a prefab to rename or delete it.

Prefab files preserve the selected map text and relevant material collection paths. They are intended for reusable map fragments. Editing a placed copy does not modify the source prefab or other placed copies.
