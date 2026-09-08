# TrenchBroom Python API

`trenchbroom` is the public editing API for the Python console, manifest plugins, and
MCP Python execution. Import it as `import trenchbroom as tb`.

The authoritative signatures, defaults, writable properties, return values, and effects
come from the loaded binding. Use MCP `tb_api` to query that binding, or generate the
distributed stub with `DumpPythonApi`. This document intentionally does not duplicate a
static signature list.

## Editing model

Start at the active document:

```python
doc = tb.documents.current()
selection = doc.selection
```

Use `Selection` for the current selection. It exposes selected entities, brushes, and
faces and supports `set`, `add`, `clear`, `translate`, `rotate`, `scale`, and
`duplicate`. `Selection.duplicate()` keeps the native behavior of selecting the copies.

Use `objects` when the targets are explicit handles. `objects.translate`, `rotate`,
`scale`, `duplicate`, and `delete` never change an unrelated selection. They accept one
`Entity` or `Brush`, or an iterable; repeated and descendant targets are normalized to
their top-level objects. `objects.duplicate(..., select=False)` returns every cloned
object, including mixed entity and brush targets.

All three-dimensional values accept `tb.Vec3` or a finite `(x, y, z)` sequence. Rotation
uses an axis and degrees. Scaling accepts a finite scalar or a finite three-component
factor. Omitted centers use the bounds center of the selected or explicit targets.

Creation is non-selecting by default. Pass `select=True` to `brushes.create`,
`brushes.create_box`, `brushes.create_boxes`, `brushes.create_prism`,
`brushes.create_prisms`, `entities.create`, or `entities.create_from_schema` when the
result should become selected.

## Domains

`documents` owns document lifecycle, persistence, and snapshots. `entities`, `brushes`,
`faces`, and `materials` own their respective map domains. `actions` lists and runs
native actions; `history` owns undo and redo; `viewport` owns camera actions. Entity
creation through `entities.create` stays distinct from validation against the game
definition via `entities.create_from_schema`.

Map mutations use native transactions and undo. A `TypeError` indicates an unsupported
input shape, `ValueError` indicates invalid finite values or handles, and `RuntimeError`
indicates a native edit or guarded execution failure.

## Migration

See [Python API migration](python-api-migration.md) for removed names and their direct
replacements. User-installed plugins are not rewritten automatically.
