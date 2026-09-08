"""Measure compact tools/list JSON-RPC bytes from the baseline and working tree.

Run in a configured C++ build environment. Only the catalog and Qt Core are
compiled, in an isolated directory under codex-logs; no editor is launched.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


BASELINE = "d98f869503f331809cd6727712438534c16c8c9e"
FILES = (
    "include/mcp/McpMode.h",
    "include/mcp/McpToolCatalog.h",
    "src/McpMode.cpp",
    "src/McpToolCatalog.cpp",
    "src/McpToolCatalogSerialization.cpp",
)
MAIN = r'''
#include <QJsonDocument>
#include "mcp/McpToolCatalog.h"
#include <iostream>
int main()
{
  const auto tools = tb::mcp::toolsListJson(tb::mcp::McpMode::Edit);
  const auto bytes = QJsonDocument{QJsonObject{
    {"jsonrpc", "2.0"}, {"id", 1}, {"result", QJsonObject{{"tools", tools}}}
  }}.toJson(QJsonDocument::Compact);
  std::cout.write(bytes.constData(), bytes.size());
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qt-prefix", required=True)
    parser.add_argument(
        "--output", default="build-release-codex/codex-logs/mcp-discovery"
    )
    args = parser.parse_args()
    repo = Path(__file__).resolve().parent.parent
    output = (repo / args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    hashes = {}
    cmake = [
        "cmake_minimum_required(VERSION 3.24)",
        "project(McpDiscoveryMeasurement LANGUAGES CXX)",
        "set(CMAKE_CXX_STANDARD 20)",
        "find_package(Qt6 REQUIRED COMPONENTS Core)",
    ]
    for name in ("baseline", "current"):
        files = FILES + (("src/McpToolCatalogInternal.h",) if name == "baseline" else ())
        for relative in files:
            source = "lib/TbMcpLib/" + relative
            data = (
                subprocess.check_output(["git", "show", BASELINE + ":" + source], cwd=repo)
                if name == "baseline" else (repo / source).read_bytes()
            )
            target = output / name / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            hashes[name + "/" + relative] = hashlib.sha256(data).hexdigest()
        (output / name / "main.cpp").write_text(MAIN, encoding="utf-8")
        sources = " ".join(name + "/" + f for f in FILES if f.endswith(".cpp"))
        cmake += [
            f"add_executable({name} {name}/main.cpp {sources})",
            f"target_include_directories({name} PRIVATE {name}/include)",
            f"target_link_libraries({name} PRIVATE Qt6::Core)",
        ]
    (output / "CMakeLists.txt").write_text("\n".join(cmake), encoding="utf-8")
    build = output / ("build-msvc" if os.name == "nt" else "build")
    compiler = ["-DCMAKE_CXX_COMPILER=cl"] if os.name == "nt" else []
    subprocess.run([
        "cmake", "-S", str(output), "-B", str(build), "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_PREFIX_PATH=" + args.qt_prefix, *compiler,
    ], check=True)
    subprocess.run(["cmake", "--build", str(build)], check=True)
    sizes = {}
    counts = {}
    for name in ("baseline", "current"):
        executable = build / (name + (".exe" if os.name == "nt" else ""))
        payload = subprocess.check_output([str(executable)])
        (output / (name + "-tools-list.json")).write_bytes(payload)
        sizes[name] = len(payload)
        counts[name] = len(json.loads(payload)["result"]["tools"])
    ratio = sizes["current"] / sizes["baseline"]
    passed = sizes["current"] <= 16 * 1024 and ratio <= 0.20
    report = {
        "baselineCommit": BASELINE,
        "headCommit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo).decode().strip(),
        "measurement": "UTF-8 compact JSON-RPC tools/list response, id=1, Edit; baseline default Modeling; excludes HTTP headers",
        "bytes": sizes, "toolCounts": counts, "payloadRatio": ratio,
        "sourceSha256": hashes, "passed": passed,
    }
    text = json.dumps(report, ensure_ascii=False, indent=2)
    (output / "report.json").write_text(text + "\n", encoding="utf-8")
    print(text)
    if not passed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
