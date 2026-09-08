# Python API migration

The API is intentionally breaking in this release. Repository scripts use the names
below; update user plugins in the same way.

| Removed | Replacement |
| --- | --- |
| `current_document()` / `document()` | `documents.current()` |
| top-level selected-object queries / `objects.selection()` | `documents.current().selection` |
| top-level transform, duplicate, delete, deselect functions | `Selection` for the current selection, `objects` for explicit handles |
| `create_brush()` | `brushes.create()` |
| `list_actions()` / `execute_action()` | `actions.list()` / `actions.execute()` |
| `documents.open_verified()` | `documents.open()` |
| `documents.save_current()` / `objects.snapshot()` | `documents.save()` / `documents.snapshot()` |
| `entities.create_checked()` / `create_checked_batch()` | `entities.create_from_schema()` / `create_from_schema_batch()` |
| `entities.entities_list()` | `entities.definitions()` |
| `entities.properties_update()` / `properties_delete()` | `entities.update_many(..., remove_keys=...)` |
| `brushes.create_boxes_batch()` / `create_polygon_batch()` | `brushes.create_boxes()` / `create_prisms()` |

Old Euler rotation calls should be converted to an axis-angle rotation or to consecutive
`Selection.rotate` calls. Explicit object operations retain the current selection unless
`objects.duplicate(..., select=True)` is requested.
