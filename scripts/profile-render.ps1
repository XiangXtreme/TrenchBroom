#requires -Version 7.0
[CmdletBinding()]
param(
  [Parameter(Mandatory)][string] $MapPath,
  [string] $Game = 'Half-Life',
  [string] $GamePath = '',
  [ValidateRange(5, 300)][int] $Seconds = 15,
  [string] $Position = '',
  [string] $Direction = '',
  [string] $BuildDir = 'build-release-codex',
  [string] $QtBin = 'D:\Qtx\6.11.1\msvc2022_64\bin',
  [ValidateRange(1, 60)][int] $WarmupSeconds = 8,
  [switch] $Play,
  [switch] $MoveForward,
  [switch] $HideModels,
  [switch] $HideLabels,
  [switch] $HideEdges,
  [switch] $NoBuild
)

$ErrorActionPreference = 'Stop'
if ($MoveForward -and -not $Play) { throw '-MoveForward requires -Play' }
$build = (Resolve-Path -LiteralPath $BuildDir).Path
$map = (Resolve-Path -LiteralPath $MapPath).Path
if ($GamePath) { $GamePath = (Resolve-Path -LiteralPath $GamePath).Path }
if (-not $NoBuild) {
  & "$PSScriptRoot/build-filtered.ps1" -BuildDir $build -Target TbUiLibTest -QtBin $QtBin
  if ($LASTEXITCODE -ne 0) { throw 'TbUiLibTest build failed' }
}

$outputDir = Join-Path $build ('codex-logs/render-profile/' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
[ordered]@{
  map = $map; game = $Game; gamePath = $GamePath; seconds = $Seconds
  position = $Position; direction = $Direction
  hideLabels = [bool]$HideLabels; hideEdges = [bool]$HideEdges; hideModels = [bool]$HideModels
  warmupSeconds = $WarmupSeconds; play = [bool]$Play; moveForward = [bool]$MoveForward
} | ConvertTo-Json | Set-Content (Join-Path $outputDir 'capture.json')
$jsonl = Join-Path $outputDir 'render.jsonl'
$exe = Join-Path $build 'lib/TbUiLib/test/TbUiLibTest.exe'
$start = [System.Diagnostics.ProcessStartInfo]::new($exe)
$start.WorkingDirectory = Split-Path $exe
$start.UseShellExecute = $false
$start.RedirectStandardOutput = $true
$start.RedirectStandardError = $true
$start.ArgumentList.Add('RenderView profile external map')
$start.Environment['PATH'] = "$QtBin;$env:PATH"
$start.Environment['TB_TEST_SHOW_WINDOWS'] = '1'
$start.Environment['TB_RENDER_PROFILE'] = $jsonl
$start.Environment['TB_RENDER_PROFILE_MAP'] = $map
$start.Environment['TB_RENDER_PROFILE_GAME'] = $Game
$start.Environment['TB_RENDER_PROFILE_GAME_PATH'] = $GamePath
$start.Environment['TB_RENDER_PROFILE_SECONDS'] = [string]$Seconds
$start.Environment['TB_RENDER_PROFILE_WARMUP'] = [string]$WarmupSeconds
$start.Environment['TB_RENDER_PROFILE_POSITION'] = $Position
$start.Environment['TB_RENDER_PROFILE_DIRECTION'] = $Direction
foreach ($flag in @('TB_RENDER_PROFILE_HIDE_LABELS', 'TB_RENDER_PROFILE_HIDE_EDGES', 'TB_RENDER_PROFILE_HIDE_MODELS', 'TB_RENDER_PROFILE_PLAY', 'TB_RENDER_PROFILE_MOVE_FORWARD')) {
  $start.Environment.Remove($flag) | Out-Null
}
if ($HideLabels) { $start.Environment['TB_RENDER_PROFILE_HIDE_LABELS'] = '1' }
if ($HideEdges) { $start.Environment['TB_RENDER_PROFILE_HIDE_EDGES'] = '1' }
if ($HideModels) { $start.Environment['TB_RENDER_PROFILE_HIDE_MODELS'] = '1' }
if ($Play) { $start.Environment['TB_RENDER_PROFILE_PLAY'] = '1' }
if ($MoveForward) {
  $start.Environment['TB_RENDER_PROFILE_MOVE_FORWARD'] = '1'
}
Write-Host "Profiling $map for $Seconds seconds after $WarmupSeconds seconds warmup (Play=$Play)."
$process = [System.Diagnostics.Process]::Start($start)
$stdout = $process.StandardOutput.ReadToEndAsync()
$stderr = $process.StandardError.ReadToEndAsync()
$process.WaitForExit()
$stdout.GetAwaiter().GetResult() | Set-Content (Join-Path $outputDir 'test-output.txt')
$stderr.GetAwaiter().GetResult() | Set-Content (Join-Path $outputDir 'test-errors.txt')
if ($process.ExitCode -ne 0) { throw "Capture failed; inspect $outputDir" }

$records = @(Get-Content -LiteralPath $jsonl | ForEach-Object { $_ | ConvertFrom-Json })
$metadata = @($records | Where-Object { $_.kind -eq 'metadata' -and $_.view -eq 'tb::ui::MapView3D' })
if ($metadata.Count -eq 0) { throw "No 3D view capture in $jsonl" }
$viewId = $metadata[0].view_id
$windows = @($records | Where-Object { $_.kind -eq 'window' -and $_.view_id -eq $viewId })
# Keep only complete reporting windows inside the measured phase.
$measurement = Get-Content -Raw -LiteralPath "$jsonl.measurement.json" | ConvertFrom-Json
$measurementStart = [DateTimeOffset]::Parse($measurement.start_utc)
$measurementEnd = [DateTimeOffset]::Parse($measurement.end_utc)
$windows = @($windows | Where-Object {
  $end = [DateTimeOffset]::Parse($_.utc)
  $end.AddMilliseconds(-$_.elapsed_ms) -ge $measurementStart -and $end -le $measurementEnd
})
if ($windows.Count -eq 0) { throw 'No steady-state windows; increase -Seconds' }
$frames = ($windows | Measure-Object frames -Sum).Sum
$elapsed = ($windows | Measure-Object elapsed_ms -Sum).Sum
$rows = foreach ($stage in @($windows | ForEach-Object { $_.timings_ms.PSObject.Properties.Name } | Sort-Object -Unique)) {
  $sum = 0.0; $samples = 0; $p95 = 0.0; $maximum = 0.0
  foreach ($window in $windows) {
    $values = $window.timings_ms.$stage
    if ($null -eq $values) { continue }
    $sum += $values.avg * $values.samples
    $samples += $values.samples
    $p95 = [Math]::Max($p95, $values.p95)
    $maximum = [Math]::Max($maximum, $values.max)
  }
  [pscustomobject]@{
    Stage = $stage
    AvgMs = [Math]::Round($sum / [Math]::Max(1, $samples), 3)
    PerFrameMs = [Math]::Round($sum / $frames, 3)
    WorstWindowP95Ms = [Math]::Round($p95, 3)
    MaxMs = [Math]::Round($maximum, 3)
    Samples = $samples
  }
}
$rows | Export-Csv (Join-Path $outputDir 'timings.csv') -NoTypeInformation
$rows | Sort-Object AvgMs -Descending | Format-Table -AutoSize | Out-Host
$counts = foreach ($name in @($windows | ForEach-Object { $_.counters.PSObject.Properties.Name } | Sort-Object -Unique)) {
  $total = 0.0
  foreach ($window in $windows) { $total += $window.counters.$name }
  [pscustomobject]@{ Counter = $name; PerFrame = [Math]::Round($total / $frames, 2); Total = $total }
}
$counts | Export-Csv (Join-Path $outputDir 'counters.csv') -NoTypeInformation
$counts | Format-Table -AutoSize | Out-Host
Write-Host ('FPS: {0:N1}; viewport: {1}x{2}; GPU: {3}' -f (1000 * $frames / $elapsed), $windows[0].width_px, $windows[0].height_px, $metadata[0].renderer)
Write-Host "Log: $jsonl"
