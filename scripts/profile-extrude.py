"""Reproduce split-extrusion timings with temporary C++ probes (Windows Release).

The patch is deliberately separate from production code. `run` builds and samples
it, then reverses only that patch and rebuilds the ordinary test executable.
`apply` / `restore` are available for manual investigation. Do not build the same
build tree from another process while this script is running.
"""

import argparse
import csv
import datetime
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess


ROOT = Path(__file__).resolve().parent.parent
PATCH = ROOT / "scripts/performance/extrude-profile.patch"


def command(args, **kwargs):
    return subprocess.run(args, cwd=ROOT, check=True, **kwargs)


def apply():
    command(["git", "apply", "--check", str(PATCH)])
    command(["git", "apply", str(PATCH)])


def restore():
    # Reverse hunks only; never discard unrelated working-tree changes.
    command(["git", "apply", "--reverse", "--check", str(PATCH)])
    command(["git", "apply", "--reverse", str(PATCH)])


def build(build_dir):
    command([
        "powershell", "-ExecutionPolicy", "Bypass", "-File",
        "scripts/build-filtered.ps1", "-BuildDir", str(build_dir),
        "-Target", "TbUiLibTest",
    ])


def summarize(output):
    result = []
    for path in sorted(output.glob("**/*.samples.csv")):
        with path.open(newline="", encoding="utf-8") as stream:
            samples = list(csv.DictReader(stream))
        frames = sorted(float(sample["frame_ms"]) for sample in samples)
        result.append({
            "sample": str(path.relative_to(output)),
            "count": len(frames),
            "mean_ms": statistics.mean(frames),
            "median_ms": statistics.median(frames),
            "p95_ms": frames[max(0, (len(frames) * 95 + 99) // 100 - 1)],
            "drag_ms": statistics.mean(float(s["drag_ms"]) for s in samples),
            "events_ms": statistics.mean(float(s["events_ms"]) for s in samples),
        })
    (output / "summary.json").write_text(
        json.dumps(result, indent=2), encoding="utf-8"
    )
    return result


def sample(build_dir, output, repeats, counts, qt_bin):
    test_dir = build_dir / "lib/TbUiLib/test"
    for folder in ("shader", "fonts", "stylesheets"):
        shutil.copytree(
            ROOT / "app/TrenchBroom/resources" / folder,
            test_dir / folder, dirs_exist_ok=True,
        )
    env = os.environ.copy()
    for key in list(env):
        if key.startswith("TB_PROFILE_"):
            del env[key]
    env.update({
        "PATH": str(qt_bin) + os.pathsep + env["PATH"],
        "QT_SCALE_FACTOR": "1",
        "TB_PROFILE_STYLE": "1",
        "TB_PROFILE_EVENTS_ONLY": "1",
    })
    scenarios = {
        "baseline": {},
        "skip-tree-selection": {"TB_PROFILE_SKIP_TREE": "1"},
        "skip-property-rebuild": {"TB_PROFILE_SKIP_PROPERTIES": "1"},
        "skip-both": {
            "TB_PROFILE_SKIP_TREE": "1", "TB_PROFILE_SKIP_PROPERTIES": "1",
        },
        "model": {},
    }
    metadata = {
        "base": command(["git", "rev-parse", "HEAD"], capture_output=True,
                        text=True).stdout.strip(),
        "repeats": repeats, "brushes": counts, "qt_bin": str(qt_bin),
        "method": "20 warmup + 120 effective controller drag updates per mode",
        "render": "processEvents, no framebuffer readback, no pacing or vsync wait",
    }
    output.mkdir(parents=True, exist_ok=True)
    (output / "metadata.json").write_text(
        json.dumps(metadata, indent=2), encoding="utf-8"
    )
    for repeat in range(1, repeats + 1):
        for scenario, overrides in scenarios.items():
            run_dir = output / f"run-{repeat}" / scenario
            run_dir.mkdir(parents=True, exist_ok=True)
            for count in counts:
                run_env = env | overrides | {
                    "TB_PROFILE_MODE": "model" if scenario == "model" else "window",
                    "TB_PROFILE_BRUSHES": str(count),
                    "TB_PROFILE_OUTPUT": str(run_dir),
                }
                log = run_dir / f"brushes-{count}.log"
                with log.open("w", encoding="utf-8") as stream:
                    command(
                        [str(test_dir / "TbUiLibTest.exe"), "Extrude drag profile"],
                        env=run_env, stdout=stream, stderr=subprocess.STDOUT,
                    )
                print(f"Passed: run {repeat}, {scenario}, {count} brushes", flush=True)
    summarize(output)
    print(f"Results: {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("run", "apply", "restore", "sample"),
                        default="run", nargs="?")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build-release-codex")
    parser.add_argument("--qt-bin", type=Path,
                        default=Path("D:/Qtx/6.11.1/msvc2022_64/bin"))
    parser.add_argument("--repeat", type=int, default=3)
    parser.add_argument("--brushes", type=int, nargs="+", default=[1, 64])
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    args.build_dir = args.build_dir.resolve()
    if args.repeat < 1 or any(count < 1 or count > 4096 for count in args.brushes):
        parser.error("repeat must be positive; brush counts must be in 1..4096")
    if args.action == "apply":
        apply()
        return
    if args.action == "restore":
        restore()
        return
    if not (args.build_dir / "CMakeCache.txt").exists():
        parser.error("An existing configured Release build tree is required")
    output = args.output or args.build_dir / "codex-logs" / (
        "extrude-profile-" + datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    )
    output = output.resolve()
    if args.action == "sample":
        sample(args.build_dir, output, args.repeat, args.brushes, args.qt_bin)
        return
    apply()
    try:
        build(args.build_dir)
        sample(args.build_dir, output, args.repeat, args.brushes, args.qt_bin)
    finally:
        restore()
        build(args.build_dir)


if __name__ == "__main__":
    main()
