# Documentation Index

Updated 2026-11-09. This index is the entry point for deciding what to trust.

- **Rules** are binding; read them before changing that area.
- **Current design and plans** are the basis for development decisions today.
- **Reference** explains behaviour that is stable.
- **Archive** is history. It explains how we got here, and must not be used to judge or
  drive current work.

## Rules

| Document | Area |
| --- | --- |
| [UI component style governance](ui-component-style-governance.md) | Foundational control appearance, `tbControlRole`, snapshot acceptance. |
| [MCP development governance](mcp-development-governance.md) | Four-entry MCP surface, mode gating, skill sync, delivery rules. |

## Current design and plans

| Document | Decides | Status |
| --- | --- | --- |
| [UI foundation improvement](ui-foundation-improvement.md) | Shared panel layouts, cards, extensibility, migration scope and acceptance. | Implemented; current guidance. |
| [Workspace function and workflow inventory](workspace-function-workflow-inventory.md) | Inspector, content browser and asset library overlaps and consolidation candidates. | Code inventory; proposals require workflow validation. |
| [Workspace unification proposal](workspace-unification-proposal.md) | Agreed navigation direction, capability preservation, workflow and staged acceptance. | Rounds 1–2 delivered; entity browsing moved to Content Browser. Drawing trial pending; remaining workflow rounds planned. |
| [Asset library master plan](asset-library-master-plan.md) | Global library, project capture, compile input, hltools handoff. | Draft; capture and compile parts are implemented. |
| [Asset browser experience todo](asset-browser-experience-todo.md) | Browser/inspector/sound/sky behaviour and acceptance. | In progress; only acceptance items remain open. |
| [Native project format design](native-project-format-design.md) | `.tbproj` container, revisions, snapshots, publishing. | Implemented core; future targets still valid. |
| [Native project usage](native-project-usage.md) | User-facing trial instructions for projects and libraries. | Current. |
| [GoldSrc terrain material design](terrain-material-authoring-design.md) | Asset library `Material` synthesis, project capture, terrain panel painting, and C++ compile baking. | MVP implemented; library/project material pipeline in progress. |
| [Terrain sculpting design](terrain-sculpting-design.md) | Terrain creation, sculpting, vertex editing, material painting, and compile handoff. | Design; implementation in progress. |
| [Project workspace implementation](project-workspace-implementation.md) | Engineering handoff for project/library/capture/compile. | Current; not every proposal is done. |
| [Workspace UI architecture](workspace-ui-architecture.md) | Panel ownership, command targets, layout persistence. | Active design; staged migration. |
| [MCP architecture](mcp-python-migration/architecture.md) | Transport, discovery, execution and editing boundaries. | Current. |
| [MCP delivery record](mcp-python-migration/development.md) | The delivery contract plus its completion record. | Delivered 2026-09; kept as the specification and evidence, not a backlog. |
| [MCP acceptance scenarios](mcp-python-migration/scenarios.md) | What a Release-process acceptance run must prove. | Current. |
| [Agent workflow](mcp-agent-workflow.md) | How an agent inspects, composes Python and captures evidence. | Current. |
| [MCP execution boundaries](mcp-security-threat-model.md) | What MCP may and may not claim. | Current. |
| [Transform card interaction](transform-card-interaction.md) | Numeric-field interaction contract for the transform card. | Current. |
| [Custom feature maintenance risks](custom-feature-maintenance-risks.md) | Known debt and its fix order. | Living document; update when a risk is fixed. |

## Reference

| Document | Covers |
| --- | --- |
| [Entity motion preview](entity-motion-preview.md) | GoldSrc doors/buttons, shared property controls, preview semantics and validation. |
| [GoldSrc light directions](goldsrc-light-directions.md) | Compiler-aligned light arrows, target precedence and pitch conventions. |
| [Python API reference](python-api-reference.md) | Entry points of the `trenchbroom` module. |
| [Python API migration](python-api-migration.md) | Breaking renames plugin authors must apply. |
| [Python plugin development guide](python-plugin-development-guide.md) | Manifest plugins, panels, events, timers. |
| [Issue browser workflow](issues-workflow.md) | Grouping, filtering, navigation, session ignores, batch fixes and acceptance. |
| [Play mode jump guides](play-mode-jump-guides.md) | Platform spans and height differences while grounded in Play mode. |
| [Play mode travel aids](play-mode-travel-aids.md) | Hook and noclip controls for navigating and testing maps in Play mode. |
| [Themes](themes.md) | Theme file format. |
| [Performance testing](performance-testing.md) | `TbPerf` benchmarks and how to run them. |

## Archive

Everything under [`archive/`](archive/README.md) is historical: migration-era MCP designs,
the 2026-09 architecture review, the custom feature wish list, one-off audits, and closed
performance investigations. The [archive index](archive/README.md) lists each document with
the reason it was archived.

## Adding a document

Put a document here only while it guides current work. Date it, state its status at the top,
and link it from this index. When it stops guiding work — the effort shipped, the design was
replaced, the measurement is closed — move it to `archive/` in the same change that
supersedes it, with an entry in the archive index.
