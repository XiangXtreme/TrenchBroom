# GoldSrcCollisionLib

Editor-independent GoldSrc collision compilation, statically linked into TB.
End users can run Play mode without downloading hltools, external tools, WADs,
or additional runtime libraries.

## Backends

- `HullCompiler.h`: existing plane-based VHLT/ZHLT expansion and compact BSP.
- `HltoolsCompiler.h`: the actual hltools MAP parser, CSG and BSP stages, using
  the pinned source snapshot under `thirdparty/hltools`. This supports all six
  cliptypes, including `legacytrim`, per-entity clip settings, `info_hullshape`,
  ORIGIN brushes and collision-only detail models.

Select **hltools** in Preferences > KreedzBroom > Clip Hulls > Compiler. Selecting
**Legacytrim (hltools)** also selects that compiler. Existing defaults stay the
same. The importer preserves the upstream planes and child relationships;
only reachable node/plane indices are compacted for the movement engine.

The UI exports a detached snapshot of visible, exportable brushes to MAP text.
The embedded compiler runs in memory with `nofill` (unfinished maps need not
be sealed) and `noopt` (there are no texture pixels to optimize). It performs
no WAD loading, studio-model injection, VIS, RAD or BSP file I/O. Thus parity
means the same brush collision as hltools with those options, rather than the
output of every possible CLI configuration. Moving/rotated entity behavior
still belongs to the Play runtime. Teleport touch volumes use the existing
Play trigger implementation. Upstream legacytrim aliases hull 2 to hull 1;
the adapter preserves that behavior.

Compiler scratch state is protected by a mutex. Completed worlds are independent
and immutable. Compiler diagnostics cross the SCS v2 C boundary as a compile
error and bounded warning text instead of terminating the editor.

## Updating the single source

The same snapshot also supplies `HltoolsLightmapWorker`, a separate executable
statically linking the full CSG/BSP/VIS/RAD stages and atlas exporter. TB builds,
deploys and installs it beside the editor, and locates it automatically. Lightmap
preview needs no external hltools installation or executable-path preference.
It uses the existing MAP/WAD export or frozen project resources, quality settings,
progress pipe and cancellation. Vulkan GPU lighting is requested by default when
the worker is built with the SDK; missing devices and unsupported maps automatically
use CPU lighting. Builds without the SDK keep the CPU path. Source ZIP output is not
part of the worker build. Fatal errors
and compiler globals stay isolated in the worker process. Previews skip collision
hulls. A session cache retains the BSP immediately after VIS; supported point-light
changes reuse it only after exact MAP/settings/WAD validation and interior-origin
checks. RAD always runs in full. Other changes trigger a complete compilation.
Project extraction
stays in TB-owned scratch storage so cancellation also cleans extracted assets.

Algorithm changes belong in hltools, including its `src/collision` and
`src/preview` entry points. Both targets use one copy of the shared stage sources.
Do not patch the bundled copy. Commit and validate upstream first, then run:

```powershell
python scripts/sync-hltools-collision.py --source D:/Code_Development/Source_code/CPP/hltools
python scripts/sync-hltools-collision.py --check
```

The script copies both targets' quoted-include dependency closure verbatim
(normalizing line endings), its license, tests and parity checker. The manifest
pins the upstream Git revision and each file's SHA256. CMake verifies the hashes
on configure, and `--check` additionally verifies the file inventory. Updates
are explicit and reviewable; normal TB builds use only repository contents and
never access the hltools checkout or network. See `manifest.json` for the exact
revision and the bundled `src/collision/README.md` for standalone validation.

## Validation

Build `TbCsLibTest` and `TbUiLibTest`; run the full collision suite plus
`buildPlayModeWorld` and `KreedzBroomPreferencePane`. The bundled standalone
collision target also tests failure recovery and concurrent callers. Its
`verify_cli_parity.py` compares planes, clipnodes and model headnodes byte for
byte with the real hltools CLI for eight fixture maps and six cliptypes (48
pairs), using the same embedded compile options.
Pass `--baseline-embedded <saved-test-executable>` to additionally require the
same collision records as a previous build when optimizing the compiler. The
standalone test executable's `--benchmark <map> <cliptype-0..5> <repetitions>`
mode reports per-stage timings without modifying the input map.

`LightmapBaker` tests exercise real baking, brush lightmap assembly, current project
resources, cancellation and failure recovery. The worker's `verify_cli_parity.py`
tests MAP and project inputs at all three quality levels and compares lighting
lumps, atlas pixels and manifests against the full CLI when `--cli` is supplied.

The upstream license is preserved at `thirdparty/hltools/LICENSE`.

## Preview interaction

The preview keeps the last successful atlas on failure. Face addresses are checked
against a snapshot of geometry, UVs, material, surface attributes and brush-entity
properties before sampling; changed or removed faces cannot consume an old tile.
Terrain lightmaps are conservatively invalidated after edits. Unchanged surfaces
can still show the previous lighting while the new bake runs.

The preview menu and bottom status surface offer manual baking, automatic updates,
cancellation, lighting-only display, output logs and the last failure's scratch
folder. View preferences control update delay, intensity and display gamma. Display
controls never start a bake. Automatic updates debounce edits, wait for the current
edit transaction, and queue at most one follow-up after a running bake. Disabling
automatic updates leaves rebaking to the manual command.

A local Normal/GPU/8-thread run on the frozen Lws_gxl_speedrun preview took 106.6 s
for the full bake and 9.7 s after a light-intensity edit with geometry/VIS reuse.
These are single-run timings, not a guarantee for other maps. CPU and GPU fixtures
compare all BSP lumps against complete compilations (normalizing the generated
entity timestamp), including brush entities, material/UV edits, WAD changes and
invalid or boundary-crossing light positions.
