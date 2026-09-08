# TrenchBroom MCP Security Threat Model

## Scope

This document covers the local MCP HTTP server, the legacy stdio shim, bridge
configuration, tool authorization, document targeting, map mutation, external
process tools, trusted Python execution, and cached MCP resources. It assumes one
desktop user and one active TrenchBroom MCP instance.

The current architecture and delivery scope are defined by
[MCP governance](mcp-development-governance.md) and the
[Python migration plan](mcp-python-migration/development.md). Controls for IR and
module replacement below apply to retained callable paths; they do not require
recreating those capabilities as Python APIs. Legacy acceptance scripts must be
adapted to the final catalog before being used as cutover evidence.

## Protected Assets

- The active map and TrenchBroom undo/redo state.
- Unsaved user work in every open document.
- The local MCP configuration.
- Stable object ids, operation history, metadata, modules, previews, and review
  resources held in memory.
- Local files read or written by IR, review, export, heightmap, and compile tools.
- External commands launched by compile profiles or the legacy Python bridge.

## Trust Boundaries

The MCP client is outside the editor trust boundary. Loopback networking narrows
exposure but does not authenticate callers: when MCP is enabled, any process running
as the local user may connect to the endpoint. Enabling `ReadOnly` or `Edit` is an
explicit decision to trust local processes at that privilege level.

The stdio shim is also outside the editor process. It reads the same TrenchBroom
config and forwards requests through the local pipe. Data-only recipes may write
IR for native validation and application. Trusted scripts submitted through
`tb_execute_python` may call the public `trenchbroom` API under document and
transaction guards. Python runs with the application's user privileges and is
not a sandbox; map rollback does not undo filesystem or process effects.

## Main Threats And Controls

| Threat | Control |
| --- | --- |
| Untrusted local process calls MCP | MCP is `Off` by default. Enabling `ReadOnly` or `Edit` explicitly trusts local-user processes; mode checks bound available operations and users should disable MCP when that trust is not acceptable. |
| Browser-origin request abuses loopback | CORS echoes only accepted loopback origins and rejects non-loopback origins. Preflight permits only the protocol and content headers used by MCP. |
| Local configuration is modified | Configuration contains no credential. Config writes still attempt owner read/write permissions, and startup validates loopback host, port, mode, and tool profile. |
| Client requests more privilege than configured | The effective mode is the stricter of configured mode and `requestedMode`. Tool catalog mode checks run before dispatch. |
| Write reaches the wrong map | Python execution binds `document.fingerprint` and, for saved maps, `document.path`. Retained legacy tools check `expectedDocumentPath` and `expectedDocumentFingerprint`; both must match when provided. |
| Partial IR mutation leaves inconsistent editor/session state | `ir_apply` uses one outer native transaction and stages history, counters, metadata, modules, and object registry state. Any stage failure rolls back all state. |
| Previewed IR or replacement target changes before apply | File preview records the IR hash. `replace_module` also guards module revision, content hash, and the exact canonical live object set; file replacement requires its cached `previewId`. Any mismatch fails before mutation. |
| Unsupported IR version changes semantics | New IR uses integer `schemaVersion:1`. Invalid versions, versions below 1, and future versions fail before mutation. |
| Missing material silently appears valid | Mutation responses distinguish requested/effective material and availability. `requireMaterialAvailable:true` rejects missing materials during preflight. |
| Review image is treated as a safety or correctness gate | Review reports construction/silhouette interpretation and remains optional evidence. It cannot change static `acceptancePassed`, map validation, BSP, or collision status. |
| Timeout causes unsafe blind retry | Tool cost classes use 10/30/120-second response waits. Timeout diagnostics report `mutatedDocument:"unknown"`, `retrySafe:false`, request identity, and history recovery steps. |
| Unbounded session data exhausts memory | Retained session limits are 1024 operation records, 128 reviews, 64 previews with a 10-minute TTL, and four document fingerprints. `tb_inspect` exposes counts and evictions; Python result/log/resource budgets follow the migration contract. |
| Stale or evicted resource is mistaken for live state | Resource reads return structured eviction and recovery guidance. Object resolution reports stale/live/mismatch state. |
| Second instance steals an active bridge endpoint | Startup probes the pipe and HTTP listener. It refuses an active instance and removes only an inactive stale endpoint. |
| Arbitrary local execution through compile or Python | Compile and Python require Edit; Python runs trusted code under the execution contract. Agent actions must remain within the user's task authorization; reuse authorization already given. Public editor API calls retain document, object and transaction guards. Scripts must not edit live `.map` files or bypass native commands. |

## Protocol Requirements

- Requests declare `jsonrpc:"2.0"`.
- `params` must be an object; other values return `-32602`.
- Initialize advertises only protocol `2025-06-18`.
- HTTP listens on loopback only.
- HTTP requests do not carry an authentication header or shared secret.
- The stdio shim uses the TrenchBroom application config path, not a separate
  `trenchbroom-mcp` config directory.

## Residual Risks

There is no caller authentication between local-user processes. Any such process can
read map data in `ReadOnly` mode and can mutate maps in `Edit` mode while MCP is
enabled. Loopback binding and Origin checks reduce network and browser exposure but
do not isolate hostile local software. Keep MCP `Off` outside active sessions and use
the least-privileged mode that supports the workflow.

Compile profiles, trusted Python execution and any remaining legacy Python bridge
can execute local programs. Mode checks do not make untrusted scripts safe.
Agents check actions against the user's authorized task and ask only when an
action exceeds it. Edit mode alone is not authorization for unrelated side effects.

MCP history mirrors native undo/redo. Manual user edits can invalidate the expected
stack order. Clients must refresh `tb_history` status after timeouts, manual edits, or
unexpected validation results.

## Verification

Run the automated gates:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TbMcpLibTest -TestExe build-release-codex\lib\TbMcpLib\test\TbMcpLibTest.exe -TestFilter "*"
powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TbUiLibTest -TestFilter "[McpBridgeServer]"
powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TbUiLibTest -TestFilter "[McpHttpServer]"
powershell -ExecutionPolicy Bypass -File scripts\mcp-reliability-acceptance.ps1
```

The real acceptance script uses disposable maps and checks tokenless loopback HTTP,
atomic IR undo/redo, rollback after entity failure, preview tampering, document
guards, review resource reads, review-resource eviction recovery, and crash-log
counts. Pass `-IncludeStdio` only when the optional compatibility shim was built and
a controlled stdio request should also be checked.
