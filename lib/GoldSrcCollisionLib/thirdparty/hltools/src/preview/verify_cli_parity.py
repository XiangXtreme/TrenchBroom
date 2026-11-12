"""Compare the bundled lightmap worker with the full hltools CLI."""

import argparse
import json
import re
from pathlib import Path
import sqlite3
import struct
import subprocess
import tempfile


def box(x0, y0, z0, x1, y1, z1, texture="WALL"):
    faces = [((x0,y0,z0),(x0,y1,z0),(x0,y0,z1)),
             ((x1,y0,z0),(x1,y0,z1),(x1,y1,z0)),
             ((x0,y0,z0),(x0,y0,z1),(x1,y0,z0)),
             ((x0,y1,z0),(x1,y1,z0),(x0,y1,z1)),
             ((x0,y0,z0),(x1,y0,z0),(x0,y1,z0)),
             ((x0,y0,z1),(x0,y1,z1),(x1,y0,z1))]
    return "{\n" + "".join(" ".join("( %s )" % " ".join(map(str,p)) for p in f)
                                  + f" {texture} 0 0 0 1 1\n" for f in faces) + "}\n"


def fixture(directory):
    payload = struct.pack("<16s6I", b"WALL",16,16,40,296,360,376)
    payload += bytes(340) + struct.pack("<H",256) + bytes([160,120,80])*256
    wad = struct.pack("<4sii",b"WAD3",1,12+len(payload)) + payload
    wad += struct.pack("<iiiBBH16s",12,len(payload),len(payload),0x43,0,0,b"WALL")
    wad_path = directory / "wall.wad"
    wad_path.write_bytes(wad)
    brushes = box(-144,-144,-16,144,144,0) + box(-144,-144,128,144,144,144)
    brushes += box(-144,-144,0,-128,144,128) + box(128,-144,0,144,144,128)
    brushes += box(-128,-144,0,128,-128,128) + box(-128,128,0,128,144,128)
    text = '{\n"classname" "worldspawn"\n"wad" "' + wad_path.as_posix() + '"\n' + brushes + '}\n'
    text += '{\n"classname" "info_player_start"\n"origin" "0 0 40"\n}\n'
    text += '{\n"classname" "light"\n"origin" "0 0 80"\n"_light" "255 210 150 300"\n}\n'
    text += '{\n"classname" "info_texlights"\n"WALL" "30 45 60 30"\n}\n'
    map_path = directory / "room.map"
    map_path.write_text(text, encoding="utf-8")
    project = directory / "room.tbproj"
    with sqlite3.connect(project) as db:
        db.executescript("""PRAGMA application_id=1413632074; PRAGMA user_version=1;
        CREATE TABLE content(id TEXT PRIMARY KEY,data BLOB);
        CREATE TABLE revisions(asset_id TEXT,revision INTEGER,type TEXT,virtual_path TEXT,display_name TEXT,compatible TEXT);
        CREATE TABLE heads(asset_id TEXT,revision INTEGER);""")
        for i, kind, name, data in [("m","map","room",text.encode()),("w","texture","wall",wad)]:
            db.execute("INSERT INTO content VALUES(?,?)",(i,data))
            db.execute("INSERT INTO revisions VALUES(?,1,?,?,?,?)",(i,kind,name,name,i))
            db.execute("INSERT INTO heads VALUES(?,1)",(i,))
    db.close()
    return map_path, project


def invoke(command, directory):
    result = subprocess.run(list(map(str, command)), cwd=directory, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=120)
    if result.returncode:
        raise RuntimeError(result.stdout.decode(errors="replace"))
    return result.stdout


def verify_preview_cache(worker, directory, gpu=False):
    source, project = fixture(directory)
    original = source.read_text(encoding="utf-8")
    cache = directory / "geometry-cache"

    def bake(cached, project_mode=False):
        destination = directory / "cache-result.bsp"
        inputs = (["--project", project, "--map", "room", "--output", destination]
                  if project_mode else [source])
        output = invoke([worker, "compile", *inputs, "-fast", "-noclip", "-threads", "1",
                         "-minlight", "1", *(["-gpu"] if gpu else []),
                         *(["-previewcache", cache] if cached else [])], directory)
        data = (destination if project_mode else source.with_suffix(".bsp")).read_bytes()
        lumps = [data[o:o+n] for o,n in
                 [struct.unpack_from("<ii", data, 4+8*j) for j in range(15)]]
        lumps[0] = re.sub(rb'"compiled_at" "[^"]*"', b'"compiled_at" "TIME"', lumps[0])
        assert not lumps[9], "preview does not build collision hulls"
        return output, lumps

    def check_edit(text, should_reuse):
        source.write_text(text, encoding="utf-8")
        output, cached = bake(True)
        assert (b"preview: reusing geometry" in output) == should_reuse, output.decode(errors="replace")
        _, full = bake(False)
        assert cached == full, f"relighting must equal a complete bake: {[i for i in range(15) if cached[i] != full[i]]}"

    check_edit(original, False)
    check_edit(original.replace("255 210 150 300", "120 180 255 500"), True)
    check_edit(original.replace('"0 0 80"', '"24 0 70"'), True)
    # A raw origin can be empty while the BSP origin+1 occupant probe is solid.
    check_edit(original.replace('"0 0 80"', '"0 0 127.5"'), False)
    check_edit(original.replace('"0 0 40"', '"10 0 40"'), False)
    check_edit(original.replace("WALL 0 0 0 1 1", "WALL 8 0 0 1 1", 1), False)
    check_edit(original, False)
    wad = directory / "wall.wad"
    changed = bytearray(wad.read_bytes())
    changed[12+40+340+2] = 90
    wad.write_bytes(changed)
    check_edit(original.replace("255 210 150 300", "250 100 100 300"), False)
    # Moving into solid must invalidate geometry reuse; full BSP retains its
    # existing leak/occupant validation rather than accepting a cached tree.
    source.write_text(original.replace('"0 0 80"', '"0 0 -8"'), encoding="utf-8")
    output, _ = bake(True)
    assert b"preview: reusing geometry" not in output

    # Brush entities take the standard only-entities model-number path too.
    modeled = original + '{\n"classname" "func_wall"\n' + box(-32,-32,0,32,32,16) + '}\n'
    check_edit(modeled, False)
    check_edit(modeled.replace("255 210 150 300", "80 160 255 400"), True)
    check_edit(modeled.replace('"_light" "255 210 150 300"', '"_light" "255 210 150 300"\n"style" "2"'), False)

    # Extracted project resources have fresh temporary paths on every run.
    # Identical bytes must still reuse geometry, changed bytes must not.
    Path(str(cache) + ".key").unlink(missing_ok=True)
    output, _ = bake(True, True)
    assert b"preview: reusing geometry" not in output
    output, cached = bake(True, True)
    assert b"preview: reusing geometry" in output
    _, full = bake(False, True)
    assert cached[1:] == full[1:]
    with sqlite3.connect(project) as db:
        db.execute("UPDATE content SET data=? WHERE id='w'", (bytes(changed),))
    db.close()
    output, _ = bake(True, True)
    assert b"preview: reusing geometry" not in output
    print("Verified exact cached relighting, geometry/material invalidation and project resources")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worker", required=True, type=Path)
    parser.add_argument("--cli", type=Path)
    parser.add_argument("--gpu", action="store_true", help="request GPU lighting")
    parser.add_argument("--require-gpu", action="store_true", help="fail unless a GPU was used")
    args = parser.parse_args()
    worker = args.worker.resolve()
    cli = args.cli.resolve() if args.cli else worker
    with tempfile.TemporaryDirectory(prefix="hltools-lightmap-") as path:
        directory = Path(path) / "lightmap with spaces"
        directory.mkdir()
        map_path, project = fixture(directory)
        for project_mode in [False, True]:
            for quality in [[], ["-fast"], ["-previewfastvis"], ["-extra"]]:
                outputs = []
                for i, executable in enumerate([cli, worker]):
                    output = directory / f"result{i}"
                    output.mkdir(exist_ok=True)
                    if project_mode:
                        source = ["--project",project,"--map","room","--output",output/"room.bsp"]
                        bsp = output/"room.bsp"
                    else:
                        source = [map_path]
                        bsp = map_path.with_suffix(".bsp")
                    compile_output = invoke([
                        executable,"compile",*source,"-threads","2","-minlight","1",
                        *( ["-gpu"] if args.gpu else [] ),*quality],directory)
                    text = compile_output.decode(errors="replace")
                    if quality in (["-fast"], ["-previewfastvis"]):
                        assert re.search(r"fast vis\s+on\b", text), text
                    if quality == ["-fast"]:
                        assert "bouncing light" not in text, text
                    if quality == ["-previewfastvis"]:
                        assert re.search(r"bouncing light\s+\(8 passes\)", text), text
                    if args.require_gpu:
                        assert "gpu device" in text.lower() and "none found" not in text.lower(), text
                        assert "-gpu:" not in text, text
                    invoke([executable,"lightmap",bsp,output/"atlas.bmp","-manifest",output/"atlas.json","-force"],directory)
                    data = bsp.read_bytes()
                    lumps = [struct.unpack_from("<ii",data,4+8*j) for j in range(15)]
                    lighting = data[lumps[8][0]:sum(lumps[8])]
                    assert lighting and max(lighting) > 1, "fixture must receive real light"
                    record = json.loads((output/"atlas.json").read_text())
                    assert record["faces"], "fixture must have lit faces"
                    outputs.append((lighting,(output/"atlas.bmp").read_bytes(),record))
                assert outputs[0] == outputs[1], f"Lighting mismatch: project={project_mode}, quality={quality}"
                print(f"MATCH project={project_mode}, quality={quality or ['normal']}")
        # A failed invocation must not poison the next worker process.
        bad = directory/"bad.map"
        bad.write_text("not a map")
        run = subprocess.run([str(worker),"compile",str(bad)],cwd=directory,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30)
        assert run.returncode != 0
        invoke([worker,"compile",map_path,"-fast","-threads","2","-minlight","1"],directory)
        assert not list(directory.glob("hltools-tbproj-*")), "project scratch must be cleaned"
        print("Verified eight lightmap pairs, project cleanup and failure recovery")
        cache_directory = directory / "cache checks"
        cache_directory.mkdir()
        verify_preview_cache(worker, cache_directory)
        if args.gpu:
            gpu_directory = directory / "GPU cache checks"
            gpu_directory.mkdir()
            verify_preview_cache(worker, gpu_directory, True)


if __name__ == "__main__":
    main()
