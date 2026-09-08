import trenchbroom as tb


panel = tb.create_plugin_panel("Texture Replacer")
panel.add_label("Select brushes and enter texture names to replace.")
panel.add_text_field("find", "Find Texture:", "")
panel.add_text_field("replace", "Replace With:", "")
panel.add_label_named("status", "Ready")


def replace_textures():
    try:
        doc = tb.documents.current()
        find_name = panel.get_text_field("find")
        replace_name = panel.get_text_field("replace")
        if not find_name or not replace_name:
            panel.set_label_text("status", "Enter both texture names.")
            return

        count = 0
        with doc.transaction("Replace Texture"):
            for brush in doc.selection.brushes:
                for face in brush.faces():
                    if face.texture_name.lower() == find_name.lower():
                        face.texture_name = replace_name
                        count += 1

        panel.set_label_text("status", f"Replaced {count} faces.")
        print(f"Replaced {count} faces.")
    except Exception as e:
        panel.set_label_text("status", f"Error: {e}")
        raise


panel.add_button_callback("Replace All in Selection", replace_textures)
