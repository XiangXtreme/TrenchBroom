import math

import trenchbroom as tb


def dot(a, b):
    return a.x * b.x + a.y * b.y + a.z * b.z


def cross(a, b):
    return tb.Vec3(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    )


def length(v):
    return math.sqrt(dot(v, v))


def normalize(v, fallback):
    value = length(v)
    return fallback if value == 0 else v / value


class CurveSweepTool:
    def __init__(self):
        self.panel = tb.create_plugin_panel("Curve Sweep")
        self.points = []
        self.preview = None
        self.preview_values = None
        self.timer = tb.set_interval(self.tick, 150)
        self.build_ui()
        self.refresh("Record path points from selected vertex handles.")

    def build_ui(self):
        self.panel.clear()
        self.panel.add_label_named("status", "")
        self.panel.add_float_field("width", "Width", 64.0, 1.0, 4096.0, 1, 8.0)
        self.panel.add_float_field("height", "Height", 32.0, 1.0, 4096.0, 1, 8.0)
        self.panel.add_text_field("material", "Material", "clip")
        self.panel.add_button_callback("Record Path From Vertex Handles", self.record_path)
        self.panel.add_button_callback("Preview Sweep", self.preview_sweep)
        self.panel.add_button_callback("Apply Sweep", self.apply_sweep)
        self.panel.add_button_callback("Cancel Preview", self.cancel_preview)

    def refresh(self, message):
        self.panel.set_label_text("status", f"{message}\nPath points: {len(self.points)}")

    def record_path(self):
        self.points = list(tb.documents.current().vertex_tool_vertices())
        self.refresh("Path recorded." if len(self.points) >= 2 else "Need at least 2 points.")

    def values(self):
        return (
            float(self.panel.get_float_field("width")),
            float(self.panel.get_float_field("height")),
            self.panel.get_text_field("material") or "clip",
        )

    def frame_at(self, index):
        prev_point = self.points[max(0, index - 1)]
        point = self.points[index]
        next_point = self.points[min(len(self.points) - 1, index + 1)]
        tangent = normalize(next_point - prev_point, tb.Vec3(1, 0, 0))
        up = tb.Vec3(0, 0, 1)
        if abs(dot(tangent, up)) > 0.95:
            up = tb.Vec3(0, 1, 0)
        side = normalize(cross(up, tangent), tb.Vec3(0, 1, 0))
        normal = normalize(cross(tangent, side), up)
        return side, normal

    def section(self, index, width, height):
        point = self.points[index]
        side, normal = self.frame_at(index)
        return [
            point + side * (-width * 0.5),
            point + side * (width * 0.5),
            point + normal * height,
        ]

    def build_sweep(self):
        if len(self.points) < 2:
            self.refresh("Need at least 2 path points.")
            return False

        width, height, material = self.values()
        sections = [self.section(i, width, height) for i in range(len(self.points))]
        for i in range(len(sections) - 1):
            tb.brushes.create(sections[i] + sections[i + 1], material)
        self.refresh(f"Built {len(sections) - 1} connected brush segments.")
        return True

    def cancel_preview(self):
        if self.preview is not None:
            self.preview.cancel()
            self.preview = None
        self.preview_values = None
        self.refresh("Preview cancelled.")

    def preview_sweep(self):
        self.cancel_preview()
        self.preview = tb.documents.current().transaction("Python API: Curve Sweep Preview")
        self.preview.__enter__()
        self.preview_values = self.values()
        if not self.build_sweep():
            self.cancel_preview()

    def apply_sweep(self):
        if self.preview is not None:
            self.preview.commit()
            self.preview = None
            self.preview_values = None
            self.refresh("Preview applied.")
            return

        with tb.documents.current().transaction("Python API: Curve Sweep"):
            self.build_sweep()

    def tick(self):
        if self.preview is None:
            return
        values = self.values()
        if values == self.preview_values:
            return
        self.preview_sweep()


_tool = CurveSweepTool()
