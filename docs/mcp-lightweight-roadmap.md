# MCP Lightweight Roadmap

> Historical design, superseded for implementation on 2026-09-08 by the
> [Python migration delivery plan](mcp-python-migration/development.md) and
> [current governance](mcp-development-governance.md). The body records the earlier
> IR/tool architecture and completed work; its future tasks, compatibility rules
> and tool lists are not the active backlog. Use the current plan's four-entry
> Python bridge cutover and final acceptance.

## Summary

The TrenchBroom MCP should become a small, reliable execution layer, not a scene generator.

The intended stack is:

1. **Skill and recipe scripts** decide scene intent, prefab-like layout, domain rules, and acceptance flow.
2. **IR files** carry large structured batches without filling the conversation.
3. **C++ MCP tools** perform guarded map mutations, undoable transactions, target recovery, geometry facts, validation, and review renders.

This roadmap keeps MCP atomic and moves complex composition into skill recipes. New C++ MCP features must be generic, reusable, and tied to TrenchBroom state or undo/document safety.

## Design Rules

The normative rule set for future implementation is
`docs/mcp-development-governance.md`. The bullets below summarize that contract;
if there is a conflict, follow the governance document and update this roadmap.

- Do not add scene prefab tools to C++ MCP. Avoid tools like `create_temple`, `create_kz_route`, `create_courtyard`, or `create_racetrack`.
- Add C++ MCP capability only when it needs TrenchBroom internals, document guards, undo transactions, selection/object identity, validation geometry, or review rendering.
- Prefer selector/module targets over long object id lists.
- Prefer file-based IR for large batches: `ir_compile_preview_from_file` then `ir_apply_from_file`.
- Default all large outputs to summaries. Full ids and full face/seam details must be opt-in.
- Keep Modeling profile focused on the normal Agent path. Hide debug, viewport, legacy, and low-level expert tools, but keep them searchable.
- Use only the active `Core` / `Modeling` / `Full` tool profiles: `Core` for compact smoke and discovery, `Modeling` as the recommended default, and `Full` for expert/debug fallback.
- Treat review images as evidence, not as the only validator. Route and slope intent must pass geometry checks.

## Ownership Matrix

Use this table before adding any new MCP tool or recipe.

| Need | Owner | Reason |
| --- | --- | --- |
| Scene layout, prefab-like composition, repeated patterns | Skill recipe | It is domain intent, not editor state. |
| Large operation transport | IR file | It avoids bloating the conversation. |
| Map mutation, undo, dirty state, document guard | C++ MCP | It must be controlled by TrenchBroom. |
| Selector/module identity, live/stale object recovery | C++ MCP | It depends on the current document and session. |
| Geometry facts, slope/continuity analysis, map validation | C++ MCP | It needs reliable map geometry. |
| Gameplay difficulty, aesthetics, route intent, final judgment | Skill or Agent | MCP should report facts, not design taste. |
| Human UI plugin flows | `trenchbroom` plugin | They serve interactive users, not Agent automation. |

Escalate a recipe need into C++ MCP only when at least two independent recipes need it and the behavior cannot be expressed with existing IR operations or selector/module tools.

## Workstream Order

The phases are ordered. Do not start broad feature expansion before completing the compaction and validation work.

1. **Response hygiene first:** compact ids, absolute paths, summary defaults.
2. **Validation semantics second:** modes for continuous routes, jumps, stairs, spirals, and closed loops.
3. **Targeting third:** selector/module coverage across edit, validation, review, texture, and entity workflows.
4. **Review readability fourth:** image clarity, label policy, presets.
5. **Recipe growth last:** add recipes only after the MCP base is quiet and predictable.

This order matters because every later phase depends on short, stable tool outputs.

## Current State

- Modeling profile has been slimmed. Hidden tools remain searchable with `tb_tools_search(detail:"schema")`.
- Legacy `Balanced` config input is treated as `Modeling`; it is not a separate advertised profile.
- Prefab placeholders and legacy direct blockout helper tool names have been removed
  from the MCP catalog; old structural `blockout_create_batch` operation types remain
  compatibility/quick-sketch payloads only.
- CPU geometry review renderer is the default isolated review path. Contact sheets default to at most two panels.
- Selector/module workflows exist and support recovery, review, delete, and transform.
- Skill recipes exist for:
  - `ascending_loop`
- Earlier `cave_pass`, `kz_bhop_route`, `simple_house`, `temple_courtyard`, and
  `terrain_pass` recipe drafts were removed after human visual acceptance failed.
  Reintroduce them only as rebuilt recipes with inspected review output.
- Recipe scripts now have manifests, parameter validation, `--describe`, `--validate-only`, grouped examples, and a skill-side validator.
- The canonical skill source is tracked under
  `skills/trenchbroom-mcp-scene-workflow`; sync it to the runtime skill directory
  with `scripts\sync-trenchbroom-mcp-skill.ps1`.
- Real TB acceptance has validated the recipe path against a disposable map.

## Phase 1: Output Compaction And Path Clarity

Goal: make MCP responses small, predictable, and easy to locate.

Key work:

- Make `idsMode` consistent across `ir_apply`, `ir_apply_from_file`, selector, module, operation resources, review, create, transform, and delete tools.
- Define exact behavior:
  - `none`: no ids or samples.
  - `count`: counts only.
  - `sample`: counts plus a small sample.
  - `full`: complete ids.
- Fix current issue where `ir_apply_from_file(idsMode:"count")` still returns long `changedObjectIds`.
- Fix current issue where `selector_preview(idsMode:"count")` still returns object samples.
- Normalize review paths:
  - Accept relative `outputDir`, but return `absoluteOutputDir`, `absolutePreferredCapturePath`, and absolute capture paths.
  - Document that skill workflows should pass absolute output paths.
- Keep `map_validate(groupByType:true)` as the recommended default for dense maps.
- Add a shared output helper for id compaction instead of hand-rolling `idsMode` in each tool.
- Make tool schemas name defaults explicitly. If a tool defaults to `sample`, say so.

Acceptance:

- A large IR apply can return fewer than 150 lines in default mode.
- No default response contains hundreds of ids.
- Review image paths can be opened directly by an Agent without searching.
- Existing tests cover `none/count/sample/full` behavior for at least IR apply, selector preview, module inspect, and review.
- A real recipe apply report can be pasted into chat without swamping useful context.

Test targets:

- `TbUiLibTest "McpBridgeServer"` for output compaction behavior.
- `TbMcpLibTest "McpToolCatalog"` for schema defaults and descriptions.
- Real TB smoke with one default recipe and one dense recipe.

## Phase 2: Validation Modes And Summary Defaults

Goal: make route and slope validation trustworthy without flooding context.

Key work:

- Add explicit continuity modes:
  - `continuous`
  - `stepped`
  - `jump_chain`
  - `spiral`
  - `closed_loop`
- Make `closedLoop:true` a deliberate choice, not a default for circular-looking routes.
- For rising 360-degree routes, use `spiral` or ordered route validation unless the start/end seam is actually connected.
- Add compact default output for `geometry_analyze_slopes`:
  - `slopeCount`
  - ascending/descending/cross counts
  - min/max slope
  - worst warnings
  - small sample
- Add compact default output for `geometry_analyze_route_continuity`:
  - `continuous`
  - `semanticContinuous`
  - `fullWidthContinuous`
  - max gaps/steps
  - failing seam count
  - small failing seam sample
- Make full face and seam listings opt-in with `detail:"full"`.
- Add route-type warnings:
  - bhop gaps can be intended
  - spiral routes should not be closed-loop checked by default
  - inferred route direction is low confidence
- Add validation profiles that recipes can reference:
  - `walkable_continuous`
  - `spiral_ascending`
  - `jump_chain`
  - `stairs_or_steps`
  - `slide_or_surf`
- Keep raw seam/face evidence available through `detail:"full"` and resource reads.

Acceptance:

- Slope and continuity tools are readable in normal Agent context.
- A route mistake such as a reversed ramp, missing slope, vertical step, or unintended gap is visible in summary mode.
- Bhop/platform routes no longer look like generic failures when gaps are intentional and declared.
- Smooth ramps still fail when `slopeCount=0`.
- Validation output tells the Agent what to do next: accept, inspect detail, or rebuild.

Test targets:

- Unit tests for each route mode with known pass/fail geometry.
- Real TB route cases: spiral ramp, flat road, slide ramp, stepped stairs.

## Phase 3: Selector And Module As The Main Target System

Goal: stop carrying object ids through conversations.

Key work:

- Ensure common edit and validation tools accept `selector`, `operationIds`, and `objectIds`.
- Keep priority predictable: explicit object ids, then operation ids, then selector.
- Expand selector coverage where missing:
  - texture operations
  - entity property operations
  - validation operations
  - review operations
- Improve `module_validate`:
  - live/stale part counts
  - route validation pass-through
  - compact default summary
- Improve `module_compact`:
  - clear stale session records
  - do not mutate the map
- Standardize metadata:
  - `moduleId`
  - `part`
  - `role`
  - `routeId`
  - `order`
  - `generatedBy`
- Add selector diagnostics:
  - matched count before limit
  - stale excluded
  - module live/stale mismatch warning
  - selector summary string
- Ensure destructive tools refuse ambiguous selectors unless preview confirms the intended target count or the caller passes an explicit confirmation token.

Acceptance:

- A user request like "stretch the left rail a bit" can be handled with `selector_preview` plus `objects_transform`, without delete/rebuild.
- A complex generated scene can be reviewed and edited by module/part selectors only.
- Stale module records are visible and cleanable.
- Wrong-map or stale-session edits fail before mutation.

Test targets:

- Selector preview/select/delete/transform for module, part, role, route, material, classname, and bounds.
- Stale module/session tests after undo, delete, document switch, and module compact.

## Phase 4: Review Renderer Clarity

Goal: make Agent visual self-acceptance reliable.

Key work:

- Keep contact sheets capped at two panels by default.
- Add stronger label defaults:
  - hide entity labels by default on dense modules
  - label only important parts or stride labels on ordered routes
  - expose `labelParts`, `labelStride`, and `autoHideLabelsThreshold`
- Improve route and terrain review presets:
  - `route_platform`
  - `spiral_route`
  - `terrain_height`
  - optional `verticalExaggeration`
- Keep individual PNGs in the bundle even when contact sheet is limited.
- Render entity placeholders as useful glyphs by classname, without letting labels dominate the image.
- Add capture metadata that helps Agents decide whether to open details:
  - source view count
  - included/omitted count
  - target coverage
  - edge density
  - label count
  - warnings
- Prefer absolute paths in all review manifests.

Acceptance:

- Review images show only target objects, not unrelated map content.
- Default contact sheets remain readable for 15 to 100 object scenes.
- Terrain, route height changes, and spiral routes can be understood from the default review plus one optional detail image.
- Dense labels never obscure the main shape in default output.

Test targets:

- Quality tests for missing, tiny, blank, and low-coverage images.
- Real visual checks for route, terrain, building, and entity-heavy scenes.

## Phase 5: Skill Recipe Production Track

Goal: make complex scene generation live in skill scripts, not MCP C++.

Key work:

- Maintain a recipe manifest for each recipe:
  - id, name, version
  - parameter schema
  - default params
  - output parts
  - expected warnings
  - recommended MCP validation
- Keep examples by variant:
  - `minimal`
  - `default`
  - `stress`
- Use `validate_recipes.py` for deterministic script validation.
- Add recipe catalog support in the skill:
  - list available recipes
  - show when to use each recipe
  - show required validation path
- Add new recipes only when they are reusable enough to justify scripted generation.
- Do not let recipes call TrenchBroom, MCP, or `trenchbroom` directly. Recipes emit IR only.
- If the human-facing `trenchbroom` plugin needs prefab behavior later, it should reuse recipe scripts to emit IR.
- Keep recipe scripts deterministic. Same params must produce the same canonical IR.
- Add recipe-level expected validation:
  - which selectors to inspect
  - which slope/continuity mode to run
  - which warnings are expected
  - which warnings are failures
- Keep recipe docs in skill references, not in MCP schema.

Acceptance:

- Every recipe can be described, validated, and generated without opening TrenchBroom.
- Every default recipe IR passes `ir_compile_preview_from_file`.
- Every route-like recipe defines route metadata and required validation.
- A new Agent can build a recipe scene without reading long JSON in the conversation.
- Adding a new recipe does not change C++ MCP unless it exposes a missing generic primitive or validator.

Test targets:

- `validate_recipes.py` for `minimal/default/stress`.
- Real TB apply and review for each default recipe.
- Codex CLI worker run with only skill and MCP, no source edits.

## Phase 6: Profile And Tool Catalog Governance

Goal: keep the default MCP tool surface small.

Default visible path:

- status/open/save/history
- IR preview/apply
- selector/module recovery
- transform/delete
- semantic entity/texture operations
- geometry validation
- map validation
- isolated review
- common atomic creation tools

Hidden but searchable:

- viewport capture tools
- debug tools
- legacy metadata selection
- low-level face/object-id tools
- duplicate convenience aliases

Key work:

- Add a catalog review test that fails if Modeling profile grows unexpectedly.
- Mark deprecated or legacy tools in schema descriptions with recommended replacements.
- Avoid adding aliases for memory convenience. Put workflow guidance in the skill instead.
- Keep Full profile available for power users and debugging.
- Add a tool classification field or convention:
  - `core`
  - `modeling`
  - `validation`
  - `review`
  - `expert`
  - `debug`
  - `legacy`
- Track the Modeling profile count over time and require justification when it grows.

Acceptance:

- Modeling profile stays short enough for Agent use.
- Hidden tools are still discoverable by exact search.
- Default workflows do not require viewport/debug tools.
- Tool search returns a recommended replacement for hidden legacy entries.

Test targets:

- Catalog tests for visible/hidden/searchable behavior.
- Exact-name search tests for hidden tools.
- Profile-size regression test.

## Phase 7: Real-World Regression Loop

Goal: measure Agent experience, not just tool coverage.

Regression scenes:

- Ascending spiral or road route.
- Dense architectural whitebox when such a recipe is rebuilt.
- KZ/bhop/slide route when such a recipe is rebuilt.
- Terrain/heightmap route when terrain tools change or such a recipe is rebuilt.
- Industrial material/entity edit pass when texture/entity tools change.

Required checks:

- `tb_status`
- `ir_compile_preview_from_file` or atomic preview
- `ir_apply_from_file` or atomic create
- `module_inspect`
- `selector_preview`
- `operation_validate`
- `geometry_analyze_slopes` when slopes are intended
- `geometry_analyze_route_continuity` when route order matters
- `map_validate(groupByType:true)`
- `module_render_review` or `render_review_selector`
- save only disposable maps
- record crash log count before and after
- record preferred review path and whether it opens directly
- record response-size friction when a tool returns too much data

Report friction as:

- P0: crash, wrong map write, data loss.
- P1: blocks real map creation.
- P2: awkward, noisy, or context-heavy.
- P3: documentation or default issue.

Acceptance:

- No new crash logs.
- No wrong-document writes.
- No workflow requires carrying hundreds of object ids.
- At least three complex scenes are understandable from review output.
- Reports identify whether a problem belongs in MCP, skill workflow, or recipe logic.
- At least one independent Codex CLI worker can complete the flow without being told the expected answer.

Artifacts:

- disposable `.map`
- generated IR
- tool summary JSON
- review bundle
- screenshot/contact sheet
- markdown experience report

## Immediate Backlog

1. Fix `ir_apply_from_file(idsMode:"count")` returning long `changedObjectIds`. Done.
2. Fix `selector_preview(idsMode:"count")` returning object samples. Done.
3. Return absolute review paths, even when `outputDir` is relative. Done.
4. Add summary/default and `detail:"full"` modes to slope and continuity analysis. Done: default responses return counts and samples; `detail:"full"` returns all slopes/surfaces/seams.
5. Add explicit route validation modes: `continuous`, `stepped`, `jump_chain`, `spiral`, and `closed_loop`. Done; legacy `continuityMode:"jump_gaps"` remains accepted and normalizes to `jump_chain`.
6. Update skill workflow to recommend absolute review output dirs. Done.
7. Add tests for output compaction across IR apply, selector, module, and review. Done.
8. Add recipe catalog command or script-level listing. Done.
9. Add Codex CLI regression prompts for the three core recipe scenarios. Done.
10. Review Modeling profile size after each MCP feature batch. Done.

## Decision Checklist For New Requests

Use this checklist when a user asks for a new MCP capability.

1. Can the behavior be expressed as existing IR operations plus metadata?
   - If yes, implement it as a recipe or skill workflow.
2. Does it need map mutation, undo, document guard, or live object identity?
   - If yes, it may belong in C++ MCP.
3. Is it a geometry fact or validation result that several workflows need?
   - If yes, it may belong in C++ MCP.
4. Is it a scene, route, building, gameplay object, or aesthetic pattern?
   - If yes, keep it in skill recipe.
5. Will adding a visible tool make the Modeling profile noisier?
   - If yes, hide it by default or put the rule in the skill.
6. Can the response be summarized by default?
   - If no, redesign the output before adding the tool.

When in doubt, start in skill recipe. Promote to MCP only after real flows show repeated need for a generic editor primitive or validator.

## Tracking Table

Use this table as the rolling implementation checklist.

| Priority | Item | Owner Layer | Status |
| --- | --- | --- | --- |
| P1 | Normalize `idsMode` across high-volume tools | C++ MCP | Done for high-volume default paths: IR apply, selector preview, transform, review, brush/blockout create, operation inspect/validate/resource reads, entity/texture/asset/problem mutations, and object deletes default to compact counts with full ids opt-in. |
| P1 | Return absolute review paths | C++ MCP | Done for review renderer outputs and compact review summaries. |
| P1 | Add summary/default modes to slope and continuity analysis | C++ MCP | Done: default summary returns counts/samples; `detail:"full"` returns full slope/seam evidence. |
| P1 | Document absolute review output path in skill workflow | Skill | Done in `trenchbroom-mcp-scene-workflow`; keep in sync when review defaults change. |
| P1 | Add route validation modes | C++ MCP + Skill | Done for MCP route continuity: `continuous`, `stepped`, `jump_chain`, `spiral`, `closed_loop`, plus recipe profile aliases. |
| P2 | Add recipe catalog/listing | Skill | Done in `trenchbroom-mcp-scene-workflow/scripts/list_recipes.py`; lists manifests, examples, and recommended validation without generating IR. |
| P2 | Add profile-size regression test | MCP catalog tests | Done: `McpToolCatalog` now guards Modeling profile size and hidden-tool search behavior. |
| P2 | Improve entity glyph and label policy in review | C++ MCP | Done: entity glyphs remain visible while dense classname labels auto-hide; `labelParts` labels important metadata parts with stride/threshold controls, and review summaries report entity/order/part label counts. |
| P2 | Add Codex CLI regression prompts | Skill/testing | Done in `skills\trenchbroom-mcp-scene-workflow\references\codex-cli-regression-prompts.md` for the retained ascending spiral disposable flow. |
| P3 | Evaluate legacy/convenience tools for deprecation text | MCP catalog docs | Done: hidden viewport, low-level review, legacy metadata, and legacy route analysis entries stay searchable and describe recommended selector/module/review/continuity replacements. |

## Non-Goals

- Do not replace TrenchBroom's renderer with a full offscreen OpenGL renderer for MCP review.
- Do not add scene-specific C++ prefab tools.
- Do not make MCP decide gameplay difficulty, aesthetics, or final route design intent.
- Do not persist module registry data into `.map` unless there is a separate design.
- Do not make recipe scripts bypass MCP by editing live maps directly.

## Success Definition

The roadmap succeeds when an Agent can build and revise complex scenes through a short loop:

1. Choose or generate recipe parameters.
2. Emit IR to a file.
3. Preview and apply through MCP.
4. Recover targets by module or selector.
5. Validate geometry facts.
6. Review a small number of clear images.
7. Iterate by selector transforms or regenerated IR.

The Agent should not need to see hundreds of ids, read huge JSON payloads, inspect UI viewport noise, or ask C++ MCP for prefab-specific behavior.

## Completion Gates

A roadmap phase is complete only when all of these are true:

- The implementation is covered by focused unit/catalog tests.
- A real TB disposable-map flow exercises the changed workflow.
- The skill workflow and roadmap are updated when behavior changes.
- The default response is compact enough for normal Agent use.
- Failures tell the Agent whether to retry, inspect detail, or rebuild.
- No unrelated map content appears in isolated review output.
- No new crash logs appear during acceptance.
