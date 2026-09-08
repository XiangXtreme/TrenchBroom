import trenchbroom as tb


doc = tb.documents.current()
panel = tb.create_plugin_panel("Plane Tools")
panel.clear()
panel.add_label("Duplicate the current selection along X, then rotate around the first selected vertex handle.")

selection = doc.selection

if len(selection.all_entities) > 0:
    with doc.transaction("Python API: build plane row"):
        for _ in range(9):
            selection.duplicate()
            selection.translate((128, 0, 0))

vertices = doc.vertex_tool_vertices()
if len(vertices) > 0:
    pivot = vertices[0]
    with doc.transaction("Python API: rotate around vertex"):
        selection.rotate((0, 0, 1), 15, center=pivot)
