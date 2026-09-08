# MCP 智能体自动化 {#mcp_automation}

## MCP 设置 {#mcp_settings}

MCP 组配置了 TrenchBroom 面向外部 Agent 的可选本地主机 HTTP 端点。访问权限默认处于 **Off** 状态。**Read-only** 提供 `tb_inspect`、`tb_api` 和 `tb_capture`；**Edit** 还提供受文档保护的 `tb_execute_python`。Python 脚本调用公开的 `trenchbroom` API，而不是一组专用 MCP 编辑工具。

显示的端点形式为 `http://127.0.0.1:<port>/mcp`。使用 **Copy URL** 可用于通用 MCP 客户端，或使用 **Copy Setup Command** 用于 Claude Code。更改访问权限会重写本地 MCP 配置并重启桥接器。

![MCP 首选项设置](images/McpPreferences.png)

服务器仅在环回接口上侦听，且不使用 Bearer 令牌。因此启用 Read-only 或 Edit 意味着信任以同一本地用户身份运行的其他进程。在不需要时请将访问保持为 Off，在允许编辑前确认当前活动文档，并使用 TrenchBroom 的撤销历史记录来检查所做的更改。
