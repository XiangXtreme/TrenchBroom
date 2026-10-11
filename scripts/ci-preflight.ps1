param(
  [string] $BuildDir = "build-release-codex",
  [string] $BaseRef = "",
  [string[]] $Paths = @(),
  [string] $QtBin = "D:\Qtx\6.11.1\msvc2022_64\bin",
  [string] $ClangCl = "",
  [switch] $Full,
  [switch] $ShowTestWindows,
  [switch] $SkipStrictCompile,
  [switch] $SkipBuild,
  [switch] $SkipTests,
  [switch] $SkipQtVersionCheck
)

$ErrorActionPreference = "Stop"

function Invoke-GitLines {
  param([string[]] $Arguments)

  $output = @(& git @Arguments)
  if ($LASTEXITCODE -ne 0) {
    throw "git $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
  }
  return $output
}

function Invoke-GitCheck {
  param([string[]] $Arguments)

  & git @Arguments
  if ($LASTEXITCODE -ne 0) {
    throw "git $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
  }
}

function Get-DefaultBaseRef {
  foreach ($candidate in @("origin/main", "main", "HEAD^")) {
    & git rev-parse --verify --quiet "$candidate^{commit}" *> $null
    if ($LASTEXITCODE -eq 0) {
      return $candidate
    }
  }
  throw "Could not determine a base ref. Pass -BaseRef explicitly."
}

function ConvertTo-RepoPath {
  param([string] $Path)

  $normalized = $Path.Replace('\', '/')
  if ($normalized.StartsWith("./")) {
    $normalized = $normalized.Substring(2)
  }
  return $normalized
}

function Get-ChangedFiles {
  param(
    [string] $EffectiveBaseRef,
    [string[]] $ExplicitPaths
  )

  $files = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)

  if ($ExplicitPaths.Count -gt 0) {
    foreach ($path in $ExplicitPaths) {
      [void] $files.Add((ConvertTo-RepoPath $path))
    }
  } else {
    foreach ($path in Invoke-GitLines @(
        "diff", "--name-only", "--diff-filter=ACMR", "$EffectiveBaseRef...HEAD", "--")) {
      [void] $files.Add((ConvertTo-RepoPath $path))
    }
    foreach ($arguments in @(
        @("diff", "--name-only", "--diff-filter=ACMR", "--"),
        @("diff", "--cached", "--name-only", "--diff-filter=ACMR", "--"),
        @("ls-files", "--others", "--exclude-standard"))) {
      foreach ($path in Invoke-GitLines $arguments) {
        [void] $files.Add((ConvertTo-RepoPath $path))
      }
    }
  }

  return @($files | Sort-Object)
}

function Test-VersionMatchesSpec {
  param(
    [string] $Version,
    [string] $Spec
  )

  $pattern = '^' + [regex]::Escape($Spec).Replace('\*', '[0-9]+') + '$'
  return $Version -match $pattern
}

function Assert-CiQtPackagesAvailable {
  param([string] $Version)

  if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    throw "CI Qt version must be exact so package availability can be checked: $Version"
  }

  $versionDirectory = "qt6_$($Version.Replace('.', ''))"
  foreach ($platform in @("windows_x86", "linux_x64", "mac_x64")) {
    $metadataUrl =
      "https://download.qt.io/online/qtsdkrepository/$platform/desktop/$versionDirectory/$versionDirectory/Updates.xml"
    try {
      $response = Invoke-WebRequest `
        -Uri $metadataUrl `
        -Method Head `
        -TimeoutSec 20 `
        -UseBasicParsing
    } catch {
      throw "CI Qt $Version package metadata is unavailable for $platform`: $metadataUrl"
    }
    if ($response.StatusCode -ne 200) {
      throw "CI Qt $Version package metadata returned HTTP $($response.StatusCode) for $platform"
    }
  }
  Write-Host "CI Qt packages: $Version metadata available for Windows, Linux, and macOS"
}

function Assert-QtVersionCompatibleWithCi {
  param(
    [string] $RepositoryRoot,
    [string] $QtBinaryDirectory
  )

  $actionPath = Join-Path $RepositoryRoot ".github\actions\setup-common\action.yml"
  $versionLine = Select-String -Path $actionPath -Pattern '^\s*version:\s*["'']?([^"''\s]+)'
  if (-not $versionLine) {
    throw "Could not read the CI Qt version from $actionPath"
  }
  $ciVersion = $versionLine.Matches[0].Groups[1].Value
  Assert-CiQtPackagesAvailable $ciVersion

  $qmake = Join-Path $QtBinaryDirectory "qmake.exe"
  if (-not (Test-Path $qmake)) {
    throw "qmake.exe not found in QtBin: $QtBinaryDirectory"
  }
  $localVersion = (& $qmake -query QT_VERSION).Trim()
  if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($localVersion)) {
    throw "Could not determine the local Qt version from $qmake"
  }
  if (-not (Test-VersionMatchesSpec $localVersion $ciVersion)) {
    $localMajor = ($localVersion -split '\.')[0]
    $ciMajor = ($ciVersion -split '\.')[0]
    if ($localMajor -ne $ciMajor) {
      throw "Qt major version mismatch: local $localVersion, CI $ciVersion"
    }
    Write-Warning "Qt version differs: local $localVersion, CI $ciVersion. Platform UI behavior may differ."
    return
  }
  Write-Host "Qt version: $localVersion (matches CI $ciVersion)"
}

function Get-TestTargets {
  param([string] $TestCMakePath)

  $contents = Get-Content -Raw -Path $TestCMakePath
  $matches = [regex]::Matches(
    $contents,
    '(?m)add_executable\(\s*([A-Za-z0-9_]+Test)\b')
  return @(
    $matches |
      ForEach-Object { $_.Groups[1].Value } |
      Sort-Object -Unique)
}

function Get-LibraryTestCMakeFiles {
  param([string] $LibraryDirectory)

  return @(
    Get-ChildItem -Path $LibraryDirectory -Filter "CMakeLists.txt" -File -Recurse |
      Where-Object { $_.Directory.Name -eq "test" } |
      Sort-Object FullName)
}

function Get-AffectedBuildPlan {
  param(
    [string] $RepositoryRoot,
    [string[]] $ChangedFiles,
    [bool] $IncludeAll
  )

  $buildTargets = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
  $testDirectories = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
  $libraries = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)

  if ($IncludeAll) {
    foreach ($directory in Get-ChildItem (Join-Path $RepositoryRoot "lib") -Directory) {
      if ((Get-LibraryTestCMakeFiles $directory.FullName).Count -gt 0) {
        [void] $libraries.Add($directory.Name)
      }
    }
    [void] $buildTargets.Add("TrenchBroom")
  } else {
    foreach ($file in $ChangedFiles) {
      if ($file -match '^lib/([^/]+)/') {
        [void] $libraries.Add($Matches[1])
      } elseif ($file -match '^app/([^/]+)/') {
        [void] $buildTargets.Add($Matches[1])
      } elseif (
        $file -eq "CMakeLists.txt" -or
        $file -match '^(cmake/|CI-[^/]+|\.github/actions/)') {
        [void] $buildTargets.Add("TrenchBroom")
      }
    }
  }

  foreach ($library in $libraries) {
    $libraryDirectory = Join-Path $RepositoryRoot "lib\$library"
    $testCMakeFiles = @(Get-LibraryTestCMakeFiles $libraryDirectory)
    if ($testCMakeFiles.Count -gt 0) {
      foreach ($testCMake in $testCMakeFiles) {
        $testDirectory = $testCMake.Directory.FullName
        foreach ($target in Get-TestTargets $testCMake.FullName) {
          [void] $buildTargets.Add($target)
        }
        [void] $testDirectories.Add($testDirectory)
      }
    } else {
      [void] $buildTargets.Add($library)
    }
  }

  return [PSCustomObject]@{
    BuildTargets = @($buildTargets | Sort-Object)
    TestDirectories = @($testDirectories | Sort-Object)
  }
}

function ConvertTo-StrictClangCommand {
  param(
    [string] $Command,
    [string] $ClangCompiler
  )

  $compilerPattern = '^(?:(?:"(?:[^"]*[\\/])?(?:ccache|sccache)\.exe"|(?:[^\s"]*[\\/])?(?:ccache|sccache)\.exe)\s+)?("[^"]*[\\/]cl\.exe"|[^\s"]*[\\/]cl\.exe)'
  $strictCommand = [regex]::Replace(
    $Command,
    $compilerPattern,
    ('"' + $ClangCompiler + '"'),
    [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
  if ($strictCommand -eq $Command) {
    throw "Expected an MSVC cl.exe compile command, but could not replace its compiler"
  }

  foreach ($option in @(
      "/MP",
      "/showIncludes",
      "/FS",
      "/experimental:external",
      "/external:anglebrackets")) {
    $strictCommand = $strictCommand -replace (
      ' ' + [regex]::Escape($option) + '(?=\s)'), ''
  }
  $strictCommand = $strictCommand -replace ' /Fo(?:"[^"]+"|\S+)', ''
  $strictCommand = $strictCommand -replace ' /Fd(?:"[^"]+"|\S+)', ''
  $strictCommand = $strictCommand -replace (
    ' (?=(?:-c|/c)\s)'),
    ' /Zs /WX /clang:-Wconversion /clang:-Wsign-conversion '
  return $strictCommand
}

function Invoke-StrictCompile {
  param(
    [System.Management.Automation.PathInfo] $ResolvedBuildDirectory,
    [string] $RepositoryRoot,
    [string[]] $ChangedFiles,
    [string] $ClangCompiler
  )

  $sourceFiles = @(
    $ChangedFiles |
      Where-Object { $_ -match '\.(c|cc|cpp|cxx)$' } |
      ForEach-Object { Join-Path $RepositoryRoot $_ } |
      Where-Object { Test-Path $_ } |
      ForEach-Object { (Resolve-Path $_).Path } |
      Sort-Object -Unique)
  if ($sourceFiles.Count -eq 0) {
    Write-Host "Strict compile: no changed C/C++ translation units"
    return
  }

  $ninja = (Get-Command ninja.exe -ErrorAction Stop).Source
  $commands = @(& $ninja -C $ResolvedBuildDirectory.Path -t commands)
  if ($LASTEXITCODE -ne 0) {
    throw "Could not read Ninja compile commands from $($ResolvedBuildDirectory.Path)"
  }

  Write-Host "Strict compile: $($sourceFiles.Count) changed translation unit(s)"
  Push-Location $ResolvedBuildDirectory.Path
  try {
    foreach ($sourceFile in $sourceFiles) {
      $pathAlternatives = @($sourceFile, $sourceFile.Replace('\', '/'))
      $pathPattern = ($pathAlternatives | ForEach-Object { [regex]::Escape($_) }) -join '|'
      $compilePattern = '(?i)(?:-c|/c)\s+"?(?:' + $pathPattern + ')"?\s*$'
      $command = $commands | Where-Object { $_ -match $compilePattern } | Select-Object -Last 1
      if (-not $command) {
        throw "No Ninja compile command found for $sourceFile. Build the affected target once and retry."
      }

      Write-Host "  clang-cl $([System.IO.Path]::GetFileName($sourceFile))"
      $strictCommand = ConvertTo-StrictClangCommand $command $ClangCompiler
      & cmd.exe /d /s /c $strictCommand
      if ($LASTEXITCODE -ne 0) {
        throw "Strict compile failed for $sourceFile"
      }
    }
  } finally {
    Pop-Location
  }
}

function Invoke-FilteredBuild {
  param(
    [string] $RepositoryRoot,
    [string] $BuildDirectory,
    [string] $Target,
    [string] $QtBinaryDirectory
  )

  $buildScript = Join-Path $RepositoryRoot "scripts\build-filtered.ps1"
  $environmentArgs = @()
  if (-not [string]::IsNullOrWhiteSpace($env:VSCMD_VER)) {
    $environmentArgs += "-NoVsDevCmd"
  }
  & powershell.exe -ExecutionPolicy Bypass -File $buildScript `
    -BuildDir $BuildDirectory `
    -Target $Target `
    -QtBin $QtBinaryDirectory @environmentArgs
  if ($LASTEXITCODE -ne 0) {
    throw "Build failed for target $Target"
  }
}

$originalLocation = Get-Location
$originalPath = $env:PATH
$originalShowTestWindows = $env:TB_TEST_SHOW_WINDOWS
try {
  $repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
  Set-Location $repositoryRoot

  $resolvedBuildDir = Resolve-Path $BuildDir -ErrorAction SilentlyContinue
  if (-not $resolvedBuildDir) {
    throw "Build directory not found: $BuildDir"
  }
  if (-not (Test-Path (Join-Path $resolvedBuildDir.Path "build.ninja"))) {
    throw "The preflight requires an existing Ninja build tree: $($resolvedBuildDir.Path)"
  }

  $effectiveBaseRef = if ([string]::IsNullOrWhiteSpace($BaseRef)) {
    Get-DefaultBaseRef
  } else {
    $BaseRef
  }
  & git rev-parse --verify --quiet "$effectiveBaseRef^{commit}" *> $null
  if ($LASTEXITCODE -ne 0) {
    throw "Base ref does not resolve to a commit: $effectiveBaseRef"
  }

  Write-Host "==> Local CI preflight"
  Write-Host "Repository: $repositoryRoot"
  Write-Host "Base ref:   $effectiveBaseRef"

  Invoke-GitCheck @("diff", "--check", "$effectiveBaseRef...HEAD", "--")
  Invoke-GitCheck @("diff", "--cached", "--check", "--")
  Invoke-GitCheck @("diff", "--check", "--")

  & (Join-Path $PSScriptRoot "check-ui-style-governance.ps1")
  if ($LASTEXITCODE -ne 0) {
    throw "UI style governance check failed"
  }

  $changedFiles = @(Get-ChangedFiles $effectiveBaseRef $Paths)
  Write-Host "Changed files considered: $($changedFiles.Count)"
  if ($changedFiles.Count -eq 0) {
    Write-Host "Nothing to check."
    exit 0
  }

  if (-not $SkipQtVersionCheck) {
    Assert-QtVersionCompatibleWithCi $repositoryRoot $QtBin
  }

  if (-not $SkipStrictCompile) {
    $clangCompiler = if ([string]::IsNullOrWhiteSpace($ClangCl)) {
      (Get-Command clang-cl.exe -ErrorAction Stop).Source
    } else {
      (Resolve-Path $ClangCl -ErrorAction Stop).Path
    }
    Invoke-StrictCompile $resolvedBuildDir $repositoryRoot $changedFiles $clangCompiler
  }

  $plan = Get-AffectedBuildPlan $repositoryRoot $changedFiles $Full
  if ($plan.BuildTargets.Count -eq 0) {
    Write-Host "Build: no affected CMake targets"
  } elseif ($SkipBuild) {
    Write-Host "Build skipped: $($plan.BuildTargets -join ', ')"
  } else {
    foreach ($target in $plan.BuildTargets) {
      Invoke-FilteredBuild $repositoryRoot $resolvedBuildDir.Path $target $QtBin
    }
  }

  if ($SkipTests) {
    Write-Host "Tests skipped"
  } else {
    $env:PATH = "$QtBin;$originalPath"
    $env:TB_TEST_SHOW_WINDOWS = if ($ShowTestWindows) { "1" } else { "0" }
    Write-Host $(if ($ShowTestWindows) {
      "UI tests: native windows enabled (-ShowTestWindows)"
    } else {
      "UI tests: background window policy; use -ShowTestWindows for desktop validation"
    })
    foreach ($testDirectory in $plan.TestDirectories) {
      $relativeTestDirectory = [System.IO.Path]::GetRelativePath(
        $repositoryRoot, $testDirectory)
      $buildTestDirectory = Join-Path $resolvedBuildDir.Path $relativeTestDirectory
      Write-Host "==> Test $relativeTestDirectory"
      & ctest --test-dir $buildTestDirectory -j --output-on-failure
      if ($LASTEXITCODE -ne 0) {
        throw "CTest failed in $buildTestDirectory"
      }
    }
  }

  Write-Host "==> Local CI preflight passed"
} finally {
  $env:PATH = $originalPath
  $env:TB_TEST_SHOW_WINDOWS = $originalShowTestWindows
  Set-Location $originalLocation
}
