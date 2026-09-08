# TrenchBroom MCP Execution Boundaries

## Scope

This document covers the thin MCP bridge and trusted Python execution defined in
[current governance](mcp-development-governance.md) and the
[delivery plan](mcp-python-migration/development.md). Existing native map editing
and user work remain protected while old MCP-specific compatibility state retires.

## Local Trust

HTTP binds to loopback and has no shared credential. MCP is Off by default.
Enabling ReadOnly permits native inspection/capture; Edit additionally permits
trusted Python with the application's user privileges. Python is not sandboxed.

CORS accepts only allowed loopback origins. JSON-RPC version and params checks,
transport limits and requestedMode privilege reduction remain in force. A transport
shim may forward the current protocol, but does not preserve old tools or profiles.

Task authorization governs what an Agent does through Python. Reuse authorization
already given; enabling Edit is not permission for unrelated file/process actions.

## Protected Boundaries

| Risk | Required behavior |
| --- | --- |
| Wrong document | Bind transaction execution to document fingerprint and saved path; do not follow active-window changes during execution. |
| Invalid object access | Check deletion, reload, cross-document access, undo/redo and address reuse before accessing native objects. |
| Partial map edit | Use one native transaction; cancel on exception, cooperative timeout, result failure or commit failure; restore selection. |
| Nontransactional action failure | Lifecycle, persistence and native undo/redo use action mode and report completed actions/partial modification. |
| Unsafe retry | Return request identity, execution outcome and truthful mutation/rollback status; inspect receipts and map facts after timeout/disconnection. |
| Excessive output | Reuse source/request/result/log bounds and cooperative execution budgets; keep any request/result cache bounded. |
| Background editor access | Dispatch native object access to the Qt main thread; reject nested MCP execution and persistent UI callbacks from transient scripts. |
| External Python effects | Map rollback does not undo file or process effects; never claim arbitrary trusted code is safe to retry after it ran. |
| Loss of user work | Preserve native UI/map behavior, do not silently discard dirty documents, and respect intervening manual edits in native undo/redo. |
| Misleading acceptance | Treat screenshots as visual evidence; report native validation, save and untested BSP/collision facts separately. |

These protections use native editor state and the existing executor. They do not
require legacy IR previews, module metadata, operationId maps, isolated Review
registries or old object-ID aliases. Remove compatibility-only state and the
associated checks; retain the native safety checks needed by surviving operations.

Cooperative timeouts cannot forcibly interrupt blocking native calls. Do not use
thread killing or processEvents to bypass that constraint.

## Verification

Use the [core execution scenarios](mcp-python-migration/scenarios.md). Build focused
test targets before running them, then validate the final Release application on
disposable maps and isolated configuration.

Verify the exact four-entry registration/dispatch set, permissions, wrong-target
rejection, invalid handles, transaction rollback, action failure reporting, native
undo/redo and screenshot output. Update affected console/plugin runtime tests to
current APIs. Remove tests solely enforcing retired-tool compatibility.

Older reliability scripts and capability-map gates may invoke removed names.
Adapt their relevant native-safety checks or replace them with core scenarios;
do not restore a legacy catalog so those scripts pass.
