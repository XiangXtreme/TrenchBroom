# UI Component Style Governance

## Purpose

TrenchBroom must present one coherent family of foundational controls across dialogs,
inspectors, browsers, tool panels, plugin panels, and application-owned Python UI.
Business screens declare control semantics and layout; they do not invent another visual
implementation of a line edit, combo box, button, indicator, slider, list, or tab.

This follows the useful part of Blender's UI architecture: feature code creates semantic
controls while a central component layer owns their state, metrics, theme values, and
rendering. TrenchBroom remains a Qt Widgets application and does not copy Blender's custom
immediate-mode renderer.

## Sources of truth

Foundational control presentation has three owners:

1. `ThemeTokens` owns theme-selectable colors. A distributable theme may change token
   values, but it cannot inject QSS or change geometry.
2. `ApplicationStyle` owns behavior and primitives which QSS cannot implement reliably,
   including shared indicator painting, platform-independent metrics, and combo popup
   behavior.
3. `app/TrenchBroom/resources/stylesheets/base.qss` owns canonical component geometry and
   visual states.

Theme-dependent control assets follow the same split: `expandThemeStyleSheet` maps
`@tb-...-image` tokens to the light/dark files under `resources/qrc/controls/`, so QSS
never hardcodes an asset path and a theme switch cannot leave a control with the wrong
icon.

Feature modules own content, data, accessibility, interaction, and layout. They may not
become a fourth source of foundational component styling.

## Canonical controls

The default role is implicit. A normal Qt control must receive the canonical appearance
without an object name, helper call, or local stylesheet.

| Family | Canonical Qt controls | Baseline |
| --- | --- | --- |
| Text input | `QLineEdit`, `QTextEdit`, `QPlainTextEdit` | Shared input surface, border, focus, selection, and disabled states |
| Choice input | `QComboBox` | Shared input surface and compact scrolling popup |
| Numeric input | `QSpinBox`, `QDoubleSpinBox` | Shared input surface and step controls |
| Boolean input | `QCheckBox`, `QRadioButton` | Shared indicator size, contrast, focus, and disabled states |
| Commands | `QPushButton`, `QToolButton` | Shared command states; icon controls remain visually quiet |
| Ranges | `QSlider`, scroll bars | Shared groove, handle, hover, and focus treatment |
| Collections | item views and headers | Shared row height and active/inactive selection treatment |
| Navigation | tabs, menus, navigation buttons | Shared navigation states with an accent reserved for active position |

## Controlled roles

One component family may have a small number of semantic roles. A role is not a new
component design: colors, borders, state transitions, icon scale, and corner hierarchy
remain related to the default control.

- `standard`: the implicit default for forms and panels.
- `compact`: a lower-height control used inside a toolbar or status surface, or as an
  inline companion toggle for a stacked vector.
- `prominent`: a primary search or command input whose larger hit area is part of the
  workflow.
- `vector`: stacked numeric channels with shared corners, centered values and no step
  buttons. Used for spatial vectors where horizontal dragging is the primary adjustment;
  20 px minimum height keeps the three channels compact. Normal numeric focus, hover,
  disabled and selection states still apply. `tbVectorPosition` marks first/middle/last.
- `navigation`: a button, tab or list used to change application regions rather than
  execute a document command. Navigation lists use 16 px icons and a 32 px minimum row
  height provided by `ApplicationStyle`; QSS owns their hover and selection states.
- `outlined`: an icon command whose persistent boundary communicates a mode or selectable
  tool outside a toolbar. It is not a general-purpose way to make one button look stronger.
- `chip`: a labelled command with an icon used inside a toolbar when the word is part of
  understanding the control, for example a filter or view disclosure whose popover has no
  other label. The persistent border and 8 px horizontal padding keep the icon and label
  legible; height and state treatment stay those of the standard command.

- `overlay`: a control above an image or OpenGL canvas. An opaque themed surface
  keeps its label legible independently of the rendered content; persistent borders
  identify the hit area. The standard 28 px command height and states apply.

New roles require a documented interaction or density reason and a component snapshot.
"This page looks different" is not a valid reason.

Use the dynamic property `tbControlRole` when a standard Qt class needs a controlled role.
Do not encode component roles in object names. Parent-context selectors are acceptable for
true compound components such as a toolbar, menu, or console, provided they do not redefine
an unrelated foundational control.

## Rules for feature code

- Prefer standard Qt controls. Subclass only to add behavior, accessibility, data handling,
  or a domain-specific visualization.
- A behavior-only subclass must inherit the canonical style. It must not paint a second
  version of the base control.
- Do not call `setStyleSheet()` for reusable control appearance. Runtime data visualization,
  such as the actual color inside a swatch, is the narrow exception.
- Do not hardcode theme colors in feature code or QSS. Add or reuse a `ThemeTokens` value.
- Do not use an object-name selector to change foundational control height, padding,
  radius, border, or interaction colors.
- Object names remain valid for discovery, tests, accessibility wiring, and styling a
  unique compound surface.
- Do not replace a Qt control with custom painting solely to make one screen look different.
- Keep focus, hover, pressed, checked, selected, inactive, read-only, and disabled states
  complete in Light, Dark, and Blender themes.
- Plugin-created standard Qt controls inherit the same component system automatically.

## Baseline metrics

Metrics are reviewed as a set rather than copied into feature modules:

- standard input minimum height: 22 px
- compact control minimum height: 20 px
- prominent input minimum height: 30 px
- input radius: 4 px
- command and grouped-surface radius: 6 px
- popup surface radius: 8 px; popup item radius: 4 px
- standard indicator size: 18 px
- standard item-view row minimum height: 22 px; navigation list row minimum: 32 px
- standard small and button icon size: 16 px; toolbar icon size: 20 px

These are logical Qt pixels and scale through Qt's device-pixel handling. A feature module
may constrain width for its content, but it must not redefine the component's visual
metrics.

Shared collapsible sections use `ClickableTitleBar` with a fixed vertical size policy;
`CollapsibleTitledPanel` constrains its layout to the visible children's minimum and
maximum sizes. A collapsed section occupies only its title row, including in resizable
scroll areas. Expanded content retains its own growth policy. `ApplicationStyle` paints
the disclosure button's `tbDisclosure` arrow using the same chevron and theme colors as
tree branches. The `components`, `terrain` and `terrain-collapsed` snapshots cover these
states; feature panels do not supply their own title geometry or arrow rendering.

## Shared composition

`PanelLayout` owns content padding, form column/row gaps, action spacing and ordinary
scroll-area defaults. Use `createPanelForm`, `createPanelColumn`, `createPanelActions`
and `createPanelScrollArea` instead of copying their setup into a feature. These return
standard Qt layouts and widgets, so content, connections and specialized width/size
constraints stay with the caller. `PanelInsets::Toolbar` provides 8 px horizontal and
4 px vertical padding for compact panel headers; `Content` provides 12 px horizontal
and bottom padding with an 8 px top inset. Form column/row gaps are 12/6 px.
`FormWithSectionsLayout` shares the form defaults; its section indent and dialog outer
margins remain part of preferences composition.

Use `PanelList` for a vertical collection of panels. It owns the bottom stretch; callers
add, insert and remove widgets without manipulating a spacer. Removing a widget leaves
ownership and visibility unchanged: explicitly hide/reuse or delete it at the call site.

Use `PanelItemList` for bounded item lists embedded in a form. Its row budget uses the
current item/grid height, font, frame and spacing, so the enclosing form scrolls instead
of crushing the list. WAD lists retain four to six rows, and the skybox grid retains two
to three complete thumbnail rows. Additional items scroll within the list. The widget
inherits the canonical item-view appearance and recalculates its geometry on model,
font and style changes. Long asset paths belong in item data/tooltips; file lists show
filenames with middle elision and do not add a horizontal scroll bar.

`CollapsibleTitledPanel::Presentation::Card` selects the shared `tbPanelStyle="card"`
surface in `base.qss`; `Section` is the flush embedded presentation. Use
`createContentLayout()` for its standard body and `addTitleWidget()` for title actions.
Call `setContentSizing(Expanding)` for a growing tree, table or canvas. Ordinary form
bodies default to `FitContents`; surplus height stays below the panels. Theme/presentation
changes and collapse keep the shared title, divider and body states in sync. Existing
object names and persisted expansion states continue to identify instances.

The scene property form keeps labels, values and actions on one row while the current
font leaves room for a ten-digit label and a sixteen-digit value. Narrower rows stack
the value below the label; font/style changes recalculate the same rule. Labels share
a bounded column and retain their full text in tooltips. Existing fields and the add
row reserve the same action column, so inline values align at ordinary sidebar widths.
Choice descriptions size the popup rather than widening or stacking the form field.
Resizing rearranges the existing widgets and preserves the current edit and focus.
`EditorLayout` owns the initial and persisted workspace splitter proportions; do not
add a second set of startup proportions in the view constructor.

The implementation and extension guide is [UI foundation improvement](ui-foundation-improvement.md).

## Exceptions

Exceptions are limited to domain renderers whose content is itself visual data, including
map views, material/model previews, color swatches, curves, and other editor canvases. An
exception must still use theme tokens for surrounding chrome and interaction states.

Temporary compatibility exceptions must carry a removal note in
`docs/archive/qt-ui-modernization-progress.md`. Permanent exceptions must be named compound
components and covered by a focused snapshot or rendering test.

## Acceptance

The `components` UI snapshot is the primary visual contract for foundational controls. Run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\ui-theme-acceptance.ps1 -Targets components
```

The snapshot must cover representative normal, focused, checked, read-only, selected, and
disabled states for inputs, commands, group surfaces, item views, headers, tabs, menus,
scroll bars, and splitters at 100% scale for Light, Dark, and Blender themes by default.
Use `-ScaleFactors @("1", "1.5", "2")` when a change needs explicit multi-scale validation.
A component change also requires focused tests for affected behavior and the representative
real screens which use the component.

Before completing a component-style change:

1. Search for `setStyleSheet`, object-name QSS selectors, custom delegates, and custom paint
   paths affecting the component family.
2. Run `scripts\check-ui-style-governance.ps1`; it rejects feature-local styles outside the
   global application style and the data-driven color swatch allowlist, as well as object-name
   selectors that restyle foundational controls.
3. Confirm that feature code only declares semantics or a documented controlled role.
4. Build the narrowest affected test target, then the Release `TrenchBroom` target.
5. Run the `components` snapshot matrix and focused real-screen snapshots.
6. Inspect the contact sheet for clipping, inconsistent metrics, weak contrast, and overlap.

## Migration order

1. Input, choice, numeric, checkbox, and radio controls.
2. Standard and tool buttons.
3. Multiline input, sliders, and group surfaces.
4. Item views, headers, tabs, menus, and scroll bars.
5. Compound editor controls and remaining compatibility exceptions.

Migration should remove duplicate styling as each family becomes canonical. Do not preserve
an accidental visual difference by naming it as a new role.

## Map view area gestures

`AreaMapView` owns the corner grip and split/join preview as domain layout visuals.
They use the application palette (Mid, Accent, Window and WindowText), have no local stylesheet,
and do not replace a foundational control. The grip supports pointer dragging and
keyboard access to the normal Qt context menu. Split dragging shows the full source
area, a high-contrast divider, the new region and a ratio/release/cancel hint after the
shared Qt drag threshold. Only a drag with an existing valid preview can commit on
release; previewing leaves the viewport tree and cameras untouched. Accept `map-areas`,
`map-area-split-preview`, `map-area-join-preview`, `map-area-partial-join-preview`
and `map-area-dock-preview` across the theme/scale matrix. Rectangular area boundaries
use the shared style's `PM_SplitterWidth` and `CE_Splitter`; the domain widget only
owns hit testing, connected-edge resizing and topology.

The map-only gestures follow the local Blender 5.2 area rules: the drag source survives;
the close mask darkens the theme window color; partial joins trim overhangs and close
source remainders into the best adjacent area; small offsets align screen edges. Commit
highlights fade over 150ms without delaying the layout edit. The common-edge band joins,
while deeper side/center
targets dock or replace, closing the old location. Qt device-independent pixels provide
Blender's scaled 29px minimum width, 26px header and 52px split minimum. Layout settings
store a validated rectangular tiling (v2), migrating previously saved splitter trees (v1).
Reference: `source/blender/editors/screen/screen_edit.cc` (`screen_area_join_ex`,
`screen_area_close`) and `screen_ops.cc` (`area_docking_target`, `area_docking_apply`).

## Workbench card edges

`WorkbenchSurface` and its shared `WorkbenchBorder` overlay own workbench card edges,
including OpenGL viewport edges. The overlay paints only the border and outer corner
cutouts with antialiasing, leaves input to the content, and does not reparent renderers.
Its radius, stroke and colors are global `tbSurfaceFrame` properties in `base.qss`.
Each viewport owns one frame; the containing workspace has no second card border.
Panel surfaces use the same frame. Area gutters use the greater of the shared splitter
and horizontal layout spacing metrics. Accept `components`, `workbench`, `outliner`
and `map-areas`, plus the existing gesture previews across the theme/scale matrix.
View selectors use standard overlay tool buttons and exclusive Qt menu actions.

## Text and semantic feedback

Use inherited UI type for region titles and sentence case for labels. Section hierarchy
comes from spacing and a 600 weight, without uppercase conversion or extra tracking.
Use `setInfoStyle` for smaller secondary text, `setErrorStyle` for validation feedback,
and `setDefaultStyle` when feedback returns to normal. These helpers maintain
`tbTextRole`; the global stylesheet owns colors, including disabled states. Secondary
text must remain distinct from disabled controls.

Domain renderers which write formatted text use `themeSemanticColor` for errors, warnings
and success. `ApplicationStyle` publishes the installed theme tokens on the application;
this also honors command-line snapshot theme overrides. Do not re-resolve a saved theme
preference or hardcode a second palette inside a console or preview renderer.

The components target includes secondary, error and disabled feedback. Acceptance for
text or navigation changes includes preferences, workbench, narrow Outliner properties,
asset library, output log and Python console.
