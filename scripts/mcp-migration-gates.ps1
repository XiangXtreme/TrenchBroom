[CmdletBinding()]
param(
  [string]$OutputPath = 'build-release-codex/codex-logs/mcp-python-migration/gates.json'
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$catalogPath = Join-Path $repoRoot 'lib/TbMcpLib/src/McpToolCatalog.cpp'
$bridgePath = Join-Path $repoRoot 'lib/TbUiLib/src/mcp/McpBridgeServer.cpp'
$expectedTools = @('tb_inspect', 'tb_api', 'tb_execute_python', 'tb_capture')
$retiredTools = @('tb_history', 'tb_validate', 'tb_status', 'tb_doctor', 'tb_tools_search')

$catalogText = Get-Content -Raw $catalogPath
$bridgeText = Get-Content -Raw $bridgePath
$checks = [ordered]@{}
$checks.developmentContractExists = Test-Path (Join-Path $repoRoot 'docs/mcp-python-migration/development.md')
$checks.scenarioDefinitionsExist = Test-Path (Join-Path $repoRoot 'docs/mcp-python-migration/scenarios.md')
$checks.exactCatalogEntryPoints = @($expectedTools | Where-Object {
  $catalogText -notmatch ('"' + [regex]::Escape($_) + '"')
}).Count -eq 0
$checks.retiredToolsAbsentFromCatalog = @($retiredTools | Where-Object {
  $catalogText -match ('"' + [regex]::Escape($_) + '"')
}).Count -eq 0
$checks.noProfileConfiguration = $catalogText -notmatch 'McpToolProfile|toolProfile'
$checks.pythonExecutionIsRegistered = $bridgeText -match 'toolName == "tb_execute_python"'
$checks.captureIsRegistered = $bridgeText -match 'toolName == "tb_capture"'

& git -C $repoRoot -c core.safecrlf=false diff --check
$checks.gitDiffWhitespace = $LASTEXITCODE -eq 0
$passed = @($checks.Values | Where-Object { $_ -eq $false }).Count -eq 0
$result = [ordered]@{
  schemaVersion = 2
  generatedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
  headCommit = (git -C $repoRoot rev-parse HEAD).Trim()
  status = if ($passed) { 'passed' } else { 'failed' }
  checks = $checks
}

$outputDirectory = Split-Path -Parent $OutputPath
if ($outputDirectory) {
  New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
}
$result | ConvertTo-Json -Depth 4 | Set-Content -Encoding utf8 $OutputPath
$result | ConvertTo-Json -Depth 4
if (-not $passed) {
  exit 1
}
