# TrenchBroom Coding Agent Guidance

## Project structure
- The project contains several applications under /app. The main application target is TrenchBroom.
- Most code lives in libraries under /lib.
- Each application target and library target has its own CMakeLists.txt file.
- Shared CMake utilities are in /cmake.
- Each library usually has a <Name>LibTest target for its tests, for example TbMdlLibTest.
- Some libraries also have a <Name>TestUtilsLib target for shared test helpers, and some of those have a matching <Name>TestUtilsLibTest target.

## Application entry point discipline
- Treat `app/TrenchBroom/src/Main.cpp` as the application composition root. It may configure process-wide Qt state, parse command-line options, create application services, and select the top-level startup path.
- Keep implementation details out of `Main.cpp`. Do not define reusable widgets, local or nested classes, `QStyle` / `QProxyStyle` implementations, custom painting, theme or stylesheet loaders, state machines, test-scene construction, or domain behavior there.
- Apart from cohesive command-line parsing, anonymous-namespace helpers in `Main.cpp` must be stateless, limited to one startup concern, and exist only to make startup ordering and top-level dispatch readable. Do not use helpers to hide implementation that belongs to another module.
- Move logic into a named module when it owns state, overrides virtual functions, manages a nontrivial lifetime, has independently testable behavior, or combines more than one clear startup concern. Prefer extending an existing owner such as `ApplicationStyle` or `UiSnapshotRunner` before creating another entry-point helper.
- Keep command-line validation near parsing, but place execution of specialized modes behind a narrow typed interface. `Main.cpp` should select a mode; the owning module should construct and run it.
- Do not use a line-count limit as an architectural target. Review `Main.cpp` changes by responsibility, dependency direction, and testability instead.
- After changing entry-point structure, build the Release `TrenchBroom` target and run the focused tests or snapshot targets for every extracted or affected startup path.

## UI component style governance
- Before adding or changing foundational Qt control appearance, read and follow `docs/ui-component-style-governance.md`.
- Business widgets declare content, behavior, layout, and the approved `tbControlRole`; they must not create another visual implementation of a foundational control with local `setStyleSheet()`, custom painting, or object-name geometry overrides.
- Keep canonical control geometry and states in `app/TrenchBroom/resources/stylesheets/base.qss`, theme-selectable colors in `ThemeTokens`, and platform-independent primitives or metrics in `ApplicationStyle`.
- Runtime data visualization such as a color swatch is the narrow local-style exception. Document any other exception and cover it with a focused visual target.
- After changing a foundational component family, run the `components` UI snapshot matrix plus representative real-screen targets.
- Run `powershell -ExecutionPolicy Bypass -File scripts\check-ui-style-governance.ps1` before committing UI changes. The check rejects new feature-local `setStyleSheet()` calls outside the global application style and the data-driven color swatch allowlist, plus object-name selectors that restyle foundational controls.

## Build and test
- TrenchBroom uses CMake as its build system.
- In Visual Studio Code, prefer CMake Tools for builds.
- Build the narrowest relevant target instead of building the whole workspace when possible.
- For library changes, prefer the corresponding <Name>LibTest target to validate the change.
- Always build the relevant test target before running tests.
- Tests use Catch2.
- If VS Code test discovery is unavailable, run the built test executable directly from the build tree, for example build/lib/TbMdlLib/test/TbMdlLibTest.
- Use --list-tests to discover available tests and Catch2 filters to run a focused subset.
- Prefer `ctest --test-dir <build>/lib/<Name>/test -j` over invoking the test binary directly. `catch_discover_tests` registers every Catch2 test case as its own CTest test, so `-j` (no thread count — let ctest pick) runs them in parallel and is markedly faster than one sequential process. Build the test target first; ctest does not build.
- Use Build.md for platform-specific setup and dependency details.

### Windows Release build used by this branch
- The active local Release build tree is usually `build-release-codex`.
- For initial configuration or a recovery clean build on this machine, use the checked-in wrapper script instead of hand-written CMake commands:
  ```powershell
  scripts\build_release_codex.cmd
  ```
- This is important because the local Windows SDK tools are installed under `D:\Windows Kits\10\...`, not only the default Visual Studio-discovered path. The wrapper script pins `VsDevCmd.bat`, `rc.exe`, and `mt.exe` so Release rebuilds stay reproducible.
- This branch's local Qt is `D:\Qtx\6.11.1\msvc2022_64`, while CI uses the newest Qt version currently available for every runner platform (`6.10.3`). Keep the local Qt `bin` directory first in `PATH` for both builds and direct test runs; if old Qt 6.9.3 DLLs are earlier in `PATH`, Qt-linked test executables can fail with `0xc0000139` during Catch2 discovery or test startup. Treat the preflight's local/CI minor-version warning as a reminder that platform-style behavior still needs robust tests; a Qt major-version difference remains an error.
- Treat `build_release_codex.cmd` as a recovery-only full-clean operation. It deletes the entire target build directory, including FetchContent sources under `_deps`; the next configure repopulates every dependency and may download the third-party repositories again. Do not use it for routine builds or validation.
- Use the clean wrapper only when the build tree does not exist, the compiler/Qt/dependency configuration changed, or the dependency database is broadly corrupted and a targeted rebuild cannot recover it. State the reason before running it. Do not point it at an arbitrary directory unless you intentionally want a full clean rebuild there.
- If you need the same clean Release flow for another directory, pass it explicitly:
  ```powershell
  scripts\build_release_codex.cmd build-release-other
  ```
- Once `build-release-codex` exists, prefer the filtered wrapper for routine incremental Release builds and focused tests. It keeps the full log under `build-release-codex\codex-logs` while hiding repetitive dependency/deploy noise from the terminal:
  ```powershell
  powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TrenchBroom
  powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TbUiLibTest -TestFilter "McpBridgeServer"
  powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TbMcpLibTest -TestExe build-release-codex\lib\TbMcpLib\test\TbMcpLibTest.exe -TestFilter "McpToolCatalog"
  ```
- Do not run two `build-filtered.ps1` / Ninja builds against the same `build-release-codex` tree at the same time. Shared targets such as `TbMdlLib` can race while writing `.obj` files and fail with `Permission denied`; run those wrapper builds serially.
- If the filtered output hides something relevant, rerun with `-NoFilter` or inspect the matching log in `build-release-codex\codex-logs`.
- Before pushing, run the checked-in local CI preflight from PowerShell 7:
  ```powershell
  & scripts\ci-preflight.ps1
  ```
  It checks patch whitespace, verifies the exact CI Qt package metadata exists for Windows/Linux/macOS, reports local/CI Qt compatibility, strict-compiles changed C/C++ translation units with `clang-cl` conversion warnings treated as errors, and builds/runs the affected library tests. The Qt availability check sends small HEAD requests to the official repository; the preflight otherwise reuses the existing `build-release-codex` Ninja graph and does not clean the build tree or download dependencies.
- For broad CMake, shared infrastructure, compiler, Qt, or CI changes, run the full local matrix instead:
  ```powershell
  & scripts\ci-preflight.ps1 -Full
  ```
  Use `-BaseRef <ref>` or `-Paths @('path1', 'path2')` only to narrow an intentional investigation; do not skip checks in the final push preflight without recording why.
- For deterministic UI theme acceptance, run the checked-in snapshot matrix:
  ```powershell
  powershell -ExecutionPolicy Bypass -File scripts\ui-theme-acceptance.ps1
  ```
  It builds `TrenchBroom`, then captures the foundational `components` contract, welcome window, representative map workbench, focused Outliner and supporting-panel states, and the View, Colors, Mouse, Keyboard, and Misc/MCP preference pages without showing a native window for Light, Dark, and Blender themes at 100%, 150%, and 200% scale. PNG snapshots, JSON manifests, an Agent-friendly contact sheet, and the combined report are written under `build-release-codex\codex-logs\ui-theme-acceptance`. Use targets such as `-Targets components`, `-Targets workbench`, `-Targets preferences-keyboard`, or `-Targets preferences-misc` for a focused matrix, and pass `-SkipBuild` only when the executable is already current. A normal run checks capture integrity; pass `-BaselineDir <approved-run-directory>` to additionally fail on visual differences. Baselines are environment-specific, so compare runs produced by the same OS, Qt version, fonts, and renderer.
- Material Browser acceptance uses a dedicated checked-in Quake 2 map/game fixture. Keep `face-inspector` and `material-browser-empty` on that fixture, and do not weaken their success condition to material-count or nonblank-image checks: every fixture texture must reach GPU-ready state. Snapshot mode may disable MCP, update checks, and other external services, but it must keep GL resource processing active. On failure, inspect the sibling `.error.txt` file surfaced by the acceptance script.
- For UI/library work, build the focused test target first. If the filtered wrapper is unavailable, use the explicit Visual Studio environment form:
  ```powershell
  cmd.exe /c 'call "D:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build build-release-codex --target TbUiLibTest --config Release --parallel'
  ```
- If a plain `cmake --build build-release-codex ...` fails with missing SDK tools, missing `kernel32.lib`, or `type_traits`/STL lookup errors, assume the shell environment is incomplete and retry through `scripts\build-filtered.ps1` when the build tree already exists. Use `scripts\build_release_codex.cmd` only when the tree also needs recovery reconfiguration; do not erase a valid tree merely to restore the shell environment.
- If Release crashes after changing a class layout in a header, suspect stale `.obj` files before chasing random Qt stacks. This happened after adding a `MapViewToolBox` member: `SwitchableMapViewContainer.cpp.obj` was not rebuilt, so `make_unique<MapViewToolBox>` allocated the old size and the constructor corrupted the heap. Force-rebuild the dependent target or use the targeted stale-object helper below; reserve a full clean rebuild for broadly corrupted dependency tracking.
- Never use `ninja -t clean <object-file>` as a substitute for deleting one stale `.obj`. Ninja follows the target's dependency graph; an observed attempt to clean only `MapViewToolBox.cpp.obj` removed 1199 build outputs. Use the checked-in helper, which accepts only one relative `.obj` path inside a configured repository build tree and can rebuild one narrow target afterward:
  ```powershell
  powershell -ExecutionPolicy Bypass -File scripts\remove-stale-object.ps1 -ObjectPath "lib\TbUiLib\CMakeFiles\TbUiLib.dir\src\MapViewToolBox.cpp.obj" -Target TbUiLibTest
  ```
  Pass `-WhatIf` to inspect the resolved file without deleting it. If the helper is blocked or rejects the path, stop and report it instead of falling back to Ninja clean, recursive deletion, or a clean wrapper.
- For Release heap corruption, do not trust the final crash stack if it ends in Qt widget/tool destructors during shutdown. Enable PageHeap with `D:\Windows Kits\10\Debuggers\x64\gflags.exe /p /enable TrenchBroom.exe /full`, run under `D:\Windows Kits\10\Debuggers\x64\cdb.exe`, and capture the first-chance stack; it is usually closer to the real write-after-free or bad capture.
- Disable PageHeap when done with `D:\Windows Kits\10\Debuggers\x64\gflags.exe /p /disable TrenchBroom.exe` (and any test exe you enabled). Leaving PageHeap on makes later smoke tests painfully slow and can turn normal UI checks into false alarms.
- If a crash disappears after `scripts\build_release_codex.cmd`, still look for the source-level lifetime bug. A clean rebuild can hide stale-object symptoms, but it does not prove the code is safe.
- Be careful with lazy ranges/views feeding async tasks. Do not capture loop variables, view elements, or transform lambda references by `&` into `task_manager` tasks; move or copy each work item into the closure before scheduling it.
- Run focused Catch2 tests directly from the build tree, for example:
  ```powershell
  build-release-codex\lib\TbUiLib\test\TbUiLibTest.exe "ModelBrowserView"
  build-release-codex\lib\TbUiLib\test\TbUiLibTest.exe "GoldSrcSpritePreview"
  build-release-codex\lib\TbUiLib\test\TbUiLibTest.exe "PythonApi"
  ```
- Before linking `TrenchBroom.exe`, make sure the Release app is not running. If link fails with `LINK : fatal error LNK1104: cannot open file 'app\TrenchBroom\TrenchBroom.exe'`, check for a stale process:
  ```powershell
  Get-Process | Where-Object { $_.ProcessName -like '*TrenchBroom*' } | Select-Object Id,ProcessName,Path
  ```
  Ask before killing a user process unless the user explicitly told you to close it.
- The expected Release executable is:
  ```text
  build-release-codex\app\TrenchBroom\TrenchBroom.exe
  ```
- Useful static checks before commit:
  ```powershell
  git diff --check
  rg -n "^(<<<<<<<|=======$|>>>>>>>)( |$)" lib app CMakeLists.txt
  ```

## Documentation and offline manual development
- The offline manual source files live under `app/TrenchBroom/resources/documentation/manual/` (`00-introduction.md` to `12-references.md` in `en/` and `zh_CN/`).
- For rapid iteration on manual contents, stylesheets, or templates, **do not run `ci-preflight.ps1` for every change**. Instead, build only the `GenerateManual` target in ~1.5s without recompiling C++ binaries:
  ```powershell
  cmake --build build-release-codex --target GenerateManual
  ```
  or through the filtered wrapper:
  ```powershell
  powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target GenerateManual
  ```
- Validate manual structure and bilingual alignment with:
  ```powershell
  python app/TrenchBroom/resources/documentation/manual/validate_manual.py --manual-root app/TrenchBroom/resources/documentation/manual --generated-root build-release-codex/app/TrenchBroom/gen-manual
  ```
- Run `ci-preflight.ps1` once as the final gatekeeper before committing or pushing changes.

## Current custom branch notes
- This repository is a custom TrenchBroom fork containing upstream master plus local custom extensions. Do not assume vanilla upstream TrenchBroom behavior when touching custom areas.
- Important custom feature areas include:
  - Python API plugin/runtime work (`trenchbroom`) and manifest-style plugins.
  - Unified GoldSrc asset browser for `.mdl`, `.spr`, and `.wav`.
  - GoldSrc sprite preview decoding and `ERROR` fallback placeholders.
  - Model browser drag/drop behavior for model and sprite assets.
  - 3D sky rendering for GoldSrc `skyname`.
  - 2D readable brush outlines.
  - Path Tool and Pie Menu action wiring.
  - Outliner and property editor customizations.
- The canonical embedded Python module is `trenchbroom`. Do not register additional module aliases unless explicitly requested; examples should use `import trenchbroom as tb`.
- If a change touches the unified asset browser, run at least `TbUiLibTest "ModelBrowserView"` and any specific parser/preview tests such as `GoldSrcSpritePreview`.
- In `ModelBrowser` toolbar-style buttons, prefer existing helpers such as `createBitmapButton(...)` instead of hand-rolling `QToolButton + loadSVGIcon(...)`; the helper keeps button setup consistent while investigating icon/resource issues.
- Qt6 QtSvg has known crash-class parser/render bugs on affected 6.x versions. One observed crash was Qt 6.9.3 loading `Map_folder.svg` through `loadSVGPixmap(...)`, ending in `QSvgRenderer` / `QSvgHandler` / `QPainterPath::cubicTo`; upgrading the local Release build to Qt 6.11.1 fixed the observed startup/test path. If a similar SVG crash returns, first verify the app/test is really loading Qt 6.11.1 DLLs, then inspect the SVG asset: prefer tiny static SVG made from simple polygons/lines or PNG, and avoid Inkscape-heavy SVG features, arcs, markers, masks, filters, CSS, and complex paths. Community workarounds such as simplifying SVGs, converting to PNG, or patching QtSvg are useful context; do not add QWebEngine or custom icon loaders for a toolbar icon unless the simple asset fix fails.
- `MapWindow` manually emits `documentWasLoadedNotifier()` after constructing the UI. Child widgets such as `ViewEditor` must treat this startup notification as a refresh for the same map, not a reason to delete and rebuild complex QWidget trees; rebuilding there has caused intermittent Qt6 Release crashes in `QObject`/`QWidget` destruction while opening maps.
- If a change touches Python plugins, run focused `TbUiLibTest` filters for `PythonApi` and `PythonPluginManifest`, plus any relevant panel/timer tests.
- If a change touches rendering, be careful with Release-only behavior and OpenGL resource lifetime. Several previous issues only reproduced in Release builds, so build the Release executable before declaring rendering work done.
- Keep local noise out of commits. In this project, `.codegraph/`, temporary markdown experiments, generated crash logs, and ad hoc asset/debug files should not be committed unless the user explicitly asks.
- When using web or external research for GoldSrc formats, prefer primary/simple references and record the practical decision in code or docs. Avoid copying large third-party implementations or license-sensitive code.

## MCP development governance
- Before MCP work, read `docs/mcp-development-governance.md` and the active delivery plan `docs/mcp-python-migration/development.md`. Historical lightweight/moderate/long-term roadmaps are references, not implementation backlogs.
- MCP uses four entry points: `tb_inspect`, `tb_api`, `tb_execute_python`, `tb_capture`. Python calls native editing, undo/redo and validation; C++ retains native commands, document guards, valid handles, transactions and rendering. Put scene composition into ordinary Python functions and loops.
- Scripts executed through `tb_execute_python` call the public Python API under its document and transaction guards. Retire old MCP-specific IR, module/selector state, operation history, isolated Review orchestration and compatibility wrappers, including copies moved into automation/Python. Offline recipes do not require the new bridge to support old IR.
- Implement one coherent architecture cutover and cleanup, then final acceptance. Do not migrate every old tool first or rebuild a 140-item replacement matrix. Reuse existing native owners and extract automation services only for real shared behavior or state.
- The canonical TrenchBroom MCP workflow skill source is `skills\trenchbroom-mcp-scene-workflow`; sync it to the local runtime copy at `C:\Users\Trh\.cc-switch\skills\trenchbroom-mcp-scene-workflow` with `scripts\sync-trenchbroom-mcp-skill.ps1`.
- After changing the TrenchBroom MCP workflow skill, run `powershell -ExecutionPolicy Bypass -File scripts\sync-trenchbroom-mcp-skill.ps1 -Check`. The retired IR recipes have no runtime validator.
- Add a native Python capability only when a core workflow needs editor internals that existing APIs cannot express. Keep the MCP surface within the four-entry contract.
- After MCP C++ source, catalog, bridge, config, or UI integration changes, build the Release `TrenchBroom` target before declaring the work done, in addition to focused MCP tests.
- New high-volume MCP outputs must be compact by default (`idsMode:"count"` or `"sample"`, `detail:"summary"` style behavior) with full ids/details opt-in.
- Remove old profiles, schemas, registrations, dispatch, aliases and compatibility-only state. Old configuration uses new defaults (Off), without profile mappings. Old Python symbol names are not compatibility gates; update repository callers and document breaks. Keep native UI functionality and user maps/assets intact, and do not delete user plugins.
- For dense old maps or ambiguous brush ownership, prefer user selection plus selection-aware MCP tools instead of complex automatic brush matching.

## Test structure and code coverage
- For each compilation unit, tests are usually in one file named tst_<CompilationUnit>.cpp.
- Prefer one test case per class.
- Prefer one section per member function.
- For free functions, prefer one test case per file and one section per function.

### Code coverage instrumentation
- **Enable coverage instrumentation**: Pass `-DTB_ENABLE_GCOV=1` for gcov-compatible coverage (works with GCC or Clang) or `-DTB_ENABLE_LCOV=1` for LLVM source-based coverage (Clang only).
- **Generate coverage data**:
  - For gcov: Build and run tests normally. `.gcno` and `.gcda` files are automatically generated in the build tree.
  - For LLVM/lcov: Run tests with `LLVM_PROFILE_FILE=default.profraw <test-executable>` to generate `.profraw` profile data.
- **Analyze coverage**: Use coverage tools to identify uncovered code paths, untested branches, and low-coverage functions.
- **Guide test improvements**: When reviewing or creating tests, examine coverage reports to identify and address gaps:
  - Suggest new tests for uncovered code paths or error conditions.
  - Improve existing tests to cover branch conditions not yet exercised.
  - Identify edge cases or exception handling that lack test coverage.
- **Reference coverage in commit messages**: When submitting test improvements motivated by coverage analysis, mention that coverage-guided testing was used to identify gaps.

## Code style
- Format changes with clang-format. The repository style is defined in /.clang-format.
- Respect the existing include ordering rules from /.clang-format. In particular, Qt headers must come first.
- Follow the surrounding file's style and patterns unless there is a clear reason not to.

## Git History
- Keep the git history as clean as possible.
- Avoid unnecessary churn, including changing the same code multiple times in a branch when a cleaner edit is possible.
- Prefer changes that read like a clean transformation from the original state to the desired result.
- When creating a series of commits, keep each commit coherent, buildable, and with the relevant tests passing when practical.
- After completing and verifying a coherent source or documentation change, commit it without waiting for the user to ask again, unless the user explicitly says not to commit or the worktree contains unrelated changes that cannot be safely separated.
- Stage only files that belong to the completed change. Leave unrelated dirty files, generated maps, screenshots, logs, and temporary reports unstaged unless the user explicitly asks to include them.
- When asked to write commit messages, explain why the change was made in the context of a feature or bug fix, not just what changed.
