import trenchbroom as tb


panel = tb.create_plugin_panel("Plane & Selection Demo")
panel.clear()

panel.add_label("<b>Plane</b>")
panel.add_label_named("plane_info", "Select 3 vertex handles, then create a plane.")


def create_plane_from_vertices():
    doc = tb.documents.current()
    vertices = doc.vertex_tool_vertices()
    if len(vertices) < 3:
        panel.set_label_text("plane_info", "Need at least 3 selected vertex handles.")
        return

    plane = tb.Plane.from_points(vertices[0], vertices[1], vertices[2])
    panel.set_label_text(
        "plane_info",
        "normal=({:.2f}, {:.2f}, {:.2f}), dist={:.2f}".format(
            plane.normal.x, plane.normal.y, plane.normal.z, plane.dist
        ),
    )
    print(plane)


panel.add_button_callback("Create Plane From Vertex Handles", create_plane_from_vertices)

panel.add_label("<b>Selection</b>")
panel.add_label_named("selection_info", "No brush vertices inspected.")


def inspect_selected_brush_vertices():
    doc = tb.documents.current()
    brush_vertices = doc.selection.brush_vertices()
    total_vertices = sum(len(vertices) for vertices in brush_vertices)
    panel.set_label_text(
        "selection_info",
        f"Brushes: {len(brush_vertices)}, vertices: {total_vertices}",
    )
    print(f"Selected brushes: {len(brush_vertices)}")
    for brush_index, vertices in enumerate(brush_vertices):
        print(f"brush[{brush_index}] vertices: {len(vertices)}")


panel.add_button_callback("Inspect Selected Brush Vertices", inspect_selected_brush_vertices)
