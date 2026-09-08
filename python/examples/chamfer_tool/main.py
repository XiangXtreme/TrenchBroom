import trenchbroom as tb


class ChamferTool:
    def __init__(self):
        self.panel = tb.create_plugin_panel("Chamfer Tool")
        self.panel.clear()
        self.panel.add_label_named("status", "Status: Ready")
        self.panel.add_float_field("distance", "Distance", 8.0, 0.0, 100000.0, 2, 1.0)
        self.panel.add_int_field("segments", "Segments", 1, 1, 64)
        self.panel.add_button_callback("Chamfer Edge Handles", self.chamfer_edges)
        self.panel.add_button_callback("Chamfer Vertex Handles", self.chamfer_vertices)

    def _distance(self):
        return float(self.panel.get_float_field("distance"))

    def _segments(self):
        return int(self.panel.get_int_field("segments"))

    def _set_status(self, text):
        self.panel.set_label_text("status", f"Status: {text}")

    def chamfer_edges(self):
        doc = tb.documents.current()
        with doc.transaction("Python API: Chamfer Edges"):
            ok = doc.selection.chamfer_edges(self._distance(), self._segments())
        self._set_status("Edges chamfered" if ok else "No selected edge handles")

    def chamfer_vertices(self):
        doc = tb.documents.current()
        with doc.transaction("Python API: Chamfer Vertices"):
            ok = doc.selection.chamfer_vertices(self._distance())
        self._set_status("Vertices chamfered" if ok else "No selected vertex handles")


_tool = ChamferTool()
