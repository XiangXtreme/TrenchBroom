# TrenchBroom MCP HTTP/Stdio Protocol Parity Record

## Goal

Expose the configured tool profile and the same retained operation/review resources
through HTTP and stdio without changing tool schemas, permissions, or map mutation
semantics.

## Implemented Contract

- Local bridge request types are `tool_call`, `resources_list`, and `resource_read`.
- Untyped legacy bridge requests still parse as tool calls.
- `resources/list` returns retained operation and review descriptors only.
- Pages contain at most 100 compact descriptors and use an opaque cursor.
- Evicted resources are omitted from enumeration; direct reads preserve structured
  eviction guidance.
- HTTP and stdio use `McpBridgeConfig::toolProfile`.
- The configured `McpMode` remains the permission ceiling.

## Final Architecture

`McpJsonRpc` owns JSON-RPC validation and response shaping. It sends tool calls and
resource operations through one typed request dispatcher. HTTP dispatches those
requests directly to `McpBridgeServer`; stdio sends the same requests through
`McpBridgeClient` and the authenticated local bridge. There is no separate stdio
protocol implementation or resource error wrapper.

Resource enumeration and reads are implemented once in `McpSessionState` and
`McpBridgeServer`. Both transports therefore observe the same pagination, retention,
and error behavior.

## Verification

Focused tests cover bridge message round trips, client request serialization,
resource pagination and eviction, JSON-RPC error mapping, HTTP behavior, local bridge
authentication, and the shared direct/stdio protocol contract. The Release app and
real stdio resource-read acceptance were also run without adding crash logs.

Implementation commits:

- `6109adccc` Add typed MCP bridge requests
- `4032822c2` Expose bounded MCP resource enumeration
- `68d492045` Unify MCP HTTP and stdio protocol behavior
