# TrenchBroom MCP Skills

This directory stores project-owned skill sources.

The canonical TrenchBroom MCP workflow skill is:

```text
skills/trenchbroom-mcp-scene-workflow
```

The local runtime copy used by this machine is:

```text
C:\Users\Trh\.cc-switch\skills\trenchbroom-mcp-scene-workflow
```

After editing the project copy, sync and verify the runtime copy:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\sync-trenchbroom-mcp-skill.ps1
powershell -ExecutionPolicy Bypass -File scripts\sync-trenchbroom-mcp-skill.ps1 -Check
```
