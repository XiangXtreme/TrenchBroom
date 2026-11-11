"""Vendor or verify the exact source closure of hltools' embedded collision target."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, help="local hltools checkout (update only)")
    parser.add_argument("--check", action="store_true", help="verify bundled source hashes")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    destination = root / "lib/GoldSrcCollisionLib/thirdparty/hltools"
    manifest_path = destination / "manifest.json"

    if args.check:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        for name, expected in manifest["files"].items():
            data = (destination / name).read_bytes()
            if digest(data) != expected:
                raise SystemExit(f"Modified bundled source: {name}; update in hltools and sync")
        actual = {p.relative_to(destination).as_posix() for p in destination.rglob("*")
                  if p.is_file() and p != manifest_path}
        if actual != manifest["files"].keys():
            raise SystemExit("Bundled source inventory differs from manifest")
        print(f"Verified {len(actual)} files from hltools {manifest['revision']}")
        return

    if args.source is None:
        parser.error("--source is required when updating")
    source = args.source.resolve()
    status = subprocess.check_output(["git", "-C", str(source), "status", "--porcelain"])
    if status:
        raise SystemExit("Commit the hltools changes before syncing a reproducible revision")
    revision = subprocess.check_output(
        ["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    sources = "src/collision/sources.cmake"
    todo = re.findall(r"src/[\w/]+\.cpp", (source / sources).read_text())
    todo += [sources, "src/collision/CMakeLists.txt", "src/collision/collision.h",
             "src/collision/test.cpp", "src/collision/README.md",
             "src/collision/verify_cli_parity.py", "LICENSE"]
    files = {}
    while todo:
        name = todo.pop()
        if name in files:
            continue
        path = (source / name).resolve()
        if not path.is_relative_to(source):
            raise SystemExit(f"Include outside source tree: {name}")
        data = path.read_bytes()
        # Checkout line endings are not part of upstream source identity.
        data = data.replace(b"\r\n", b"\n")
        files[name] = data
        for include in re.findall(rb'^\s*#include\s+"([^"\r\n]+)"', data, re.MULTILINE):
            include = include.decode("utf-8")
            candidate = path.parent / include
            if not candidate.exists():
                candidate = source / "src" / include
            todo.append(candidate.resolve().relative_to(source).as_posix())

    previous = json.loads(manifest_path.read_text()) if manifest_path.exists() else {"files": {}}
    for name in previous["files"].keys() - files.keys():
        (destination / name).unlink()
    for name, data in files.items():
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    manifest = {"revision": revision, "files": {name: digest(data) for name, data in sorted(files.items())}}
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Synced {len(files)} files from hltools {revision}")


if __name__ == "__main__":
    main()
