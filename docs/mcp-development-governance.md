# MCP Development Governance

## Current Contract

MCP exposes a small entry surface around trusted Python execution and native
editor inspection. Scene composition and repeated editing belong in Python;
TrenchBroom retains document identity, native commands, transactions, validation,
rendering, and object lifetime checks.

This is the normative architecture contract. The active delivery plan is
[MCP Python migration](mcp-python-migration/development.md), with
[core acceptance scenarios](mcp-python-migration/scenarios.md).
The older lightweight, moderate, and long-term roadmaps are historical references.
Their tool inventories and IR-first workflows do not authorize new migration work.

The migration deliberately retires the old MCP catalog in capability-family
batches. Native capabilities, Python composition, and retirement are all valid
outcomes. Completion requires working core scenarios and removal of old entry
points; it does not require one Python replacement for every old tool.

## Layer Ownership

| Layer | Responsibility |
| --- | --- |
| MCP | Local transport, permissions, bounded discovery, execution dispatch, receipts and resources; six public entry points. |
| Python runtime | Trusted code in the existing process and Qt main thread, fresh globals, document-bound execution and result conversion. |
| trenchbroom API | Composable objects, named parameters, collections and useful batches over native editor operations. |
| Native modules / automation | Map commands, undo/redo, valid object identity, geometry, validation and rendering; shared state where actually needed. |
| Python scripts / skills | Loops, functions, scene generation, reusable arrangements, domain intent and workflow judgment. |

The default edit path is MCP -> Python runtime -> trenchbroom API -> native
commands. Read-only inspection, history, validation and capture may call native
services directly.

Reuse existing automation services when they remove real duplication or own
necessary shared state. A binding that calls an existing native command does not
need another forwarding service. Extraction is driven by ownership and actual
dependencies, not by a checklist requiring every old tool to move to automation.
Automation must not depend on Python or JSON-RPC. Python must not call old MCP
handlers through JSON, tool names, or a generic call-old-tool adapter.

C++ additions must be generic native editor capabilities required by a concrete
core workflow, such as brush construction or CSG. Finished scenes, stair/room
arrangements, gameplay interpretation, and repeated layouts belong in Python
scripts. Keep one implementation of native geometry and command algorithms.

## Public Surface And Tool Retirement

The final Edit catalog contains only tb_inspect, tb_api, tb_execute_python,
tb_history, tb_validate and tb_capture. ReadOnly excludes tb_execute_python.
Off exposes no callable tools or resources. History mutations require Edit.

Existing profile strings may be accepted for configuration migration, but must
normalize to the same permission-filtered final catalog. Full and exact-name
lookup must not retain a second tool surface. After cutover, old schemas,
registrations, dispatch branches, hidden aliases and unused dedicated wrappers
are deleted. Retained native helpers may be reused by the six entry points.
Do not move the entire old catalog into an action parameter table or accept an
arbitrary old tool name through the six entry points. Keep only the operations
needed for each entry's responsibility; compose edits in Python.

Record a disposition for each old capability family and test affected core
workflows. Remove a family once its useful behavior is provided by native APIs or
Python composition, or explicitly retired in the migration record. There is no
mandatory hide/deprecate waiting period for this authorized catalog replacement.

Keep existing public Python plugin/console APIs and native UI functionality
working. Removing an MCP entry point is not permission to remove a shared
algorithm still used by those clients. Existing focused tests provide regression
coverage; retain or adapt tests for the surviving behavior.

Discovery uses tb_inspect for current editor facts and tb_api for actual Python
bindings. Improve symbol descriptions and script examples before adding native
APIs. The public tool count is bounded even when Python capabilities grow.

## Scripts, IR And Targeting

Trusted scene scripts may import trenchbroom and call its public API when executed
through tb_execute_python, with the same document and transaction guards as inline
code. They may also compute data outside the editor and submit it to that execution
path. Skills do not edit live .map files or bypass native commands and undo.

IR is an optional data format for existing import/preview workflows. New scene
scripts can compose native Python APIs directly. Full IR operation parity,
module replacement, dedicated route generators and heightmap/compile automation
are extension decisions, not prerequisites for the six-entry delivery.

Retained IR support remains versioned: schemaVersion:1 is current; unversioned
input may be accepted as v1 with legacyUnversionedIr, and malformed or unsupported
versions fail before mutation. Document supported operations and reject unsupported
ones explicitly. Do not replace a guarded operation with a weaker fallback.

Retained replace_module paths must check IR hash, module revision/content hash
and the exact canonical live object set; file replacement uses its previewId.
Failures occur before mutation. Parent undo restores map content and any retained
metadata/module identity together. These requirements apply only where that
capability remains callable; they do not require building new module machinery.

Within an execution, prefer object handles and collections. Across executions,
recover by stable ID, a fresh query or user selection. Native groups are useful
for human-visible organization. JSON selectors and module metadata are optional.
Dense old maps with ambiguous ownership should use user selection.

## Execution And Failure Semantics

Keep the existing trusted-Python execution contract in the migration plan:

- Python runs only in Edit, on the Qt main thread, with fresh globals per request.
- Bind execution to the requested document fingerprint and saved path. Do not
  follow a changed active window during execution.
- Default transaction mode commits one native parent operation. Exceptions,
  cooperative timeout, invalid/oversized results and commit failures cancel it,
  restore selection and discard staged state.
- Document lifecycle, persistence and general editor actions use explicit action
  mode and report completed actions and partialMutation on failure.
- Preserve handle checks for deleted, reloaded, cross-document and reused objects,
  including after undo/redo.
- Reject nested MCP execution and background-thread editor access. Transient
  scripts cannot leave persistent callbacks, timers or panels.
- A cooperative timeout cannot forcibly interrupt a blocking native call. Do not
  use processEvents, thread termination or interpreter killing to simulate it.
- Once trusted code runs, external side effects may exist even after map rollback.
  Only rejection before execution may claim retrySafe:true.
- Preserve executionId replay/conflict handling, source identity, bounded logs,
  result resources and truthful mutation/rollback receipts.

Crash, wrong-map write, data loss and unclear mutation state remain P0 issues.
History must respect intervening manual edits. A timeout or disconnection requires
receipt/history inspection before retry. Never claim a map rollback undid file or
process effects.

## Output, Performance And Evidence

Ordinary structured summaries plus compatibility text are bounded to 16 KiB.
Python source is limited to 256 KiB, requests to 4 MiB, complete JSON results to
1 MiB and captured stdout/stderr to 1 MiB with discarded-byte counts. Python
cooperative budgets default to 30 seconds and are capped at 90 seconds.
Execution receipts retain 1024 entries; execution resources are bounded to
128 groups and 128 MiB. Only application-owned registered cache files are evicted.

Existing native tool response budgets remain Fast 10 seconds, Normal 30 seconds,
Long 120 seconds, with 5-second connection/write waits. Retained session structures
remain bounded: 1024 operation records, 128 review resources, 64 IR previews with
10-minute TTL, and current plus three recent document fingerprints. tb_inspect
exposes applicable limits, counts and evictions. Evicted resources return recovery
guidance. These bounds do not require retaining an otherwise retired subsystem.

Large results return counts, samples, bounds, warnings and resource paths first.
Full IDs, object listings and face/seam details are opt-in. Capture paths must be
absolute or directly openable. Review contact sheets default to at most two panels;
keep individual captures and bounded labels.

Review is optional visual evidence and never changes static acceptancePassed.
Report save, review, validation, BSP and game-collision status separately.
Material names are not proof a WAD or texture is loaded. Retained
requireMaterialAvailable checks fail during preflight when requested.

For retained route validation, declare continuous/stepped/jump_chain/spiral or
closed_loop intent; closedLoop must be explicit. Smooth ascending intent with
zero detected slopes fails. Report seamRelation, positiveGap, overlapDepth,
walkableContinuous and unavailable facts separately from curve quality.
qualityPolicy draft/balanced overruns warn; explicit smooth may fail acceptance;
thresholds must be positive finite numbers. Polyline direction metrics do not
prove mathematical tangent continuity, aesthetics, BSP or collision.

## Local Trust And Protocol

HTTP listens only on loopback and has no shared credential. Enabling ReadOnly or
Edit trusts local-user processes to connect to /mcp; Edit additionally permits
trusted Python with the user's process privileges. Python execution is not a
sandbox. Keep MCP Off by default and migrate old configuration to Off.

CORS echoes only accepted loopback origins. Requests declare jsonrpc:"2.0" with
object params; initialize advertises the supported 2025-06-18 protocol.
The stdio shim and application use the same config. requestedMode can only lower
effective permissions. Keep request, connection and output limits.

Task authorization applies to the relevant script actions. Reuse authorization
already given; ask only when an action exceeds it. Enabling Edit does not grant an
Agent unrelated filesystem or external-program actions.

## Delivery And Verification

Implement the three batches in the active migration plan: core Python workflows
and default discovery, complete old-tool cutover, then final Release acceptance.
Each batch may span several capability families. Source and relevant tests belong
in the same coherent change; do not split every helper or status update into a
separate delivery.

Build focused tests before running them, and build Release TrenchBroom for MCP
source/catalog/bridge/config/integration changes. Use disposable real maps for
mutation, identity, rollback, validation and capture acceptance. Keep original
evidence and check for new crash logs.

Test what survives and what is retired: six-entry permissions, actual failure of
all old names after cutover, core Python workflows, plugin/console regressions,
and retained extension guards. Update migration gate scripts to these assertions.
The historical capability-map and its structural gate do not prove delivery.

After required checks pass, move to the next batch. Repeat or expand testing only
for new changes, failures or unresolved risks. Final matrix requirements and
conditional Skill/manual/UI checks are specified in the migration plan.
Documentation-only governance changes require static checks, not a Release build.

## Skill And Documentation Maintenance

The project skill source is skills/trenchbroom-mcp-scene-workflow; synchronize
runtime copies through scripts/sync-trenchbroom-mcp-skill.ps1. Update discovery and
execution routing with the catalog cutover. Existing IR recipe scripts may retain
their data-generation interface; skills route supported output through the current
execution layer. Native Python composition may use script files directly.

When skill/recipes change, run their validator and synchronization check. Any
validator assumptions about IR-only output must be scoped to IR recipes when
direct Python examples are introduced. This does not weaken editor guards.

Skills own intent, API discovery, compact results and recovery judgment. Tool
schemas and Python symbol metadata own parameter details. Use current governance
and the active migration plan for implementation; installed workflow copies and
historical documents do not reintroduce retired-tool migration requirements.
