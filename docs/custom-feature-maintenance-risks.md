# Custom Feature Maintenance Risks

This document tracks custom features that are useful, but currently more invasive or less
maintainable than the upstream TrenchBroom style. Keep this file updated as the issues are
fixed.

Only current risks belong here. The 2026-09 architecture review and the custom feature
wish list moved to `archive/`; they describe a baseline that has since changed (WAD
textures, the unified asset browser, the four-entry MCP surface) and must not be used to
judge today's code.

## High Priority

### Python API Runtime and Plugin API

- Risk: `PythonApiModule.cpp` is about 6,600 lines (209 KB) and mixes bindings, UI panel
  construction, callbacks, transactions, geometry helpers and, since 2026-11, the asset
  catalog/preview/sheet bindings.
- Risk: process-global callback, event, handle, and transaction state makes
  document/window/plugin unload behavior harder to reason about.
- Suggested fix: split the bindings by domain (`documents`, `assets`, `geometry`,
  `panel`, `events`, `actions`) and move callback/timer ownership fully into
  `PythonPluginSession`. Do this after the asset acceptance work, not during it.

### Asset Automation Semantics

- Risk: `assets.*` behaviour is documented only by catalog signatures. Three rules an
  agent must know are not written down anywhere executable:
  `documents.save()` on a project map saves the whole project (through the save handler
  installed by `ProjectSession::bindDocument`); `documents.open_project_map` leaves the
  current execution bound to its original document; `assets.capture` and
  `assets.import_file` live in memory until a save.
- Risk: the game catalog merges two very different sources: a file scan for
  `mdl/spr/wav` (`AutomationAssets::collectAutomationAssets`) and the material manager
  for textures. Textures are therefore identified by material name rather than by file,
  have no preview, and only appear while the map has its material collections loaded. New
  asset features must keep both sources in mind.
- Risk: positive paths are covered in C++ (`ProjectSessionAssetLibrary`), but the
  Python/MCP layer only asserts error contracts. `capture -> save -> reopen` has no
  automated test, so a save-handler or staging change can regress it silently.
- Suggested fix: document the three rules in the bindings and the scene workflow skill,
  add the save/reopen regression, and settle whether `type` or `asset_type` is the filter
  key before adding more asset types.

### Outliner Tree and Property Editor

- Risk: `OutlinerTreeWidget` stores raw `mdl::Node*` in tree items and contains comments
  about avoiding `dynamic_cast` on deleted objects.
- Risk: `OutlinerEntityPropertyEditor` rebuilds its whole QWidget tree for many updates,
  which can reset scroll position and cause UI latency.
- Risk: embedded smart editors such as `SmartSkyboxEditor` can update map properties
  through a separate path from the native entity inspector; if they force a full Outliner
  rebuild, undo/redo and selection refresh can become visibly inconsistent.
- Risk: embedded smart editors that scan filesystem resources or load preview icons can
  make ordinary selection changes slow if they are rebuilt repeatedly.
- Suggested fix: move the tree to a model/view design with stable node ids or explicit
  invalidation; split property row creation into a row factory and update individual row
  values when possible; cache smart-editor resource scans and previews by game path, with
  explicit manual refresh for expensive reloads.

### Python API Event Dispatch

- Risk: editor notifications such as selection changes can become unexpectedly expensive if
  they initialize Python or import plugin modules when no plugin callback is registered.
- Suggested fix: keep event emission lazy; do not initialize Python for passive editor
  notifications, and check for registered callbacks before entering plugin dispatch.

### Sky Rendering

- Risk: `SkyRenderer` handles resource lookup, loose texture loading, sky brush geometry
  collection, GL state, and render submission in one class.
- Risk: `canRender()` can traverse sky brush geometry, and selection/locked renderers
  depend on `canRender()` to decide whether to hide original sky faces.
- Risk: sky brush geometry invalidation is currently coarse-grained; if future changes make
  it invalidate on selection changes, 3D selection feedback can become visibly slow on
  large maps.
- Suggested fix: split sky resource resolution, sky brush geometry caching, and rendering;
  invalidate geometry only when map visibility, brush geometry, or sky-related materials
  change.

### MCP / Agent Bridge

- 风险：MCP 会把编辑器控制能力暴露给本机进程；它没有调用方鉴权，默认 Off、
  只绑定 localhost、按 mode 收紧工具边界这几条必须保持。
- 风险：`tb_execute_python` 执行的是受信任代码；阻塞的原生调用无法被协作超时强制
  中断，客户端必须根据执行回执和随后 `tb_inspect` 的地图事实决定恢复动作。
- 风险：资产动作是工程级、不在地图 undo 栈里；agent 可以“只捕获不改图”，随后保存
  才落盘。不要新增绕过 `ProjectSession` 保存路径的工程修改。
- 建议修复：维持四入口；生命周期与持久化动作继续要求 `mode='action'`；新增自动化
  面必须用真实 Release 进程验收，不能只靠单元测试。

## Medium Priority

### Model Browser and Resource Access

- Risk: UI-specific model browsing exposed `Map::gameFileSystem()` and
  `Map::reloadEntityModels(...)`, increasing the public surface of `mdl::Map`.
- Suggested fix: introduce a UI/resource facade or service for model browser operations,
  keeping `Map` focused on document state and core operations.

### Brush Chamfer Operations

- Risk: chamfer algorithms are implemented directly in `mdl::Brush`, increasing the core
  class size and upstream merge conflict surface.
- Suggested fix: move chamfer code into a dedicated geometry helper or command module,
  leaving `Brush` with smaller primitive operations.

### Preferences and Misc UI

- Risk: Misc preferences can become a catch-all for Python plugins, Pie Menu, language, and
  editor behavior.
- Suggested fix: keep the upstream preferences structure and use dedicated panes for Python
  plugins, Pie Menu, language, and custom tools.

## Lower Priority

### Generated Python Package Metadata

- Status: cleaned. `python/src/trenchbroom_api.egg-info` was removed from the repository,
  and `*.egg-info/` is ignored.
- Keep this as a guardrail: generated Python packaging metadata should not be committed
  again.

### Development Scripts and Debug Artifacts

- Risk: skybox/debug helper scripts and local notes can accumulate in the repository root.
- Suggested fix: keep reusable tools under `scripts/` or `tools/dev/`, and ignore/delete
  one-off debug output.
