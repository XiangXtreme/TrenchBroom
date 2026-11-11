"""Compare embedded planes, clipnodes and model headnodes with the real CLI."""

import argparse
from pathlib import Path
import struct
import subprocess
import tempfile


def box(x0, y0, z0, x1, y1, z1, material="__TB_empty"):
    faces = [((x0, y0, z0), (x0, y1, z0), (x0, y0, z1)),
             ((x1, y0, z0), (x1, y0, z1), (x1, y1, z0)),
             ((x0, y0, z0), (x0, y0, z1), (x1, y0, z0)),
             ((x0, y1, z0), (x1, y1, z0), (x0, y1, z1)),
             ((x0, y0, z0), (x1, y0, z0), (x0, y1, z0)),
             ((x0, y0, z1), (x0, y1, z1), (x1, y0, z1))]
    return "{\n" + "".join(" ".join("( %s )" % " ".join(map(str, p)) for p in f)
                                 + f" {material} 0 0 0 1 1\n" for f in faces) + "}\n"


def entity(properties, brushes):
    return "{\n" + "".join(f'"{k}" "{v}"\n' for k, v in properties.items()) + brushes + "}\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", required=True, type=Path)
    parser.add_argument("--embedded", required=True, type=Path)
    args = parser.parse_args()
    world = lambda b: entity({"classname": "worldspawn"}, b)
    ramp = box(0, -64, -64, 128, 64, 128).replace(
        "( 0 -64 128 ) ( 0 64 128 ) ( 128 -64 128 )",
        "( 0 -64 0 ) ( 0 64 0 ) ( 128 -64 128 )")
    fixtures = {
        "cube": world(box(0, 0, 0, 64, 64, 64)),
        "seams": world(box(-128, -128, -16, 0, 128, 0)
                       + box(0, -128, -16, 128, 128, 0) + ramp),
        "tools": world(box(-128, -128, -16, 128, 128, 0)
                       + box(100, -32, 0, 116, 32, 96, "CLIPHULL3")
                       + box(-116, -32, 0, -100, 32, 96, "NOCLIP")),
        "detail": world(box(-256, -128, -16, 256, 128, 0)) + entity(
            {"classname": "func_detail", "zhlt_cliptype": "legacytrim"}, ramp),
        "origin": world(box(-256, -128, -16, 256, 128, 0)) + entity(
            {"classname": "func_wall"}, box(100, -32, 0, 116, 32, 96)
            + box(104, -4, 40, 112, 4, 48, "ORIGIN")),
    }
    with tempfile.TemporaryDirectory(prefix="hltools-parity-") as directory:
        work = Path(directory)
        miptex = struct.pack("<16s6I", b"__TB_EMPTY", 16, 16, 40, 296, 360, 376)
        miptex += bytes(340) + struct.pack("<H", 256) + bytes(768)
        wad = work / "textures.wad"
        origin = b"ORIGIN".ljust(16, b"\0") + miptex[16:]
        wad.write_bytes(struct.pack("<4sii", b"WAD3", 2, 12 + 2 * len(miptex)) + miptex + origin
                        + struct.pack("<iiiBBH16s", 12, len(miptex), len(miptex),
                                      0x43, 0, 0, b"__TB_EMPTY")
                        + struct.pack("<iiiBBH16s", 12 + len(miptex), len(miptex), len(miptex),
                                      0x43, 0, 0, b"ORIGIN"))
        for name, text in fixtures.items():
            path = work / f"{name}.map"
            text = text.replace('"classname" "worldspawn"',
                                f'"classname" "worldspawn"\n"wad" "{wad.as_posix()}"')
            path.write_text(text, encoding="utf-8")
            for i, mode in enumerate(("smallest", "normalized", "simple", "precise", "legacy", "legacytrim")):
                for command in ([str(args.cli.resolve()), "csg", "-cliptype", mode, str(path)],
                                [str(args.cli.resolve()), "bsp", "-nofill", "-noopt", str(path)],
                                [str(args.embedded.resolve()), str(path), str(i), str(work / "embedded.bin")]):
                    run = subprocess.run(command, cwd=work, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                    if run.returncode:
                        raise RuntimeError(run.stdout.decode(errors="replace"))
                data = path.with_suffix(".bsp").read_bytes()
                lumps = [struct.unpack_from("<ii", data, 4 + 8 * j) for j in range(15)]
                chunk = lambda j: data[lumps[j][0]:lumps[j][0] + lumps[j][1]]
                models = chunk(14)
                expected = struct.pack("<I", len(chunk(1)) // 20) + chunk(1)
                expected += struct.pack("<I", len(chunk(9)) // 8) + chunk(9)
                expected += struct.pack("<I", len(models) // 64)
                expected += b"".join(models[j + 36:j + 52] for j in range(0, len(models), 64))
                if expected != (work / "embedded.bin").read_bytes():
                    raise AssertionError(f"Collision differs for {name}/{mode}")
                print(f"MATCH {name}/{mode}")
        print(f"Verified {len(fixtures) * 6} CLI/embedded collision pairs")


if __name__ == "__main__":
    main()
