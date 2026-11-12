#!/usr/bin/env python3
"""Compile the lesson maps and check that their intended comparisons work."""

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
from inspect_pvs import inspect


def lumps(path):
    data = path.read_bytes()
    if struct.unpack_from("<i", data)[0] != 30:
        raise AssertionError("not a GoldSrc BSP")
    result = []
    for i in range(15):
        start, length = struct.unpack_from("<ii", data, 4 + 8 * i)
        result.append(data[start:start + length])
    return result


def world_hull_contents(bsp, point):
    planes = list(struct.iter_unpack("<4fi", bsp[1]))
    clips = list(struct.iter_unpack("<ihh", bsp[9]))
    world = struct.unpack_from("<9f7i", bsp[14])
    node = world[10]  # hull 1, standing player
    for _ in range(len(clips) + 1):
        if node < 0:
            return node
        planenum, front, back = clips[node]
        plane = planes[planenum]
        distance = sum(plane[i] * point[i] for i in range(3)) - plane[3]
        node = front if distance >= 0 else back
    raise AssertionError("cyclic collision tree")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worker", type=Path, required=True, help="path to HltoolsLightmapWorker")
    args = parser.parse_args()
    worker = args.worker.resolve(strict=True)
    with closing(sqlite3.connect(generate.PROJECT)) as db:
        assert db.execute("PRAGMA integrity_check").fetchone()[0] == "ok"
        assert not list(db.execute("PRAGMA foreign_key_check"))
        for name, _, factory in generate.MAPS:
            payload = db.execute(
                "SELECT c.data FROM revisions r JOIN content c ON c.id=r.compatible WHERE r.virtual_path=?",
                (f"maps/{name}.map",),
            ).fetchone()[0]
            assert payload == factory().encode("utf-8"), f"{name}: rebuild the project"
        wad = db.execute("SELECT c.data FROM revisions r JOIN content c ON c.id=r.compatible WHERE r.type='texture'").fetchone()[0]
        assert wad == generate.WAD.read_bytes(), "embedded WAD differs from the source"

    with tempfile.TemporaryDirectory(prefix="hltools-tutorial-check-") as directory:
        root = Path(directory)
        counter = 0

        def compile_map(name, edit=None, flags=(), expect_leak=False):
            nonlocal counter
            counter += 1
            project = root / f"case-{counter}.tbproj"
            output = root / f"case-{counter}.bsp"
            shutil.copyfile(generate.PROJECT, project)
            if edit:
                with closing(sqlite3.connect(project)) as db, db:
                    content_id, payload = db.execute(
                        "SELECT r.compatible,c.data FROM revisions r JOIN content c ON c.id=r.compatible WHERE r.virtual_path=?",
                        (f"maps/{name}.map",),
                    ).fetchone()
                    changed = edit(payload.decode("utf-8")).encode("utf-8")
                    assert changed != payload, f"{name}: comparison did not alter the map"
                    digest = hashlib.sha256(changed).hexdigest()
                    db.execute("UPDATE content SET data=?,hash=? WHERE id=?", (changed, digest, content_id))
                    db.execute("UPDATE revisions SET content_hash=? WHERE compatible=?", (digest, content_id))
            command = [str(worker), "compile", "--project", str(project), "--map", name,
                       "--output", str(output), "-threads", "2", "-bounce", "0", *flags]
            result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
            log = result.stdout.decode("utf-8", errors="replace")
            if expect_leak:
                assert result.returncode != 0 and "LEAK" in log, f"{name}: expected a leak"
                assert not output.exists()
                return None
            if result.returncode or not output.exists():
                raise RuntimeError(f"{name} failed:\n{log[-4000:]}")
            return output

        for name, _, _ in generate.MAPS:
            compile_map(name, expect_leak=name == "06_leak")
        marker = '\n}\n{\n"classname" "info_player_start"'
        compile_map("06_leak", lambda text: text.replace(
            marker, "\n" + generate.box(304, -256, 0, 320, 256, 256) + marker, 1))

        # The editor compiles a staged MAP and copies the leak and intermediate files.
        # Exercise that input mode too, so the instructions have files to open.
        shutil.copyfile(generate.WAD, root / "tutorial.wad")
        def compile_staged(name, source, flags=()):
            map_path = root / f"{name}.map"
            map_path.write_text(source.replace(
                '"classname" "worldspawn"',
                '"classname" "worldspawn"\n"wad" "tutorial.wad"', 1), encoding="utf-8")
            return subprocess.run([str(worker), "compile", str(map_path), "-threads", "2",
                                   "-fast", *flags], cwd=root, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, timeout=90)
        leaked = compile_staged("leak", generate.leak())
        assert leaked.returncode != 0 and (root / "leak.pts").is_file()
        staged_hint = compile_staged("hint", generate.tools_map(), ("-dumpintermediates",))
        assert staged_hint.returncode == 0 and (root / "hint.prt").is_file()
        assert (root / "hint.p0").is_file()
        print("PASS: normal maps compile; the leak produces a point file and the repair compiles")
        print("PASS: staged HINT compilation produces portal and CSG files")

        reference = lumps(compile_map("01_lighting"))[8]
        for flags in (("-ao",), ("-pcf", "2"), ("-blurclamp", "0.75")):
            assert lumps(compile_map("01_lighting", flags=flags))[8] != reference, flags
        print("PASS: AO, PCF and blur clamp change the baked lightmap")

        reference = lumps(compile_map("02_detail_csg"))
        for key, old, new in (("zhlt_detaillevel", "2", "1"),
                              ("zhlt_chopdown", "1", "0"),
                              ("zhlt_chopup", "1", "0"),
                              ("zhlt_coplanarpriority", "1", "0"),
                              ("zhlt_clipnodedetaillevel", "1", "0")):
            before = f'"{key}" "{old}"'
            after = f'"{key}" "{new}"'
            changed = lumps(compile_map("02_detail_csg", lambda text: text.replace(before, after)))
            assert any(reference[i] != changed[i] for i in (1, 5, 7, 9, 12, 13)), key
        print("PASS: detail levels, chopping, coplanar priority and clipnode level affect BSP data")

        collision = lumps(compile_map("03_detail_collision"))
        assert world_hull_contents(collision, (480, -112, 48)) == -2
        assert world_hull_contents(collision, (480, 112, 48)) == -1
        print("PASS: the solid and passable detail brushes have different player collision")

        hint = lumps(compile_map("04_tools_hint"))
        no_hint = lumps(compile_map("04_tools_hint", lambda text: text.replace(" HINT [", " SKIP [")))
        assert hint[5] != no_hint[5] or hint[4] != no_hint[4]
        print("PASS: the HINT face changes the BSP split or visibility")

        def visibility(edit=None):
            bsp = compile_map("05_info_portal", edit)
            return inspect(bsp, (-480, 0, 64), (480, 0, 64))[2:]

        assert visibility() == (True, True)
        assert visibility(lambda text: text.replace('"oneway" "0"', '"oneway" "1"')) == (True, False)
        assert visibility(lambda text: re.sub(r'\{\n"classname" "info_portal".*?\n\}\n', "", text, flags=re.S)) == (False, False)
        print("PASS: portal link is bidirectional, one-way, or absent as configured")


if __name__ == "__main__":
    main()
