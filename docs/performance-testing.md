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

For a repeatable **Valve-format GoldSrc map** capture, use PowerShell 7:

```powershell
.\scripts\profile-render.ps1 `
  -MapPath 'D:\steam\steamapps\common\JACK\mapper\Source\kz_skyline.map' `
  -GamePath 'D:\steam\steamapps\common\Half-Life' `
  -Position '3100 1000 320' -Direction '-1 0 0' -WarmupSeconds 8 -Seconds 15 -Play
```

The script builds `TbUiLibTest`, opens a native editor window with background services
and plugins disabled, disables the frame cap in its in-memory test preferences, and
continuously redraws the 3D view. It loads the original map read-only. It does not
save changes to the map or user preferences. `-NoBuild` reuses current binaries;
`-Play` starts the real Play controller from the requested camera position and waits
for collision-world construction before warming up. `-MoveForward` holds W during
measurement and requires `-Play`. Without `-Play`, the camera remains in editor mode.
`-HideModels`, `-HideLabels`, and `-HideEdges` provide diagnostic comparisons; they
change the workload and should not be presented as rendering optimizations. Keep
the same camera, viewport, desktop scale, materials, and driver settings between
captures. Play applies its own FOV, gravity, and collision, so compare Play captures
with other Play captures. The native window captures the mouse until the run ends.
The capture ignores physical keyboard input and sets mouse sensitivity to zero in
test preferences; scripted W input still uses the real view/controller path.
Measurement metadata records the starting and ending camera position and direction.

Results are saved under `build-release-codex/codex-logs/render-profile/<timestamp>/`:
raw `render.jsonl`, `timings.csv`, `counters.csv`, test output/errors, the application log (including asset-load warnings), capture
parameters, measurement UTC boundaries, and a final 3D framebuffer PNG. The table
includes only complete reporting windows inside the measured phase, after the
configurable warmup (default eight seconds). Startup resource uploads and brush
cache rebuilds can last several seconds; inspect the raw log and increase warmup
if they continue into measurement. `WorstWindowP95Ms` is the largest per-window P95, not a recomputed
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
- `cpu.models.visibility`, `cpu.models.entity_setup`, and `cpu.models.submit`
  separate conservative view-frustum checks, per-entity uniform setup, and material
  renderer submission. `models.render_calls` counts submitted entities;
  `models.culled_pass_entities` counts rejected candidates across both opaque and
  transparent passes, so it is not a unique-entity count. Billboard spheres include
  offset origins and model scale; intersecting spheres remain visible.
  The renderer caches the matching model frame alongside each material renderer
  and refreshes both on entity updates, so visibility checks do not reevaluate
  model-definition expressions every frame.
- `cpu.play.update` measures simulation advancement and camera synchronization;
  `play.physics_ticks` counts simulation ticks across frames. `cpu.play.hud_build`
  measures HUD construction, with HUD drawing occurring later in the render batch.

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

### Play investigation: kz_skyline

On 2026-10-20, a Release capture on an RTX 4060 Laptop GPU at 1368x737 compared
the original model-rendering path with conservative model/sprite frustum culling,
cached frame lookup, and removal of unused legacy model-view matrix updates.
Both runs started at `3100 1000 320`, facing `-1 0 0`, and used 15 seconds of
warmup followed by 15 seconds of measurement. Play settled the eye at approximately
`3100 1000 442.03125`. Physical input was filtered and other GPU-heavy work stopped.

| Metric | Original path | Optimized path |
| --- | ---: | ---: |
| FPS | 70.7 | 82.5 |
| CPU frame, mean | 12.13 ms | 10.64 ms |
| GPU frame, mean | 8.67 ms | 8.31 ms |
| Model stage, mean per frame | 6.71 ms | 5.83 ms |
| Submitted model entities per frame | 185 | 138 |
| Play update, mean per frame | 0.04 ms | 0.04 ms |

Holding W in a separate 12-second capture moved the eye to approximately
`2799 934 437`, averaged 86.2 FPS, and submitted about 127 models per frame.
This traversal is a different workload from the stationary comparison.

A diagnostic stationary capture with `-HideModels` reached 139.9 FPS and a
2.09 ms mean GPU frame, compared with 8.31 ms with models visible. That comparison
helps isolate the remaining model-rendering workload; it changes visible content.

The stationary optimized run still spent about 5.3 ms in model material submission,
2.2 ms drawing faces, and 1.6 ms drawing edges. It submitted 3406 logical face draws
per frame, including 1706 transparent items. Further work should investigate model
material/state submission and GoldSrc brush batching while preserving transparency
and conveyor behavior. Physics was a small fraction of steady-state frame time.
CPU submission can include driver stalls, so these measurements do not establish
that CPU work alone limits the frame rate.

Some WAD references in this local map were unavailable; the captures establish a
comparison for that asset set, not a promise of the same FPS on a fully resolved map
or another computer. Compare final framebuffer images and asset warnings with each
new run. Timing probes also add overhead. Resource-loading brush-cache rebuilds in
early windows must be separated from steady-state Play costs.

Qt references: [timer query availability](https://doc.qt.io/qt-6/qopengltimerquery.html#isResultAvailable),
[composition and swap signals](https://doc.qt.io/qt-6/qopenglwidget.html#signals).
