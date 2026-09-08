import trenchbroom as tb


def modify_entity_brushes():
    doc = tb.documents.current()
    selection = doc.selection
    count = 0

    with doc.transaction("Python API: Modify Entity Brushes"):
        for entity in selection.all_entities:
            brushes = entity.brushes
            if not brushes:
                continue

            print(f"Entity '{entity.classname}' has {len(brushes)} brushes.")
            for brush in brushes:
                for face in brush.faces():
                    offset_x, offset_y = face.offset
                    face.offset = (offset_x + 16.0, offset_y)
                    count += 1

    print(f"Modified {count} faces.")
    return count


if __name__ == "__main__":
    modify_entity_brushes()
else:
    modify_entity_brushes()
