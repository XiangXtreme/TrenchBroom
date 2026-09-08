import trenchbroom as tb


doc = tb.documents.current()
selection = doc.selection
entities = [entity for entity in selection.all_entities if entity.classname != "worldspawn"]

if len(entities) != 1:
    print(f"Select exactly one non-worldspawn entity. Selected: {len(entities)}")
else:
    entity = entities[0]
    total_angle = float(entity.get("_angle", "90.0"))
    count = int(entity.get("_count", "5"))
    pivot = [float(value) for value in entity.get("_pivot", "0 0 0").split()]
    axis = [float(value) for value in entity.get("_axis", "0 0 1").split()]

    if count < 1:
        raise RuntimeError("_count must be greater than 0")
    if len(pivot) != 3:
        raise RuntimeError("_pivot must contain 3 numbers")
    if len(axis) != 3:
        raise RuntimeError("_axis must contain 3 numbers")

    step_angle = total_angle / count
    with doc.transaction("Python API: Spin Entity"):
        for _ in range(count):
            selection.duplicate()
            selection.rotate(axis, step_angle, center=pivot)

    print(f"Generated {count} copies around {pivot}")
