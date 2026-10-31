param(
  [string] $BuildDir = "build-release-codex",
  [string] $Target = "TrenchBroom",
  [string] $Config = "Release",
  [string] $TestFilter = "",
  [string] $TestTarget = "",
  [string] $TestExe = "",
  [string] $QtBin = "D:\Qtx\6.11.1\msvc2022_64\bin",
  [string] $VsDevCmd = "D:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat",
  # Zero preserves CMake's default for standalone builds; preflight supplies a cap.
  [ValidateRange(0, 256)]
  [int] $Jobs = 0,
  [switch] $NoVsDevCmd,
  [switch] $NoFilter
)

$ErrorActionPreference = "Stop"

function Join-CommandArgs {
  param([string[]] $Parts)

  return ($Parts | ForEach-Object {
      if ($_ -match '[\s"&|<>^]') {
        '"' + ($_ -replace '"', '\"') + '"'
      } else {
        $_
      }
    }) -join " "
}

function Get-SafeFileNamePart {
  param([string] $Value)

  if ([string]::IsNullOrWhiteSpace($Value)) {
    return "all"
  }

  $safe = $Value
  foreach ($character in [System.IO.Path]::GetInvalidFileNameChars()) {
    $safe = $safe.Replace($character, "_")
  }
  $safe = $safe -replace '\s+', '_'
  $safe = $safe -replace '[^A-Za-z0-9_.-]', '_'
  $safe = $safe.Trim("._-")
  if ([string]::IsNullOrWhiteSpace($safe)) {
    return "filter"
  }
  if ($safe.Length -gt 80) {
    return $safe.Substring(0, 80)
  }
  return $safe
}

function Get-TestExePath {
  param(
    [System.Management.Automation.PathInfo] $ResolvedBuildDir,
    [string] $Target,
    [string] $TestTarget,
    [string] $TestExe
  )

  if (-not [string]::IsNullOrWhiteSpace($TestExe)) {
    return $TestExe
  }

  $effectiveTarget = if ([string]::IsNullOrWhiteSpace($TestTarget)) { $Target } else { $TestTarget }
  if ($effectiveTarget -notmatch 'Test$') {
    throw "TestFilter was provided, but '$effectiveTarget' is not a test target. Pass -TestTarget or -TestExe."
  }

  $candidateRoots = @(
    (Join-Path $ResolvedBuildDir "lib"),
    (Join-Path $ResolvedBuildDir "app")
  )
  foreach ($root in $candidateRoots) {
    if (-not (Test-Path $root)) {
      continue
    }
    $matches = @(Get-ChildItem -Path $root -Recurse -Filter "$effectiveTarget.exe" -File -ErrorAction SilentlyContinue)
    if ($matches.Count -eq 1) {
      return $matches[0].FullName
    }
    if ($matches.Count -gt 1) {
      throw "Multiple test executables found for '$effectiveTarget'. Pass -TestExe explicitly: $($matches.FullName -join ', ')"
    }
  }

  throw "Test executable not found for target '$effectiveTarget'. Pass -TestExe explicitly."
}

function Should-ShowLine {
  param([string] $Line)

  if ($NoFilter) {
    return $true
  }

  if ([string]::IsNullOrWhiteSpace($Line)) {
    return $false
  }

  $looksLikeLocalizedIncludeTrace =
    ($Line -notmatch '(?i)\b(error|warning|fatal|failed)\b') -and
    ($Line -match '^\S.{0,16}:\s+') -and
    ($Line -match '(?i)(\\include\\|/include/|\\lib\\|/lib/)') -and
    ($Line -notmatch '^[A-Za-z]:\\')
  if ($looksLikeLocalizedIncludeTrace) {
    return $false
  }

  $noisePatterns = @(
    '^Note: including file:',
    '^Adding local dependency',
    '^Adding in plugin type ',
    '^Adding Qt6',
    '^Skipping plugin ',
    '^Direct dependencies:',
    '^All dependencies\s+:',
    '^To be deployed\s+:',
    '^Qt6.*\.dll is up to date\.$',
    '^opengl32sw\.dll is up to date\.$',
    '^d3dcompiler_47\.dll is up to date\.$',
    '^dxcompiler\.dll is up to date\.$',
    '^dxil\.dll is up to date\.$',
    '^[A-Za-z0-9_.-]+\.dll is up to date\.$',
    '^q.*\.dll is up to date\.$',
    '^Creating qt_.*\.qm\.*$'
  )

  foreach ($pattern in $noisePatterns) {
    if ($Line -match $pattern) {
      return $false
    }
  }

  return $true
}

function Invoke-FilteredCommand {
  param(
    [string] $Title,
    [string] $Command,
    [string] $LogPath
  )

  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $LogPath) | Out-Null
  "" | Set-Content -Path $LogPath

  Write-Host "==> $Title"
  Write-Host "    log: $LogPath"

  $psi = [System.Diagnostics.ProcessStartInfo]::new()
  $psi.FileName = "cmd.exe"
  $psi.Arguments = "/d /s /c `"$Command 2>&1`""
  $psi.WorkingDirectory = (Get-Location).Path
  $psi.UseShellExecute = $false
  $psi.RedirectStandardOutput = $true
  $psi.RedirectStandardError = $false
  $psi.CreateNoWindow = $true

  $process = [System.Diagnostics.Process]::new()
  $process.StartInfo = $psi

  $shown = 0
  $hidden = 0
  $tail = [System.Collections.Generic.Queue[string]]::new()

  [void] $process.Start()
  while (-not $process.StandardOutput.EndOfStream) {
    $line = $process.StandardOutput.ReadLine()
    Add-Content -Path $LogPath -Value $line

    if ($tail.Count -ge 30) {
      [void] $tail.Dequeue()
    }
    $tail.Enqueue($line)

    if (Should-ShowLine $line) {
      Write-Host $line
      $shown++
    } else {
      $hidden++
    }
  }

  $process.WaitForExit()
  Write-Host "==> $Title finished with exit code $($process.ExitCode) ($shown shown, $hidden filtered)"

  if ($process.ExitCode -ne 0) {
    Write-Host ""
    Write-Host "Last log lines:"
    foreach ($line in $tail) {
      Write-Host $line
    }
    throw "$Title failed with exit code $($process.ExitCode). Full log: $LogPath"
  }
}

function Add-PathPrefixToCommand {
  param(
    [string] $Command,
    [string] $PathPrefix
  )

  if ([string]::IsNullOrWhiteSpace($PathPrefix)) {
    return $Command
  }
  if (-not (Test-Path $PathPrefix)) {
    throw "Path prefix not found: $PathPrefix"
  }
  return "set `"PATH=$PathPrefix;%PATH%`" && $Command"
}

$resolvedBuildDir = Resolve-Path -Path $BuildDir -ErrorAction SilentlyContinue
if (-not $resolvedBuildDir) {
  throw "Build directory not found: $BuildDir"
}

$logDir = Join-Path $resolvedBuildDir "codex-logs"
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"

$cmakeArgs = @(
  "cmake",
  "--build", $resolvedBuildDir.Path,
  "--target", $Target,
  "--config", $Config,
  "--parallel"
)
if ($Jobs -gt 0) {
  $cmakeArgs += [string] $Jobs
}
$cmakeCommand = Join-CommandArgs $cmakeArgs

if (-not $NoVsDevCmd) {
  if (-not (Test-Path $VsDevCmd)) {
    throw "VsDevCmd not found: $VsDevCmd"
  }
  # Ninja's MSVC dependency parser expects the English /showIncludes prefix.
  $cmakeCommand =
    "call `"$VsDevCmd`" -arch=x64 -host_arch=x64 >nul && set `"VSLANG=1033`" && $cmakeCommand"
} else {
  $cmakeCommand = "set `"VSLANG=1033`" && $cmakeCommand"
}
$cmakeCommand = Add-PathPrefixToCommand $cmakeCommand $QtBin

Invoke-FilteredCommand `
  -Title "Build $Target ($Config)" `
  -Command $cmakeCommand `
  -LogPath (Join-Path $logDir "$stamp-build-$Target.log")

if ($TestFilter) {
  $testExe = Get-TestExePath `
    -ResolvedBuildDir $resolvedBuildDir `
    -Target $Target `
    -TestTarget $TestTarget `
    -TestExe $TestExe
  if (-not (Test-Path $testExe)) {
    throw "Test executable not found: $testExe"
  }

  $testName = if ([string]::IsNullOrWhiteSpace($TestTarget)) { [System.IO.Path]::GetFileNameWithoutExtension($testExe) } else { $TestTarget }
  $safeFilter = Get-SafeFileNamePart $TestFilter
  $testCommand = Join-CommandArgs @($testExe, $TestFilter)
  $testCommand = Add-PathPrefixToCommand $testCommand $QtBin
  Invoke-FilteredCommand `
    -Title "Run $testName $TestFilter" `
    -Command $testCommand `
    -LogPath (Join-Path $logDir "$stamp-test-$testName-$safeFilter.log")
}
