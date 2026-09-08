import trenchbroom as tb


def create_texture_browser():
    doc = tb.documents.current()
    panel = tb.create_plugin_panel("Texture Browser")
    panel.clear()
    panel.add_label_named("status", "")

    collections = doc.material_collections
    if not collections:
        panel.set_label_text("status", "No material collections found.")
        return

    lines = ["Material Collections:"]
    for collection in collections:
        materials = collection.materials
        lines.append(f"Collection: {collection.name}")
        lines.append(f"  Count: {len(materials)}")
        for material in materials[:10]:
            lines.append(f"  - {material.name} ({material.width}x{material.height})")
        if len(materials) > 10:
            lines.append(f"  ... and {len(materials) - 10} more.")

    panel.set_label_text("status", "\n".join(lines))


create_texture_browser()
