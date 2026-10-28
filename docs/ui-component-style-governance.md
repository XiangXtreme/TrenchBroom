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
- `navigation`: an icon or tab used to change application regions rather than execute a
  document command.
- `outlined`: an icon command whose persistent boundary communicates a mode or selectable
  tool outside a toolbar. It is not a general-purpose way to make one button look stronger.

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
- standard item-view row minimum height: 22 px
- standard small and button icon size: 16 px; toolbar icon size: 20 px

These are logical Qt pixels and scale through Qt's device-pixel handling. A feature module
may constrain width for its content, but it must not redefine the component's visual
metrics.

## Exceptions

Exceptions are limited to domain renderers whose content is itself visual data, including
map views, material/model previews, color swatches, curves, and other editor canvases. An
exception must still use theme tokens for surrounding chrome and interaction states.

Temporary compatibility exceptions must carry a removal note in
`docs/qt-ui-modernization-progress.md`. Permanent exceptions must be named compound
components and covered by a focused snapshot or rendering test.

## Acceptance

The `components` UI snapshot is the primary visual contract for foundational controls. Run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\ui-theme-acceptance.ps1 -Targets components
```

The snapshot must cover representative normal, focused, checked, read-only, selected, and
disabled states for inputs, commands, group surfaces, item views, headers, tabs, menus,
scroll bars, and splitters at 100%, 150%, and 200% scale for Light, Dark, and Blender themes.
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
