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
