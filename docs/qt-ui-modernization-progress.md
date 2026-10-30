# Qt UI Modernization Progress

## Workspace architecture follow-up (2026-10-30)

The active workspace and panel architecture design is
[工作空间与面板体系设计](workspace-ui-architecture.md). It defines panel ownership,
command targets, focus behavior, layout persistence, and staged migration from the current
project drawer. The historical visual modernization phases below remain a record of the
theme and component work; they do not imply that the workspace architecture is complete.

## Status

- Branch: `codex/qt-ui-modernization`
- Baseline commit: `4d3a1dca7` (`Restore shortcut preference semantics`)
- Last updated: 2026-08-17
- Current stage: Phases 4.4 and 4.5 in progress; Material Browser density work continues,
  while foundational component governance, canonical first-pass controls, and component
  snapshot acceptance are complete

## Objective

Modernize TrenchBroom's desktop UI with a compact, consistent workbench design while
preserving the existing C++ editor core, Qt application lifecycle, input handling, and
OpenGL viewports.

The target is a TrenchBroom-specific design influenced by the information density and
visual hierarchy of VS Code. It is not a pixel-for-pixel VS Code clone and does not add
a runtime or source dependency on Code - OSS.

## Decisions So Far

1. Keep Qt Widgets for the first modernization effort.
2. Preserve `QOpenGLWidget` map and UV views instead of moving rendering to a web view.
3. Do not add Rust or Electron solely to improve appearance.
4. Use VS Code's public theme token taxonomy and default theme JSON files as design
   references only.
5. Establish TrenchBroom-owned theme tokens as the single source of truth for
   `QPalette`, `QProxyStyle`, QSS, and editor overlay colors.
6. Defer custom native title bars and general-purpose drag-and-drop docking until the
   lower-risk visual work has been evaluated.
7. Follow Code - OSS Modern UI's 4 px card gaps, 6 px compact-control radius, and 8 px
   workbench-surface radius without copying its DOM or adding a source dependency.
8. Use a narrow Inspector navigation rail with repository-owned icons, persistent page
   headings, tooltips, accessible names, and keyboard activation. This replaces the
   earlier horizontal segmented navigation after the supporting surfaces established a
   stable visual hierarchy.

## Repository Findings

- `app/TrenchBroom/resources/stylesheets/base.qss` is currently small and handles only
  a limited set of widget-specific adjustments.
- `app/TrenchBroom/src/Main.cpp` applies the dark theme through the Qt Fusion style, a
  custom `QPalette`, and a narrow `QProxyStyle` override.
- `MapWindow` uses horizontal and vertical splitters to arrange the map view, bottom
  information panel, and right inspector.
- The application already has reusable UI building blocks including `TabBar`,
  `TitleBar`, `Splitter`, `ContainerBar`, and `CommandPaletteDialog`.
- The main rendering and editing path is already separated from most widget styling,
  so a theme and workbench-shell pass can avoid changes to map geometry and rendering.
- The inspector and outliner contain substantial custom behavior and painting. They
  should be modernized after global metrics and state colors are stable.

## Proposed Theme Tokens

The initial token set should remain deliberately small:

- window, editor, sidebar, panel, input, and elevated backgrounds
- primary, secondary, disabled, and inverse text
- subtle, normal, strong, and focus borders
- hover, pressed, selected, inactive-selected, and disabled states
- accent, error, warning, success, and information colors
- compact control, toolbar, tab, tree-row, and status-bar heights
- narrow, normal, and wide spacing values
- small and normal icon sizes

Qt style sheets do not support reusable variables. Theme values should therefore live
in a typed C++ structure or a validated data file, with QSS placeholders expanded from
that source rather than duplicating literal colors throughout the application.

## Work Plan

### Phase 0: Discovery

Status: complete

- [x] Inspect the application and library boundaries.
- [x] Measure the current Qt UI and theme surface.
- [x] Identify the map-window layout and existing reusable UI controls.
- [x] Compare Qt modernization with Electron, Rust, and Code - OSS reuse.
- [x] Confirm that VS Code source is useful as a reference but unnecessary as a
      dependency.

### Phase 1: Theme Foundation

Status: complete

- [x] Add a TrenchBroom theme token model.
- [x] Generate `QPalette` and QSS values from the same token source.
- [x] Extend `QProxyStyle` for shared control metrics and focus behavior.
- [x] Standardize hover, pressed, checked, selected, disabled, and focus-visible states.
- [x] Standardize 16 px and 20 px icon metrics.
- [x] Add deterministic visual checks for light, dark, and Blender themes at 100%,
      150%, and 200% scale factors.

Expected benefit: high. This should remove most of the platform-default Qt appearance
without changing window structure.

### Phase 2: Main Workbench Shell

Status: complete

- [x] Reduce toolbar height and visual noise.
- [x] Move persistent grid and editing state into compact status-bar segments where
      appropriate.
- [x] Restyle map-view tabs and contextual tool controls.
- [x] Give the editor, inspector, and bottom panel consistent card borders, gaps, and
      rounded corners.
- [x] Evaluate a narrow activity bar and retain labeled segmented inspector navigation
      for this phase.

Expected benefit: very high. This phase creates most of the recognizable compact
workbench layout while retaining the current splitter ownership model.

### Phase 3: Inspector And Outliner

Status: complete

- [x] Standardize tree row height, indentation, rounded selection, hover, and inactive
      states.
- [x] Reserve stable 28 px columns for visibility and lock while retaining node type
      and group indicators in the name hierarchy.
- [x] Add a consistent inset search, sort, and properties action row.
- [x] Align property names, values, and actions into a predictable responsive layout.
- [x] Introduce reusable collapsible section headers.
- [x] Verify multi-selection, Escape handling, and lock/visibility hit targets.
- [x] Verify keyboard row navigation and cross-layer drag-and-drop through the production
      transaction and tree-refresh path.
- [x] Preserve the existing explicit layer/group rename actions; inline tree editing is
      intentionally not introduced because it is not part of the current interaction model.
- [x] Evaluate warning indicators and defer them until the outliner has a stable warning
      data source.

Expected benefit: very high. Difficulty is medium to high because the outliner has
significant custom interaction and painting code.

### Phase 4: Supporting Surfaces

Status: complete

- [x] Restyle Console, Issues, Python panel tabs, and the Assets browser surface.
- [x] Expand the status bar with concise grid and snap state; defer selection, mode, and
      warning segments until they have stable application-state sources.
- [x] Restyle the existing command palette as a focused workbench overlay.
- [x] Apply the shared design system to preferences and high-traffic dialogs.

Expected benefit: medium. These changes complete consistency after the main editing
surface has established the visual language.

### Phase 4.1: Browser Content And Face Inspector Refinement

Status: complete

- [x] Draw shared rounded cell surfaces with palette-derived default, hover, and selected
      states in the OpenGL-backed Entity, Material, and Assets browsers.
- [x] Remove nested square Assets backgrounds and apply theme-aware sprite, sound, prefab,
      audio-control, and error presentation.
- [x] Separate Face tools, attributes, UV controls, and browser controls into stable styled
      sections without introducing nested cards.
- [x] Verify the affected supporting surfaces in Light and Dark themes at 100%, 150%, and
      200% scale factors.

Expected benefit: medium to high. The most visible custom-rendered content now follows the
same rounded component language as the surrounding Qt workbench instead of only sharing its
colors.

### Phase 4.2: Inspector Navigation Rail

Status: complete

- [x] Replace the visible horizontal Inspector tabs with a compact 44 px vertical icon
      rail while retaining `TabBook` page ownership and state persistence.
- [x] Add palette-driven hover, focus, checked, and accent-indicator states.
- [x] Keep page identity explicit through synchronized headings, tooltips, accessible
      names, and the existing keyboard shortcuts.
- [x] Verify Map, Entity, Face, Outliner, and Plugin navigation without changing editor,
      plugin, or document lifecycles.
- [x] Verify representative Inspector pages in Light and Dark themes at 100%, 150%, and
      200% scale factors.

Expected benefit: high. The Inspector now has a recognizable workbench navigation layer
without taking on the platform risk of a custom title bar or general docking system.

### Phase 4.3: Browser Controls And Empty States

Status: complete

- [x] Move Entity and Material browser controls above their content and split them into a
      full-width search row plus a stable sort/filter row for narrow Inspector widths.
- [x] Replace generic filter push buttons with compact rounded toggle controls and shared
      palette-driven hover, focus, pressed, and checked states.
- [x] Add centered theme-aware empty and no-match messages to OpenGL cell browsers while
      keeping filtering, selection, drag-and-drop, and resource loading behavior unchanged.
- [x] Add deterministic Entity and Material empty-result snapshot targets and verify normal
      and empty browser states in Light and Dark themes at 100%, 150%, and 200% scale.

Expected benefit: medium to high. Search and filter actions now read as a deliberate browser
toolbar, stay usable in the constrained Inspector, and provide clear feedback instead of a
blank content region when filters have no results.

### Phase 4.4: Material Browser Density And Selection Context

Status: in progress

The resource-backed acceptance capture shows that the current browser is functional, but its
content layout still reflects the legacy texture-at-native-aspect approach. Tall textures set
the row height while shorter textures leave uneven dark areas, short labels receive as much
space as long paths, and changing thumbnail size still requires leaving the browser for View
preferences.

- [ ] Put every material in a stable preview frame and aspect-fit the texture within it so
      mixed texture dimensions produce aligned rows without distorting the image.
- [x] Distribute residual browser width across complete material columns around the selected
      thumbnail size, and keep the scrollbar thumb visible before hover.
- [x] Replace passive collection labels with compact disclosure headers showing material counts,
      full-row pointer states, persistent collapse state, sticky expanded headers, and keyboard
      navigation/activation.
- [x] Support Ctrl+wheel thumbnail resizing through the existing persisted
      `MaterialBrowserIconSize` preference, with discrete size steps, high-resolution wheel
      accumulation, and unchanged ordinary scrolling.
- [ ] Elide long labels to one predictable line while retaining the full material name and
      dimensions in the existing tooltip.
- [x] Add an inline compact thumbnail-size control backed by the existing
      `MaterialBrowserIconSize` preference; reuse the established Assets browser control
      pattern instead of creating a second size setting.
- [ ] Add a quiet selected-material information row for the full name, dimensions, collection,
      and usage count, without introducing a permanent large preview pane.
- [ ] Extend deterministic acceptance with a dense mixed-aspect fixture state covering a
      selected material, used/default borders, long names, grouping, and scrolling.
- [ ] Validate click selection, face application, reveal, filtering, grouping, context actions,
      and the Replace Material dialog before closing the phase.

Expected benefit: high for repeated material-selection work. Difficulty is medium because the
OpenGL cell layout and hit testing must change together, but this remains much lower risk than
new docking or a separate full-size material browser window.

### Phase 4.5: Foundational Component Governance

Status: complete

TrenchBroom now follows a central component contract inspired by Blender's separation between
feature layout and shared widget rendering. Feature screens declare content, behavior, layout,
and one of a small set of semantic roles; `ThemeTokens`, `ApplicationStyle`, and `base.qss` own
the visual implementation.

- [x] Add `docs/ui-component-style-governance.md` and make it required guidance for UI work.
- [x] Add a deterministic `components` snapshot covering input, choice, numeric, multiline,
      checkbox, radio, range, push-button, tool-button, group, collection, header, tab, menu,
      scroll-bar, and splitter states.
- [x] Add canonical multiline input, radio, slider, and tool-button styling to the global
      component layer.
- [x] Make radio indicators use the same theme-aware application style path as checkboxes.
- [x] Replace Command Palette and status-bar object-name styling with explicit semantic roles.
- [x] Remove first-pass browser, Outliner, Smart Face, MCP error, and plugin-output style
      duplication while retaining behavioral object names.
- [x] Consolidate group surfaces and remove foundational button styling based on object names,
      retaining `compact` and `outlined` semantic roles for real density and boundary needs.
- [x] Consolidate item views, headers, tabs, menus, scroll bars, and splitters around canonical
      states and move feature-owned width constraints back into their layouts.
- [x] Audit custom delegates and paint paths. Command Palette delegates reuse the canonical
      item background and only lay out semantic text; entity-property and shortcut delegates
      only provide editors. Outliner full-row state and skybox preview painting remain documented
      compound/data-renderer exceptions.

Expected benefit: high. New and plugin-created standard Qt controls inherit a coherent default,
and intentional density differences remain reviewable instead of becoming page-specific visual
forks.

### Phase 5: Optional Structural Work

Status: deferred

- [ ] Evaluate a custom native title bar.
- [ ] Evaluate document tabs across maps.
- [ ] Evaluate general-purpose movable and dockable panels.

These items have high platform and interaction risk. They should proceed only if user
testing shows that the earlier phases do not meet the product goal.

## Initial Acceptance Criteria

- Existing map editing, input, focus, shortcuts, drag-and-drop, and OpenGL rendering
  behavior remains unchanged.
- Light and dark themes have complete active, inactive, disabled, hover, pressed,
  selected, and focus states.
- Text and icons remain readable at 100%, 150%, and 200% display scaling.
- The main editor dedicates more vertical space to map views than the current toolbar
  and contextual-control arrangement.
- Repeated controls use consistent heights, padding, border treatment, and icon size.
- The UI remains usable on Windows, macOS, and Linux without platform-specific layout
  breakage.

## Validation Strategy

- Build the focused `TbUiLibTest` target before running UI tests.
- Run relevant focused Catch2 filters for every changed component.
- Build the Release `TrenchBroom` target before completing a workbench phase.
- Run `powershell -ExecutionPolicy Bypass -File scripts\ui-theme-acceptance.ps1` to
  capture the isolated welcome window, representative map workbench, and deterministic
  foundational component contract, expanded Outliner/entity-properties, Entity Browser,
  Face Inspector, supporting Assets panel, Python Console, plugin inspector, Command Palette,
  Preferences pages for View, Colors, Mouse, Keyboard, and Misc/MCP, plus Entity/Material empty
  result states in Light, Dark, and Blender themes at 100%, 150%, and 200% scale. The 153-state
  matrix validates image dimensions, device-pixel ratio,
  nonblank pixels, font support, and file integrity, then writes PNG and JSON evidence plus
  a labeled visual-comparison contact sheet under
  `build-release-codex\codex-logs\ui-theme-acceptance`.
- Pass `-BaselineDir <approved-run-directory>` to turn the matrix into a visual regression
  gate. It compares downsampled pixels with configurable per-pixel and changed-area
  tolerances and fails when an expected baseline is missing or exceeded. Baselines must come
  from the same OS, Qt version, installed fonts, and renderer; a run without this option is a
  capture-integrity smoke test, not a visual regression test.
- Face Inspector and Material empty-result captures use the checked-in Quake 2 map/game
  fixtures rather than the generic empty map. Snapshot mode keeps external background services
  disabled but continues GL resource processing, waits up to five seconds for every fixture
  texture to reach GPU-ready state, and fails on missing, failed, or pending resources. Failures
  write a sibling `.error.txt` file which the acceptance script includes in its exception.
- Use `--ui-snapshot <path> --ui-snapshot-theme system|light|dark|blender
  [--ui-snapshot-page map|components|outliner|entity-browser|entity-browser-empty|face-inspector|material-browser-empty|plugin-inspector|supporting|python-console|command-palette|preferences|preferences-colors|preferences-mouse|preferences-keyboard|preferences-misc]
  [--ui-snapshot-game-path game-directory] [map-file]` for a focused
  single capture. Omitting the map captures Welcome unless `components` or a `preferences*` page is
  selected; supplying one captures the workbench. The Outliner
  page expands its hierarchy, properties panel, and active selection before rendering;
  the Entity Browser and Face Inspector pages widen the inspector for browser-color
  review; the Supporting page opens Assets and expands the bottom panel; the Command
  Palette uses a constrained 640 x 480 layout; the Preferences pages select View,
  Colors, Mouse, or Keyboard in the category sidebar. Snapshot mode uses temporary
  preferences, disables background services, and renders through the native Qt platform
  without showing the window. Material targets require loaded, GPU-ready textures before
  capture.
- Verify keyboard-only navigation, focus visibility, disabled states, and text fit.
- Keep generated screenshots and temporary visual reports out of commits unless they
  are intentionally selected as maintained references.

## External References

- Code - OSS Modern UI floating panels (pinned reference):
  <https://github.com/microsoft/vscode/blob/6c27443ce6fdf6ac798c64025d45175e2e23c4b4/src/vs/workbench/browser/media/floatingPanels.css>
- Code - OSS corner-radius sizes (pinned reference):
  <https://github.com/microsoft/vscode/blob/6c27443ce6fdf6ac798c64025d45175e2e23c4b4/src/vs/platform/theme/common/sizes/baseSizes.ts>
- Code - OSS sidebar section headers (pinned reference):
  <https://github.com/microsoft/vscode/blob/6c27443ce6fdf6ac798c64025d45175e2e23c4b4/src/vs/workbench/browser/parts/sidebar/media/sidebarpart.css>
- VS Code theme color reference:
  <https://code.visualstudio.com/api/references/theme-color>
- VS Code Dark Modern theme:
  <https://github.com/microsoft/vscode/blob/main/extensions/theme-defaults/themes/dark_modern.json>
- VS Code 2026 Dark theme:
  <https://github.com/microsoft/vscode/blob/main/extensions/theme-defaults/themes/2026-dark.json>
- Blender 5.2 default UI theme (pinned reference):
  <https://projects.blender.org/blender/blender/src/commit/fbe6228777e7d9afefcd61a413844e790ae75db7/release/datafiles/userdef/userdef_default_theme.c>
- Qt style sheets:
  <https://doc.qt.io/qt-6/stylesheet.html>
- Qt `QProxyStyle`:
  <https://doc.qt.io/qt-6/qproxystyle.html>

## Progress Log

| Date | State | Change |
| --- | --- | --- |
| 2026-08-15 | Complete | Reviewed the current Qt, document, tool, and OpenGL boundaries. |
| 2026-08-15 | Complete | Chose incremental Qt modernization over an appearance-driven Electron/Rust rewrite. |
| 2026-08-15 | Complete | Defined the phased roadmap, initial token scope, and acceptance criteria. |
| 2026-08-15 | Complete | Added the theme-token foundation, palette generation, QSS expansion, and compact shared control styling. |
| 2026-08-15 | Verified | Passed the Release `TbUiLibTest "Theme"` test, built `TrenchBroom`, and completed a warning-free current-theme startup and screenshot check. |
| 2026-08-15 | Complete | Added first-class light theme tokens and deterministic in-process UI snapshot capture with PNG, JSON, and a labeled comparison contact sheet. |
| 2026-08-15 | Verified | Passed `UiSnapshot` and `Theme` tests, built Release `TrenchBroom`, and passed the hidden Light/Dark acceptance matrix at 100%, 150%, and 200% scale with readable native fonts. |
| 2026-08-15 | Complete | Pinned Code - OSS reference commit `6c27443c` and adapted its 4/6/8 px spacing and corner-radius hierarchy to Qt. |
| 2026-08-15 | Complete | Added rounded editor, inspector, and bottom-panel surfaces; compact segmented tabs; an inspector header; and a quieter toolbar with Grid moved to the status bar. |
| 2026-08-15 | Complete | Extended deterministic acceptance to capture a real map workbench as well as Welcome without showing native windows. |
| 2026-08-15 | Verified | Passed `MapWindow`, `Theme`, and `UiSnapshot` tests, built Release `TrenchBroom`, and passed the 12-state Welcome/Workbench Light/Dark DPI matrix. |
| 2026-08-15 | Complete | Added an inset Outliner toolbar, icon-only state columns, compact hierarchy metrics, rounded full-row interaction states, and a quieter responsive entity-property layout. |
| 2026-08-15 | Complete | Added deterministic Outliner snapshot setup with expanded hierarchy, visible properties, and active selection. |
| 2026-08-15 | Verified | Passed `OutlinerTreeWidget`, `OutlinerEntityPropertyEditor`, `MapWindow`, `Theme`, and `UiSnapshot` tests, built Release `TrenchBroom`, and passed the 18-state Welcome/Workbench/Outliner Light/Dark DPI matrix. |
| 2026-08-15 | Complete | Replaced collapsible section show/hide text with compact chevron headers, full-row mouse activation, keyboard activation, hover, and focus states while preserving switchable panel text states. |
| 2026-08-15 | Complete | Extracted the Outliner drop execution path for deterministic acceptance and covered keyboard row navigation, deliberate non-inline rename behavior, cross-layer reparenting, selection retention, and local tree refresh. |
| 2026-08-15 | Verified | Passed `CollapsibleTitledPanel` (31 assertions), `OutlinerTreeWidget` (86), `OutlinerEntityPropertyEditor` (107), `MapWindow` (70), `Theme` (19), and `UiSnapshot` (18), built Release `TrenchBroom`, and passed the 18-state matrix at `20260815-221023-403`. |
| 2026-08-15 | Complete | Modernized Console, Python Console, Issues, Assets, and shared bottom-panel tabs; added concise Grid/Snap status actions and a compact command-palette presentation. |
| 2026-08-15 | Complete | Added keyboard activation for shared tab buttons, typed info-panel page selection, and a deterministic Supporting snapshot state. |
| 2026-08-15 | Verified | Passed `TabBook` (13 assertions), `MapWindow` (88), `Theme` (19), `UiSnapshot` (18), `ModelBrowserView` (30), and `GoldSrcSpritePreview` (13), built Release `TrenchBroom`, and passed the 24-state matrix at `20260815-222920-657`. |
| 2026-08-15 | Complete | Replaced the Preferences top toolbar with a compact keyboard-navigable category sidebar, stable page header, editor surface, and themed action footer. |
| 2026-08-15 | Complete | Applied the shared themed action footer to Game, Compile, Replace, Export, Launch, Crash, and related high-traffic dialogs. |
| 2026-08-15 | Complete | Added a deterministic Preferences snapshot page and expanded the default visual acceptance matrix from 24 to 30 states. |
| 2026-08-15 | Verified | Passed `PreferenceDialog,CompilationDialog` (42 assertions), built Release `TrenchBroom`, and passed the 30-state matrix at `20260815-224706-711`. |
| 2026-08-15 | Fixed | Diagnosed the Preferences-close crash as stale MSVC object code caused by localized `/showIncludes` output; forced English compiler diagnostics in the checked-in Release build wrappers so Ninja records header dependencies. |
| 2026-08-15 | Verified | Completed a clean Release `TrenchBroom` rebuild, confirmed `AppController.cpp.obj` has 454 Ninja dependency entries including `PreferenceDialog.h`, and passed the production-entry `PreferenceDialog` open/close regression test (27 assertions) from a freshly built `TbUiLibTest`. |
| 2026-08-15 | Fixed | Added the missing explicit Light theme preference and restored visible color swatches using decoration data that cannot be covered by table QSS. |
| 2026-08-15 | Complete | Added the Preferences Colors page to deterministic UI acceptance, expanding the default matrix from 30 to 36 states. |
| 2026-08-15 | Verified | Passed `PreferenceDialog`, `PreferenceDialog.preferencePanes`, `Theme`, and `UiSnapshot` (88 assertions), built Release `TrenchBroom`, and visually confirmed Light/Dark Preferences Colors snapshots at `20260815-233423-160`. |
| 2026-08-16 | Fixed | Replaced Command Palette's space-separated item text with a fixed-height two-line delegate that reserves an independent right-aligned shortcut column and elides each field within its own bounds. |
| 2026-08-16 | Fixed | Made the default Entity, Material, Asset, and UV browser background, group, and text colors follow the active Light/Dark palette while preserving non-default user color preferences. |
| 2026-08-16 | Complete | Added Entity Browser, Face Inspector, and constrained Command Palette snapshot targets, expanding the default visual acceptance matrix from 36 to 54 states. |
| 2026-08-16 | Verified | Passed `Theme` and `MapWindow` (119 assertions) plus `ModelBrowserView` and `GoldSrcSpritePreview` (43 assertions), built Release `TrenchBroom`, and visually confirmed all affected Light/Dark pages at 100%, 150%, and 200% in `20260815-235531-161`, `20260815-235755-213`, and `20260815-235941-408`. |
| 2026-08-16 | Complete | Added shared rounded browser cells with centralized hover/selection state, removed nested Assets tiles, and made custom placeholder, audio, and error rendering theme-aware. |
| 2026-08-16 | Complete | Refined Face/UV and Entity/Material browser section hierarchy with stable QSS object names and secondary field labels. |
| 2026-08-16 | Verified | Passed `Theme`, `ModelBrowserView`, and `MapWindow` (162 assertions), built Release `TrenchBroom`, and visually confirmed the 18 affected Light/Dark states at 100%, 150%, and 200% in `20260816-002459-821` and `20260816-002756-942`. |
| 2026-08-16 | Complete | Replaced the Inspector's visible horizontal tabs with a 44 px vertical icon rail and synchronized page headings while retaining `TabBook` state, shortcuts, plugin ownership, accessibility, and keyboard activation. |
| 2026-08-16 | Verified | Passed `Theme`, `MapWindow`, and `TabBook` (175 assertions), built Release `TrenchBroom`, and visually confirmed 24 representative Inspector states at 100%, 150%, and 200% in `20260816-004245-712` and `20260816-004359-063`. |
| 2026-08-16 | Complete | Moved Entity and Material search/filter controls into compact top-mounted two-row toolbars with dedicated rounded toggle states. |
| 2026-08-16 | Complete | Added shared theme-aware OpenGL browser empty messages and deterministic Entity/Material empty-result snapshot targets, expanding the default matrix from 54 to 66 states. |
| 2026-08-16 | Verified | Passed `Theme`, `MapWindow`, and `ModelBrowserView` (238 assertions), built Release `TrenchBroom`, and visually confirmed 24 normal and empty browser states at 100%, 150%, and 200% in `20260816-010203-138` and `20260816-010310-148`. |
| 2026-08-16 | Complete | Hardened Material Browser snapshots with a real Quake 2 fixture, isolated game-path configuration, active GL resource processing, GPU-ready texture gates, bounded polling, and persisted failure diagnostics. |
| 2026-08-16 | Verified | Passed `Theme`, `MapWindow`, `ModelBrowserView`, and `UiSnapshot` (256 assertions), built Release `TrenchBroom`, visually checked and passed the complete 66-state Light/Dark matrix at 100%, 150%, and 200% in `20260816-015438-123`, and confirmed the invalid-game-path guard reports a deterministic resource timeout. |
| 2026-08-16 | Complete | Replaced the full-width accent status bar with a neutral theme surface and top divider, reserving blue for the compact active Snap state and interactive focus feedback. |
| 2026-08-16 | Verified | Passed `Theme` and `MapWindow` (208 assertions), built Release `TrenchBroom`, and visually checked 12 Workbench/Face Inspector Light/Dark states at 100%, 150%, and 200% in `20260816-021724-731`. |
| 2026-08-16 | Complete | Made Entity Browser columns responsive around the existing preferred tile size, distributing residual width across complete rows while retaining a practical minimum tile width. |
| 2026-08-16 | Verified | Passed `Theme` and `MapWindow` (208 assertions), built Release `TrenchBroom`, and visually checked normal and empty Entity Browser states in Light/Dark at 100%, 150%, and 200% in `20260816-022503-965`. |
| 2026-08-16 | Complete | Aligned global combo boxes and checkboxes with Code - OSS compact controls using theme-selected chevrons, 4 px select corners, reserved arrow space, rounded popup rows, and vector-drawn 18 px checked/mixed indicators with complete interaction states. |
| 2026-08-16 | Verified | Passed `Theme` (31 assertions) and `PreferenceDialog,MapWindow` (207 assertions), built Release `TrenchBroom`, and visually checked Preferences and Entity Browser controls in Light/Dark at 100%, 150%, and 200% in `20260816-100250-965`, plus both controls in the System theme at 100%. |
| 2026-08-16 | Complete | Reduced `Main.cpp` to an application composition root by extracting proxy-style, palette, and QSS installation into `ApplicationStyle`, and snapshot target construction, resource readiness, capture scheduling, and dialog lifetime management into `UiSnapshotRunner`. Added repository guidance that keeps specialized UI and test-mode implementation out of the entry point. |
| 2026-08-16 | Verified | Passed `Theme,UiSnapshot,PreferenceDialog,MapWindow` (256 assertions), rebuilt Release `TrenchBroom`, visually checked the complete 66-state Light/Dark snapshot matrix at 100%, 150%, and 200% in `20260816-102151-481`, and rechecked the corrected explicit Outliner target in `20260816-102935-344`. |
| 2026-08-16 | Fixed | Centered the custom 18 px checkbox indicator with floating-point `QRectF` geometry so even-sized indicator rectangles no longer place the left and top half-pixel of their border outside Qt's paint clip. Added the Mouse preferences snapshot target to the default acceptance matrix, expanding it from 66 to 72 states. |
| 2026-08-16 | Verified | Passed `Theme,UiSnapshot,PreferenceDialog,MapWindow` (256 assertions), rebuilt Release `TrenchBroom`, visually checked the focused Mouse preferences matrix in `20260816-105354-479`, and passed the complete 72-state Light/Dark matrix at 100%, 150%, and 200% in `20260816-104915-316`. |
| 2026-08-16 | Fixed | Removed the blank leading column from the Keyboard and Colors preference tables by explicitly hiding their unused vertical headers. Added the Keyboard preferences target to the default acceptance matrix, expanding it from 72 to 78 states. |
| 2026-08-16 | Verified | Passed `PreferenceDialog` (27 assertions) and `PreferenceDialog.preferencePanes` (26 assertions), rebuilt Release `TrenchBroom`, and visually checked Keyboard and Colors in Light/Dark at 100%, 150%, and 200% in `20260816-110526-478` and `20260816-110546-071`. |
| 2026-08-16 | Complete | Aligned global integer and floating-point spin boxes with the compact control language: stable 19 px stepper columns, 4 px outer corners, subtle dividers, flat hover/pressed feedback, and theme-specific chevrons with distinct disabled states. |
| 2026-08-16 | Verified | Passed `Theme,MapWindow` (211 assertions), rebuilt Release `TrenchBroom`, and visually checked enabled and disabled spin boxes in the Face Inspector Light/Dark snapshots at 100%, 150%, and 200% in `20260816-113138-605`. |
| 2026-08-16 | Complete | Made Material Browser columns responsive around the selected thumbnail size so complete rows consume the viewport without a residual right strip. Increased resting scrollbar contrast while retaining distinct hover and pressed feedback. |
| 2026-08-16 | Complete | Hardened the Face Inspector snapshot to use 3x material thumbnails, directly covering the constrained two-column layout and persistent scrollbar instead of relying on the default small-tile state. |
| 2026-08-16 | Verified | Passed `Theme,MapWindow` (211 assertions), rebuilt Release `TrenchBroom`, and visually checked the responsive two-column grid and resting scrollbar in Light/Dark at 100%, 150%, and 200% in `20260816-121008-032`. |
| 2026-08-16 | Complete | Turned Material Browser collection labels into 24 px disclosure headers with chevrons, right-aligned material counts, full-row hover/pressed/focus feedback, compact collapsed geometry, persistent collapse state, keyboard operation, and automatic expansion when revealing a material. |
| 2026-08-16 | Verified | Passed `CellLayout` (13 assertions) and `CellView,Theme,MapWindow,ModelBrowserView` (251 assertions), rebuilt Release `TrenchBroom`, and visually checked grouped 3x thumbnails plus visible disclosure indicators in Light/Dark at 100%, 150%, and 200% in `20260816-124357-565`. |
| 2026-08-16 | Fixed | Increased unchecked checkbox edge contrast without changing global control borders: enabled borders now blend against each interaction-state background, hover and pressed states receive stronger emphasis, disabled borders remain distinguishable, and focus retains the theme accent. |
| 2026-08-16 | Verified | Passed Release `TbUiLibTest "Theme"` (31 assertions), rebuilt Release `TrenchBroom`, and visually compared the focused Mouse preferences Light/Dark snapshots at 100% in `20260816-133344-413` against the previous acceptance capture. |
| 2026-08-16 | Complete | Added Ctrl+wheel Material Browser thumbnail resizing across the 100%-500% preference steps, including high-resolution wheel accumulation, bounds handling, persisted preference updates, and unchanged unmodified scrolling. |
| 2026-08-16 | Verified | Passed the new Release `MaterialBrowserView` interaction test (7 assertions), passed `CellView,Theme,MapWindow,ModelBrowserView` (251 assertions), and rebuilt Release `TrenchBroom`. |
| 2026-08-16 | Complete | Added a compact Material Browser percentage indicator and selector to the filter row. It shares the 100%-500% preference steps, stays synchronized with Ctrl+wheel and View preferences, and keeps sort/filter controls stable at constrained Inspector widths. |
| 2026-08-16 | Verified | Passed Release `MaterialBrowserView,MapWindow` (194 assertions), rebuilt Release `TrenchBroom`, and visually checked the inline percentage indicator without clipping or toolbar overlap in Face Inspector Light/Dark snapshots at 100%, 150%, and 200% in `20260816-140448-183`. |
| 2026-08-16 | Complete | Changed the shared Material Browser thumbnail range to nine 50% steps from 100% through 500%, including the inline selector, View preferences, Ctrl+wheel bounds, legacy below-range clamping, and the maximum-size acceptance state. |
| 2026-08-16 | Verified | Passed Release `MaterialBrowserView,PreferenceDialog,MapWindow` (223 assertions), rebuilt Release `TrenchBroom`, and visually checked the `500%` Face Inspector state plus View preferences in Light/Dark at 100%, 150%, and 200% in `20260816-141642-333`. |
| 2026-08-16 | Fixed | Removed duplicate browser resize/reload work, made legacy MCP token cleanup non-blocking, consolidated Outliner row background painting in its delegate, and added asynchronous snapshot log capture plus optional pixel-tolerant baseline comparison. |
| 2026-08-16 | Verified | Passed Release `CellView`, `MaterialBrowserView`, `OutlinerTreeWidget`, and `McpBridgeConfig` (158 assertions), rebuilt Release `TrenchBroom`, confirmed identical Welcome captures report zero changed pixels, rejected a deliberately wrong baseline at 91.477%, and kept the selected Outliner row within the approved 0.2% visual tolerance. |
| 2026-08-16 | Complete | Replaced the standalone MCP preference page with a compact `McpSettingsWidget` embedded in Misc, reduced navigation to seven pages, removed compatibility pipe/config fields, and replaced the clipped command field with dedicated URL and Claude Code copy actions. Added an isolated config-path seam so preference reset tests never rewrite the user's MCP config. |
| 2026-08-16 | Verified | Passed Release `MiscPreferencePane`, `PreferenceDialog`, and `PreferenceDialog.preferencePanes` (100 assertions) plus `McpBridgeConfig` (53 assertions), rebuilt Release `TrenchBroom`, and passed zero-difference Light/Dark baseline comparisons for the new `preferences-misc` snapshot target in `20260816-170134-884`. |
| 2026-08-16 | Fixed | Prevented `Qt::Popup` panels from closing and immediately reopening when their trigger button is clicked a second time. `PopupWindow` now consumes the trigger-button mouse press before Qt can replay it, while preserving normal outside-click dismissal and subsequent reopening. |
| 2026-08-16 | Verified | Added the focused `PopupButton` regression test (5 assertions), passed `ViewEditor` (10 assertions), and rebuilt Release `TrenchBroom`. |
| 2026-08-16 | Fixed | Routed the expanded Misc/MCP preferences through the shared scrollable preference content so a dialog resized on another page cannot compress and overlap the MCP form rows. |
| 2026-08-16 | Verified | Added a compact-height Misc regression covering scrollbar activation and MCP row separation, passed `MiscPreferencePane` (42 assertions), `PreferenceDialog` (25 assertions), and `PreferenceDialog.preferencePanes` (46 assertions), rebuilt Release `TrenchBroom`, and passed the `920x560` Light/Dark snapshot matrix at 100%, 150%, and 200% in `20260816-192300-167`. |
| 2026-08-16 | Complete | Extended the `SETTINGS` region-anchor language through a shared semantic style used by Preferences and Inspector, and strengthened the existing bottom-panel tabs as uppercase `CONSOLE`, `PYTHON CONSOLE`, `ISSUES`, and `ASSETS` anchors without adding a duplicate title row. |
| 2026-08-16 | Verified | Passed Release `PreferenceDialog,MapWindow` (220 assertions) and `Theme` (31 assertions), rebuilt `TrenchBroom`, and visually checked Supporting, Preferences, and Outliner in Light/Dark at 100%, 150%, and 200% in `20260816-193347-889`. |
| 2026-08-16 | Complete | Added a persistent fourth `Blender` theme derived from Blender 5.2's pinned default UI palette, including layered editor/sidebar/panel backgrounds, native control and selection colors, Preferences integration, dark platform semantics, and `--ui-snapshot-theme blender` support. Expanded the default visual matrix from 84 to 126 states. |
| 2026-08-16 | Verified | Passed Release `Theme,PreferenceDialog,MapWindow` (262 assertions), rebuilt `TrenchBroom`, visually checked the complete 42-state Blender page/DPI matrix in `20260816-200002-492`, and rechecked Light/Dark Workbench at 100%, 150%, and 200% in `20260816-200147-001`. |
| 2026-08-16 | Complete | Replaced hardcoded theme branches with a validated `ThemeRegistry` and distributable single-file `.tbtheme` format. Built-ins now use the same JSON schema as third-party themes; user themes support stable IDs, inheritance, strict color-token overrides, deterministic duplicate handling, diagnostics, Preferences discovery, legacy preference migration, and snapshot CLI IDs without allowing external QSS. |
| 2026-08-16 | Verified | Passed Release `Theme,UiSnapshot,PreferenceDialog*,MapWindow` (385 assertions), rebuilt `TrenchBroom`, visually checked the Light/Dark/Blender Preferences acceptance capture at `20260816-202748-339`, and installed `example.midnight` into an isolated portable user directory where its custom ID, inherited palette, dynamic Preferences entry, and 960x640 snapshot all completed successfully. |
| 2026-08-16 | Next | Continue Phase 4.4 with stable mixed-aspect preview frames and predictable single-line labels before considering any deferred Phase 5 structural experiment. |
| 2026-08-17 | Complete | Added a compact Outliner toolbar action for creating layers, backed by a shared model operation so Outliner and the legacy Layers panel use identical ordering, transaction, undo, and active-layer behavior. New layers clear active filtering and are revealed immediately. |
| 2026-08-17 | Verified | Passed Release `Map_Layers` (191 assertions) and `OutlinerTreeWidget` (91 assertions), rebuilt `TrenchBroom`, and visually checked the focused Outliner matrix in Light, Dark, and Blender themes at 100%, 150%, and 200% in `20260817-133138-953`. |
| 2026-08-17 | Complete | Normalized the Outliner sources to the repository format, removed unused declarations and stale implementation comments, and deleted the unreferenced first-pass `OutlinerModel` plus its isolated test and build entries. The live UI remains owned by `OutlinerTreeWidget`, `OutlinerEntityPropertyEditor`, and `OutlinerInspector`. |
| 2026-08-17 | Verified | Passed Release `TbUiLibTest "Outliner*"` (198 assertions), rebuilt `TrenchBroom`, and visually checked the focused Outliner matrix in Light, Dark, and Blender themes at 100%, 150%, and 200% in `20260817-134611-809`. |
| 2026-08-17 | Complete | Replaced the Inspector rail's reused cube, entity, face-tool, folder, and settings artwork with a coherent monochrome 24 px semantic set for map layers, entities, selected faces, hierarchy, and plugins. The new icons share stroke weight and optical bounds, remain theme-invertible, and preserve the existing navigation layout and state treatment. |
| 2026-08-17 | Verified | Passed Release `MapWindow` (203 assertions), rebuilt `TrenchBroom`, and visually checked Map- and Outliner-selected rail states in the 18-state Workbench/Outliner Light, Dark, and Blender matrix at 100%, 150%, and 200% in `20260817-140520-086`. |
| 2026-08-17 | Complete | Added foundational component governance modeled on Blender's centralized semantic-control pipeline, made it required agent guidance, added a local preflight guard against feature-local styles, and introduced a deterministic component gallery. Consolidated the first input, choice, numeric, multiline, checkbox, radio, slider, push-button, and tool-button families while replacing page-specific visual overrides with shared defaults or explicit semantic roles. |
| 2026-08-17 | Verified | Passed the style-governance preflight, strict-compiled all nine changed C++ translation units, passed Release `MapWindow`, Python API, `SmartFaceSelectionPanel`, and `MiscPreferencePane` tests (634 assertions), rebuilt `TrenchBroom`, passed the 9-state component matrix at `20260817-205042-050`, and visually checked 72 affected Workbench, Outliner, Entity Browser, Face Inspector, Command Palette, View preferences, Misc/MCP, and Supporting states at `20260817-204311-158`. |
| 2026-08-17 | Complete | Finished foundational component consolidation for group surfaces, item views, headers, tabs, menus, scroll bars, and splitters; replaced remaining foundational object-name selectors with canonical defaults or `compact` / `outlined` roles; and extended governance enforcement and the component gallery. Audited all item delegates, retaining only behavior, semantic-content, full-row compound, and data-preview rendering. |
| 2026-08-17 | Verified | Passed the strengthened style-governance check, strict-compiled all 13 changed C++ translation units, passed nine focused Release UI test cases (561 assertions), rebuilt `TrenchBroom`, passed and visually checked the 9-state component matrix at `20260817-212125-443`, 54 affected real-screen states at `20260817-212214-243`, and 9 compact Misc/MCP states at `20260817-212452-697`. |
