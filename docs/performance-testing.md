# Performance Testing

`TbPerf` is a small Release-only benchmark runner for custom features that are likely
to affect editor responsiveness. It is intentionally separate from Catch2 tests so
normal correctness tests stay fast and deterministic.

## Quick Start

```powershell
scripts\perf-codex.ps1
```

The script builds `TbPerf`, runs the default `mcp` suite, prints a compact table, and
writes JSON to:

```text
build-release-codex\perf\latest.json
```

List available benchmarks:

```powershell
scripts\perf-codex.ps1 -List
```

Run without rebuilding:

```powershell
scripts\perf-codex.ps1 -NoBuild -Iterations 1000
```

Compare against an older run and fail if the median time regresses by more than 10%:

```powershell
scripts\perf-codex.ps1 -Baseline build-release-codex\perf\baseline.json -MaxRegressionPercent 10
```

## Current Coverage

The first suite focuses on MCP catalog and JSON-RPC response construction:

- `mcp.default_tool_catalog`
- `mcp.tools_list_json_edit`
- `mcp.tool_diagnostics_json_edit`
- `mcp.initialize_result`
- `mcp.tools_list_result_edit`
- `mcp.find_all_tools`

These benchmarks are useful because MCP is active during editor sessions and can
accidentally add UI-thread cost if tool catalog or response generation becomes too
heavy.

## Expansion Roadmap

Good next benchmark suites:

- `selection`: 2D box selection candidate filtering on synthetic map sizes.
- `asset`: GoldSrc `.mdl/.spr/.wav` asset scanning and preview state loading.
- `render`: frame timing probes for skybox, readable outlines, and overlay passes.
- `python`: plugin manifest scan, direct script load, timer cleanup.

Each benchmark should prefer stable model-level code over live UI automation.
UI frame timing uses the explicit capture below.

## Live render-chain capture

Set `TB_RENDER_PROFILE=1` before starting TrenchBroom to append per-view JSONL
statistics to `TrenchBroom.log.render.jsonl` beside the normal application log.
Alternatively set it to an absolute JSONL filename. Open the map and reproduce the
slow camera movement or Play session. Unset the variable to disable profiling;
normal runs allocate no collectors and do not read timing clocks in stage probes.
Each active view writes one summary per second plus a partial summary on shutdown.

```powershell
$env:TB_RENDER_PROFILE = 'D:\captures\skyline.jsonl'
& .\build-release-codex\app\TrenchBroom\TrenchBroom.exe
Remove-Item Env:TB_RENDER_PROFILE
```

For a repeatable stationary **Valve-format GoldSrc map** capture, use PowerShell 7:

```powershell
.\scripts\profile-render.ps1 `
  -MapPath 'D:\steam\steamapps\common\JACK\mapper\Source\kz_skyline.map' `
  -GamePath 'D:\steam\steamapps\common\Half-Life' `
  -Position '3100 1000 320' -Direction '-1 0 0' -Seconds 15
```

The script builds `TbUiLibTest`, opens a native editor window with background services
and plugins disabled, disables the frame cap in its in-memory test preferences, and
continuously redraws the 3D view. It loads the original map read-only. It does not
save changes to the map or user preferences. `-NoBuild` reuses current binaries;
`-HideLabels` and `-HideEdges` provide controlled comparisons. Keep the same camera,
viewport, desktop scale, materials, and driver settings between captures. This is
not a scripted Play physics benchmark; capture the normal app for that scenario.

Results are saved under `build-release-codex/codex-logs/render-profile/<timestamp>/`:
raw `render.jsonl`, `timings.csv`, `counters.csv`, test output/errors, the application log (including asset-load warnings), capture
parameters, and a final 3D framebuffer PNG. The table excludes the first two reporting windows and a short
shutdown window. `WorstWindowP95Ms` is the largest per-window P95, not a recomputed
P95 across the entire run. Check test output and the PNG for missing assets before
comparing results. WAD/model availability affects workload.

The raw log separates:

- `frame.interval`: time between starts of rendered frames; includes event-loop,
  frame-cap, composition, and driver scheduling. FPS uses frames/report duration.
- `cpu.frame`: the `RenderView::render()` wall time, with inclusive child stages for
  input, map/camera simulation, scene construction, overlays, batch preparation and
  draw, face/transparent-sort/edge/model/link/text work. Scene stages measure queuing;
  the batch stages measure preparation and actual submission. CPU wall time can
  include driver waits. Nested values **must not be added together**.
- `gpu.frame`: asynchronous `GL_TIME_ELAPSED` covering the view's render commands.
  A ring of four queries is polled without waiting; results are retrieved only when
  available. Unsupported drivers omit this metric; a full ring increments
  `gpu.skipped_queries`. Delayed results can appear in a later reporting window;
  pending results at shutdown are discarded. This is not a CPU/GPU queue-latency metric.
- `present.render_to_compose`, `present.compose_swap`, `present.frame_to_swap`:
  intervals from render completion to Qt composition, composition to `frameSwapped`,
  and render start to `frameSwapped`. They describe Qt composition/swap return,
  not display scanout or physical input-to-photon latency. They can overlap GPU work.
  Repaints replaced before composition increment `present.replaced_frames`.
- Counters such as `faces.draw_calls`, material groups, opaque/transparent items,
  and batch size help explain the CPU submission cost. These are logical renderer
  calls/items, not a driver-level capture of every OpenGL draw in the application.

Each timing contains `samples`, `avg`, `p95`, and `max` in milliseconds. CPU stages
also contain `per_frame_avg`, dividing total stage time by all frames, including
frames where the stage did not execute. Metadata records GPU/GL identity and the
requested swap interval (the driver can override it). Profiling adds CPU clock,
collector, GPU query, and once-per-second file I/O overhead; discard startup and
compare with profiling disabled when measuring a final optimization.

The existing on-screen `Frame` statistic is the **maximum frame interval in the
last reporting window**, not average rendering time. For example, 80 FPS implies
an average interval near 12.5 ms while a 27 ms maximum indicates an occasional long
frame. Use the separate CPU/GPU/presentation metrics to locate that cost.

Qt references: [timer query availability](https://doc.qt.io/qt-6/qopengltimerquery.html#isResultAvailable),
[composition and swap signals](https://doc.qt.io/qt-6/qopenglwidget.html#signals).
