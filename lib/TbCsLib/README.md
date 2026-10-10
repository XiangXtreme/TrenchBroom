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
| Clip hulls from brushes, in memory | `src/HullCompiler` | VHLT and ZHLT hlcsg `ExpandBrush` (all five `-cliptype`s, BEVEL / CLIPHULLn / NOCLIP), hlbsp contents rules |
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

Clip hulls are expanded exactly like VHLT's or ZHLT's hlcsg for each `-cliptype`, so
cliptype artifacts reproduce, e.g. legacy's 15.23 unit lower 45 degree slopes and
the invisible steps of slopes split into several brushes. The BSP tree is TbCsLib's
own, not hlbsp's: the solid space is the same, but edge cases that depend on hlbsp's
clipnode order (some epsilon-sized gaps between brushes) may behave differently.

`CBasePlayer`'s fall damage bookkeeping (`PlayerPreThink` / `PlayerPostThink`) is
replicated for reporting only: the player never loses health.

Not simulated yet: triggers, moving or rotated brush entities, conveyors,
studio models, texture materials (footstep sounds only).

## Licensing

TbCsLib's own code is GPLv3 like TrenchBroom. `PmTrace` and most of
`PlayerHost` are ported from ReHLDS (MIT); a few small helpers in `PlayerHost`
come from Xash3D FWGS (GPLv3). The vendored `thirdparty/regamedll` files are
MIT licensed (see the README and LICENSE there).
