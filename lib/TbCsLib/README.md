# TbCsLib

Embedded Counter-Strike 1.6 player movement for TrenchBroom's play mode
(Run > Play, F7). It lets mappers test KZ, bhop and surf maps with the game's
physics without compiling the map or launching the game.

TbCsLib does not re-implement CS movement. It compiles the game's own movement
code unmodified and plays the role of the engine around it, both taken from
projects that were reverse engineered from the original game binaries:

| Part | File | Based on |
|---|---|---|
| Movement (`pm_shared`) | `thirdparty/regamedll` | [ReGameDLL_CS](https://github.com/rehlds/ReGameDLL_CS) (the CS 1.6 server game library), compiled unmodified with its `REGAMEDLL_*` extensions off |
| Engine: hull traces, point contents, position tests | `src/PmTrace` | [ReHLDS](https://github.com/rehlds/ReHLDS) `engine/pmovetst.cpp`, `engine/world.cpp` |
| Engine: per-command player setup, physents | `src/PlayerHost` | ReHLDS `engine/sv_user.cpp` (`SV_RunCmd`, `SV_AddLinksToPM`) |
| Clip hulls from brushes, in memory | [`GoldSrcCollisionLib`](../GoldSrcCollisionLib) | Plane-based VHLT/ZHLT backends, plus the statically embedded hltools MAP/CSG/BSP backend (six cliptypes including legacytrim) |
| C API used by TbUiLib | `include/cs/scs.h`, `src/ScsApi` | Half-Life SDK `cl_dll/input.cpp` for the client side helpers |

`src/ReGameDllCompat.h` stands in for ReGameDLL's `precompiled.h`, providing only
what `pm_shared` needs instead of the whole game server.

Everything is compiled with `-ffp-contract=off -fno-fast-math` so float math
rounds like the code it mirrors. The original binaries used x87 floating point,
which no modern build reproduces bit for bit (ReHLDS and ReGameDLL included).

## Fidelity

Replicated: the engine's hull traces (including the 1/32 unit epsilon and the 5%
back-off), point contents, `SV_RunCmd`'s player setup (state that persists
between commands, truncated duck / swim timers, command splitting), usercmd
network quantization (16-bit angles, 12-bit moves), `KeyState` /
`CL_ButtonBits` semantics and the CS server cvars. `TbCsLibTest` checks known
numbers (resting height 36.03125, jump apex ~45, 250 ground speed, the 239.988
bunnyhop cap, ladders, surf ramps, determinism) and engine specific behaviour.

The plane-based backends expand clip hulls like VHLT's or ZHLT's hlcsg for each `-cliptype`, so
cliptype artifacts reproduce, e.g. legacy's 15.23 unit lower 45 degree slopes and
the invisible steps of slopes split into several brushes. The BSP tree is TbCsLib's
own, not hlbsp's: the solid space is the same, but edge cases that depend on hlbsp's
clipnode order (some epsilon-sized gaps between brushes) may behave differently.

The **hltools** backend runs the original upstream MAP parser, CSG and BSP code
from a pinned source snapshot, including direct legacytrim trees and per-entity
hull settings. It uses `nofill` and omits texture/studio processing. See
[GoldSrcCollisionLib](../GoldSrcCollisionLib) for parity tests, scope and the
source update workflow. SCS API v2 accepts MAP text for this backend and returns
compiler diagnostics without exiting the process.

`CBasePlayer`'s fall damage bookkeeping (`PlayerPreThink` / `PlayerPostThink`) is
replicated for reporting only: the player never loses health.

Not simulated yet: general entity/trigger logic beyond Play teleport support,
moving or rotated brush entities, conveyors,
studio models, texture materials (footstep sounds only).

## Licensing

TbCsLib's own code is GPLv3 like TrenchBroom. `PmTrace` and most of
`PlayerHost` are ported from ReHLDS (MIT); a few small helpers in `PlayerHost`
come from Xash3D FWGS (GPLv3). The vendored `thirdparty/regamedll` files are
MIT licensed (see the README and LICENSE there).
