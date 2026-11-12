#!/usr/bin/env python3
"""Build the self-contained showcase project from its maps and WAD."""

from __future__ import annotations

import argparse
from contextlib import closing
import hashlib
from pathlib import Path
import sqlite3
import uuid

from showcase import HERE, MAPS, tutorial


PROJECT = HERE / "hltools-showcase.tbproj"
WAD = HERE / "showcase-textures.wad"
NAMESPACE = uuid.UUID("4c30bd2a-6b51-4e6e-8d29-29eb790ebf37")


def stable_id(name):
    return str(uuid.uuid5(NAMESPACE, name))


def build_project(output, wad):
    if not wad.is_file():
        raise FileNotFoundError(f"Build the showcase WAD first: {wad}")
    temp = output.with_suffix(output.suffix + ".tmp")
    if temp.exists():
        raise FileExistsError(f"Refusing to overwrite a temporary file: {temp}")
    try:
        with closing(sqlite3.connect(temp)) as db, db:
            db.execute("PRAGMA foreign_keys=ON")
            db.executescript(tutorial.native_schema())
            db.execute("INSERT INTO project VALUES(?,?,?)", (stable_id("project"), "hltools 交互实验室", "Half-Life"))

            def add(kind, path, name, payload, recipe=""):
                digest = hashlib.sha256(payload).hexdigest()
                content_id, asset_id = stable_id("content:" + digest), stable_id("asset:" + path)
                db.execute("INSERT OR IGNORE INTO content VALUES(?,?,?)", (content_id, digest, payload))
                db.execute("INSERT INTO revisions(asset_id,type,virtual_path,publish_path,display_name,source_name,revision,recipe,original,compatible,content_hash) VALUES(?,?,?,?,?,?,1,?,?,?,?)",
                           (asset_id, kind, path, path, name, "showcase-textures.wad" if kind == "texture" else "", recipe, content_id, content_id, digest))
                db.execute("INSERT INTO heads VALUES(?,1,?)", (asset_id, path))

            add("texture", "textures/showcase-textures.wad", "实验室原创纹理", wad.read_bytes(), '{"importer":"wad","version":1}')
            for name, display, factory in MAPS:
                add("map", f"maps/{name}.map", display, factory().encode("utf-8"))
            errors = list(db.execute("PRAGMA foreign_key_check"))
            if errors or db.execute("PRAGMA integrity_check").fetchone()[0] != "ok":
                raise RuntimeError(f"invalid project database: {errors}")
        temp.replace(output)
    finally:
        temp.unlink(missing_ok=True)
    print(f"Wrote {output} with {len(MAPS)} maps")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=PROJECT)
    parser.add_argument("--wad", type=Path, default=WAD)
    parser.add_argument("--force", action="store_true", help="replace an existing output project")
    args = parser.parse_args()
    if args.output.exists() and not args.force:
        parser.error(f"output already exists: {args.output}; use --force to replace it")
    build_project(args.output, args.wad)


if __name__ == "__main__":
    main()
