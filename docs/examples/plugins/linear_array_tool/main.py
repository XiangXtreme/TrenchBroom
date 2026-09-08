"""
Linear Array Generator Plugin for TrenchBroom (trenchbroom API)
Demonstrates creating a UI panel on the Plugins inspector page,
reading input widgets, and performing safe undoable transformations.
"""

import trenchbroom as tb

panel = None

def generate_array():
    doc = tb.documents.current()
    if not doc.selection.brushes and not doc.selection.entities:
        panel.set_label_text("status", "Status: Please select at least one brush or entity.")
        return

    count = panel.get_int_field("count")
    dx = panel.get_float_field("dx")
    dy = panel.get_float_field("dy")
    dz = panel.get_float_field("dz")

    if count < 1:
        panel.set_label_text("status", "Status: Count must be at least 1.")
        return

    with doc.transaction(f"Linear Array ({count} copies)"):
        for _ in range(count):
            doc.selection.duplicate()
            doc.selection.translate((dx, dy, dz))

    panel.set_label_text("status", f"Status: Created {count} array copies successfully.")

def init_plugin():
    global panel
    panel = tb.create_plugin_panel("Linear Array Generator")
    panel.add_label("Duplicates the active selection along an offset vector.")

    group = panel.add_group("config", "Parameters")
    group.add_int_field("count", "Copies", value=3, min=1, max=100)
    group.add_float_field("dx", "Step X", value=64.0, min=-4096.0, max=4096.0, step=8.0)
    group.add_float_field("dy", "Step Y", value=0.0, min=-4096.0, max=4096.0, step=8.0)
    group.add_float_field("dz", "Step Z", value=0.0, min=-4096.0, max=4096.0, step=8.0)

    panel.add_button("Generate Array", generate_array)
    panel.add_label_named("status", "Status: Ready")

init_plugin()
