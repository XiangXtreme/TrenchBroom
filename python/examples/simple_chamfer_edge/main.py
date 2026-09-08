import trenchbroom as tb


distance = 8.0
doc = tb.documents.current()

with doc.transaction(f"Python API: Chamfer Edges ({distance})"):
    ok = doc.selection.chamfer_edges(distance)

if ok:
    print("Chamfer complete")
else:
    print("Chamfer failed: select edge handles in the edge tool and try again")
