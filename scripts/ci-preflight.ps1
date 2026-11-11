param(
  [string] $BuildDir = "build-release-codex",
  [string] $BaseRef = "",
  [string[]] $Paths = @(),
  [string] $QtBin = "D:\Qtx\6.11.1\msvc2022_64\bin",
  [string] $ClangCl = "",
  [ValidateRange(1, 256)]
  [int] $BuildJobs = 4,
  [ValidateRange(1, 256)]
  [int] $TestJobs = 2,
  [string] $TestFilter = "",
  [switch] $AllTests,
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
  # Include commits that would be pushed even when unrelated worktree files exist.
  # Use the local tracking ref; this does not contact or modify the remote.
  $tracking = @(& git rev-parse --abbrev-ref --symbolic-full-name '@{upstream}' 2>$null)
  if ($LASTEXITCODE -eq 0 -and $tracking.Count -eq 1) {
    $aheadText = (& git rev-list --count "$($tracking[0])..HEAD" 2>$null)
    $aheadCount = 0
    if ($LASTEXITCODE -eq 0 -and [int]::TryParse("$aheadText".Trim(), [ref] $aheadCount) -and $aheadCount -gt 0) {
      return $tracking[0]
    }
  }

  # On a short-lived feature branch off local 'main', compare against 'main'.
  & git rev-parse --verify --quiet "main^{commit}" *> $null
  if ($LASTEXITCODE -eq 0) {
    & git merge-base --is-ancestor main HEAD *> $null
    if ($LASTEXITCODE -eq 0) {
      $aheadText = (& git rev-list --count main..HEAD 2>$null)
      $aheadCount = 0
      if ($LASTEXITCODE -eq 0 -and [int]::TryParse("$aheadText".Trim(), [ref] $aheadCount)) {
        if ($aheadCount -ge 1 -and $aheadCount -le 15) {
          return "main"
        }
      }
    }
  }

  foreach ($candidate in @("HEAD^", "main", "origin/main")) {
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
  param(
    [string] $Version,
    [string] $CacheFilePath = "",
    [bool] $ForceNetworkCheck = $false
  )

  if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    throw "CI Qt version must be exact so package availability can be checked: $Version"
  }

  if (-not $ForceNetworkCheck -and -not [string]::IsNullOrWhiteSpace($CacheFilePath) -and (Test-Path $CacheFilePath)) {
    try {
      $cacheItem = Get-Item $CacheFilePath -ErrorAction Stop
      $cachedVersion = (Get-Content -Raw -Path $CacheFilePath -ErrorAction Stop).Trim()
      if ($cachedVersion -eq $Version -and ((Get-Date) - $cacheItem.LastWriteTime).TotalHours -lt 24) {
        Write-Host "CI Qt packages: $Version metadata available (cached <24h)"
        return
      }
    } catch {
      # Ignore cache read issues and fall back to live network verification.
    }
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

  if (-not [string]::IsNullOrWhiteSpace($CacheFilePath)) {
    try {
      New-Item -ItemType Directory -Force -Path (Split-Path -Parent $CacheFilePath) | Out-Null
      Set-Content -Path $CacheFilePath -Value $Version -NoNewline
    } catch {
      # Non-fatal if cache cannot be written.
    }
  }
  Write-Host "CI Qt packages: $Version metadata available for Windows, Linux, and macOS"
}

function Assert-QtVersionCompatibleWithCi {
  param(
    [string] $RepositoryRoot,
    [string] $QtBinaryDirectory,
    [string] $BuildDirectory,
    [bool] $ForceNetworkCheck = $false
  )

  $actionPath = Join-Path $RepositoryRoot ".github\actions\setup-common\action.yml"
  $versionLine = Select-String -Path $actionPath -Pattern '^\s*version:\s*["'']?([^"''\s]+)'
  if (-not $versionLine) {
    throw "Could not read the CI Qt version from $actionPath"
  }
  $ciVersion = $versionLine.Matches[0].Groups[1].Value
  $cachePath = Join-Path $BuildDirectory "codex-logs\ci-qt-package-cache.txt"
  Assert-CiQtPackagesAvailable $ciVersion $cachePath $ForceNetworkCheck

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

function Get-FocusedTestFilterForDirectory {
  param(
    [string] $RepositoryRoot,
    [string] $TestDirectory,
    [string[]] $ChangedFiles
  )

  $relativeTestDir = (ConvertTo-RepoPath ([System.IO.Path]::GetRelativePath($RepositoryRoot, $TestDirectory)))
  if ($relativeTestDir -notmatch '^(lib/[^/]+)/') {
    return $null
  }
  $libraryPrefix = $Matches[1]
  $libraryChanges = @($ChangedFiles | Where-Object { $_ -like "$libraryPrefix/*" })
  if ($libraryChanges.Count -eq 0) {
    return $null
  }

  # If only CMake or non-C++ files changed in the library without any C/C++ source/header
  # or test changes, do not narrow to a subset.
  $codeChanges = @($libraryChanges | Where-Object { $_ -match '\.(c|cc|cpp|cxx|h|hpp|hxx|inl)$' })
  if ($codeChanges.Count -eq 0) {
    return $null
  }

  $affectedTestFiles = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
  $stems = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)

  foreach ($file in $codeChanges) {
    $fullPath = Join-Path $RepositoryRoot ($file.Replace('/', '\'))
    if ($file -like "$relativeTestDir/*" -and (Test-Path $fullPath)) {
      [void] $affectedTestFiles.Add((Resolve-Path $fullPath).Path)
    } else {
      $stem = [System.IO.Path]::GetFileNameWithoutExtension($file)
      if ($stem -match '^tst_(.+)$') {
        $stem = $Matches[1]
      }
      if (-not [string]::IsNullOrWhiteSpace($stem)) {
        [void] $stems.Add($stem)
      }
    }
  }

  $allTestSources = @(Get-ChildItem -Path $TestDirectory -Filter "*.cpp" -File -Recurse -ErrorAction SilentlyContinue)
  if ($stems.Count -gt 0 -and $allTestSources.Count -gt 0) {
    foreach ($testSource in $allTestSources) {
      $testBaseName = [System.IO.Path]::GetFileNameWithoutExtension($testSource.Name)
      if ($testBaseName -match '^tst_(.+)$' -and $stems.Contains($Matches[1])) {
        [void] $affectedTestFiles.Add($testSource.FullName)
      }
    }

    $stemPattern = '\b(' + (($stems | ForEach-Object { [regex]::Escape($_) }) -join '|') + ')\b'
    foreach ($matchInfo in (Select-String -Path $allTestSources.FullName -Pattern $stemPattern -ErrorAction SilentlyContinue)) {
      [void] $affectedTestFiles.Add($matchInfo.Path)
    }
  }

  if ($affectedTestFiles.Count -eq 0) {
    return $null
  }

  $testCases = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::Ordinal)
  foreach ($testFile in $affectedTestFiles) {
    $content = Get-Content -Raw -Path $testFile -ErrorAction SilentlyContinue
    if ([string]::IsNullOrWhiteSpace($content)) {
      continue
    }
    foreach ($m in [regex]::Matches($content, '(?m)^\s*TEST_CASE\(\s*"([^"]+)"')) {
      [void] $testCases.Add($m.Groups[1].Value)
    }
  }

  if ($testCases.Count -eq 0 -or $testCases.Count -gt 30) {
    return $null
  }

  $escapedCases = @($testCases | Sort-Object | ForEach-Object { [regex]::Escape($_) })
  return [PSCustomObject]@{
    Regex = '^(' + ($escapedCases -join '|') + ')(: .*)?$'
    CaseCount = $testCases.Count
    FileCount = $affectedTestFiles.Count
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
    [string] $ClangCompiler,
    [int] $MaxJobs = 4
  )

  $sourceFiles = @(
    $ChangedFiles |
      Where-Object { $_ -match '\.(c|cc|cpp|cxx)$' } |
      # This immutable upstream snapshot has its own build/tests and hash checks.
      # Keep TB's stricter warning policy on the adapter, not the imported sources
      # (which also include a standalone test program outside the TB build).
      Where-Object { $_ -notmatch '^lib/GoldSrcCollisionLib/thirdparty/hltools/' } |
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

  # Index compile commands in a single pass for O(1) lookup per changed source file.
  $commandBySource = [System.Collections.Generic.Dictionary[string, string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase)
  foreach ($line in $commands) {
    if ($line -match '(?:-c|/c)\s+"?([^"]+)"?\s*$') {
      $normalizedSource = $Matches[1].Trim().Replace('/', '\')
      $commandBySource[$normalizedSource] = $line
    }
  }

  $workItems = [System.Collections.Generic.List[PSCustomObject]]::new()
  foreach ($sourceFile in $sourceFiles) {
    $normalizedFile = $sourceFile.Replace('/', '\')
    $command = $null
    if (-not $commandBySource.TryGetValue($normalizedFile, [ref] $command)) {
      throw "No Ninja compile command found for $sourceFile. Build the affected target once and retry."
    }
    $strictCommand = ConvertTo-StrictClangCommand $command $ClangCompiler
    $workItems.Add([PSCustomObject]@{
      SourceFile = $sourceFile
      FileName = [System.IO.Path]::GetFileName($sourceFile)
      StrictCommand = $strictCommand
    })
  }

  $effectiveConcurrency = [Math]::Max(1, [Math]::Min($MaxJobs, $workItems.Count))
  Write-Host "Strict compile: $($workItems.Count) changed translation unit(s) (jobs=$effectiveConcurrency)"

  $running = [System.Collections.Generic.List[PSCustomObject]]::new()
  $nextIndex = 0
  try {
    while ($nextIndex -lt $workItems.Count -or $running.Count -gt 0) {
      while ($nextIndex -lt $workItems.Count -and $running.Count -lt $effectiveConcurrency) {
        $item = $workItems[$nextIndex]
        $nextIndex++

        Write-Host "  clang-cl $($item.FileName)"
        $psi = [System.Diagnostics.ProcessStartInfo]::new()
        $psi.FileName = "cmd.exe"
        $psi.Arguments = "/d /s /c `"$($item.StrictCommand) 2>&1`""
        $psi.WorkingDirectory = $ResolvedBuildDirectory.Path
        $psi.UseShellExecute = $false
        $psi.RedirectStandardOutput = $true
        $psi.RedirectStandardError = $false
        $psi.CreateNoWindow = $true

        $process = [System.Diagnostics.Process]::new()
        $process.StartInfo = $psi
        [void] $process.Start()
        $outputTask = $process.StandardOutput.ReadToEndAsync()

        $running.Add([PSCustomObject]@{
          Item = $item
          Process = $process
          OutputTask = $outputTask
        })
      }

      $completedIndex = -1
      for ($i = 0; $i -lt $running.Count; $i++) {
        if ($running[$i].Process.HasExited) {
          $completedIndex = $i
          break
        }
      }

      if ($completedIndex -lt 0) {
        [void] $running[0].Process.WaitForExit(100)
        continue
      }

      $completed = $running[$completedIndex]
      $running.RemoveAt($completedIndex)
      $completed.Process.WaitForExit()
      $output = $completed.OutputTask.GetAwaiter().GetResult()
      $exitCode = $completed.Process.ExitCode
      $completed.Process.Dispose()

      if (-not [string]::IsNullOrWhiteSpace($output)) {
        Write-Host $output.TrimEnd()
      }
      if ($exitCode -ne 0) {
        throw "Strict compile failed for $($completed.Item.SourceFile)"
      }
    }
  } finally {
    foreach ($active in $running) {
      try {
        if (-not $active.Process.HasExited) {
          $active.Process.Kill()
        }
      } catch {
      } finally {
        $active.Process.Dispose()
      }
    }
  }
}

function Invoke-FilteredBuild {
  param(
    [string] $RepositoryRoot,
    [string] $BuildDirectory,
    [string] $Target,
    [string] $QtBinaryDirectory,
    [int] $ParallelJobs
  )

  $buildScript = Join-Path $RepositoryRoot "scripts\build-filtered.ps1"
  $environmentArgs = @()
  if (-not [string]::IsNullOrWhiteSpace($env:VSCMD_VER)) {
    $environmentArgs += "-NoVsDevCmd"
  }
  & powershell.exe -ExecutionPolicy Bypass -File $buildScript `
    -BuildDir $BuildDirectory `
    -Target $Target `
    -QtBin $QtBinaryDirectory -Jobs $ParallelJobs @environmentArgs
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

  # Catch discovery also executes UI test binaries during the build. Apply the
  # window policy before any child process, regardless of the caller's environment.
  $env:TB_TEST_SHOW_WINDOWS = if ($ShowTestWindows) { "1" } else { "0" }
  # Native focus/mouse-capture tests cannot safely share the desktop.
  $effectiveTestJobs = if ($ShowTestWindows) { 1 } else { $TestJobs }

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
  Write-Host "Concurrency: build=$BuildJobs, tests=$effectiveTestJobs"

  Invoke-GitCheck @("diff", "--check", "$effectiveBaseRef...HEAD", "--")
  Invoke-GitCheck @("diff", "--cached", "--check", "--")
  Invoke-GitCheck @("diff", "--check", "--")

  & (Join-Path $PSScriptRoot "check-ui-style-governance.ps1")
  if ($LASTEXITCODE -ne 0) {
    throw "UI style governance check failed"
  }

  $changedFiles = @(Get-ChangedFiles $effectiveBaseRef $Paths)
  Write-Host "Changed files considered: $($changedFiles.Count)"
  if ($Full -or @($changedFiles | Where-Object {
        $_ -match '^lib/GoldSrcCollisionLib/' -or $_ -eq 'scripts/sync-hltools-collision.py'
      }).Count -gt 0) {
    & python (Join-Path $PSScriptRoot 'sync-hltools-collision.py') --check
    if ($LASTEXITCODE -ne 0) {
      throw 'Bundled hltools source verification failed'
    }
  }
  if ($changedFiles.Count -eq 0 -and -not $Full) {
    Write-Host "Nothing to check."
    exit 0
  }

  if (-not $SkipQtVersionCheck) {
    $forceQtNetworkCheck = [bool] $Full -or ($changedFiles -contains ".github/actions/setup-common/action.yml")
    Assert-QtVersionCompatibleWithCi $repositoryRoot $QtBin $resolvedBuildDir.Path $forceQtNetworkCheck
  }

  $plan = Get-AffectedBuildPlan $repositoryRoot $changedFiles $Full
  if ($plan.BuildTargets.Count -eq 0) {
    Write-Host "Build: no affected CMake targets"
  } elseif ($SkipBuild) {
    Write-Host "Build skipped: $($plan.BuildTargets -join ', ')"
  } else {
    foreach ($target in $plan.BuildTargets) {
      Invoke-FilteredBuild $repositoryRoot $resolvedBuildDir.Path $target $QtBin $BuildJobs
    }
  }

  if (-not $SkipStrictCompile) {
    $clangCompiler = if ([string]::IsNullOrWhiteSpace($ClangCl)) {
      (Get-Command clang-cl.exe -ErrorAction Stop).Source
    } else {
      (Resolve-Path $ClangCl -ErrorAction Stop).Path
    }
    Invoke-StrictCompile $resolvedBuildDir $repositoryRoot $changedFiles $clangCompiler $BuildJobs
  }

  if ($SkipTests) {
    Write-Host "Tests skipped"
  } else {
    $env:PATH = "$QtBin;$originalPath"
    Write-Host $(if ($ShowTestWindows) {
      "UI tests: native windows enabled (-ShowTestWindows)"
    } else {
      "UI tests: background window policy; use -ShowTestWindows for desktop validation"
    })
    foreach ($testDirectory in $plan.TestDirectories) {
      $relativeTestDirectory = [System.IO.Path]::GetRelativePath(
        $repositoryRoot, $testDirectory)
      $buildTestDirectory = Join-Path $resolvedBuildDir.Path $relativeTestDirectory

      $ctestArgs = @(
        "--test-dir", $buildTestDirectory,
        "--parallel", $effectiveTestJobs,
        "--output-on-failure"
      )

      if (-not [string]::IsNullOrWhiteSpace($TestFilter)) {
        Write-Host "==> Test $relativeTestDirectory (filter: $TestFilter)"
        $ctestArgs += @("-R", $TestFilter)
      } elseif (-not $Full -and -not $AllTests) {
        $focusedFilter = Get-FocusedTestFilterForDirectory $repositoryRoot $testDirectory $changedFiles
        if ($null -ne $focusedFilter) {
          Write-Host "==> Test $relativeTestDirectory ($($focusedFilter.CaseCount) focused case(s) from $($focusedFilter.FileCount) test file(s); pass -AllTests for full suite)"
          $ctestArgs += @("-R", $focusedFilter.Regex)
        } else {
          Write-Host "==> Test $relativeTestDirectory (full suite)"
        }
      } else {
        Write-Host "==> Test $relativeTestDirectory (full suite)"
      }

      & ctest @ctestArgs
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
