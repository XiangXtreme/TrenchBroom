import trenchbroom as tb


panel = tb.create_plugin_panel("Vec3 & Color Demo")
panel.clear()

panel.add_label("<b>Vector Operations</b>")
panel.add_label("Vector A:")
panel.add_float_field("ax", "X", 1.0)
panel.add_float_field("ay", "Y", 0.0)
panel.add_float_field("az", "Z", 0.0)

panel.add_label("Vector B:")
panel.add_float_field("bx", "X", 0.0)
panel.add_float_field("by", "Y", 1.0)
panel.add_float_field("bz", "Z", 0.0)


def vec(prefix):
    return tb.Vec3(
        panel.get_float_field(prefix + "x"),
        panel.get_float_field(prefix + "y"),
        panel.get_float_field(prefix + "z"),
    )


def set_result(text):
    panel.set_label_text("result", text)


panel.add_button_callback(
    "Calculate Dot (A . B)", lambda: set_result(f"Dot Product: {vec('a').dot(vec('b'))}")
)
panel.add_button_callback(
    "Calculate Cross (A x B)",
    lambda: set_result(f"Cross Product: {vec('a').cross(vec('b'))}"),
)
panel.add_button_callback("Calculate Length (A)", lambda: set_result(f"Length A: {vec('a').length()}"))


def normalize_a():
    value = vec("a")
    if value.length() == 0:
        set_result("Result: Zero vector")
    else:
        set_result(f"Normalized A: {value.normalize()}")


panel.add_button_callback("Calculate Normalize (A)", normalize_a)
panel.add_label_named("result", "Result: ")

panel.add_label("<b>Color</b>")
panel.add_color_field("color", "Select Color", (255, 0, 0))


def apply_color():
    color = panel.get_color_field("color")
    color_str = f"{color[0] / 255.0} {color[1] / 255.0} {color[2] / 255.0}"
    doc = tb.documents.current()
    if not doc.selection.all_entities:
        panel.set_label_text("status", "Status: No entities selected")
        return

    with doc.transaction("Apply Color"):
        doc.selection.set_property("_color", color_str)

    panel.set_label_text("status", f"Status: Applied color {color_str}")


panel.add_button_callback("Apply Color to Selection (_color)", apply_color)
panel.add_label_named("status", "Status: Ready")
