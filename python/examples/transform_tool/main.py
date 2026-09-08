import trenchbroom as tb


class TransformTool:
    def __init__(self):
        self.panel = tb.create_plugin_panel("Transform Tool")
        self.pivot = None
        self.pivot_mode = "first"
        self.require_explicit_entities = False
        self.message = "Ready"
        self.build_ui()
        self.refresh_status()

    def build_ui(self):
        self.panel.clear()
        self.panel.add_label_named("status", "")
        self.panel.add_button_callback("Record Pivot From Vertex Handles", self.record_pivot)
        self.panel.add_button_callback("Toggle Pivot Mode", self.toggle_pivot_mode)
        self.panel.add_button_callback("Toggle Explicit Entity Requirement", self.toggle_target_mode)

        self.panel.add_float_field("chamfer_dist", "Chamfer Distance", 8.0, 0.0, 100000.0, 2, 1.0)
        self.panel.add_button_callback("Chamfer Selected Vertex Handles", self.chamfer_vertices)

        self.panel.add_int_field("dup_count", "Duplicate Count", 10, 1, 999)
        self.panel.add_float_field("step_x", "Step X", 128.0, -100000.0, 100000.0, 2, 1.0)
        self.panel.add_float_field("step_y", "Step Y", 0.0, -100000.0, 100000.0, 2, 1.0)
        self.panel.add_float_field("step_z", "Step Z", 0.0, -100000.0, 100000.0, 2, 1.0)
        self.panel.add_float_field("axis_x", "Axis X", 0.0, -1.0, 1.0, 3, 0.1)
        self.panel.add_float_field("axis_y", "Axis Y", 0.0, -1.0, 1.0, 3, 0.1)
        self.panel.add_float_field("axis_z", "Axis Z", 1.0, -1.0, 1.0, 3, 0.1)
        self.panel.add_float_field("rotate_deg", "Rotate Degrees", 15.0, -360.0, 360.0, 2, 1.0)
        self.panel.add_button_callback("Apply Duplicate + Rotate", self.apply_duplicate_rotate)

    def status_text(self):
        lines = ["Transform Tool"]
        if self.pivot is None:
            lines.append("- Pivot: <not recorded>")
        else:
            lines.append(f"- Pivot: ({self.pivot.x:.2f}, {self.pivot.y:.2f}, {self.pivot.z:.2f})")
        lines.append(f"- Pivot mode: {self.pivot_mode}")
        target = "explicit entities only" if self.require_explicit_entities else "any selection"
        lines.append(f"- Target: {target}")
        lines.append(f"- Status: {self.message}")
        return "\n".join(lines)

    def refresh_status(self, message=None):
        if message is not None:
            self.message = message
        self.panel.set_label_text("status", self.status_text())

    def record_pivot(self):
        vertices = tb.documents.current().vertex_tool_vertices()
        if not vertices:
            self.refresh_status("No selected vertex handles")
            return
        if self.pivot_mode == "average":
            total = tb.Vec3(0, 0, 0)
            for vertex in vertices:
                total = total + vertex
            self.pivot = total / len(vertices)
        else:
            self.pivot = vertices[0]
        self.refresh_status("Pivot recorded")

    def toggle_pivot_mode(self):
        self.pivot_mode = "average" if self.pivot_mode == "first" else "first"
        self.refresh_status("Pivot mode updated")

    def toggle_target_mode(self):
        self.require_explicit_entities = not self.require_explicit_entities
        self.refresh_status("Target mode updated")

    def has_target_selection(self, selection):
        if self.require_explicit_entities:
            return len(selection.entities) > 0
        return len(selection.all_entities) > 0

    def chamfer_vertices(self):
        doc = tb.documents.current()
        distance = float(self.panel.get_float_field("chamfer_dist"))
        with doc.transaction("Python API: Chamfer Vertex Handles"):
            ok = doc.selection.chamfer_vertices(distance)
        self.refresh_status("Chamfer complete" if ok else "No selected vertex handles")

    def apply_duplicate_rotate(self):
        if self.pivot is None:
            self.refresh_status("Record a pivot first")
            return

        doc = tb.documents.current()
        selection = doc.selection
        if not self.has_target_selection(selection):
            self.refresh_status("Selection is empty")
            return

        duplicate_count = int(self.panel.get_int_field("dup_count"))
        step_x = float(self.panel.get_float_field("step_x"))
        step_y = float(self.panel.get_float_field("step_y"))
        step_z = float(self.panel.get_float_field("step_z"))
        axis_x = float(self.panel.get_float_field("axis_x"))
        axis_y = float(self.panel.get_float_field("axis_y"))
        axis_z = float(self.panel.get_float_field("axis_z"))
        rotate_deg = float(self.panel.get_float_field("rotate_deg"))

        with doc.transaction("Python API: Duplicate And Rotate"):
            for _ in range(duplicate_count):
                selection.duplicate()
                selection.translate((step_x, step_y, step_z))
                selection.rotate(
                    (axis_x, axis_y, axis_z), rotate_deg, center=self.pivot
                )

        self.refresh_status("Duplicate + rotate complete")


_tool = TransformTool()
