#!/usr/bin/env python3
"""Compile the exhibit and verify the documented GoldSrc comparisons."""

from __future__ import annotations

import argparse
from contextlib import closing
import hashlib
from pathlib import Path
import re
import shutil
import sqlite3
import struct
import subprocess
import tempfile

import generate
from showcase import MAPS, SIGNS, tutorial
from inspect_pvs import inspect


def lumps(path):
    data = path.read_bytes()
    if struct.unpack_from("<i", data)[0] != 30:
        raise AssertionError("not a GoldSrc BSP v30")
    result = []
    for index in range(15):
        offset, size = struct.unpack_from("<ii", data, 4 + 8 * index)
        if offset < 0 or size < 0 or offset + size > len(data):
            raise AssertionError("BSP lump outside file")
        result.append(data[offset:offset + size])
    return result


def world_hull_contents(bsp, point):
    planes = list(struct.iter_unpack("<4fi", bsp[1]))
    clips = list(struct.iter_unpack("<ihh", bsp[9]))
    model = struct.unpack_from("<9f7i", bsp[14])
    node = model[10]  # hull 1, standing player
    for _ in range(len(clips) + 1):
        if node < 0:
            return node
        plane_index, front, back = clips[node]
        plane = planes[plane_index]
        distance = sum(plane[i] * point[i] for i in range(3)) - plane[3]
        node = front if distance >= 0 else back
    raise AssertionError("cyclic collision tree")


def entity_records(bsp):
    text = bsp[0].decode("utf-8", errors="replace")
    return [dict(re.findall(r'"([^"\n]*)"\s+"([^"\n]*)"', item))
            for item in re.findall(r'\{([^{}]*)\}', text, re.S)]


def remove_entity(classname):
    pattern = re.compile(r'\{\n"classname" "' + re.escape(classname) + r'".*?\n\}\n', re.S)
    def edit(text):
        changed, count = pattern.subn("", text)
        assert count == 1, (classname, count)
        return changed
    return edit


def validate_project(project, wad):
    with closing(sqlite3.connect(f"{project.as_uri()}?mode=ro", uri=True)) as db:
        assert db.execute("PRAGMA integrity_check").fetchone()[0] == "ok"
        assert not list(db.execute("PRAGMA foreign_key_check"))
        assert db.execute("SELECT game_id FROM project").fetchone()[0] == "Half-Life"
        signs_seen = set()
        for name, _, factory in MAPS:
            row = db.execute(
                "SELECT c.data FROM revisions r JOIN content c ON c.id=r.compatible WHERE r.virtual_path=?",
                (f"maps/{name}.map",),
            ).fetchone()
            assert row and row[0] == factory().encode("utf-8"), f"{name}: rebuild the project"
            text = row[0].decode("utf-8")
            signs_seen.update(re.findall(r'\b(S\d\d_[A-Z]+) \[', text))
        assert signs_seen == set(SIGNS), f"missing or unknown signs: {signs_seen ^ set(SIGNS)}"
        embedded = db.execute(
            "SELECT c.data FROM revisions r JOIN content c ON c.id=r.compatible WHERE r.type='texture'"
        ).fetchone()[0]
        assert embedded == wad.read_bytes(), "embedded WAD differs from the source"

    # WAD3 directory entries are 32 bytes; names are 16-byte, NUL-terminated ASCII.
    data = wad.read_bytes()
    magic, count, offset = struct.unpack_from("<4sii", data)
    assert magic == b"WAD3" and count == len(SIGNS) + 16
    names = {struct.unpack_from("<16s", data, offset + 32 * i + 16)[0].split(b"\0", 1)[0].decode("ascii")
             for i in range(count)}
    assert set(SIGNS) <= names


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worker", type=Path, required=True, help="path to HltoolsLightmapWorker")
    parser.add_argument("--project", type=Path, default=generate.PROJECT)
    parser.add_argument("--wad", type=Path, default=generate.WAD)
    args = parser.parse_args()
    worker = args.worker.resolve(strict=True)
    project = args.project.resolve(strict=True)
    wad = args.wad.resolve(strict=True)
    validate_project(project, wad)
    print(f"PASS: project integrity, all {len(MAPS)} maps, {len(SIGNS)} signs and the embedded WAD", flush=True)

    with tempfile.TemporaryDirectory(prefix="hltools-showcase-check-") as directory:
        root = Path(directory)
        counter = 0

        def compile_map(name, edit=None, flags=("-bounce", "0"), expect_leak=False):
            nonlocal counter
            counter += 1
            working = root / f"case-{counter}.tbproj"
            output = root / f"case-{counter}.bsp"
            shutil.copyfile(project, working)
            if edit:
                with closing(sqlite3.connect(working)) as db, db:
                    content_id, payload = db.execute(
                        "SELECT r.compatible,c.data FROM revisions r JOIN content c ON c.id=r.compatible WHERE r.virtual_path=?",
                        (f"maps/{name}.map",),
                    ).fetchone()
                    changed = edit(payload.decode("utf-8")).encode("utf-8")
                    assert changed != payload, f"{name}: comparison did not change the map"
                    digest = hashlib.sha256(changed).hexdigest()
                    db.execute("UPDATE content SET data=?,hash=? WHERE id=?", (changed, digest, content_id))
                    db.execute("UPDATE revisions SET content_hash=? WHERE compatible=?", (digest, content_id))
            command = [str(worker), "compile", "--project", str(working), "--map", name,
                       "--output", str(output), "-threads", "2", *flags]
            result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
            log = result.stdout.decode("utf-8", errors="replace")
            if expect_leak:
                assert result.returncode != 0 and "LEAK" in log and not output.exists(), name
                return None, log
            if result.returncode or not output.exists():
                raise RuntimeError(f"{name} failed:\n{log[-6000:]}")
            return output, log

        compiled = {}
        for name, _, _ in MAPS:
            result, _ = compile_map(name, expect_leak=name == "09_leak")
            if result:
                compiled[name] = result
        marker = '\n}\n{\n"classname" "info_player_start"'
        compile_map("09_leak", lambda text: text.replace(
            marker, "\n" + tutorial.box(304, -256, 0, 320, 256, 256) + marker, 1))
        print("PASS: nine maps compile; the leak fails and compiles once repaired", flush=True)

        lighting = lumps(compiled["01_lighting"])[8]
        for classname in ("info_texlights", "light_spot"):
            changed, _ = compile_map("01_lighting", remove_entity(classname))
            assert lumps(changed)[8] != lighting, classname
        for flags in (("-ao",), ("-pcf", "2"), ("-blurclamp", "0.75")):
            changed, _ = compile_map("01_lighting", flags=("-bounce", "0", *flags))
            assert lumps(changed)[8] != lighting, flags
        print("PASS: texture light, spotlight, AO, PCF and blur clamp affect lightmaps", flush=True)

        reference = lumps(compiled["02_detail_csg"])
        for key, old, new in (("zhlt_detaillevel", "2", "1"), ("zhlt_chopdown", "1", "0"),
                              ("zhlt_chopup", "1", "0"), ("zhlt_coplanarpriority", "1", "0"),
                              ("zhlt_clipnodedetaillevel", "1", "0")):
            before, after = f'"{key}" "{old}"', f'"{key}" "{new}"'
            changed, _ = compile_map("02_detail_csg", lambda text: text.replace(before, after))
            changed = lumps(changed)
            assert any(reference[i] != changed[i] for i in (1, 5, 7, 9, 12, 13)), key
        collision = lumps(compiled["03_collision"])
        assert world_hull_contents(collision, (480, -112, 48)) == -2
        assert world_hull_contents(collision, (480, 112, 48)) == -1
        print("PASS: detail settings change the BSP; passable and solid brushes differ", flush=True)

        hint = lumps(compiled["04_tool_textures"])
        no_hint, _ = compile_map("04_tool_textures", lambda text: text.replace(" HINT [", " SKIP ["))
        no_hint = lumps(no_hint)
        assert hint[5] != no_hint[5] or hint[4] != no_hint[4]
        assert world_hull_contents(hint, (-256, 0, 48)) == -2  # CLIP
        assert world_hull_contents(hint, (256, 128, 48)) == -2  # NULL underside
        assert world_hull_contents(hint, (800, 128, 48)) == -1  # NOCLIP underside
        print("PASS: HINT changes the BSP; CLIP, NULL and NOCLIP collision is correct", flush=True)

        def visibility(path):
            return inspect(path, (-480, 0, 64), (480, 0, 64))[2:]
        assert visibility(compiled["05_portal"]) == (True, True)
        one_way, _ = compile_map("05_portal", lambda text: text.replace('"oneway" "0"', '"oneway" "1"'))
        disconnected, _ = compile_map("05_portal", remove_entity("info_portal"))
        assert visibility(one_way) == (True, False)
        assert visibility(disconnected) == (False, False)
        print("PASS: portal PVS is bidirectional, one-way or disconnected as configured", flush=True)

        entities = entity_records(lumps(compiled["06_brush_entities"]))
        def named(classname):
            return [item for item in entities if item.get("classname") == classname]
        door = named("func_door_rotating")
        trigger = named("trigger_multiple")
        wall = named("func_wall")
        assert len(door) == len(trigger) == len(wall) == 1
        assert door[0]["origin"] == "-720 88 64" and door[0]["targetname"] == trigger[0]["target"]
        assert all(item["model"].startswith("*") for item in (door[0], trigger[0], wall[0]))
        assert wall[0]["zhlt_lightflags"] == "2"
        assert not named("func_group") and not named("func_detail")
        clear_wall, _ = compile_map("06_brush_entities", lambda text: text.replace(
            '"zhlt_lightflags" "2"', '"zhlt_lightflags" "0"'))
        assert lumps(clear_wall)[8] != lumps(compiled["06_brush_entities"])[8]
        print("PASS: ORIGIN pivot, trigger target, runtime brush models and opaque lighting", flush=True)

        sunless, _ = compile_map("07_sky", remove_entity("light_environment"))
        assert lumps(sunless)[8] != lumps(compiled["07_sky"])[8]
        sun = [e for e in entity_records(lumps(compiled["07_sky"])) if e.get("classname") == "light_environment"]
        assert len(sun) == 1 and sun[0]["angles"] == "-60 45 0"
        no_minlight, _ = compile_map("08_rad_settings", remove_entity("info_minlights"))
        assert lumps(no_minlight)[8] != lumps(compiled["08_rad_settings"])[8]
        _, default_log = compile_map("08_rad_settings", flags=())
        _, override_log = compile_map("08_rad_settings", flags=("-bounce", "1"))
        assert re.search(r'\bbounces\s+0\s', default_log)
        assert re.search(r'\bbounces\s+1\s', override_log)
        print("PASS: sky sunlight, per-texture minimum light and compiler-option precedence", flush=True)

        # The editor's compile path stages MAP + WAD and copies these diagnostics.
        shutil.copyfile(wad, root / "showcase.wad")
        def compile_staged(name, source, flags=()):
            source = source.replace('"classname" "worldspawn"',
                                    '"classname" "worldspawn"\n"wad" "showcase.wad"', 1)
            path = root / f"{name}.map"
            path.write_text(source, encoding="utf-8")
            return subprocess.run([str(worker), "compile", str(path), "-threads", "2", "-fast", *flags],
                                  cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
        leak = compile_staged("leak", MAPS[-1][2]())
        hint = compile_staged("hint", next(factory for name, _, factory in MAPS if name == "04_tool_textures")(),
                              ("-dumpintermediates",))
        assert leak.returncode != 0 and (root / "leak.pts").is_file()
        assert hint.returncode == 0 and (root / "hint.prt").is_file() and (root / "hint.p0").is_file()
        print("PASS: staged builds produce the documented PTS, PRT and CSG diagnostics", flush=True)


if __name__ == "__main__":
    main()
