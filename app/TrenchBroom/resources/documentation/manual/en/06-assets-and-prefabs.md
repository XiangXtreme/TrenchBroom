# Assets and Prefabs {#assets_and_prefabs}

## Unified Asset Browser {#asset_browser}

The **Assets** tab in the bottom info panel provides one browser for game assets and reusable prefabs. For enabled GoldSrc mods it presents the `models`, `sprites`, and `sound` roots together with a user `prefabs` root. The browser recognizes `.mdl`, `.spr`, `.wav`, and `.tbprefab` files.

Use the folder tree, the breadcrumb row, or the back and forward arrows to navigate; double-click the breadcrumb row to type a folder path directly. The search field states its scope: **This folder** searches inside the current folder, while **All assets** searches the whole listing. **Filters** holds the source, type, library, sound, tag, and map filters and only shows the ones that can apply to the current folder; **View** holds the thumbnail size and WAD grouping. The reload button rescans enabled game and mod paths, and changes in watched asset directories are picked up automatically.

Model cells render their model preview, sprite cells render the first supported sprite frame, and selected sound cells expose a play/stop control when the platform can decode the file. A placeholder and the asset tooltip still identify files whose preview cannot be rendered.

Drag a model or sprite into a viewport to create an entity using that asset. Drag a sound to assign or create the appropriate sound reference where the current game and editor context support it. Drag a prefab to place its stored map objects.

![Asset Browser](images/AssetBrowser.png)

### Reusable Prefabs {#reusable_prefabs}

Select map objects and click **Save selection as prefab** in the Assets toolbar to create a `.tbprefab` file and thumbnail. The prefab directory is configured under **Preferences > Misc > Tools**. Prefabs appear under the `prefabs` root and can be dragged into the map; right-click a prefab to rename or delete it.

Prefab files preserve the selected map text and relevant material collection paths. They are intended for reusable map fragments. Editing a placed copy does not modify the source prefab or other placed copies.
