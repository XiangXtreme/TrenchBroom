---
name: trenchbroom-mcp-scene-workflow
description: Use for TrenchBroom MCP scene editing. Inspect the running editor, discover the public Python API, compose trusted Python, and capture viewport evidence.
---

# TrenchBroom MCP Scene Workflow

## Contract

TrenchBroom MCP has exactly four public entry points:

- `tb_inspect`: bounded document, map, selection, and native problem facts.
- `tb_api`: discovery of actual `trenchbroom` Python symbols.
- `tb_execute_python`: trusted Python composition over the public API.
- `tb_capture`: a capture of the current viewport.

Read-only mode exposes `tb_inspect`, `tb_api`, and `tb_capture`. Edit adds
`tb_execute_python`. Off exposes no tools. Do not call or search for profiles,
IR, modules, selectors, operation history, review renderers, or any retired
MCP tool name. Those are not aliases and have no compatibility path.

The implementation contract is in `docs/mcp-development-governance.md` and
`docs/mcp-python-migration/development.md`. This skill guides scene work; it
does not extend the MCP catalog.

## Workflow

1. Call `tb_inspect` with `view:"document"`, then use `view:"map"`,
   `view:"selection"`, or `view:"problems"` only when needed. Record the
   returned document fingerprint and, for saved maps, its path.
2. Call `tb_api` before using a Python symbol that is not already known. Use
   `symbol` for an exact name or a short `query` for discovery.
   Search responses include `total`, `truncated`, and `nextOffset`. Continue with
   that `offset` and the same query; `limit` is 1–50 (default 8), with a 16 KiB
   page budget. Qualified names start with `trenchbroom.`. `signature` comes from
   the native binding; properties report `writable`, and `effect` distinguishes
   read, edit, action, plugin, and value operations. Action requires MCP action
   mode; plugin operations belong to persistent plugins, not transient scripts.
3. Compose ordinary Python with `import trenchbroom as tb`. Put loops,
   geometry composition, entity policy, validation, and undo/redo calls in the
   script, using only symbols returned by `tb_api`.
4. Execute with `tb_execute_python`, a fresh `executionId`, `document` guard,
   and `mode:"transaction"` for normal edits. Use `mode:"action"` only for
   documented lifecycle or native history operations. Read the receipt before
   retrying: timeout or failure after code begins is not automatically safe to
   replay.
5. Inspect map facts and native problems again. Use `tb_capture` for visual
   evidence when it helps; a screenshot does not prove map validity, BSP
   compilation, collision, or gameplay.

## Safety

- The API runs on the editor main thread with fresh Python globals. Never ask
  it to create persistent callbacks, timers, panels, nested MCP requests, or
  background editor work.
- Keep source under 256 KiB. The default cooperative budget is 30 seconds and
  the maximum is 90 seconds. A blocking native call cannot be force-cancelled.
- Use the fingerprint for every execution. Saved documents also require the
  inspected path. The target does not follow an active-window change.
- Transaction failures roll back the map and selection where native operations
  permit it, but cannot undo external file or process side effects.
- Report map facts, problem output, capture evidence, save status, and any
  untested engine behavior separately.
- Entity handles survive property changes. Removed/reparented objects and
  reloads can invalidate handles; reacquire them from their current owner.
  Never select worldspawn. Edit its properties directly. Handles used from
  background threads or a different guarded document are rejected.
- After opening or activating another document, inspect it and start a new
  guarded execution before editing its contents. Document ids and paths can
  be read to identify a target, but the current execution stays bound to its
  original document.
- Receipts include bounded stdout/stderr previews. Deduplication retains up to
  1024 request identities and 16 MiB of serialized receipt payloads. A retained
  identity whose payload expired returns `receipt_expired` instead of running
  again. After eviction, restart, timeout, or disconnection, inspect the editor
  before deciding whether another execution is appropriate.

## Viewport Evidence

Discover `trenchbroom.viewport.state`, `set_camera`, and `focus_selection` with
`tb_api`. `state()` reads the current camera. In action mode,
`tb.viewport.set_camera(position, target, up=(0, 0, 1))` activates the native 3D
view and applies the pose synchronously. Position and target must differ, and
up must not be parallel to their direction. `focus_selection()` immediately
frames the current selection in 3D. These actions stop the previous camera
animation and report their completion; they do not modify map geometry.

Call `tb_capture` afterward. Its response includes the actual camera state and
document path, so an orthographic capture cannot be mistaken for a 3D view.

## Python Shape

```python
import trenchbroom as tb

# Discover concrete symbols with tb_api before relying on this outline.
# Use tb's document, object, brush, entity, material, CSG, history, and
# validation APIs directly. Return compact JSON-compatible data in `result`.
result = {"status": "completed"}
```

Do not generate old MCP JSON, recipes, IR, profile settings, or compatibility
wrappers. A missing public Python primitive is feedback for a generic native API
binding, not a reason to restore a specialized MCP tool.
