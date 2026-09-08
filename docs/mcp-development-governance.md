# MCP Development Governance

## Architecture

MCP is a thin bridge for trusted Python execution over TrenchBroom's native editor
API. The active contract is [Python execution delivery](mcp-python-migration/development.md);
[core scenarios](mcp-python-migration/scenarios.md) define acceptance.

The local Blender reference implements receive -> queue -> main-thread dispatch
-> exec with bpy -> output. TrenchBroom follows that division of responsibility,
using its existing Python bindings and native transaction support. Scene
composition is ordinary Python code.

Old MCP tools, profiles, IR, module state, selector languages and operation
history formats have no compatibility commitment in this replacement.
Historical roadmaps and capability inventories do not authorize implementation
work. APIs introduced during the migration are also subject to simplification.

## Four Entry Points

| Entry | Responsibility | Permission |
| --- | --- | --- |
| tb_inspect | Bounded editor/document/selection facts and essential native problem summaries | ReadOnly |
| tb_api | Discover actual public Python bindings | ReadOnly |
| tb_execute_python | Execute inline code or a script, with arguments, results and logs | Edit |
| tb_capture | Capture the current viewport | ReadOnly |

Edit exposes four entries, ReadOnly three, Off zero. Listing, exact lookup and
dispatch share the same registered set. Retire standalone tb_history/tb_validate;
Python already exposes native history and validation. Necessary ReadOnly problem
inspection can call native validators without Python evaluation.

Delete tool profiles, old schemas, aliases, dispatch branches and wrappers.
Old configuration uses new safe defaults (Off); retired fields can be ignored
without profile mapping or a compatibility subsystem. Do not place the old tool
catalog behind an action parameter or JSON forwarding API.

## Native API Ownership

The editing path is MCP -> Python runtime -> trenchbroom API -> native commands.
Inspection, API discovery and screenshot capture may use their native owners
directly. C++ owns document identity, object validity, geometry algorithms and
undoable map commands. Python owns loops, functions and scene composition.

Add a generic binding only when a core workflow cannot use existing APIs.
Reuse native owners. Extract automation services only for real shared behavior
or state; single-command bindings do not need another forwarding layer.
Do not migrate all old handlers into automation before removing them.
Python must not call old MCP handlers through JSON or tool names.

Existing native UI functionality, maps and user assets remain outside the removal
scope. Console and plugin runtime mechanisms may use the same Python API.
Old Python symbol names and migration wrappers are not compatibility gates:
update repository-owned callers and examples when simplification changes them.
Document third-party script breaks instead of adding aliases. Never delete user
plugins or remove native algorithms that surviving UI/code still uses.

## Retire Dedicated MCP State

Remove the old MCP-only IR/blockout executor, preview caches, module revision/hash
metadata, JSON selector DSL, operation-history mappings, audit children, isolated
Review orchestration, old object-ID aliases and compatibility response machinery.
Remove their migrated automation/Python wrappers as well if the new core does not
need them. Directory relocation is not justification for retention.

Retain document checks, live handles, native transactions and native undo/redo.
If a shared class mixes these with retired state, simplify that class or use the
existing native owner. Reusing an algorithm does not require its old schema,
registry, metadata or history model.

Scene scripts may call the public trenchbroom API through tb_execute_python.
Old data-only recipes may remain offline or be archived; they do not require IR
support in the new bridge. New examples compose native APIs directly. Do not
build an IR interpreter or adapter to preserve obsolete script contracts.

## Execution Guarantees

Reuse the working execution foundation:

- Trusted Python runs in Edit on the Qt main thread, with fresh globals.
- Bind transaction execution to a document fingerprint and saved path, and keep
  that target fixed throughout execution.
- Use one native parent transaction for normal edits. Exception, cooperative
  timeout, invalid/oversized result or commit failure cancels it and restores
  selection. No retired module or audit state is part of the new transaction.
- Lifecycle/persistence operations and native undo/redo use explicit action mode;
  report completed actions and partial mutation on failure.
- Check handles after deletion, reload, undo/redo, cross-document access and
  address reuse. Keep native history separate from any MCP-only operation ledger.
- Preserve request identity, results, errors, logs and truthful mutation/rollback
  status. Existing executionId deduplication may remain a bounded request cache.
- Reject nested MCP execution, background editor access and persistent callbacks,
  timers or panels left by transient scripts.
- Cooperative timeout cannot forcibly interrupt blocking native calls. Do not
  use processEvents, thread termination or interpreter killing to simulate it.

Map rollback cannot undo external file/process effects. Once code runs, a failure
must not claim a blind retry is safe. After timeout/disconnection, inspect the
receipt and current editor facts. Crash, wrong-map write, data loss and unclear
mutation state are P0 issues.

## Bounded Output And Local Trust

Reuse source 256 KiB, request 4 MiB, JSON result 1 MiB, stdout/stderr 1 MiB limits,
and Python cooperative budgets of 30 seconds by default, at most 90 seconds.
Summaries are bounded to 16 KiB. Any retained result/log cache is bounded and
only evicts its own registered resources; it does not require legacy history or
review-resource formats. Do not add a new framework to reimplement these limits.

HTTP remains loopback-only without shared credentials. MCP defaults to Off.
ReadOnly cannot execute Python. requestedMode may only lower permissions.
Edit runs trusted code with the application's user privileges, not a sandbox.
Reuse task authorization; unrelated file or program actions require authorization.

Keep existing JSON-RPC validation, transport limits and supported protocol behavior.
Replacing the editor API does not require rewriting the HTTP server or adding
background editing processes, persistent Python sessions or task schedulers.
Transport shims may remain only as thin transport adapters, with no old-tool layer.

## Evidence And Delivery

Native validation reports editor facts. A screenshot is visual evidence, not
proof of map validity, BSP compilation, game collision or route playability.
Material names are not proof textures loaded. Report save, validation, capture
and untested facts separately. Domain judgment belongs in scripts and skills.

Perform one complete architecture cutover, then final acceptance. Build Release
TrenchBroom and affected tests; run the new core scenarios on disposable real maps.
Remove or update tests whose sole purpose is the retired catalog/compatibility.
Keep tests proving surviving native behavior and runtime correctness.

Acceptance proves the four-entry set, mode checks, old-name rejection, Python
composition, rollback, native undo/redo, object validity and capture. It does not
require old capability parity, module recovery, IR round trips, advanced Review,
route generators or old plugin symbol aliases.

The old capability-map can be archived or removed. Update the migration gate to
the new architecture or consolidate it into tests; historical structural checks
are not delivery evidence. Use the delivery plan's final preflight and conditional
Skill/manual/UI checks. Documentation-only changes require static validation.

## Skills And Documentation

The project skill source is skills/trenchbroom-mcp-scene-workflow. Update and sync
it with the new catalog using scripts/sync-trenchbroom-mcp-skill.ps1, and run the
applicable validator and synchronization check.

Skills discover actual Python symbols, compose scripts, use compact results and
recover from concrete failures. Their examples must not invoke retired tools or
require the new bridge to consume old IR. Data-only recipe validators may remain
for archived/offline outputs; they are not new-MCP acceptance requirements.

Record supported behavior and breaking changes concisely. Historical or installed
workflow instructions do not restore old compatibility requirements.
