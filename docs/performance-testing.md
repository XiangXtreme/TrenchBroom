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

### Whole-Map Movement

Use `-SelectAll` for a stationary selected-map baseline, or `-MoveSelection` to
select all nodes and repeatedly drag them. Add `-FourPanes` to use the four-pane
layout and drag in a 2D view; otherwise the drag runs in the single 3D view.
For `kz_skyline.map`, an overview camera avoids near-surface snapping and a
horizontal ray parallel to the 3D movement plane:

```powershell
.\scripts\profile-render.ps1 `
  -MapPath 'D:\steam\steamapps\common\JACK\mapper\Source\kz_skyline.map' `
  -GamePath 'D:\steam\steamapps\common\Half-Life' `
  -Position '10000 1000 5000' -Direction '-1 0 -0.5' `
  -WarmupSeconds 5 -Seconds 8 -MoveSelection -FourPanes
```

The capture sends internal mouse events through the view's normal picking and
drag controllers, bypassing OS mouse injection and input-event queuing. It moves
between cursor positions 64 pixels either side of the starting hit, checks that
every update changes the selection bounds, and waits for the next 3D
`frameSwapped` signal without framebuffer readback. A four-pane presentation also
includes the other visible views. A second warmup follows selection, excluding its
initial cache rebuild from the measured movement or stationary phase.

`selection.json` records selection counts, bounds, grid and texture-lock settings,
selection latency, release latency, undo time, and each movement sample.
`move-updates.csv` contains synchronous picking/controller/command time
(`sync_ms`), the time from that update's start to Qt presentation
(`to_present_ms`), and the actual world-space delta. The latter includes the former;
do not add them. These are scripted update times, not physical mouse-to-photon
latencies. Since the controller runs before painting, its synchronous work appears
in frame intervals rather than `cpu.input` or `cpu.frame`. GPU and renderer stages
remain separately available in `render.jsonl` and `timings.csv`.

The move is committed once on release, then undone. The capture verifies restored
bounds and an unmodified document, and never saves the source map. Selection
captures cannot be combined with `-Play`. Multiple views append complete JSONL
records using refreshed file positions, so a shared capture remains parseable.

On 2026-10-21, Release captures on an i9-12900HX / RTX 4060 Laptop GPU selected
3,382 nodes: 3,027 brushes and 355 entities. Each run used the camera above,
five-second warmups, an eight-second measurement, a 16-unit grid, texture lock,
4x MSAA, and no FPS cap. The single 3D viewport was 1368x737; the four-pane 3D
viewport was 690x375.

| Capture | 3D presentation FPS | Mean move sync | Mean update to presentation | Update P95 |
| --- | ---: | ---: | ---: | ---: |
| Selected, stationary, single 3D | 152.6 | n/a | n/a | n/a |
| Whole-map drag, single 3D | 7.7 | 85.1 ms | 130.5 ms | 154.4 ms |
| Selected, stationary, four panes | 151.5 | n/a | n/a | n/a |
| Whole-map 2D drag, four panes | 7.5 | 82.2 ms | 135.9 ms | 156.2 ms |

The movement rows contain 62 and 59 effective updates respectively. In the
four-pane drag, selection took 868.6 ms synchronously / 930.4 ms to presentation;
release took 1.1 ms / 28.6 ms, and undo took 56.1 ms synchronously. The 3D render
stage averaged 23.5 ms, including 15.1 ms per frame rebuilding brush render
caches, while its GPU stage averaged 7.4 ms. GPU and nested CPU stages overlap;
these are not additive. The synchronous 82.2 ms still includes picking, geometry
transforms, commands and observers; this capture does not split those costs.

The captures are `20261021-130152-153`, `20261021-130054-569`,
`20261021-130844-053`, and `20261021-130731-250`, in table order. Six original WAD
paths were unresolved, as in earlier captures, so these are measurements of the
available asset workload. The source file SHA-256 remained
`ABC8D0B7342D4407562DF7F7D97FB72B06F9BC9906422DCC605E8CDF307B7071`.

#### Bulk Selection and Movement Optimization

Temporary CPU probes on the same workload attributed 774.9 ms of an 852.8 ms
selection command to Outliner selection synchronization, and another 72.1 ms to
renderer selection notifications. These timings are nested within the command.
During dragging, model change processing averaged 34.8 ms within an 80.8 ms
transform, including tag updates and search-index maintenance. The probes were
removed after the investigation.

The optimized path submits Outliner row ranges in one Qt selection update,
coalesces repeated invalidations of shared readable outlines, and retains each
node's distinct searchable strings so only changed keys enter or leave the trie.
Tag glob matching handles literal prefixes directly. Transform tasks operate in
small batches, with preallocated result storage to avoid copying brush geometry
when collecting results. Geometry validation, material locking and undo still run
for each effective movement update.

Fresh Release processes with the same map, camera, assets, grid and timing settings
above produced the following results, without concurrent builds or tests:

| Capture | Select sync / to presentation | 3D FPS | Mean move sync | Mean update to presentation | Update P95 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Four-pane baseline | 868.6 / 930.4 ms | 7.5 | 82.2 ms | 135.9 ms | 156.2 ms |
| Four panes, optimized run 1 | 26.8 / 90.9 ms | 8.7 | 61.2 ms | 116.6 ms | 138.7 ms |
| Four panes, optimized run 2 | 26.7 / 101.1 ms | 8.3 | 63.8 ms | 121.2 ms | 146.0 ms |
| Single 3D baseline | n/a | 7.7 | 85.1 ms | 130.5 ms | 154.4 ms |
| Single 3D, optimized | 23.0 / 71.0 ms | 9.6 | 58.8 ms | 105.6 ms | 123.3 ms |

Optimized captures are `20261021-134959-570`, `20261021-135058-886` and
`20261021-135220-537`, containing 69, 66 and 76 effective updates. Undo restored
the original bounds and unmodified document in every run. The source SHA-256
remained unchanged; the six original WAD paths were still unresolved. GPU
temperature after the first optimized run was 70 C.

Selection synchronization is substantially faster, but whole-map dragging remains
slow. The four-pane 3D CPU frame still averages 23.9-25.0 ms, including 15.2-15.9 ms
per frame rebuilding brush render caches, with a 7.5-7.8 ms GPU stage. These stages
overlap and exclude the separately measured synchronous move command. Further work
must address per-update scene mutation and cache rebuilding; the measured change
does not establish smooth whole-map dragging.

#### Spatial Update Follow-up

Moving an indexed object within the same octree cell now keeps its existing
entry. Root entries also retain their shared, possibly expanded root address
when the new bounds still require root storage. Moving to another cell still
removes and reinserts the object, including when bounds grow or shrink.

Entity and group nodes previously propagated child bounds changes a second time
after the base node had already invalidated their bounds and notified ancestors.
Removing those overrides leaves one notification per ancestor and avoids repeated
parent bounds calculations and spatial-index updates in nested hierarchies.
Regression coverage checks notification counts, cached ancestor bounds, nested
brush-entity queries, and repeated octree updates against remove/reinsert queries.

The same Release workload, with no concurrent builds or tests, produced:

| Capture | Select sync / to presentation | 3D FPS | Mean move sync | Mean update to presentation | Update P95 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Four panes, follow-up run 1 | 24.0 / 83.8 ms | 9.2 | 56.6 ms | 113.8 ms | 136.6 ms |
| Four panes, follow-up run 2 | 23.2 / 81.8 ms | 9.3 | 54.1 ms | 107.6 ms | 128.6 ms |
| Single 3D, follow-up run 1 | 25.9 / 79.4 ms | 10.0 | 56.3 ms | 101.7 ms | 119.7 ms |
| Single 3D, follow-up run 2 | 25.9 / 80.8 ms | 10.4 | 54.9 ms | 100.5 ms | 124.8 ms |

Captures are `20261021-142357-390`, `20261021-142527-853`,
`20261021-142445-550` and `20261021-142657-585` in table order, with 71, 75, 79
and 80 effective updates. The mean of the two four-pane run means improved about
11% for synchronous movement and 7% to presentation against the previous section's
two optimized runs. Single-3D improvements are smaller, and its second P95 exceeds
the previous single-3D result; these short captures do not establish uniformly
better frame pacing.

All four runs restored the original bounds and unmodified state with undo. The
source map SHA-256 remained unchanged, the same six WAD paths were unresolved,
and GPU temperature after the first three captures was 69 C. Inspection of the
four-pane framebuffer found the expected selected scene with dense entity labels.
Brush cache validation still costs 14.9-15.6 ms per 3D frame, within a 23.7-24.5 ms
CPU frame. Whole-map dragging remains around 9-10 FPS: further substantial gains
require reducing per-update geometry work and render-cache rebuilding.

#### Movement Cost Attribution

Temporary CPU scopes on revision `efb1e72d9` measured the same four-pane drag
through the real input/controller path. Three independent Release captures,
`20261021-143719-882`, `20261021-143812-381` and `20261021-144058-714`, recorded
72, 70 and 71 effective movement updates. Their mean synchronous input times
were 55.36, 56.81 and 56.26 ms; mean update-to-presentation times were 111.73,
114.69 and 113.46 ms. All runs passed the move and undo checks, and the source
map SHA-256 remained unchanged. Camera, assets and settings matched the previous
section, including the six unresolved WAD paths.

The third capture added separate scopes for Outliner property refresh, selection
synchronization and transaction notification cleanup. These non-overlapping parts
of its synchronous input processing averaged:

| Part | Mean per effective update | Share of synchronous input |
| --- | ---: | ---: |
| Model postprocessing | 20.61 ms | 36.7% |
| Transaction creation, collation and discarded-command destruction | 5.54 ms | 9.9% |
| Transaction completion/document change notifications | 3.96 ms | 7.0% |
| Geometry transform tasks and result collection | 8.99 ms | 16.0% |
| Node contents swap and bounds propagation | 7.57 ms | 13.5% |
| Outliner row updates and property refresh | 3.61 ms | 6.4% |
| MapRenderer node-change notification | 4.23 ms | 7.5% |
| Remaining input, collection and command overhead | 1.67 ms | 3.0% |

Rounded components sum to the 56.18 ms input scope. The benchmark's 56.26 ms
`sync_ms` additionally includes scope teardown and writing its diagnostic line.
The first two captures agreed on the principal costs: model postprocessing
20.41/20.84 ms, geometry tasks 8.97/9.20 ms, contents/bounds 7.20/7.46 ms,
Outliner rows 3.36/3.48 ms, renderer notification 4.11/4.25 ms, and total
transaction commit 9.34/9.58 ms. The table splits that last aggregate and does
not add it again.

Within the third capture's 20.61 ms model postprocessing, tags cost 9.25 ms,
search-index maintenance 4.82 ms, entity models 2.33 ms, materials 2.30 ms,
entity definitions 1.05 ms, links 0.82 ms, and selection invalidation 0.04 ms.
Outliner row refresh cost 3.40 ms and its property refresh 0.21 ms; document
selection synchronization was not invoked during the measured movement events.
The transaction notification scope excludes subsequent destruction, which was
negligible in the additional cleanup scope. These scopes do not attribute
deferred Qt painting or all event-loop work to individual panels.

Separately, the three captures spent 25.40/25.72/25.68 ms in each 3D CPU frame,
including 16.35/16.55/16.52 ms per frame rebuilding brush render caches. GPU
queries averaged 7.47/7.76/7.22 ms. These are nested render timings, outside the
input scope; GPU and CPU work can overlap, and the other panes and Qt composition
also contribute to update-to-presentation time. They must not be summed into a
complete frame budget. Presentation throughput remained 9.0/8.8/8.9 FPS.

Outliner synchronization caused most of the original select-all pause, but its
measured synchronous refresh is a small part of the remaining drag cost. The
larger targets are repeated model postprocessing, geometry mutation, command
collation and render-cache rebuilding on each movement update. Property-sensitive
invalidation and a drag preview that reuses geometry are candidates for further
investigation; these captures do not establish their savings or correctness.
All temporary scopes were removed after sampling.

A subsequent capture after rebuilding without the probes,
`20261021-144348-816`, passed 294 assertions across 68 effective updates and
emitted no probe records. It averaged 57.98 ms synchronous input, 117.80 ms to
presentation and 8.6 FPS, with a 212.44 ms maximum frame interval. The short-run
variation remains visible; this investigation attributes costs rather than
claiming a new performance improvement. The Release application was also rebuilt
after removing the scopes.

#### Content Change Postprocessing Optimization

Content swaps now compare the live and replacement contents before notifying
observers. Search indexing runs only for nodes whose searchable properties,
group names or material names changed. Resource binding visits the entity and
geometry contents actually replaced instead of revisiting a brush entity's
unchanged children or a transformed group's entire subtree. Undo contents still
release their resource references; replacement contents bind against the current
resource managers before UI/render observers receive the change notification.
The full notification path remains in use for resource reloads and default world
properties.

Built-in material, surface-flag and owner-classname matchers explicitly allow
preserving tags after geometry edits with unchanged matcher inputs. Other
matchers retain full reevaluation. Unchanged faces take their current tags from
the live brush, so undo does not restore stale matches from before a material
metadata update. Manual tag changes and changes to face count, material, surface
attributes or owning entity classname still trigger tag updates. Regression
tests cover current material tags through move/undo/redo, manual face tags,
geometry-dependent custom matchers, observer-visible resource bindings and
searchable material edits after movement. Existing model, asset, entity-link,
linked-group and tool tests cover the broader notification change.

The same map, camera, asset set, grid and warmup/measurement settings produced
the following Release captures without concurrent builds or tests:

| Capture | Effective updates | Mean move sync | Mean update to presentation | Update P95 | 3D FPS |
| --- | ---: | ---: | ---: | ---: | ---: |
| Four panes, run 1 | 71 | 47.30 ms | 112.75 ms | 225.22 ms | 8.9 |
| Four panes, run 2 | 77 | 45.43 ms | 104.30 ms | 117.43 ms | 9.8 |
| Four panes, diagnostic scopes | 74 | 47.34 ms | 108.14 ms | 125.82 ms | 9.3 |
| Four panes, scopes removed | 76 | 46.72 ms | 106.39 ms | 124.09 ms | 9.5 |
| Single 3D, scopes removed | 79 | 50.73 ms | 101.50 ms | 118.88 ms | 10.0 |

Captures are `20261021-151149-415`, `20261021-151226-055`,
`20261021-151505-658`, `20261021-170937-167` and `20261021-171020-757`, in table
order. The first run contains long stalls: maximum move-to-presentation time
263.85 ms, maximum frame interval 283.67 ms, and maximum GPU query 76.67 ms.
Their cause remains unresolved; that run is retained rather than discarded.
Across the three captures without diagnostic scopes, the mean of the four-pane
run means is 46.48 ms synchronous input, about 16% below the previous spatial
update follow-up's 55.35 ms. Mean update-to-presentation time is 107.81 ms versus
110.70 ms previously, a smaller change amid the observed variation. The single
3D result does not establish better presentation throughput than the earlier
10.0/10.4 FPS captures.

Temporary diagnostic scopes measured 3,627 replaced contents per update: the
3,382 selected nodes plus fully moved brush-entity owners. Only 355 entity
property sets required search indexing in ordinary subsequent updates. Across
74 effective updates, change classification averaged 1.55 ms and model
postprocessing averaged 8.31 ms, compared with the earlier 20.61 ms postprocessing
scope. The first update required a tag refresh; the remaining 73 did not. This
is about 60% less time inside postprocessing, or about 52% less when including
the new classification work. The diagnostic scopes were removed afterward.

The final two four-pane runs without diagnostic scopes still spent
25.82/26.12 ms per 3D CPU frame, including 16.39/16.68 ms per frame rebuilding
brush render caches. Their GPU queries averaged 7.78/7.76 ms. Those render
timings are separate from synchronous input and overlap internally; cache
rebuilding, geometry mutation and command processing remain substantial targets.
This change does not make whole-map dragging smooth.

All five captures passed bounds restoration and unmodified-document checks after
undo. The source map retained its SHA-256, and the same six WAD paths remained
unresolved. GPU temperature after the captures was 71 C. Inspection of the
four-pane framebuffer showed the selected map and its dense entity labels.
Validation included strict compilation, all 135 model tests, 5 model test-helper
tests, the UI suite (one native Play capture test intentionally skipped under its
background-window policy), all 49 application/tool tests, and a Release
`TrenchBroom` build.

### Loading Milestones

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

### Timing Fields

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
