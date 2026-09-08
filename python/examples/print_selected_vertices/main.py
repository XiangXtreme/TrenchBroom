import trenchbroom as tb


doc = tb.documents.current()
vertices = doc.vertex_tool_vertices()
print(f"selected vertex handles: {len(vertices)}")

for index, vertex in enumerate(vertices):
    print(f"{index}: {vertex.x} {vertex.y} {vertex.z}")

brush_vertices = doc.selection.brush_vertices()
print(f"selected brushes: {len(brush_vertices)}")

for brush_index, vertices in enumerate(brush_vertices):
    print(f"brush[{brush_index}] vertices: {len(vertices)}")
    for vertex_index, vertex in enumerate(vertices):
        print(f"  v[{vertex_index}]: ({vertex.x}, {vertex.y}, {vertex.z})")
