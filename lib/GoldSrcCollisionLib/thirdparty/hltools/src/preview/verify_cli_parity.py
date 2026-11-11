"""Compare the bundled lightmap worker with the full hltools CLI."""

import argparse
import json
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
            for quality in [[], ["-fast"], ["-extra"]]:
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
                    if args.require_gpu:
                        text = compile_output.decode(errors="replace")
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
        print("Verified six lightmap pairs, project cleanup and failure recovery")


if __name__ == "__main__":
    main()
