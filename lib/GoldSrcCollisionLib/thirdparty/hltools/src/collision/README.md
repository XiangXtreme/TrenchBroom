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

The embedded stage uses `-nofill -noopt`: an editor's map can be incomplete,
and texture pixels are not loaded. It writes no BSP, sidecars, leak files or
face extents. VIS, RAD, WAD resolution and studio asset injection are outside
this target. Brush geometry, CSG ordering, BSP partitioning, clipnode merging,
brink correction and direct-tree installation use the CLI source unchanged.

TrenchBroom vendors this source closure at a committed hltools revision. Its
sync tool copies the target sources and their quoted-include dependencies
verbatim, and records hashes for offline verification. Change the geometry
here and sync the snapshot, rather than maintaining an editor-specific fork.
