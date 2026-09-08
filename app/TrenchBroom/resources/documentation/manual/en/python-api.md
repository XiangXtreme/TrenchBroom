# Python API {#python_api}

Import the public API with `import trenchbroom as tb`. It is shared by the Python
console, manifest plugins, and MCP Python execution. `tb_api` exposes the exact loaded
signature, default values, return type, property writability, and effect for every public
symbol; the distributed `trenchbroom.pyi` provides editor completion.

## Documents and selection {#python_api_documents}

```python
doc = tb.documents.current()
selection = doc.selection
```

`documents` owns map lifecycle and persistence. `documents.open(path)` verifies the opened
document, `documents.save(path=None)` saves the active one, and `documents.snapshot()`
returns a compact map summary.

Use `Selection` for queries and edits to the current selection. Its `entities`,
`all_entities`, `brushes`, and `brush_faces` properties return live handles. `set`, `add`,
and `clear` change the selection. `translate(offset)`, `rotate(axis, angle_degrees, center=None)`,
and `scale(factors, center=None)` use map units, axis-angle degrees, and a
bounds-center default. `duplicate()` retains the editor behavior of selecting the copies.

## Explicit object edits {#python_api_objects}

Use `objects.translate(targets, offset)`, `objects.rotate(targets, axis, angle_degrees, center=None)`,
`objects.scale(targets, factors, center=None)`,
`objects.duplicate(targets, select=False)`, and `objects.delete(targets)` when the target
is known. These operations preserve an unrelated selection. Targets are one `Entity` or
`Brush`, or an iterable; duplicate values and descendants of another target are processed
only once.

Positions, vectors, axes, and centers accept `tb.Vec3` or finite `(x, y, z)` values.
Scaling accepts a finite scalar or a finite three-value factor. Input-shape errors raise
`TypeError`, invalid finite values or handles raise `ValueError`, and native edit failures
raise `RuntimeError` without committing a partial edit.

## Creation and domains {#python_api_domains}

`brushes.create`, `create_box`, `create_boxes`, `create_prism`, and `create_prisms` create
convex geometry. `entities.create` creates ordinary entities; `entities.create_from_schema`
and `create_from_schema_batch` also validate the game definition. Creation preserves the
current selection unless `select=True` is supplied.

`entities.update_many(entities, properties, remove_keys=...)` applies property changes in
one call. `entities.definitions()` lists game definitions. `faces` and `materials` own
surface and UV operations; UV loop coordinates are texture pixels, not normalized values.
`actions.list()` and `actions.execute()` expose native actions. `history` owns undo and
redo, and `viewport` owns action-mode camera changes.

## Handles and transactions {#python_api_handles}

Handles are live editor references. Reloading or closing a document and deleting a node
invalidates dependent handles; reacquire them from the current document. Use
`with doc.transaction("Description"):` to group an ordinary script edit into one undo step.

The previous top-level editing names and camelCase aliases were removed. See the project
Python API migration guide for direct replacements.
