# GoldSrcCollisionLib

`GoldSrcCollisionLib` is the editor-independent GoldSrc collision core. It
turns convex brush planes into the four runtime clip hulls used by
`pm_shared`: plane tables, clipnodes, contents leaves, and hull bounds.

The library has no Qt, map document, renderer, or player-movement dependency.
`TbCsLib` links it statically, so the Play mode executable remains standalone
and end users do not need to install or download `hltools`.

The public boundary for another compiler is `HullCompiler.h`:

```cpp
std::unique_ptr<BrushModel> compileBrushModel(
  std::span<const BrushDesc> brushes,
  const HullCompileOptions& options,
  HullCompileStats& stats);
```

`hltools` can consume this target at its CSG/BSP boundary and keep its existing
map, surface, and file-writing stages. That arrangement leaves hull expansion
and runtime clipnode construction in one implementation while allowing each
application to retain its own front end and output format.

The core currently covers the five GoldSrc `-cliptype` modes and the VHLT/ZHLT
expansion rules already used by Play mode. The direct-surface `legacytrim`
tree remains a separate compiler-stage feature and is intentionally not
pretended to be a brush hull here; it will be added to this boundary together
with the corresponding direct-tree representation.
