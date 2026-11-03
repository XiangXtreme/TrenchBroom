# TrenchBroom MCP Transport Hardening Record

## Goal

Bound unauthenticated HTTP and local-socket request framing, connection counts, and
incomplete-request lifetimes without changing valid MCP behavior.

## Implemented Limits

| Transport | Limit | Default |
| --- | --- | ---: |
| HTTP | Header bytes | 64 KiB |
| HTTP | Body bytes | 4 MiB |
| HTTP | Incomplete request timeout | 10 s |
| HTTP | Ordinary connections | 32 |
| HTTP | SSE connections | 8 |
| Local bridge | Request bytes | 4 MiB |
| Local bridge | Incomplete request timeout | 10 s |
| Local bridge | Connections | 32 |

The defaults are injectable through `McpHttpServerLimits` and
`McpBridgeTransportLimits` so boundary behavior can be tested without slow tests or
production-only constants.

## HTTP Behavior

- Header and body limits are enforced before JSON parsing or tool dispatch.
- `Content-Length` is required for POST, must be a non-negative decimal value, and
  conflicting duplicates are rejected.
- Unsupported transfer encodings and oversized/incomplete requests are rejected and
  disconnected.
- Ordinary and SSE connections use separate budgets.
- Each accepted connection has a single-shot deadline owned by the socket.
- Authentication, CORS, JSON-RPC, and valid request behavior are unchanged.

## Local Bridge Behavior

- Newline-delimited input is bounded before parsing.
- Oversized lines, incomplete requests, and excess connections are rejected before
  token or tool processing.
- A valid completed request refreshes the per-connection deadline.
- Existing response framing and token validation remain unchanged.

## Scope

This work did not change tool schemas, profiles, resources, recipes, IR, mutation
handlers, or map fixtures. HTTP and local transport budgets remain independent.

## Verification

Focused Catch2 coverage includes defaults, fragmented input, invalid framing,
oversized requests, timeouts, ordinary/SSE accounting, and local connection limits.
The Release `TrenchBroom` target and disposable-map MCP reliability acceptance were
also run without adding crash logs.

Implementation commits:

- `99ec5714e` Add injectable MCP transport budgets
- `9d65e99c1` Bound MCP HTTP request framing
- `4837a20f0` Bound MCP HTTP connection lifetimes
- `8bdd7af59` Bound MCP local bridge connections
- `60869111c` Sync MCP reliability catalog gate
