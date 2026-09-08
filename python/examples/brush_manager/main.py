import trenchbroom as tb


PANEL_TITLE = "Brush Manager"
MATERIAL_PRESETS = ["common/caulk", "common/nodraw", "common/weapclip", "common/trigger"]


class BrushManager:
    def __init__(self):
        self.panel = tb.create_plugin_panel(PANEL_TITLE)
        self.build_ui()

    def build_ui(self):
        self.panel.clear()
        self.panel.add_label("Generator")
        self.panel.add_int_field("cube_size", "Size", 64, 16, 512)
        self.panel.add_float_field("offset_x", "X", 0.0, -4096.0, 4096.0, 0, 16.0)
        self.panel.add_float_field("offset_y", "Y", 0.0, -4096.0, 4096.0, 0, 16.0)
        self.panel.add_float_field("offset_z", "Z", 0.0, -4096.0, 4096.0, 0, 16.0)
        self.panel.add_button_callback("Create Cube", self.create_cube)

        self.panel.add_label("Modifier")
        self.panel.add_combo_box("material", "Material", MATERIAL_PRESETS)
        self.panel.add_float_field("tex_scale", "Scale", 1.0, 0.1, 10.0, 2, 0.1)
        self.panel.add_button_callback("Apply to Selection", self.apply_material)

        self.panel.add_label("Info")
        self.panel.add_label_named("status", "Ready")
        self.panel.add_button_callback("Analyze Selection", self.analyze_selection)

    def log(self, message):
        print(f"[{PANEL_TITLE}] {message}")
        self.panel.set_label_text("status", message)

    def create_cube(self):
        size = float(self.panel.get_int_field("cube_size"))
        offset = tb.Vec3(
            self.panel.get_float_field("offset_x"),
            self.panel.get_float_field("offset_y"),
            self.panel.get_float_field("offset_z"),
        )
        half = size / 2.0
        points = [
            offset + tb.Vec3(-half, -half, -half),
            offset + tb.Vec3(half, -half, -half),
            offset + tb.Vec3(half, half, -half),
            offset + tb.Vec3(-half, half, -half),
            offset + tb.Vec3(-half, -half, half),
            offset + tb.Vec3(half, -half, half),
            offset + tb.Vec3(half, half, half),
            offset + tb.Vec3(-half, half, half),
        ]

        doc = tb.documents.current()
        with doc.transaction("Python API: Create Cube"):
            brush = tb.brushes.create(
                points, self.panel.get_combo_box_text("material"), select=True
            )
        if brush:
            self.log(f"Created cube at {offset}")
        else:
            self.log("Failed to create brush.")

    def selected_brushes(self):
        selection = tb.documents.current().selection
        brushes = list(selection.brushes)
        if brushes:
            return brushes
        for entity in selection.all_entities:
            brushes.extend(entity.brushes)
        return brushes

    def apply_material(self):
        material = self.panel.get_combo_box_text("material")
        scale = float(self.panel.get_float_field("tex_scale"))
        count = 0
        doc = tb.documents.current()

        with doc.transaction("Python API: Apply Material"):
            for brush in self.selected_brushes():
                for face in brush.faces():
                    face.texture_name = material
                    face.scale = (scale, scale)
                    count += 1

        self.log(f"Updated {count} faces.")

    def analyze_selection(self):
        selection = tb.documents.current().selection
        brushes = self.selected_brushes()
        face_count = sum(len(brush.faces()) for brush in brushes)
        self.log(
            f"Selected: {len(selection.all_entities)} entities, "
            f"{len(selection.brushes)} explicit brushes, {face_count} faces."
        )


_manager = BrushManager()
