param(
  [string]$Source = "",
  [string]$Destination = "C:\Users\Trh\.cc-switch\skills\trenchbroom-mcp-scene-workflow",
  [switch]$Check
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Source)) {
  $Source = Join-Path $PSScriptRoot "..\skills\trenchbroom-mcp-scene-workflow"
}

$Source = (Resolve-Path -LiteralPath $Source).Path

function Get-SkillFiles {
  param([string]$Root)

  Get-ChildItem -LiteralPath $Root -Recurse -File |
    Where-Object {
      $_.FullName -notmatch "\\__pycache__\\" -and
      $_.Extension -ne ".pyc"
    } |
    ForEach-Object {
      [pscustomobject]@{
        RelativePath = $_.FullName.Substring($Root.Length).TrimStart("\")
        FullName = $_.FullName
      }
    } |
    Sort-Object RelativePath
}

if ($Check) {
  if (-not (Test-Path -LiteralPath $Destination)) {
    throw "Destination skill directory does not exist: $Destination"
  }
  $Destination = (Resolve-Path -LiteralPath $Destination).Path

  $sourceFiles = Get-SkillFiles -Root $Source
  $destinationFiles = Get-SkillFiles -Root $Destination
  $sourceByPath = @{}
  $destinationByPath = @{}
  foreach ($file in $sourceFiles) { $sourceByPath[$file.RelativePath] = $file.FullName }
  foreach ($file in $destinationFiles) { $destinationByPath[$file.RelativePath] = $file.FullName }

  $differences = New-Object System.Collections.Generic.List[string]
  foreach ($path in ($sourceByPath.Keys | Sort-Object)) {
    if (-not $destinationByPath.ContainsKey($path)) {
      $differences.Add("missing in destination: $path")
      continue
    }
    $sourceHash = (Get-FileHash -LiteralPath $sourceByPath[$path] -Algorithm SHA256).Hash
    $destinationHash = (Get-FileHash -LiteralPath $destinationByPath[$path] -Algorithm SHA256).Hash
    if ($sourceHash -ne $destinationHash) {
      $differences.Add("content differs: $path")
    }
  }
  foreach ($path in ($destinationByPath.Keys | Sort-Object)) {
    if (-not $sourceByPath.ContainsKey($path)) {
      $differences.Add("extra in destination: $path")
    }
  }

  if ($differences.Count -gt 0) {
    $differences | ForEach-Object { Write-Error $_ }
    throw "Skill sync check failed with $($differences.Count) difference(s)."
  }

  Write-Host "Skill sync check passed: $Source -> $Destination"
  exit 0
}

New-Item -ItemType Directory -Force -Path $Destination | Out-Null
$Destination = (Resolve-Path -LiteralPath $Destination).Path

$sourceRoot = [System.IO.Path]::GetFullPath($Source)
$destinationRoot = [System.IO.Path]::GetFullPath($Destination)
if ($sourceRoot.TrimEnd("\") -eq $destinationRoot.TrimEnd("\")) {
  throw "Source and destination are the same directory."
}

$sourceFiles = Get-SkillFiles -Root $Source
$sourcePaths = [System.Collections.Generic.HashSet[string]]::new(
  [System.StringComparer]::OrdinalIgnoreCase)
foreach ($file in $sourceFiles) {
  [void] $sourcePaths.Add($file.RelativePath)
}

# The runtime copy is managed exclusively by this script, so remove retired source files.
Get-SkillFiles -Root $Destination | Where-Object {
  -not $sourcePaths.Contains($_.RelativePath)
} | ForEach-Object {
  Remove-Item -LiteralPath $_.FullName -Force
}

$sourceFiles | ForEach-Object {
  $target = Join-Path $Destination $_.RelativePath
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
  Copy-Item -LiteralPath $_.FullName -Destination $target -Force
}

Write-Host "Synced TrenchBroom MCP skill:"
Write-Host "  Source:      $Source"
Write-Host "  Destination: $Destination"
