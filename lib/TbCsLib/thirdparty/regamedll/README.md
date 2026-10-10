# ReGameDLL_CS pm_shared (third-party)

Unmodified copies of the Counter-Strike 1.6 player movement code (`pm_shared`,
`pm_math`) and the headers it needs, taken from
[ReGameDLL_CS](https://github.com/rehlds/ReGameDLL_CS) at commit `4a50c42`
(2026-09-17). Only line endings may have been normalized. Do not edit them, so
they can be compared with upstream.

TbCsLib compiles them against `src/ReGameDllCompat.h` (in place of ReGameDLL's
`precompiled.h`) with `REGAMEDLL_ADD`, `REGAMEDLL_FIXES`, `REGAMEDLL_API` and
`PLAY_GAMEDLL` undefined, which selects the original CS 1.6 behaviour.

**License:** MIT, see `LICENSE` (ReGameDLL_CS moved from GPLv3 to MIT in July
2025). Some headers still carry their earlier GPLv2-or-later notices, which are
compatible with TrenchBroom's GPLv3.
