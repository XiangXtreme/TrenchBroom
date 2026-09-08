# MCP Moderate Lightweight Design

> Historical design, superseded for implementation on 2026-09-08 by the
> [Python migration delivery plan](mcp-python-migration/development.md) and
> [current governance](mcp-development-governance.md). The body records the earlier
> IR/tool architecture and completed work; its future tasks, compatibility rules
> and tool lists are not the active backlog. Use the current plan's three batches.

## Summary

The right lightweight direction is not to make TrenchBroom MCP weaker. It is to make
the C++ MCP layer smaller, more stable, and easier to trust.

The target stack is:

1. **C++ MCP** handles editor state, safe mutation, target identity, validation, and
   review.
2. **IR files** carry large geometry batches without filling the conversation.
3. **Skill recipes** handle prefab-like scene composition, domain planning, and
   reusable generation patterns.

This is a medium lightweight plan. It keeps the MCP useful on its own, while moving
complex scene generation out of C++ and into skill scripts.

## Current Size

Current `lib/TbUiLib/src/mcp` size is about **927 KB** across 31 source/header files.
Reliability work increased the total because authentication, bounded session state,
atomic IR rollback, structured recovery, and focused interfaces now live in the C++
kernel. The lightweight target remains a direction, not a claim that this branch has
already reached 550-650 KB.

The largest files are:

| File | Approx size |
| --- | ---: |
| `McpBrushTools.cpp` | 200 KB |
| `McpSelectorTools.cpp` | 101 KB |
| `McpReviewRenderTools.cpp` | 81 KB |
| `McpObjectTools.cpp` | 56 KB |
| `McpEntityTools.cpp` | 52 KB |
| `McpRouteTools.cpp` | 51 KB |
| `McpTextureTools.cpp` | 48 KB |

The size is not caused by transport code alone. The MCP layer now contains editor
automation, object identity, metadata/module tracking, geometry primitives, validation,
review rendering, and a large amount of JSON parsing and diagnostics.

## Target Size

Medium lightweight target:

| Area | Current | Target |
| --- | ---: | ---: |
| `lib/TbUiLib/src/mcp` C++ code | ~927 KB | ~550-650 KB |
| Default Modeling profile | short but still broad | shorter, high-frequency only |
| Skill recipe scripts | growing | larger and more capable |
| Total capability | high | same or higher |

The active tool-profile surface is `Core` / `Modeling` / `Full`: `Core` remains a
compact smoke/discovery set, `Modeling` is the recommended Agent workspace, and
`Full` is the expert/debug fallback. Legacy `Balanced` config input may migrate to
`Modeling`, but it is no longer a separate design target.

This means the total system may not become smaller. The point is to move code to the
right layer:

- C++ becomes a reliable editor automation kernel.
- Skill recipes become the scene generation layer.
- IR remains the transport format between them.

## Layer Boundaries

Future MCP development is governed by `docs/mcp-development-governance.md`.
Treat that document as the normative rule set for deciding whether a capability
belongs in C++ MCP, IR, or a skill recipe.

### C++ MCP Owns

C++ MCP should keep capabilities that require TrenchBroom internals:

- active document checks and `expectedDocumentPath`
- safe map mutation through undoable transactions
- stable object id registry
- selection, group, module, and selector target recovery
- operation history, undo, redo, and operation resources
- IR preview and apply
- map validation and problem checks
- geometry fact extraction
- slope and route continuity analysis
- review rendering and contact sheets
- texture, entity, face, and object editing when it changes live map state

These features depend on the current map, node lifetimes, selection state, undo stack,
or renderer. They belong in C++.

### Skill Recipes Own

Skill recipes should own complex composition:

- cottages and houses
- temples and courtyards
- ascending loops and spiral routes
- KZ, bhop, slide, or surf route layouts
- dense architectural whitebox layouts
- repeating decorative structures
- domain rules, style choices, and gameplay intent

Recipes should emit IR JSON files. They should not mutate the map directly, call
TrenchBroom internals, or bypass MCP guards.

### IR Owns

IR should stay as the boundary format:

- large operation transport
- deterministic recipe output
- preview before mutation
- apply through MCP transaction/history
- metadata attachment: `moduleId`, `part`, `role`, `routeId`, `order`,
  `generatedBy`

IR is not a full plugin API. It is the batch edit plan that C++ MCP can validate and
apply safely.

## Functional Changes After Lightweighting

### What Stays In MCP

The following workflows must still work without a skill:

1. Open or verify the active map.
2. Create simple geometry with atomic tools.
3. Apply an IR file.
4. Select, inspect, transform, or delete objects by selector/module/group.
5. Validate map state and geometry facts.
6. Render review images.
7. Undo, redo, and inspect operations.

MCP remains useful as a standalone editing automation layer.

### What Moves To Skill

The following should stop growing in C++:

1. Scene-level prefab generation.
2. Long route construction logic.
3. Building layout grammar.
4. KZ difficulty judgment.
5. Repeated aesthetic details.
6. Domain-specific acceptance narratives.

The Agent should call a recipe script to produce IR, then use MCP to preview, apply,
recover, validate, review, and iterate.

### What Becomes Hidden Or Deprecated

Some tools can remain available but should not appear in the default Modeling profile:

- viewport capture/debug tools
- legacy metadata selection tools
- duplicate convenience aliases
- low-level object id tools when selector/module alternatives exist
- expert face/texture helpers that have safer batch alternatives

Hidden tools must stay searchable with `tb_tools_search(detail:"schema")`.

## Expected Source Layout

Current large files mix multiple responsibilities. Medium lightweighting should split
and remove complexity at the same time.

Suggested target layout:

| Current area | Target |
| --- | --- |
| `McpBrushTools.cpp` | split primitive creation, IR apply, route primitives, metadata helpers |
| `McpSelectorTools.cpp` | split selector resolver, module registry, IR tools |
| `McpReviewRenderTools.cpp` | keep in C++, but isolate manifest/contact-sheet helpers |
| `McpObjectTools.cpp` | keep transform/delete/group object operations |
| `McpSelectionViewportTools.cpp` | hide most viewport tools from Modeling profile |
| `McpToolCatalog.cpp` | move repeated schema fragments into smaller helper builders |

Current structural progress:

- `McpSessionState.cpp` owns bounded session data and operation-record serialization.
- `McpToolRegistry.cpp` owns tool registration and dispatch lookup.
- `McpToolCatalogSerialization.cpp` owns catalog JSON and profile statistics.
- IR and history declarations have focused internal headers.
- Session-state and registry contracts have independent Catch2 files and filters.

`McpBrushTools.cpp`, `McpSelectorTools.cpp`, and the remaining broad bridge test still
need deeper domain splits. Do those splits only when they remove shared logic or make a
subsystem independently testable.

File splitting alone does not make MCP lighter. It only improves maintainability. Real
lightweighting comes from moving prefab-like composition into recipes.

## Migration Plan

### Phase 1: Freeze C++ Prefab Growth

Rules:

- Do not add `create_temple`, `create_cottage`, `create_kz_route`, or similar tools.
- New C++ primitives must be generic and useful across several recipes.
- Prefer recipe IR when a request is scene-specific.

Deliverables:

- Update docs and skill workflow.
- Mark current scene-like paths as recipe candidates.
- Add review checks to prevent Modeling profile growth without justification.

Current Phase 1 guardrails:

- The `trenchbroom-mcp-scene-workflow` skill states that scene-specific prefab
  behavior belongs in recipes and IR, not in MCP tools.
- `prefabs_list` and `prefab_create` have been removed from the MCP catalog. Recipe
  discovery belongs to the skill's recipe manifest/listing scripts, and C++ MCP keeps
  only IR preview/apply as the transport boundary.
- Catalog tests reject scene-level tool names such as `create_temple`,
  `create_cottage`, `create_kz_route`, `create_courtyard`, and `create_racetrack`.
  The tests also keep removed prefab placeholders and direct blockout helper names
  out of every tool profile and exact-name search result.
- Existing borderline paths are classified as follows:

| Path | Phase 1 classification |
| --- | --- |
| `blockout_create_batch` typed operations | Keep in MCP as generic atomic/batch geometry. |
| `brush_create_boxes_batch` / `brush_create_polygon_batch` | Keep in MCP as generic batch primitives. |
| `ir_compile_preview_from_file` / `ir_apply_from_file` | Keep in MCP as the recipe transport boundary. |
| `blockout_create_spiral_stairs` | Keep as existing generic stair primitive; do not expand into route/building prefab logic. |
| Legacy `blockout_create_room/corridor/stairs/ramp/doorway/cover/sky_shell` helpers | Direct tool surface removed; batch operation types remain compatibility-only. |
| `python_generate_blockout` | Legacy/script bridge; complex reusable scene composition should move to skill recipe scripts that emit IR files. |
| Temple/courtyard/KZ/route/house/industrial scene layouts | Recipe candidates, not C++ MCP tools. |

### Phase 2: Move Composition To Recipes

Move reusable scene construction into skill scripts:

- `ascending_loop`
- future `cottage_house`
- future `industrial_module`

Each recipe must support:

- `--describe`
- `--validate-only`
- `--params <params.json>`
- `--out <ir.json>`
- deterministic output
- manifest metadata
- minimal/default/stress examples

Current Phase 2 status:

- The `trenchbroom-mcp-scene-workflow` skill contains the retained
  production-style recipe script for `ascending_loop`.
- Each recipe exposes a `MANIFEST` with id, name, version, parameter specs,
  defaults, output parts, expected warnings, and recommended MCP validation tools.
- Each recipe supports `--describe`, `--validate-only`, `--params <params.json>`,
  and `--out <ir.json>`.
- Grouped `minimal`, `default`, and `stress` parameter examples exist for the
  retained recipe.
- `scripts/validate_recipes.py` validates params, builds IR twice to check
  deterministic output, checks metadata coverage, required parts, quality/review
  policies, and can emit a concise markdown report plus generated IR files.
- Recipe scripts only emit IR JSON. They do not call TrenchBroom, MCP, or `trenchbroom`
  directly.
- The canonical skill source is tracked under
  `skills/trenchbroom-mcp-scene-workflow`; sync it to the runtime skill directory
  with `scripts\sync-trenchbroom-mcp-skill.ps1`.

Phase 2 historical validation evidence from the current branch:

- `python skills\trenchbroom-mcp-scene-workflow\scripts\validate_recipes.py`
  validates the retained examples.
- Real Release TB MCP validation used disposable `map_test\unnamed.map` sessions
  and `ir_compile_preview_from_file` / `ir_apply_from_file` for default IR files.
- The earlier `ascending_loop/default` previewed 42 recipe operations as 73 compiled brushes,
  applied 75 objects, recovered module parts, reported `slopeCount=32`, and route
  continuity reported `continuous=true` / `fullWidthContinuous=true`.
- Earlier `temple_courtyard` and `kz_bhop_route` drafts were removed after human
  visual acceptance failed; rebuild them before using those scene families again.
- Crash log count stayed at 17 before and after the real TB recipe validation.

The current recipe contract supersedes that default payload: `ascending_loop`
version 1.1.0 uses 36 segments and explicit `draft` / `balanced` / `smooth`
examples. The validator currently passes all 12 grouped examples deterministically;
every manifest declares `qualityPolicy` and optional `reviewPolicy`.

### Phase 3: Keep MCP As The Execution Kernel

Harden the C++ layer around:

- stable ids
- selector/module consistency
- stale cleanup
- operation validation
- compact responses
- review path clarity
- map/document guards

This phase protects the Agent from wrong-map writes, stale selections, and misleading
visual review.

Current Phase 3 status:

- Mutating tools reject mismatched `expectedDocumentPath` before dispatch and return
  active path, process, bridge, and port diagnostics.
- `McpObjectRegistry` resolves external object ids against the active map and reports
  stale diagnostics instead of treating stale ids as live targets.
- Selector/module state is scoped by document fingerprint so metadata from another map
  is not reused in the active document.
- File-based IR apply is covered as an execution-kernel path: `ir_apply_from_file`
  writes through normal transactions/history, registers module metadata, and the
  resulting module can be recovered with `module_list` and `selector_preview` without
  carrying long object id arrays.
- Existing generated modules can be atomically replaced with exact preview guards
  for IR hash, module revision/content hash, and canonical live objects. One parent
  operation owns undo/redo while child ids remain audit detail.
- Module inspection separates stored aliases from canonical live nodes and reports
  revision, content hash, active operation, duplicate aliases, and stale references.
- Continuity output distinguishes positive gaps, touching, and overlap from legacy
  endpoint distance, and combines walkability with explicit curve quality policy in
  `acceptancePassed`.
- Review tools return `preferredCapturePath` and contact-sheet metadata, and default
  contact sheets include at most two source captures while keeping individual PNGs in
  the manifest.
- Review supports construction edges and projected silhouette-only evidence; it is
  optional and never changes static acceptance.
- Compact `idsMode` responses are covered for create, transform, delete, entity, IR,
  selector, review, and operation flows.
- Real TB validation during Phase 2 exercised the execution kernel with three
  disposable recipe maps through file IR preview/apply, module recovery, selector
  preview, slope/continuity validation, map validation, and review rendering with no
  new crash logs.

### Phase 4: Slim The Default Profile

The Modeling profile should show only the normal path:

- status/open/save/history
- IR preview/apply
- selector/module/group recovery
- transform/delete
- validation
- review
- common atomic create/edit tools

Everything else should be hidden but searchable.

Current Phase 4 status:

- The catalog baseline is 139 tools, 137 implemented tools, and 47
  Modeling-visible implemented tools. Catalog tests lock these values for this branch.
- Default-visible tools keep the normal path: status/open, IR preview/apply from inline
  JSON or file, selector/module/group inspect and review, transform/delete, entity
  checked creation and property edits, common batch geometry, heightmap import/preview,
  geometry/map validation, texture search/apply, and review rendering.
- Lower-frequency recovery and diagnostic tools are hidden from Modeling by default but
  remain searchable by exact name: `map_search`, `history_list`, `operation_select`,
  `module_select`, and `ir_validate`.
- The workflow skill mirrors this split: default flows use `map_snapshot`,
  `history_status`, `operation_inspect`, `selector_preview`, `module_inspect`, and IR
  preview/apply; hidden tools are reserved for diagnostics or manual recovery.

### Phase 5: Remove Or Retire Redundant C++ Convenience

Do not delete immediately. First:

1. Mark as deprecated or expert.
2. Add replacement guidance in tool descriptions.
3. Confirm real scenario runs no longer need the tool.
4. Remove only after compatibility risk is low.

Current Phase 5 status:

- The unused prefab placeholders and legacy direct blockout helper tool surface have
  been deleted from the catalog. Compatibility for old payloads remains in
  `blockout_create_batch` operation types while default discovery is slimmer.
- Catalog descriptions now mark legacy/compatibility/expert paths and point to the
  replacement workflow:
  - legacy blockout batch operation types (`room`, `corridor`, `stairs`, `ramp`,
    `doorway`, `cover`, `sky_shell`) are described as compatibility/quick-sketch
    payloads; new composition should use recipes/IR or explicit primitives.
  - `python_generate_blockout` points to skill recipe scripts that write IR files and
    `ir_compile_preview_from_file` / `ir_apply_from_file`.
  - low-level deletion/recovery helpers (`objects_delete_by_filter`,
    `objects_delete_by_operation`, `operation_select`, `history_list`) point to
    selector/module/operation inspect/status workflows.
  - legacy metadata and route aliases continue to point to structured selectors,
    modules, `geometry_analyze_slopes`, and `geometry_analyze_route_continuity`.
- Catalog tests enforce that the removed direct helper names no longer appear in
  Full/profile/search output, and that `blockout_create_batch` carries the
  compatibility/recipe guidance for legacy operation types.
- Real Phase 2 and Phase 4 TB scenario runs completed through the recipe/IR and
  default Modeling paths without relying on the removed direct helper surface, which
  satisfies the removal gate for this iteration.

Further removal of compatibility payload support remains intentionally deferred. A
batch operation type should only be deleted after old IR/workflows have migrated and
no known external client depends on the old payload shape.

## Size Reduction Strategy

### High-Value Reductions

| Reduction | Expected impact |
| --- | ---: |
| Move scene composition to recipes | high |
| Stop adding C++ prefab-like tools | high |
| Consolidate repeated id/output helpers | medium |
| Hide legacy/debug tools from Modeling profile | medium UX impact, low source impact |
| Split large files by responsibility | medium maintenance impact |
| Remove deprecated convenience tools later | medium |

### Low-Value Reductions

| Reduction | Why not enough |
| --- | --- |
| File splitting only | Same code, just smaller files |
| Removing schema descriptions | Saves little and hurts Agent use |
| Moving validation out of C++ | Loses access to reliable map geometry |
| Moving review out of C++ | Loses deterministic isolated review |
| Forcing all edits through Python | Duplicates plugin work and bypasses undo/document safety |

## Capability Matrix

| Capability | After medium lightweighting |
| --- | --- |
| MCP standalone simple editing | kept |
| MCP complex scene generation | moved to skill recipe |
| Safe map mutation | kept in C++ |
| Undo/redo/history | kept in C++ |
| Selector/module recovery | kept in C++ |
| Native group organization | kept in C++ |
| IR file apply | kept in C++ |
| Route/slope validation | kept in C++ |
| Review screenshots | kept in C++ |
| KZ/domain difficulty judgment | skill |
| Temple/cottage/route layout planning | recipe |
| Human UI prefab workflows | future `trenchbroom` reuse of recipes |

## Risks

### Risk: MCP Becomes Too Weak

If too much moves out, the Agent loses reliable editor facts.

Mitigation:

- Keep validation, review, selector, module, group, history, and safe mutation in C++.
- Recipes only generate IR. They do not replace MCP.

### Risk: Skill Recipes Duplicate `trenchbroom`

The `trenchbroom` plugin is for human interactive UI. Skill recipes are for Agent generation.

Mitigation:

- Put shared prefab logic in recipe scripts that emit IR.
- Let future `trenchbroom` prefab UI reuse those scripts instead of rewriting layout logic.

### Risk: More Layers Make Debugging Harder

More layers can obscure failures.

Mitigation:

- Every recipe output must be inspectable as IR.
- `ir_compile_preview_from_file` must explain counts, bounds, parts, and warnings.
- MCP validation must say whether failure belongs to IR, map state, selector, or review.

### Risk: Existing Tools Break

Agents or users may depend on current tools.

Mitigation:

- Hide before removing.
- Keep exact-name search.
- Add replacement text.
- Use real scenario regression before deleting anything.

## Acceptance Criteria

Medium lightweighting is successful when:

- `lib/TbUiLib/src/mcp` is near **550-650 KB** without losing the core execution path.
- Modeling profile is short enough for normal Agent use.
- Complex scenes are generated through skill recipes and IR files.
- MCP can still work alone for simple map edits.
- No normal workflow requires carrying hundreds of object ids.
- Selector/module/group recovery remains stable after create, transform, delete, undo,
  redo, and grouping.
- Route and slope validation catch missing slopes, wrong direction, vertical steps, and
  unintended gaps.
- Review contact sheets remain readable by default.
- Hidden tools remain searchable.
- Real TB disposable-map scenario runs pass without new crash logs or wrong-map writes.

## Test Plan

### Unit And Catalog Tests

Run:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TbUiLibTest -TestFilter "[McpBridgeServer]"
powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TbMcpLibTest -TestExe build-release-codex\lib\TbMcpLib\test\TbMcpLibTest.exe -TestFilter "McpToolCatalog"
powershell -ExecutionPolicy Bypass -File scripts\mcp-reliability-acceptance.ps1
```

Required coverage:

- selector/module/operation round trips
- stale cleanup after delete and undo
- group creation and transform
- IR preview/apply from file
- idsMode compact output
- route/slope validation summary and full modes
- review contact sheet defaults
- hidden tool search

### Recipe Tests

Run each recipe in:

- minimal
- default
- stress

Check:

- deterministic output
- valid JSON
- metadata coverage
- preview counts and bounds
- expected warnings

### Real TB Acceptance

Run retained and rebuilt disposable scenes:

1. Ascending spiral or road route.
2. Dense building whitebox after a rebuilt recipe exists.
3. KZ/bhop/slide route after a rebuilt recipe exists.

Each run must record:

- `tb_status`
- IR preview/apply operation ids
- `module_inspect`
- `selector_preview`
- `operation_validate`
- slope/continuity validation when relevant
- `map_validate(groupByType:true)`
- review preferred capture path
- crash log count before and after

## Decision Rule For New Work

Use this rule before adding a C++ MCP tool:

1. If it is scene-specific, put it in a skill recipe.
2. If it is a reusable geometry primitive that several recipes need, C++ MCP may own it.
3. If it needs undo, document guard, selection state, object identity, map validation, or
   review rendering, C++ MCP should own it.
4. If it is gameplay, style, layout, or difficulty judgment, skill should own it.
5. If the response cannot be compact by default, redesign the output before adding the
   tool.

When unsure, start in a recipe. Promote to C++ only after repeated real workflows prove
that it is a generic editor primitive or validator.

## Final Shape

The final shape should feel like this:

- MCP can still build and edit simple scenes by itself.
- Skill recipes make complex scenes faster and more consistent.
- C++ stays close to TrenchBroom's map, undo, object, and review systems.
- Python stays close to generation strategy and reusable scene recipes.
- Agents see fewer tools, shorter outputs, clearer screenshots, and better recovery
  after edits.

That is the useful lightweight direction: a smaller C++ kernel, not a weaker system.
