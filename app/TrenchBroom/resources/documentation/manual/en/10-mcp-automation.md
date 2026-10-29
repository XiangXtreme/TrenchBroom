# MCP Automation {#mcp_automation}

## MCP Settings {#mcp_settings}

Use `tb_inspect` with `view:"workspace"` to list projects and loaded documents, including retained maps whose tabs are closed. Follow `nextOffset` for additional pages. Inspect, execute and capture accept `target:{fingerprint:"..."}` or `target:{projectId:"...",mapId:"..."}`. Without a target, each call resolves the active map; execution remains bound to that document even if a script activates another tab. Invalid explicit targets fail.

Use a fresh `executionId` for each edit. Repeating an identical request returns its original receipt after focus changes or document unload. Closing a project tab preserves document handles; reload or unload invalidates them. Retained documents support edits and undo without a view. Capture requires a visible viewport and reports the actual document and camera. An optional `document` guard requires the inspected fingerprint and the saved document path.

The MCP group configures TrenchBroom's optional localhost HTTP endpoint for external agents. Access is **Off** by default. **Read-only** exposes `tb_inspect`, `tb_api`, and `tb_capture`; **Edit** also exposes guarded `tb_execute_python`. Python scripts use the public `trenchbroom` API, rather than a collection of specialized MCP editing tools.

The displayed endpoint has the form `http://127.0.0.1:<port>/mcp`. Use **Copy URL** for a generic MCP client or **Copy Setup Command** for Claude Code. Changing access rewrites the local MCP configuration and restarts the bridge.

![MCP Preferences](images/McpPreferences.png)

The server listens only on the loopback interface and does not use a bearer token. Enabling Read-only or Edit therefore trusts other processes running as the same local user. Leave access Off when it is not needed, verify the active document before allowing edits, and use TrenchBroom's undo history to review changes.
