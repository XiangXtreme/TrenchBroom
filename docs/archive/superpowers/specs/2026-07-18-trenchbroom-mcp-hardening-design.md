# TrenchBroom MCP Hardening and Maintainability Design

## Status

- Date: 2026-07-18
- State: proposed
- Scope owner: TrenchBroom MCP execution kernel and transports
- Target branch: `feature/latest-upstream-merge`

## Objective

Raise the TrenchBroom MCP implementation from a feature-rich and well-tested system
to a protocol-consistent, abuse-resistant, maintainable editor integration with
repeatable real-application acceptance evidence.

The work must preserve the current governance boundary:

- C++ MCP owns guarded access to live TrenchBroom state, document identity, native
  transactions, object recovery, validation, and review rendering.
- Skill recipes own scene composition, domain judgment, and reusable prefab-like
  scene families.
- IR remains the deterministic preview/apply boundary between recipes and C++.

This program does not seek a higher score by adding more tools. It improves the
reliability and maintainability of the existing capability surface.

## Current Baseline

The reviewed branch currently has:

- 142 catalog entries, 140 implemented tools, and 53 tools visible in the Modeling
  profile.
- Approximately 1.0 MiB and 32,000 lines under `lib/TbUiLib/src/mcp`.
- Loopback HTTP without caller authentication and a local-socket compatibility transport.
- Document path and fingerprint guards for mutating tools.
- Transaction-backed map mutation, MCP history, stable object ids, bounded session
  state, guarded module replacement, and atomic IR application.
- Seven recipe families with 18 validated examples.
- Passing focused Release tests: 70 test cases and 6,016 assertions across protocol,
  stdio client, bridge, and HTTP coverage.

The review identified three main gaps:

1. HTTP and local-socket request framing is not fully bounded before dispatch.
2. The stdio shim ignores the configured tool profile and cannot read operation or
   review resources that are readable through HTTP.
3. The tool registry registers every implemented catalog entry through one legacy
   dispatcher, so the registry contract cannot prove that each tool has a real
   handler.

The lightweight design document also has stale size and catalog baselines.

## Success Criteria

The hardening program is complete when all of the following are true:

1. No unauthenticated local connection can retain unbounded request data or remain in
   an incomplete request state indefinitely.
2. HTTP and stdio expose the configured profile and equivalent supported MCP resource
   behavior.
3. Every `implemented=true` tool maps to exactly one concrete handler, and startup or
   tests reject missing or duplicate mappings.
4. Mutation failures continue to report `mutatedDocument`, `partialMutation`,
   `retrySafe`, and a concrete recovery action.
5. The default Modeling profile contains only high-frequency Agent workflows; hidden
   expert tools remain discoverable through exact search.
6. Focused unit and integration tests pass, recipe validation passes, and a real
   disposable-map acceptance run produces no new crash logs or wrong-document writes.
7. Current catalog and size metrics are generated or checked so documentation cannot
   silently drift.

An aspirational score is not itself an acceptance criterion. The measurable target is
no known P0/P1 issue, no unaccepted P2 issue in the changed area, and repeatable
evidence for the claims above.

## Non-Goals

- Adding scene-specific C++ tools or aliases.
- Adding new architectural, KZ, surf, bhop, terrain, or route prefab systems to C++.
- Replacing TrenchBroom transactions with a parallel mutation model.
- Making tool profiles an authorization boundary; mode remains the authorization
  boundary and profile remains a discovery boundary.
- Rewriting all MCP handlers in one change.
- Reducing source size through file movement alone.
- Claiming BSP, game collision, or aesthetic validation from map validators or review
  renders.

## Delivery Strategy

The work is divided into four independently reviewable subprojects. Each subproject
must build and pass its focused tests before the next one starts. Each commit should be
coherent and leave the branch usable.

### Subproject 1: Transport Framing and Connection Budgets

Harden `McpHttpServer` and `McpBridgeServerTransport` before changing higher-level
protocol behavior.

HTTP requirements:

- Reject a header whose terminator appears after `MaxHeaderBytes`.
- Bound total buffered request bytes independently of `Content-Length`.
- Preserve the existing 64 KiB header and 4 MiB body limits, with an aggregate limit
  equal to those two budgets plus the four-byte header terminator.
- Reject duplicate `Content-Length` fields and every `Transfer-Encoding` payload; the
  server does not implement chunked request decoding.
- Apply a 10-second deadline from connection acceptance until a complete
  HTTP request has been received.
- Allow at most 32 ordinary active HTTP connections and 8 long-lived SSE
  connections. Reject excess connections with `503 Service Unavailable` before
  reading a body.
- Preserve loopback binding, explicit local-process trust, allowed-origin behavior, and the
  existing response contract.

Local-socket requirements:

- Define a 4 MiB maximum bridge request line size, including JSON but excluding the
  terminating newline.
- Disconnect and return a structured invalid-request response when practical once the
  limit is exceeded.
- Apply a 10-second deadline from connection acceptance until a complete request line
  is available, and allow at most 32 active local-socket connections.
- Keep valid newline-delimited JSON bridge requests compatible.

Tests must cover fragmented valid input, oversized headers, oversized bodies,
declared-length mismatch, slow/incomplete requests, missing newlines, and connection
cleanup. Transport limits must be constants with explicit rationale, not test-only
magic values.

### Subproject 2: HTTP and Stdio Protocol Parity

Make the two public transports share one JSON-RPC capability contract.

- Pass `McpBridgeConfig::toolProfile` through the stdio request path.
- Extend the local bridge with typed resource list/read requests so stdio does not
  pretend that a resource capability is available without a reader.
- Implement `resources/list` for the currently retained operation and review resources
  on both transports. Return compact URI/name/type metadata, paginate at 100 entries,
  and use an opaque cursor. Evicted resources remain absent from enumeration but keep
  their existing structured recovery response when an old URI is read directly.
- Preserve `resources/read` for operation and review URIs on both transports.
- Add a shared contract suite that runs the same initialize, tools/list, tools/call,
  error, notification, and resource scenarios against HTTP and stdio adapters.
- Keep `McpMode` as the effective permission ceiling. Profile differences must never
  expand configured permissions.

This resource contract preserves operation/review links on both transports without
forcing full operation details inline. Resource enumeration is diagnostic discovery,
not an alternative to the default compact tool workflow.

### Subproject 3: Concrete Domain Tool Registration

Replace pass-through registration with domain-owned registration while preserving
schemas and tool names.

Introduce a small registration record concept containing a definition identity and a
handler. Domain registration functions should own handler wiring, for example:

- document and status
- selection and viewport
- objects, groups, and selectors
- brushes and entities
- modules, IR, and history
- textures and assets
- validation, compilation, and review

Startup/test validation must prove:

- every implemented catalog name has exactly one handler;
- no unimplemented entry has a callable handler;
- no duplicate name exists;
- handler registration preserves catalog mode and mutation metadata;
- dispatch of an implemented name cannot fall through to `not wired yet`.

The catalog may remain the public schema source in this subproject. A larger schema
generation rewrite is out of scope unless the concrete registration work shows that a
single source of truth can be introduced without broad churn.

### Subproject 4: Focused Decomposition and Quality Evidence

Split only where doing so creates an independently understandable and testable unit.

Priority boundaries:

- primitive and batch geometry compilation out of `McpBrushTools.cpp`;
- selector resolution, module state, and IR preview/apply out of
  `McpSelectorTools.cpp`;
- bridge tests split by the corresponding production subsystem;
- repeated catalog schema fragments moved to focused builders.

Add quality evidence:

- coverage instrumentation in a compatible Clang configuration, with reports focused
  on protocol branches, transport errors, preflight rejection, rollback, and recovery;
- fuzz or property tests for JSON-RPC envelopes, HTTP framing, selectors, and IR
  version/shape parsing;
- fault-injection tests for transaction failure, staged IR failure, response timeout,
  and disconnect-after-dispatch;
- repeated real TrenchBroom disposable-map acceptance covering status binding,
  guarded mutation, undo/redo, module replacement, validation, review resources, and
  shutdown.

Source size is a diagnostic, not the primary goal. The first maintainability target is
to bring `lib/TbUiLib/src/mcp` below 800 KiB through removed duplication and retired
legacy paths, while preserving behavior and exact-name discovery. Further reduction
requires separate evidence that compatibility is no longer needed.

## Data Flow

The intended request path is:

1. A transport incrementally receives a bounded request.
2. The transport authenticates and parses a valid JSON-RPC envelope.
3. Protocol dispatch applies configured mode and profile behavior.
4. The concrete tool registry resolves exactly one handler.
5. Central dispatch applies document guards and object-id internalization.
6. The handler performs preflight and, for mutation, uses a native transaction.
7. Session metadata and operation history reconcile only after successful commit.
8. The result is externalized and compacted; full details remain available through a
   bounded resource or explicit detail mode.
9. Timeout or disconnect reports unknown mutation state where certainty is impossible
   and directs the client to history/status recovery.

No skill recipe, stdio shim, or HTTP adapter may write a map directly.

## Error and Recovery Contract

Transport rejection before dispatch must report or log that the document was not
mutated. Tool errors retain the existing structured error model.

For mutating calls:

- preflight failure: `mutatedDocument:false`, `partialMutation:false`,
  `retrySafe:true`;
- committed success: one authoritative undo target and explicit completion state;
- rolled-back aggregate failure: no document/session delta and a safe retry action;
- client timeout or connection loss after dispatch: mutation state is `unknown`, retry
  is unsafe, and recovery requires `tb_status` plus `history_status` or operation
  inspection.

Transport limits should not echo untrusted request bodies in logs.

## Testing and Verification

Every subproject runs the narrowest relevant Release targets first.

Required focused targets:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TbMcpLibTest -TestExe build-release-codex\lib\TbMcpLib\test\TbMcpLibTest.exe -TestFilter "Mcp*"
powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TbMcpStdioClientTest -TestExe build-release-codex\lib\TbMcpLib\test\TbMcpStdioClientTest.exe -TestFilter "McpBridgeClient"
powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TbUiLibTest -TestExe build-release-codex\lib\TbUiLib\test\TbUiLibTest.exe -TestFilter "[McpBridgeServer]"
powershell -ExecutionPolicy Bypass -File scripts\build-filtered.ps1 -Target TbUiLibTest -TestExe build-release-codex\lib\TbUiLib\test\TbUiLibTest.exe -TestFilter "McpHttpServer"
```

After C++ MCP behavior changes, also build the Release `TrenchBroom` target and run the
real reliability acceptance script against a disposable map. Recipe and skill checks
remain required when their source changes:

```powershell
python skills\trenchbroom-mcp-scene-workflow\scripts\validate_recipes.py
powershell -ExecutionPolicy Bypass -File scripts\sync-trenchbroom-mcp-skill.ps1 -Check
```

Static completion checks:

```powershell
git diff --check
rg -n "^(<<<<<<<|=======$|>>>>>>>)( |$)" lib app CMakeLists.txt
```

## Metrics and Release Gates

Track these metrics in a generated or test-checked report:

- catalog total, implemented total, and visible counts by profile;
- lifecycle and category counts;
- MCP source bytes and lines by subsystem;
- focused test case and assertion counts;
- transport rejection coverage;
- real acceptance run count, failure count, and crash-log delta;
- default response size samples for high-volume tools.

Release gates for this program:

- no known P0/P1 issue;
- no unaccepted P2 issue in the changed subsystem;
- all focused tests and the Release application build pass;
- recipe/skill checks pass when applicable;
- at least one current real TrenchBroom disposable-map run passes for each mutation,
  transport, or review behavior change;
- worktree contains no generated maps, review images, crash logs, or temporary reports
  staged for commit.

## Governance Decision Record

Layer decision:

- Owner layer: C++ MCP transports, protocol adapter, registry, and execution kernel.
- Why not recipe: framing, authentication, handler identity, document safety, and
  failure recovery require editor and transport internals.
- Why not existing MCP tools: these changes correct infrastructure behavior rather
  than express a scene workflow.
- Required TrenchBroom internals: native transactions, live document identity, object
  registry, session history, and review resources.
- Default output mode: compact summaries and resource links; full detail remains
  opt-in.
- Modeling profile visibility: no new visible tools; later work should reduce the
  default surface using existing searchable tools.
- Compatibility/lifecycle: preserve existing names and IR v1; transport behavior is
  hardened additively, and any resource capability change requires contract tests.
- Performance budget: bounded request sizes and connection lifetimes; preserve Fast,
  Normal, and Long response classes.
- Failure recovery behavior: pre-dispatch rejection is retry-safe; post-dispatch
  timeout remains unknown and requires history inspection.
- Validation path: focused catalog, protocol, stdio, HTTP, bridge, Release application,
  and real disposable-map acceptance.
- Real TB acceptance plan: use the checked-in reliability acceptance script, retain
  output only in the build tree, and verify no new crash logs.

## Implementation Order

1. Transport framing and connection budgets.
2. HTTP/stdio protocol parity.
3. Concrete domain handler registration.
4. Focused decomposition, coverage, fuzzing, and repeated real acceptance.

Each step receives its own implementation plan after this design is approved. The
first implementation plan must cover only Subproject 1.
