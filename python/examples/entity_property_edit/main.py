import trenchbroom as tb

doc = tb.documents.current()
worldspawn = doc.entities[0]
worldspawn.set("_trenchbroom_example", "enabled")
print(f"_trenchbroom_example = {worldspawn.get('_trenchbroom_example')}")
