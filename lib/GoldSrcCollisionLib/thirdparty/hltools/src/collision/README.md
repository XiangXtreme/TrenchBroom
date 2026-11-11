# Embedded GoldSrc collision

This target compiles the same `src/csg` and `src/bsp` geometry as the hltools
CLI. It supports every cliptype, including direct-surface `legacytrim`, local
`func_detail` cliptype overrides, origins, hull shapes and tool textures.

It can be built independently, without fetching the CLI's ZIP, SQLite or GPU
dependencies:

```sh
cmake -S src/collision -B build/collision -DCMAKE_BUILD_TYPE=Release
cmake --build build/collision
ctest --test-dir build/collision --output-on-failure
```

Consumers call `hltools::collision::compile` with MAP text and receive the
compiled in-memory BSP plus bounded warning messages. The build replaces CLI
logging/progress with an in-memory sink and fatal exits with exceptions. Calls
are serialized around the BSP stage's shared scratch state. Legacytrim's
workers propagate errors through their futures.

Legacytrim bounds each composed exterior region with axial and edge/box-axis
bevel planes. These keep thin sloped ends inside the player-box sweep while
preserving the legacy slope offsets and removal of source-brush partition seams.
The collision parity checker includes thin roofs, diagonal ends and a segmented
roof ring alongside the basic brush fixtures (48 CLI/embedded pairs).

`result.timings` reports MAP parsing, CSG brush construction, intermediate
generation, BSP-data preparation and BSP processing in milliseconds. The
standalone test executable can benchmark a saved MAP without modifying it:

```sh
build/collision/HltoolsCollisionTest --benchmark example.map 5 5
```

The arguments select cliptype 0..5 and repetition count; each run prints a JSON
record. Total time includes compilation and its temporary-data cleanup, but
excludes editor snapshot/export, movement-world import and UI startup.

Geometry work retains its original order and precision. Small winding scratch
uses stack storage with a heap fallback, CSG fragment buffers retain capacity,
BSP partition scores reuse storage, and brink expansion allocates its actual
tree size. These changes preserve the compiled collision instead of substituting
a different hull or spatial-partition algorithm.

Use `verify_cli_parity.py --cli <cli> --embedded <current-test-executable>
--baseline-embedded <previous-test-executable>` to additionally compare every
fixture against a saved build before a performance change. This compares the
plane and clipnode records and all model headnodes byte for byte.

The embedded stage uses `-nofill -noopt`: an editor's map can be incomplete,
and texture pixels are not loaded. It writes no BSP, sidecars, leak files or
face extents. VIS, RAD, WAD resolution and studio asset injection are outside
this target. Brush geometry, CSG ordering, BSP partitioning, clipnode merging,
brink correction and direct-tree installation use the CLI source unchanged.

TrenchBroom vendors this source closure at a committed hltools revision. Its
sync tool copies the target sources and their quoted-include dependencies
verbatim, and records hashes for offline verification. Change the geometry
here and sync the snapshot, rather than maintaining an editor-specific fork.
