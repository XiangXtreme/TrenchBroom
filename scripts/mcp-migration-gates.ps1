[CmdletBinding()]
param(
  [string]$BaseRef = 'd98f869503f331809cd6727712438534c16c8c9e',
  [string]$OutputPath = 'build-release-codex/codex-logs/mcp-python-migration/gates.json'
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$mapPath = Join-Path $repoRoot 'docs/mcp-python-migration/capability-map.json'
$catalogPath = Join-Path $repoRoot 'lib/TbMcpLib/src/McpToolCatalog.cpp'
$requiredTools = @('tb_inspect', 'tb_api', 'tb_execute_python', 'tb_history', 'tb_validate', 'tb_capture')

$checks = [ordered]@{}
$checks.baseRefExists = [bool](git -C $repoRoot rev-parse --verify "$BaseRef^{commit}" 2>$null)
$checks.developmentContractExists = Test-Path (Join-Path $repoRoot 'docs/mcp-python-migration/development.md')
$checks.capabilityMapExists = Test-Path $mapPath
$checks.scenarioDefinitionsExist = Test-Path (Join-Path $repoRoot 'docs/mcp-python-migration/scenarios.md')
$catalogText = Get-Content -Raw $catalogPath
$checks.requiredEntryPointsRegistered = @($requiredTools | Where-Object {
    $catalogText -notmatch ('"' + [regex]::Escape($_) + '"')
  }).Count -eq 0

if ($checks.capabilityMapExists) {
  try {
    $map = Get-Content -Raw $mapPath | ConvertFrom-Json
    $checks.capabilityMapHasEntries = @($map.entries).Count -gt 0
    $checks.capabilityMapHasBaseline = $map.baseCommit -eq $BaseRef
    $checks.capabilityMapSchemaVersion = $map.schemaVersion -eq 2
    $checks.capabilityMapHas142BaselineEntries = @($map.entries).Count -eq 142
    $checks.capabilityMapHasTwoPlaceholders = @(
      $map.entries | Where-Object { $_.status -eq 'placeholder' }
    ).Count -eq 2
    $effectiveEntries = @($map.entries | Where-Object { $_.status -ne 'placeholder' })
    $checks.capabilityMapHas140EffectiveEntries = $effectiveEntries.Count -eq 140
    $checks.capabilityMapHasUniqueLegacyTools = @($map.entries.legacyTool | Sort-Object -Unique).Count -eq 142
    $checks.capabilityMapEffectiveEntriesAudited = @(
      $effectiveEntries | Where-Object {
        [string]::IsNullOrWhiteSpace($_.actualCapability) -or
        $_.actualCapability -match 'detailed semantic mapping pending' -or
        [string]::IsNullOrWhiteSpace($_.domain) -or
        @($_.replacementSymbols).Count -eq 0 -or
        $null -eq $_.verification -or
        [string]::IsNullOrWhiteSpace($_.verification.testId) -or
        [string]::IsNullOrWhiteSpace($_.verification.scenarioId)
      }
    ).Count -eq 0
    $checks.capabilityMapPlaceholderReasons = @(
      $map.entries | Where-Object {
        $_.status -eq 'placeholder' -and [string]::IsNullOrWhiteSpace($_.placeholderReason)
      }
    ).Count -eq 0
    $checks.capabilityMapMatchesCatalog = @(
      $map.entries | Where-Object {
        $catalogText.IndexOf('"' + $_.legacyTool + '"') -lt 0
      }
    ).Count -eq 0
  }
  catch {
    $checks.capabilityMapParses = $false
  }
}

$previousErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
& git -C $repoRoot -c core.safecrlf=false diff --check 2>$null | Out-Null
$diffCheckExitCode = $LASTEXITCODE
$ErrorActionPreference = $previousErrorActionPreference
$checks.gitDiffWhitespace = $diffCheckExitCode -eq 0
$passed = @($checks.Values | Where-Object { $_ -eq $false }).Count -eq 0
$result = [ordered]@{
  schemaVersion = 1
  generatedAtUtc = (Get-Date).ToUniversalTime().ToString('o')
  baseRef = $BaseRef
  headCommit = (git -C $repoRoot rev-parse HEAD).Trim()
  status = if ($passed) { 'passed' } else { 'failed' }
  checks = $checks
}

$outputDirectory = Split-Path -Parent $OutputPath
if ($outputDirectory) {
  New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null
}
$result | ConvertTo-Json -Depth 8 | Set-Content -Encoding utf8 $OutputPath
$result | ConvertTo-Json -Depth 8
if (-not $passed) {
  exit 1
}
