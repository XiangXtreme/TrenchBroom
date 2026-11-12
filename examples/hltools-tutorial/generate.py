#!/usr/bin/env python3
"""Rebuild the self-contained hltools teaching project from its WAD and code.

This is an authoring helper, not needed to open the project. Run from the repository
root: python examples/hltools-tutorial/generate.py
"""

from __future__ import annotations

import ast
from contextlib import closing
import hashlib
from pathlib import Path
import re
import sqlite3
import uuid


HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
PROJECT = HERE / "hltools-tutorial.tbproj"
WAD = HERE / "tutorial-textures.wad"
NAMESPACE = uuid.UUID("53eb210b-5cd4-4bf6-aeda-c8cc97281d57")


def stable_id(name: str) -> str:
    return str(uuid.uuid5(NAMESPACE, name))


def box(x0, y0, z0, x1, y1, z1, texture="WALL", faces=None):
    points = [
        ((x0, y0, z0), (x0, y1, z0), (x0, y0, z1)),
        ((x1, y0, z0), (x1, y0, z1), (x1, y1, z0)),
        ((x0, y0, z0), (x0, y0, z1), (x1, y0, z0)),
        ((x0, y1, z0), (x1, y1, z0), (x0, y1, z1)),
        ((x0, y0, z0), (x1, y0, z0), (x0, y1, z0)),
        ((x0, y0, z1), (x0, y1, z1), (x1, y0, z1)),
    ]
    axes = [
        "[ 0 1 0 0 ] [ 0 0 -1 0 ]",
        "[ 0 -1 0 0 ] [ 0 0 -1 0 ]",
        "[ 1 0 0 0 ] [ 0 0 -1 0 ]",
        "[ -1 0 0 0 ] [ 0 0 -1 0 ]",
        "[ 1 0 0 0 ] [ 0 -1 0 0 ]",
        "[ 1 0 0 0 ] [ 0 -1 0 0 ]",
    ]
    result = ["{"]
    for i, face in enumerate(points):
        coords = " ".join("( " + " ".join(map(str, p)) + " )" for p in face)
        result.append(f"{coords} {(faces or {}).get(i, texture)} {axes[i]} 0 1 1")
    return "\n".join(result + ["}", ""])


def entity(classname, properties=None, brushes=()):
    result = ["{", f'"classname" "{classname}"']
    result.extend(f'"{key}" "{value}"' for key, value in (properties or {}).items())
    result.extend(brushes)
    return "\n".join(result + ["}", ""])


def room(x, y, height=256, omit=None):
    result = [
        box(-x, -y, -16, x, y, 0, "FLOOR"),
        box(-x, -y, height, x, y, height + 16, "WALL"),
        box(-x, -y, 0, -x + 16, y, height, "WALL"),
        box(x - 16, -y, 0, x, y, height, "WALL"),
        box(-x + 16, -y, 0, x - 16, -y + 16, height, "WALL"),
        box(-x + 16, y - 16, 0, x - 16, y, height, "WALL"),
    ]
    if omit is not None:
        del result[omit]
    return result


def light(x, y, z, brightness=500, color="255 235 205"):
    return entity("light", {"origin": f"{x} {y} {z}", "_light": f"{color} {brightness}"})


def map_text(title, brushes, entities, start):
    result = "// Game: Half-Life\n// Format: Valve\n// Generator: hltools tutorial\n"
    result += entity("worldspawn", {"message": title}, brushes)
    result += entity("info_player_start", {"origin": f"{start[0]} {start[1]} {start[2]}", "angle": "0"})
    return result + "".join(entities)


def detail(name, brushes, **properties):
    settings = {"targetname": name, "zhlt_detaillevel": "1", "zhlt_clipnodedetaillevel": "1"}
    settings.update({key: str(value) for key, value in properties.items()})
    return entity("func_detail", settings, brushes)


def lighting():
    brushes = room(512, 320)
    brushes += [
        box(-96, -128, 0, -64, 128, 128, "BLUE"),
        box(80, -48, 0, 160, 48, 112, "TRIM"),
        box(208, -192, 0, 256, -96, 64, "ORANGE"),
        box(208, -64, 0, 256, 32, 64, "ORANGE"),
        box(208, 64, 0, 256, 160, 64, "ORANGE"),
        box(-416, -80, 224, -288, 80, 240, "LAMP"),
    ]
    entities = [light(-352, 0, 176, 650), light(288, -192, 200, 220, "150 180 255")]
    entities += [entity("info_texlights", {"origin": "-400 220 40", "LAMP": "255 205 130 150"})]
    return map_text("01 lighting", brushes, entities, (-400, -208, 40))


def details():
    brushes = room(1088, 320)
    entities = [light(x, y, 208, 600) for x in (-800, -256, 256, 800) for y in (-192, 192)]
    centers = (-800, -256, 256, 800)
    for row, cy in (("a", -144), ("b", 144)):
        # A large and a small intersecting brush are repeated at each station.
        for station, cx in enumerate(centers, 1):
            base = box(cx - 112, cy - 48, 0, cx + 48, cy + 48, 80, "BLUE")
            top = box(cx - 16, cy - 64, 32, cx + 96, cy + 64, 112, "ORANGE")
            base_props = {}
            top_props = {}
            if station == 1 and row == "b":
                top_props["zhlt_detaillevel"] = 2
            if station == 2:
                top_props["zhlt_detaillevel"] = 2
                if row == "b":
                    top_props["zhlt_chopdown"] = 1
            if station == 3:
                top_props["zhlt_detaillevel"] = 2
                if row == "b":
                    base_props["zhlt_chopup"] = 1
            if station == 4:
                # Same-height, coplanar overlap, so priority controls the top surface.
                base = box(cx - 112, cy - 48, 0, cx + 48, cy + 48, 80, "BLUE")
                top = box(cx - 16, cy - 48, 0, cx + 96, cy + 48, 80, "ORANGE")
                (base_props if row == "a" else top_props)["zhlt_coplanarpriority"] = 1
            entities.append(detail(f"{station:02}_{row}_base", [base], **base_props))
            entities.append(detail(f"{station:02}_{row}_cutter", [top], **top_props))
    return map_text("02 detail CSG", brushes, entities, (-1000, -240, 40))


def collisions():
    brushes = room(1088, 320)
    entities = [light(x, 0, 208, 650) for x in (-800, -480, -160, 160, 480, 800)]
    for i, (cx, mode) in enumerate(zip((-800, -480, -160, 160), ("simple", "legacy", "legacytrim", "normalized")), 1):
        steps = [box(cx - 64, -192 + 96 * j, 0, cx + 64, -96 + 96 * j, 16 * (j + 1), "TRIM") for j in range(3)]
        entities.append(detail(f"{i:02}_{mode}", steps, zhlt_cliptype=mode))
    for row, cy in (("a", -112), ("b", 112)):
        entities.append(detail(
            f"05_{row}_" + ("solid" if row == "a" else "noclip"),
            [box(416, cy - 48, 0, 544, cy + 48, 96, "ORANGE")],
            zhlt_noclip=0 if row == "a" else 1))
        entities.append(detail(
            f"06_{row}_clipnode_base",
            [box(720, cy - 48, 0, 832, cy + 48, 96, "BLUE")],
            zhlt_clipnodedetaillevel=1 if row == "a" else 0))
        entities.append(detail(
            f"06_{row}_clipnode_overlap",
            [box(800, cy - 48, 32, 880, cy + 48, 128, "ORANGE")],
            zhlt_clipnodedetaillevel=1))
    return map_text("03 detail collision", brushes, entities, (-1000, -256, 40))


def vis_geometry():
    brushes = room(704, 320)
    # Two offset doorways interrupt a direct view through the long room.
    for x, low, high in ((-176, -224, -64), (176, 64, 224)):
        brushes += [box(x - 8, -304, 0, x + 8, low, 240, "WALL"),
                    box(x - 8, high, 0, x + 8, 304, 240, "WALL"),
                    box(x - 8, low, 176, x + 8, high, 240, "WALL")]
    return brushes


def tools_map():
    brushes = vis_geometry()
    # One HINT face across the doorway; the remaining five faces are SKIP.
    brushes.append(box(-176, -224, 0, -172, -64, 176, "SKIP", {0: "HINT"}))
    # Invisible blocking volume, plus a visible object whose hidden bottom is NULL.
    brushes.append(box(480, -96, 0, 496, 96, 96, "CLIP"))
    brushes.append(box(384, 160, 0, 448, 224, 96, "TRIM", {4: "NULL"}))
    # NOCLIP on one hidden side turns off collision for the entire brush.
    brushes.append(box(512, 160, 0, 576, 224, 96, "ORANGE", {4: "NOCLIP"}))
    entities = [light(x, 0, 200, 600) for x in (-480, 0, 480)]
    return map_text("04 hint skip clip null", brushes, entities, (-600, -160, 40))


def portals():
    # The solid divider makes the visibility link observable: without the portal,
    # neither room's leaf needs the other one in its PVS.
    brushes = room(704, 320)
    brushes.append(box(-16, -304, 0, 16, 304, 256, "WALL"))
    entities = [light(x, 0, 200, 600) for x in (-480, 480)]
    entities += [entity("info_portal", {"origin": "-480 0 64", "target": "portal_east", "oneway": "0", "neighbor": "0", "targetneighbor": "0"}),
                 entity("info_target", {"origin": "480 0 64", "targetname": "portal_east"})]
    return map_text("05 info portal", brushes, entities, (-600, -160, 40))


def leak():
    brushes = room(320, 256, omit=3)
    entities = [light(0, 0, 176, 500)]
    return map_text("06 leak exercise", brushes, entities, (0, 0, 40))


MAPS = [
    ("01_lighting", "01 光照预览与编译", lighting),
    ("02_detail_csg", "02 func_detail 切割与共面", details),
    ("03_detail_collision", "03 func_detail 碰撞", collisions),
    ("04_tools_hint", "04 HINT 与工具纹理", tools_map),
    ("05_info_portal", "05 info_portal 可见性", portals),
    ("06_leak", "06 漏洞排查（故意漏空）", leak),
]


def native_schema():
    # Reuse the actual ProjectStore schema, including its cache invalidation triggers.
    source = (ROOT / "lib/TbProjectLib/src/ProjectStore.cpp").read_text(encoding="utf-8")
    body = source.split("constexpr auto Schema =", 1)[1].split("// Format 1 stored", 1)[0]
    body = "\n".join(line.split("//", 1)[0] for line in body.splitlines())
    schema = "".join(ast.literal_eval(match) for match in re.findall(r'"(?:\\.|[^"\\])*"', body))
    if "PRAGMA user_version=4;" not in schema or "CREATE TABLE terrain_compile(" not in schema:
        raise RuntimeError("ProjectStore schema changed; review the generator first")
    return schema


def generate():
    if not WAD.is_file():
        raise FileNotFoundError(f"Build the tutorial WAD first: {WAD}")
    temp = PROJECT.with_suffix(".tmp")
    temp.unlink(missing_ok=True)
    try:
        with closing(sqlite3.connect(temp)) as db, db:
            db.execute("PRAGMA foreign_keys=ON")
            db.executescript(native_schema())
            db.execute("INSERT INTO project VALUES(?,?,?)", (stable_id("project"), "hltools 场景教程", "Half-Life"))

            def add(kind, path, name, payload, recipe=""):
                digest = hashlib.sha256(payload).hexdigest()
                content_id, asset_id = stable_id("content:" + digest), stable_id("asset:" + path)
                db.execute("INSERT OR IGNORE INTO content VALUES(?,?,?)", (content_id, digest, payload))
                db.execute("INSERT INTO revisions(asset_id,type,virtual_path,publish_path,display_name,source_name,revision,recipe,original,compatible,content_hash) VALUES(?,?,?,?,?,?,1,?,?,?,?)",
                           (asset_id, kind, path, path, name, "tutorial-textures.wad" if kind == "texture" else "", recipe, content_id, content_id, digest))
                db.execute("INSERT INTO heads VALUES(?,1,?)", (asset_id, path))

            add("texture", "textures/tutorial-textures.wad", "Tutorial textures", WAD.read_bytes(), '{"importer":"wad","version":1}')
            for name, display, factory in MAPS:
                add("map", f"maps/{name}.map", display, factory().encode("utf-8"))
            errors = list(db.execute("PRAGMA foreign_key_check"))
            if errors or db.execute("PRAGMA integrity_check").fetchone()[0] != "ok":
                raise RuntimeError(f"invalid project database: {errors}")
        temp.replace(PROJECT)
    finally:
        temp.unlink(missing_ok=True)
    print(f"Wrote {PROJECT} with {len(MAPS)} maps")


if __name__ == "__main__":
    generate()
