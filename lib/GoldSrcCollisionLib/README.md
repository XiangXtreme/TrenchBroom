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

Algorithm changes belong in hltools, including its `src/collision` entry point.
Do not patch the bundled copy. Commit and validate upstream first, then run:

```powershell
python scripts/sync-hltools-collision.py --source D:/Code_Development/Source_code/CPP/hltools
python scripts/sync-hltools-collision.py --check
```

The script copies the target's quoted-include dependency closure verbatim
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
byte with the real hltools CLI for five fixture maps and six cliptypes (30
pairs), using the same embedded compile options.

The upstream license is preserved at `thirdparty/hltools/LICENSE`.
