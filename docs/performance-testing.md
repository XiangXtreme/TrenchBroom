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
continuously redraws the 3D view. It uses the application's OpenGL defaults, including
4x MSAA and a zero swap-interval request. The application uses its FPS preference
for frame limiting; the platform or driver can override the swap-interval request.
It loads the original map read-only. It does not
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

Add `-LoadTiming` to write `render.jsonl.loading.json`. Its cumulative milestones
start immediately before `MapDocument::loadDocument`, excluding process startup,
test setup, and application-service construction: document load returned, window
construction returned, first 3D `frameSwapped`, and editor resources settled. Resource
settling requires no pending resource processing or resource-count changes for
250 ms and at least two presented frames. Failed resources also settle; this is
completion of the available workload, not proof that every referenced asset exists.
With `-Play`, the file additionally records the separate Play activation duration
and cumulative readiness after a second resource-settling check. These checks add
quiet intervals and alter startup ordering; use this mode for loading measurements,
not comparisons with earlier steady-state FPS captures. Warmup and measurement
durations are outside the recorded loading time. Fresh processes still benefit
from OS file caches; this mode does not establish cold-disk startup performance.

On 2026-10-21, three fresh-process loading captures of `kz_skyline.map` with the
Half-Life asset directory and camera `3100 1000 320`, direction `-1 0 0`, reported:

| Milestone (seconds from document-load start) | Play run 1 | Play run 2 | Editor only |
| --- | ---: | ---: | ---: |
| Document load returned | 0.533 | 0.550 | 0.729 |
| Window construction returned | 1.035 | 1.055 | 1.347 |
| First 3D frame swapped | 2.612 | 2.613 | 3.277 |
| Editor resources settled | 5.918 | 6.239 | 7.175 |
| Editor plus Play resources settled | 13.234 | 13.096 | n/a |

Play activation alone took 7.054 / 6.601 seconds, while the collision-builder log
reported only 1.515 / 1.508 seconds; collision build time is not the entire Play
startup time. The remaining activation time has not been separately attributed.
Captures are `20261021-010511-893`, `20261021-010554-751`, and
`20261021-010622-322` under the render-profile log directory. Each used
`-LoadTiming -NoBuild -WarmupSeconds 1 -Seconds 5`, with `-Play` for the first two.
These are diagnostic, file-cache-dependent measurements with the same six missing
WAD references recorded in earlier captures, not a complete-asset or cold-start
guarantee. Readiness includes one 250 ms quiet check for the editor and another for
Play; actual display scanout and assets requested by later camera movement are not
measured.

The raw log separates:

- `frame.interval`: time between starts of rendered frames; includes event-loop,
  frame-cap, composition, and driver scheduling. FPS uses frames/report duration.
- `cpu.frame`: the `RenderView::render()` wall time, with inclusive child stages for
  input, map/camera simulation, scene construction, overlays, batch preparation and
  draw, face/transparent-sort/edge/model/link/text work. Scene stages measure queuing;
  the batch stages measure preparation and actual submission. CPU wall time can
  include driver waits. Nested values **must not be added together**.
- `cpu.paint_event`: the surrounding `QOpenGLWidget::paintEvent()` wall time,
  including Qt's context/framebuffer preparation and the render call. It overlaps
  `cpu.frame`; do not add them. `cpu.profile.begin_frame` measures the profiler's
  initial bookkeeping and query setup, excluding first-frame metadata output.
- `present.paint_to_render` and `present.render_to_paint_end`: the parts of that
  paint event before and after rendering. `present.swap_to_paint` measures from a
  tracked Qt swap return to the next accepted paint event. It can include input,
  event handling, frame limiting and driver scheduling, rather than just idle time.
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
  `faces.transparent_state_runs` counts consecutive transparent items that share
  material and all entity settings. Each run activates state once, while retaining
  the individual sorted draw calls and their index ranges.
- `gl.frame.*`, `gl.models.*`, `gl.faces.*`, and `gl.edges.*` count API submissions
  through `GlQt` in those scopes. A multi-draw is one `draw_calls` entry but contributes
  each subrange to `draw_ranges`. `submitted_vertices` sums array counts and index
  counts, including repeated indices and degenerate padding; it is neither a unique
  vertex count nor a visible triangle count. Buffer allocation can change padding
  and adjacent batch sizes between runs. Texture binds/parameters, uniform calls,
  buffer binds, and vertex attribute changes expose state traffic. Pass scopes are
  inclusive; do not add them to the frame counters. Qt's own compositor GL calls and
  resource uploads outside the view's render scope are not included. Fixed arrays
  collect this traffic without per-call clocks, map lookups, or driver queries, and
  remain inactive when profiling is disabled.
- Add `-GlCallTiming` (or set `TB_RENDER_PROFILE_GL_TIMING=1` alongside
  `TB_RENDER_PROFILE`) to time forwarded GL calls by category. Metadata records
  `gl_call_timing`; `cpu.gl.frame.*` and the corresponding pass scopes report CPU
  wall time inside draw, uniform, texture, buffer, attribute, state-stack, and
  selected pipeline-state calls. These include driver processing/waits and clock
  overhead, not GPU stage time. Pipeline-state coverage includes program, texture
  unit, blend/depth/raster state, and legacy matrix updates; it is not an exhaustive
  trace of every GL function. Scopes are inclusive. The extra clocks can substantially
  perturb performance: use this mode for attribution, then disable it for before/after
  FPS comparisons. Ordinary workload counting does not take these per-call clocks.
- Metadata includes the global `default_samples_requested` and
  `default_swap_interval_requested`, plus the view context's `context_samples`,
  `surface_depth_bits` and `swap_interval_requested`. An offscreen widget context
  need not report the top-level compositing surface's swap setting. Neither value
  proves what the driver presents. `framebuffer_samples` queries `GL_SAMPLES` in
  the view's currently bound FBO: context samples can be zero while the widget
  framebuffer still uses MSAA.
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

### Submission and opaque batching follow-up

The same stationary Play scene was measured again with GL API counters. A
12-second baseline after 15 seconds of warmup averaged 84.0 FPS, with a 10.46 ms
CPU frame and 8.35 ms elapsed GPU query. The model pass submitted 138 entities as
833 actual GL draws, 116763 vertex references, 4998 attribute changes, and 3652
uniform calls per frame. Faces contributed 3406 draws and 33607 uniform calls.

Two changes retain the same rendering content:

- Texture objects cache their effective minification/magnification filters. Masked
  filtering, single-level fallbacks, and runtime filter/alpha-domain changes still
  apply. The cache follows the shared texture object across binds and moves.
- During brush-cache validation, opaque ranges are sorted by material and index
  position. Adjacent ranges merge only when their material and entity settings
  match, including conveyor speed. Index gaps remain separate. Transparent ranges
  retain their per-item distance sort.

| Per-frame metric | Baseline | Filters cached | Filters cached + opaque batching |
| --- | ---: | ---: | ---: |
| FPS | 84.0 | 83.7 | 107.2 / 107.6 |
| CPU frame, mean | 10.46 ms | 10.35 ms | 8.10 / 8.01 ms |
| GPU query, mean | 8.35 ms | 8.24 ms | 2.08 / 2.00 ms |
| Opaque face stage | 1.02 ms | 1.07 ms | 0.26 / 0.25 ms |
| Model submission stage | 5.26 ms | 5.00 ms | 4.78 / 4.80 ms |
| GL draws | 4266 | 4266 | 3001 / 2996 |
| GL uniform calls | 37458 | 37458 | 24023 / 23971 |
| GL texture parameter calls | 8478 | 0 | 0 |
| GL buffer binds | 8556 | 8556 | 6026 / 6016 |

The two final captures used 15 seconds each after the same warmup. Their opaque
range counts were 321 and 316, compared with 1586 before merging; the transparent
item count stayed at 1706. Filter caching alone produced no measurable FPS gain
in this sample. Batching reduced actual GL traffic and improved FPS by about 28%.
The camera and model workload stayed fixed. A framebuffer comparison against the
baseline changed 0.24% of pixels, mostly small edge/depth-order differences, with
no missing geometry apparent in the inspected images. Real-GL preview tests cover
glass, render color, additive sprites, and conveyor animation; mock rendering tests
also verify merging and splitting after entity ownership/settings changes.

GPU telemetry during the first final run showed roughly 35-41% utilization and
480-510 MHz graphics clocks around measurement start; clocks were not locked.
These are whole-device, coarse samples, not process-specific GPU busy times.
An elapsed GPU query can include scheduling and pipeline idle intervals, so the
drop in that query is not proof of a fourfold reduction in shader work or a fixed
500 FPS hardware ceiling. The remaining model submission wall time is the largest
measured stage. A driver timeline is needed to separate host state processing from
waits before choosing instancing, state reuse, or an API migration.

Local capture directories: baseline `20261020-231840-278`, filters only
`20261020-232403-358`, final `20261020-232653-055` and `20261020-232823-497`.
Generated logs and image comparisons remain outside version control.

### Single-range draw dispatch follow-up

Opt-in GL call timing narrowed the remaining model submission cost to the draw
API itself. In an instrumented capture, model draw calls accounted for 8.14 ms
of a 9.84 ms submission stage; model attribute changes took 0.29 ms, uniforms
0.18 ms, and each binding category under 0.1 ms. That instrumented run averaged
61.6 FPS, so its timings must not be compared directly with ordinary captures.

`VertexArray` previously used `glMultiDrawArrays` for its range-list overload even
when the list contained exactly one range. It now uses `glDrawArrays` for that
case and retains multi-draw for multiple ranges, preserving their topology and
boundaries. This follows the [OpenGL 2.1 specification](https://registry.khronos.org/OpenGL/specs/gl/glspec21.pdf):
multi-draw behaves as separate draw-array calls over the supplied ranges.

The same stationary Play scene was captured with extra GL call timing disabled:

| Per-frame metric | Original dispatch, repeated | Single-range dispatch | Single-range dispatch, repeated |
| --- | ---: | ---: | ---: |
| FPS | 107.0 | 140.9 | 137.1 |
| CPU frame, mean | 8.01 ms | 3.98 ms | 4.68 ms |
| Model submission | 4.69 ms | 0.34 ms | 0.40 ms |
| Model stage | 5.14 ms | 0.70 ms | 0.84 ms |
| Edge stage | 0.53 ms | 0.01 ms | 0.01 ms |
| Transparent face stage | 1.18 ms | 1.17 ms | 1.34 ms |
| GPU elapsed query | 1.96 ms | 1.64 ms | 1.85 ms |

The first optimized capture was followed by a source-level reversion of only the
dispatch change, an incremental rebuild, and the original-dispatch capture. The
change was then restored, rebuilt, and captured again. Each capture warmed up for
15 seconds; the first two measured 12 seconds and the last measured 15 seconds.
The last capture had a 40.42 ms maximum CPU frame, including a 28.91 ms text
preparation spike, so the mean improvement does not imply all hitches are resolved.

All three submitted 138 model entities, 833 model draws, and 116763 model vertex
references per frame. Model state traffic also stayed fixed. The benefit is a
less costly driver dispatch path on this machine, rather than fewer models or
draws; it is not a universal multi-draw performance claim. Opaque brush range
counts vary slightly with allocation, as in the previous comparison. Camera
metadata matched. The inspected before/after framebuffer comparison changed
4241 pixels (0.42%), mainly the focus border; excluding a two-pixel border left
35 differing pixels, with no missing geometry apparent.

Remaining ordinary captures spend about 1.2-1.3 ms in transparent faces. The frame
interval also exceeds render-to-swap-return time by roughly 2-3 ms in the optimized
captures. Event processing, update scheduling and driver behavior outside the
measured render scope need separate attribution before inferring an FPS ceiling
or choosing an API migration.

Capture directories: instrumented `20261020-234046-550`, original dispatch
`20261020-234501-794`, optimized `20261020-234259-644` and
`20261020-234634-776`.

Some WAD references in this local map were unavailable; the captures establish a
comparison for that asset set, not a promise of the same FPS on a fully resolved map
or another computer. Compare final framebuffer images and asset warnings with each
new run. Timing probes also add overhead. Resource-loading brush-cache rebuilds in
early windows must be separated from steady-state Play costs.

### Paint boundaries and refresh synchronization follow-up

The external-map runner now shares the application's GL default-format setup.
Earlier captures above did not explicitly request the application's 4x MSAA in
the test executable. They support their within-configuration comparisons, but
must not be used as strict before/after baselines against this new configuration.

New paint-event boundaries exposed time outside `RenderView::render()`. The
machine's active display was 144 Hz, and the previous default swap interval of
one coincided with a 6.95 ms frame interval even with the FPS preference set to
Unlimited. Startup now requests interval zero so RenderView's FPS preference
controls the application cap. All other default format settings are preserved.

Stationary Play captures at the same camera and 1368x737 viewport, with 15 seconds
of warmup and 12 seconds of measurement, compared these startup requests:

| Metric | Interval 1 | Interval 0 | Interval 0, repeated | Interval 1, restored |
| --- | ---: | ---: | ---: | ---: |
| FPS | 143.8 | 193.4 | 193.8 | 141.3 |
| Frame interval, mean | 6.95 ms | 5.08 ms | 5.07 ms | 6.95 ms |
| CPU frame, mean | 3.94 ms | 4.02 ms | 3.96 ms | 4.11 ms |
| Paint event, mean | 5.55 ms | 4.46 ms | 4.42 ms | 5.59 ms |
| Paint entry to render | 1.57 ms | 0.40 ms | 0.43 ms | 1.45 ms |
| Swap return to next paint | 1.11 ms | 0.34 ms | 0.36 ms | 1.06 ms |
| GPU elapsed query | 1.62 ms | 1.42 ms | 1.57 ms | 1.68 ms |

Capture directories, in table order: `20261021-000358-155`,
`20261021-000720-216`, `20261021-001104-531`, and `20261021-001331-818`.
The source-level restoration changed only the swap-interval request. All runs
requested 4x MSAA; the new FBO query confirmed four samples in both zero-interval
captures and the restored one-interval capture. The widget context still reported
interval one, so that context value alone did not describe top-level presentation.

All four captures submitted 138 models, 833 model draws and 116763 model vertex
references per frame, with 1706 transparent brush items. Opaque range counts vary
with allocation. The first and third framebuffer images differed in 173 pixels
(0.017%); inspection showed the same scene. The repeated zero-interval result is
about 35% faster than the first one-interval baseline without reducing requested
quality. These are rendered-frame throughput measurements, not a measured change
in display scanout or input latency. Platforms and driver settings can override
the synchronization request.

A final zero-interval capture with W held (`20261021-001527-584`) averaged
191.6 FPS, a 5.16 ms frame interval, 4.09 ms CPU frame and 1.43 ms GPU query.
The eye moved from `3100 1000 442.03125` to approximately `2799 934 437` and
submitted about 127 models per frame. This validates the moving Play path but
is a different workload from the stationary comparison. Its metadata recorded
default interval zero and four actual FBO samples.

Remaining steady-state work includes about 1.2 ms of transparent face processing
(including 0.24 ms of sorting), with CPU frame wall time near 4 ms. This
identifies a next target for reducing state changes while preserving draw order;
it does not establish a hardware FPS ceiling or require an API migration.

### Sorted transparent state reuse follow-up

The transparent pass now retains material, entity uniforms and index-buffer state
across consecutive items with identical material and entity settings. It still
sorts every frame and submits each item's original range as a separate draw.
Material, render mode, amount, color or conveyor-speed changes end the run before
new settings are installed, preserving the material/entity state-stack pairing.
The opaque range merger uses the same settings equality. Alpha-test uniforms are
also applied only by the material callback instead of twice per activation.

The stationary Play scene was captured with the same camera, 1368x737 viewport,
four actual FBO samples and interval-zero request as the previous section.
Extra per-GL-call timing remained disabled. Each run warmed up for 15 seconds.

| Per-frame metric | Baseline | State reuse | State reuse + duplicate uniforms removed |
| --- | ---: | ---: | ---: |
| FPS | 193.7 | 210.4 | 219.0 |
| CPU frame, mean | 4.02 ms | 3.61 ms | 3.46 ms |
| Transparent faces, including sort | 1.18 ms | 0.97 ms | 0.91 ms |
| Transparent sort | 0.25 ms | 0.25 ms | 0.25 ms |
| Transparent items | 1706 | 1706 | 1706 |
| Transparent state activations | 1706 | 900 | 901 |
| Face uniform calls | 20215 | 12903 | 11371 |
| Face buffer binds | 4290 | 2686 | 2680 |
| Face texture binds | 4286 | 2682 | 2676 |
| Model draws | 833 | 833 | 833 |

These 12-second captures are `20261021-002744-696`, `20261021-003250-703`
and `20261021-003521-575`. The baseline activated state once per transparent item;
the new counter records runs directly. Opaque range/padding counts and the order
of equal-distance transparent items can vary with allocation, so total face traffic
is not identical between runs. All submitted 138 model entities and 116763 model
vertex references per frame. The baseline and final framebuffer differed in 165
pixels (0.016%); inspection showed the same scene.

A 12-second final-version repeat (`20261021-003621-881`) averaged 182.0 FPS,
including a one-second reporting window at 24.4 FPS. Its CPU frame maximum was
32.38 ms and frame-interval maximum 61.16 ms, with stalls across preparation,
rendering and presentation. Other windows were mostly 205-221 FPS. The cause of
this transient remains unresolved; it is retained in the evidence and the faster
sample does not establish that frame pacing or all hitches are fixed.

A longer 15-second final-version repeat (`20261021-003753-749`) averaged
220.8 FPS, a 4.47 ms frame interval, 3.47 ms CPU frame and 0.91 ms transparent
face processing (including 0.25 ms sorting). Its maximum frame interval was
7.61 ms; the earlier long stall did not recur in this capture. It retained 1706
transparent items with 900 state activations, 833 model draws and four FBO samples.

The final W-held Play capture (`20261021-003901-598`, 12 seconds) averaged
214.4 FPS, a 4.62 ms frame interval and 3.60 ms CPU frame. It followed the same
approximately `3100 1000 442` to `2799 934 437` traversal as the earlier moving
capture, submitting about 127 models per frame. Transparent processing averaged
0.94 ms. This moving workload is separate from the stationary comparisons.

Regression tests cover camera-dependent draw order, an intervening material,
same-material entity amount/color/mode/conveyor changes, the return to the batch
fade, and state cleanup across frames. Real-GL preview tests exercise glass,
render color, additive sprites and conveyor animation.

Qt references: [timer query availability](https://doc.qt.io/qt-6/qopengltimerquery.html#isResultAvailable),
[composition and swap signals](https://doc.qt.io/qt-6/qopenglwidget.html#signals),
[swap interval requests](https://doc.qt.io/qt-6/qsurfaceformat.html#setSwapInterval).
GL references: [timer query semantics](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_timer_query.txt),
[texture parameters](https://wikis.khronos.org/opengl/GLAPI/glTexParameter).

### Material-batch invalidation during loading

Loading diagnostics also bracket the resource-notification observers installed
during document/window construction. `resource_notification_ms` is their cumulative
wall time, `resource_notification_max_ms` is the slowest batch, and
`resource_notification_batches` counts batches up to the readiness milestone.
These timings overlap the cumulative loading milestones. With `-LoadTiming`, the
application log additionally reports slow GL resource polls split into context
setup, resource processing including notifications, and cleanup.

The readable 2D brush-outline path previously invalidated the entire brush
renderer for every face matching a changed material. This repeatedly walked all
brushes and copied the invalid-brush set, even when those brushes were already
invalid. A material batch now clears each affected brush's vertex cache once and
invalidates the shared edge stream once after visiting all affected brushes.
The ordinary outline path invalidates only affected brushes, once per brush;
empty material batches return immediately. `brushes.invalidate_all` counts global
invalidations when a render timing capture is active.

Two editor-only captures before and after this change used the same loading mode,
camera, viewport, and available assets:

| Metric | Before 1 | Before 2 | After 1 | After 2 |
| --- | ---: | ---: | ---: | ---: |
| Document load returned (s) | 0.558 | 0.545 | 0.541 | 0.552 |
| First 3D frame swapped (s) | 2.676 | 2.658 | 1.904 | 1.927 |
| Editor resources settled (s) | 6.150 | 5.887 | 3.432 | 3.533 |
| Resource notification total (s) | 2.601 | 2.523 | 0.156 | 0.180 |
| Slowest notification batch (ms) | 907.3 | 914.9 | 58.0 | 76.1 |

Mean editor readiness fell from 6.019 to 3.483 seconds (42% less time). The interval
from document-load return to editor readiness fell from 5.467 to 2.936 seconds
(46% less time). Before captures are `20261021-011520-400` and
`20261021-011716-368`; after captures are `20261021-011917-188` and
`20261021-012002-108`. All used `-LoadTiming -NoBuild -WarmupSeconds 1 -Seconds 5`.
The repeated after captures retained 140 rendered models, 370 model-pass entities,
and 1706 transparent items per frame. Their PNGs differ from the second baseline
in 0.057% / 0.069% of pixels; visual inspection found the same scene. Opaque range
padding and allocation can change submitted vertex counts between processes.
The same six WAD files remain unavailable; these are warm-cache measurements of
the available asset set, not cold-disk or complete-asset results.

An additional Play capture, `20261021-012040-359`, reached editor readiness in
3.616 seconds and Play/resource readiness in 10.670 seconds. Play activation took
6.803 seconds, including a logged 1.578-second collision build; this change does
not optimize that separate activation stage. It used `-LoadTiming -Play`, eight
seconds of warmup, and twelve seconds of measurement. A subsequent control with
the original invalidation loop restored, `20261021-012347-151`, took 6.092 seconds
to editor readiness and 13.157 seconds to Play/resource readiness with the same
parameters; its notification total returned to 2.599 seconds.
Restoring the optimized loop and rebuilding produced `20261021-012700-711`:
3.573 seconds to editor readiness, 10.617 seconds to Play/resource readiness,
and 0.160 seconds in notification observers. The matched control pair reported
211.3 / 210.1 steady-state FPS and 3.640 / 3.661 ms CPU frame time, respectively:
steady-state performance was comparable. Both rendered 138 models and 1706
transparent items per frame; their PNGs differ in 77 pixels (0.0076%).
