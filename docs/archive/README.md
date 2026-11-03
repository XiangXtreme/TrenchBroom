# Archived Documentation

This folder keeps historical planning notes, scenario reports, one-off investigations and
superseded designs. They are useful for context, but they are not the current source of
truth for MCP development, the asset pipeline, or UI decisions.

Current guidance lives in the parent folder; start at [the documentation
index](../README.md). In short:

- [MCP governance](../mcp-development-governance.md)
- [MCP architecture](../mcp-python-migration/architecture.md)
- [MCP delivery record](../mcp-python-migration/development.md)
- [Core acceptance scenarios](../mcp-python-migration/scenarios.md)
- [Agent workflow](../mcp-agent-workflow.md)

Archived documents should not be used to justify new C++ MCP prefab tools, profile
expansion, or asset features that contradict current governance. If an archived note
conflicts with current guidance, follow `../mcp-development-governance.md`.

## Contents

| Document | Reason archived |
| --- | --- |
| `mcp-integration-plan.md` | Early integration plan replaced by governance, roadmap, and workflow docs. |
| `mcp-scene-iteration-cases.md` | Large scenario log used as historical evidence, not active workflow guidance. |
| `mcp-composite-atomic-validation.md` | Historical validation notes folded into the current Python execution layer. |
| `mcp-usage-feedback-2026-06-28.md` | One-off experience report already incorporated into later MCP/skill changes. |
| `kz-mcp-tool-development.md` | KZ-specific development notes superseded by generic route validation and skill recipe ownership. |
| `mcp-lightweight-roadmap.md` | Migration-era design; superseded by the delivered four-entry Python layer. |
| `mcp-moderate-lightweight-design.md` | Migration-era design; superseded by the delivered four-entry Python layer. |
| `mcp-long-term-refactor.md` | Migration-era blueprint; the parts that mattered are now architecture/governance. |
| `custom-feature-architecture-review.md` | Review baseline `be0ca0be5` predates WAD textures, the unified asset browser, and the MCP cutover. |
| `custom-feature-roadmap.md` | Wish list whose checkboxes no longer reflect what shipped; keep only for historical intent. |
| `custom-feature-manual-audit.md` | One-off manual/screenshot audit from 2026-09. |
| `project-workspace-acceptance.md` | Snapshot at `9f152d2bf`; superseded by the asset library master plan and the implementation handoff. |
| `qt-ui-modernization-progress.md` | Theme/component modernization record for a finished phase; current guidance is the style governance. |
| `split-extrusion.md`, `split-extrusion-optimization.md` | Closed performance investigation with its measurement CSVs. |
| `superpowers/` | Dated MCP hardening records from 2026-07/08. |

When an archived document is needed as evidence, quote it with its date; do not restate its
conclusions as current design.
