param(
  [string] $BuildDir = "build-release-codex",
  [string] $QtBin = "D:\Qtx\6.11.1\msvc2022_64\bin",
  [string[]] $Targets = @(
    "welcome",
    "workbench",
    "outliner",
    "entity-browser",
    "entity-browser-empty",
    "face-inspector",
    "material-browser-empty",
    "plugin-inspector",
    "supporting",
    "python-console",
    "command-palette",
    "components",
    "preferences",
    "preferences-colors",
    "preferences-mouse",
    "preferences-keyboard",
    "preferences-misc",
    "preferences-kreedzbroom"
  ),
  [string[]] $Themes = @("light", "dark", "blender"),
  [string[]] $ScaleFactors = @("1", "1.5", "2"),
  [string] $MapPath = "lib\TbMdlLib\test\fixture\mdl\Map\initialMap.map",
  [string] $OutlinerMapPath =
    "lib\TbUiLib\test\fixture\ui\UiSnapshot\outlinerHierarchy.map",
  [string] $MaterialMapPath =
    "lib\TbMdlLib\test\fixture\mdl\Map\reloadMaterialCollectionsQ2.map",
  [string] $MaterialGamePath = "lib\TbUiLib\test\fixture\mdl\Game\Quake2",
  [string] $OutputDir = "",
  [string] $BaselineDir = "",
  [int] $PixelDifferenceThreshold = 24,
  [double] $MaxChangedPixelRatio = 0.002,
  [int] $TimeoutSeconds = 45,
  [switch] $SkipBuild
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

function Assert-UiSnapshotMetadata {
  param(
    [pscustomobject] $Metadata,
    [string] $ExpectedTarget,
    [string] $ExpectedTheme,
    [string] $ExpectedScale,
    [string] $ImagePath
  )

  if ($Metadata.status -ne "ok") {
    throw "Snapshot manifest did not report success: $ImagePath"
  }
  if ($Metadata.target -ne $ExpectedTarget) {
    throw "Expected target '$ExpectedTarget', got '$($Metadata.target)': $ImagePath"
  }
  if ($Metadata.theme -ne $ExpectedTheme) {
    throw "Expected theme '$ExpectedTheme', got '$($Metadata.theme)': $ImagePath"
  }
  if ($Metadata.scaleFactor -ne $ExpectedScale) {
    throw "Expected scale '$ExpectedScale', got '$($Metadata.scaleFactor)': $ImagePath"
  }
  if ($Metadata.sampledColorCount -lt 2 -or $Metadata.luminanceRange -lt 8) {
    throw "Snapshot appears visually blank: $ImagePath"
  }
  if (-not $Metadata.fontSupportsBasicLatin -or [string]::IsNullOrWhiteSpace($Metadata.fontFamily)) {
    throw "Snapshot font cannot render required acceptance text: $ImagePath"
  }
  if ($Metadata.sha256 -notmatch '^[0-9a-f]{64}$') {
    throw "Snapshot manifest has an invalid SHA-256: $ImagePath"
  }
  $actualSha256 =
    (Get-FileHash -LiteralPath $ImagePath -Algorithm SHA256).Hash.ToLowerInvariant()
  if ($Metadata.sha256 -ne $actualSha256) {
    throw "Snapshot image does not match its manifest SHA-256: $ImagePath"
  }

  $scale = [double]::Parse($ExpectedScale, [Globalization.CultureInfo]::InvariantCulture)
  $expectedWidth = [math]::Round([double] $Metadata.logicalSize.width * $scale)
  $expectedHeight = [math]::Round([double] $Metadata.logicalSize.height * $scale)
  if ([math]::Abs([double] $Metadata.pixelSize.width - $expectedWidth) -gt 2) {
    throw "Snapshot width does not reflect scale factor ${ExpectedScale}: $ImagePath"
  }
  if ([math]::Abs([double] $Metadata.pixelSize.height - $expectedHeight) -gt 2) {
    throw "Snapshot height does not reflect scale factor ${ExpectedScale}: $ImagePath"
  }
  if ([math]::Abs([double] $Metadata.devicePixelRatio - $scale) -gt 0.05) {
    throw "Snapshot device pixel ratio does not reflect scale factor ${ExpectedScale}: $ImagePath"
  }
}

function New-UiSnapshotContactSheet {
  param(
    [object[]] $Runs,
    [int] $ColumnCount,
    [string] $Path
  )

  $padding = 12
  $labelHeight = 28
  $cellWidth = 700
  $cellHeight = 500
  $rowCount = [math]::Ceiling($Runs.Count / $ColumnCount)
  $canvasWidth = $padding + $ColumnCount * ($cellWidth + $padding)
  $canvasHeight = $padding + $rowCount * ($labelHeight + $cellHeight + $padding)
  $bitmap = [Drawing.Bitmap]::new(
    $canvasWidth,
    $canvasHeight,
    [Drawing.Imaging.PixelFormat]::Format24bppRgb)
  $graphics = [Drawing.Graphics]::FromImage($bitmap)
  $font = [Drawing.Font]::new("Segoe UI", 11, [Drawing.FontStyle]::Bold)
  $labelBrush = [Drawing.SolidBrush]::new([Drawing.Color]::White)

  try {
    $graphics.Clear([Drawing.Color]::FromArgb(30, 30, 30))
    $graphics.InterpolationMode =
      [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality

    for ($index = 0; $index -lt $Runs.Count; ++$index) {
      $run = $Runs[$index]
      $column = $index % $ColumnCount
      $row = [math]::Floor($index / $ColumnCount)
      $x = $padding + $column * ($cellWidth + $padding)
      $y = $padding + $row * ($labelHeight + $cellHeight + $padding)
      $label = "$($run.Target)  $($run.Theme)  $($run.Scale)x  $($run.PixelSize)"
      $graphics.DrawString($label, $font, $labelBrush, $x, $y)

      $source = [Drawing.Image]::FromFile($run.Image)
      try {
        $imageScale = [math]::Min(
          [double] $cellWidth / $source.Width,
          [double] $cellHeight / $source.Height)
        $imageWidth = [math]::Round($source.Width * $imageScale)
        $imageHeight = [math]::Round($source.Height * $imageScale)
        $imageX = $x + [math]::Floor(($cellWidth - $imageWidth) / 2)
        $imageY = $y + $labelHeight + [math]::Floor(($cellHeight - $imageHeight) / 2)
        $destination = [Drawing.Rectangle]::new(
          $imageX, $imageY, $imageWidth, $imageHeight)
        $graphics.DrawImage($source, $destination)
      } finally {
        $source.Dispose()
      }
    }

    $bitmap.Save($Path, [Drawing.Imaging.ImageFormat]::Png)
  } finally {
    $labelBrush.Dispose()
    $font.Dispose()
    $graphics.Dispose()
    $bitmap.Dispose()
  }
}

function Compare-UiSnapshot {
  param(
    [string] $ActualPath,
    [string] $BaselinePath,
    [int] $DifferenceThreshold,
    [double] $AllowedChangedPixelRatio
  )

  $actual = [Drawing.Bitmap]::new($ActualPath)
  $baseline = [Drawing.Bitmap]::new($BaselinePath)
  try {
    if ($actual.Width -ne $baseline.Width -or $actual.Height -ne $baseline.Height) {
      throw "UI snapshot size differs from baseline: $ActualPath is $($actual.Width)x$($actual.Height), baseline is $($baseline.Width)x$($baseline.Height)"
    }

    $sampleWidth = [math]::Min(320, $actual.Width)
    $sampleHeight = [math]::Max(
      1,
      [math]::Round([double] $actual.Height * $sampleWidth / $actual.Width))
    $actualSample = [Drawing.Bitmap]::new($sampleWidth, $sampleHeight)
    $baselineSample = [Drawing.Bitmap]::new($sampleWidth, $sampleHeight)
    try {
      foreach ($pair in @(
          @{ Source = $actual; Destination = $actualSample },
          @{ Source = $baseline; Destination = $baselineSample }
        )) {
        $graphics = [Drawing.Graphics]::FromImage($pair.Destination)
        try {
          $graphics.InterpolationMode =
            [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
          $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
          $graphics.DrawImage($pair.Source, 0, 0, $sampleWidth, $sampleHeight)
        } finally {
          $graphics.Dispose()
        }
      }

      $changedPixels = 0
      for ($y = 0; $y -lt $sampleHeight; ++$y) {
        for ($x = 0; $x -lt $sampleWidth; ++$x) {
          $actualPixel = $actualSample.GetPixel($x, $y)
          $baselinePixel = $baselineSample.GetPixel($x, $y)
          $maxDifference = [math]::Max(
            [math]::Abs([int] $actualPixel.R - [int] $baselinePixel.R),
            [math]::Max(
              [math]::Abs([int] $actualPixel.G - [int] $baselinePixel.G),
              [math]::Abs([int] $actualPixel.B - [int] $baselinePixel.B)))
          if ($maxDifference -gt $DifferenceThreshold) {
            ++$changedPixels
          }
        }
      }

      $sampledPixels = $sampleWidth * $sampleHeight
      $changedPixelRatio = [double] $changedPixels / $sampledPixels
      if ($changedPixelRatio -gt $AllowedChangedPixelRatio) {
        throw "UI snapshot differs from baseline by $([math]::Round($changedPixelRatio * 100, 3))% (allowed $([math]::Round($AllowedChangedPixelRatio * 100, 3))%): $ActualPath"
      }

      return [pscustomobject]@{
        ChangedPixels = $changedPixels
        SampledPixels = $sampledPixels
        ChangedPixelRatio = $changedPixelRatio
      }
    } finally {
      $actualSample.Dispose()
      $baselineSample.Dispose()
    }
  } finally {
    $actual.Dispose()
    $baseline.Dispose()
  }
}

$normalizedTargets = @($Targets | ForEach-Object { $_.ToLowerInvariant() })
foreach ($target in $normalizedTargets) {
  if ($target -notin @(
      "welcome",
      "workbench",
      "outliner",
      "outliner-hierarchy",
      "outliner-filter",
      "outliner-properties-entity",
      "outliner-reparent-layer-before",
      "outliner-reparent-layer-after",
      "outliner-brush-entity-before",
      "outliner-brush-entity-after",
      "entity-browser",
      "entity-browser-empty",
      "face-inspector",
      "material-browser-empty",
      "plugin-inspector",
      "supporting",
      "path-tool-preview",
      "python-console",
      "command-palette",
      "components",
      "preferences",
      "preferences-colors",
      "preferences-mouse",
      "preferences-keyboard",
      "preferences-misc",
      "preferences-kreedzbroom"
    )) {
    throw "Unsupported target '$target'. Expected welcome, workbench, outliner, outliner-hierarchy, outliner-filter, outliner-properties-entity, outliner-reparent-layer-before, outliner-reparent-layer-after, outliner-brush-entity-before, outliner-brush-entity-after, entity-browser, entity-browser-empty, face-inspector, material-browser-empty, plugin-inspector, supporting, path-tool-preview, python-console, command-palette, components, preferences, preferences-colors, preferences-mouse, preferences-keyboard, preferences-misc, or preferences-kreedzbroom."
  }
}
foreach ($theme in $Themes) {
  if ($theme -notin @("light", "dark", "blender")) {
    throw "Unsupported theme '$theme'. Expected light, dark, or blender."
  }
}
foreach ($scaleFactor in $ScaleFactors) {
  $parsedScale = 0.0
  if (
    -not [double]::TryParse(
      $scaleFactor,
      [Globalization.NumberStyles]::Float,
      [Globalization.CultureInfo]::InvariantCulture,
      [ref] $parsedScale) -or
    $parsedScale -le 0.0
  ) {
    throw "Invalid scale factor '$scaleFactor'."
  }
}
if ($PixelDifferenceThreshold -lt 0 -or $PixelDifferenceThreshold -gt 255) {
  throw "PixelDifferenceThreshold must be between 0 and 255."
}
if ($MaxChangedPixelRatio -lt 0.0 -or $MaxChangedPixelRatio -gt 1.0) {
  throw "MaxChangedPixelRatio must be between 0 and 1."
}

if (-not $SkipBuild) {
  & (Join-Path $PSScriptRoot "build-filtered.ps1") `
    -BuildDir $BuildDir `
    -Target TrenchBroom `
    -QtBin $QtBin
}

$resolvedBuildDir = Resolve-Path -Path $BuildDir
$resolvedQtBin = Resolve-Path -Path $QtBin
$usesMaterialFixture =
  $normalizedTargets -contains "face-inspector" -or
  $normalizedTargets -contains "material-browser-empty"
$resolvedMaterialMapPath =
  if ($usesMaterialFixture) { Resolve-Path -Path $MaterialMapPath } else { $null }
$resolvedMaterialGamePath =
  if ($usesMaterialFixture) { Resolve-Path -Path $MaterialGamePath } else { $null }
$usesOutlinerFixture =
  @($normalizedTargets | Where-Object { $_.StartsWith("outliner") }).Count -gt 0
$resolvedOutlinerMapPath =
  if ($usesOutlinerFixture) { Resolve-Path -Path $OutlinerMapPath } else { $null }
$resolvedMapPath = if (
  $normalizedTargets -contains "workbench" -or
  $normalizedTargets -contains "entity-browser" -or
  $normalizedTargets -contains "entity-browser-empty" -or
  $normalizedTargets -contains "plugin-inspector" -or
  $normalizedTargets -contains "supporting" -or
  $normalizedTargets -contains "path-tool-preview" -or
  $normalizedTargets -contains "python-console" -or
  $normalizedTargets -contains "command-palette") {
  Resolve-Path -Path $MapPath
} else {
  $null
}
$executable = Join-Path $resolvedBuildDir.Path "app\TrenchBroom\TrenchBroom.exe"
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
  throw "TrenchBroom executable not found: $executable"
}

$platformPluginDir = Join-Path (Split-Path -Parent $executable) "platforms"
$windowsPlugin = Join-Path $platformPluginDir "qwindows.dll"
if (-not (Test-Path -LiteralPath $windowsPlugin -PathType Leaf)) {
  throw "Deployed Qt Windows platform plugin not found: $windowsPlugin"
}

$acceptanceRoot = if ([string]::IsNullOrWhiteSpace($OutputDir)) {
  Join-Path $resolvedBuildDir.Path "codex-logs\ui-theme-acceptance"
} else {
  [IO.Path]::GetFullPath($OutputDir)
}
$resolvedBaselineDir = if ([string]::IsNullOrWhiteSpace($BaselineDir)) {
  $null
} else {
  Resolve-Path -LiteralPath $BaselineDir
}
$runDirectory = Join-Path $acceptanceRoot (Get-Date -Format "yyyyMMdd-HHmmss-fff")
New-Item -ItemType Directory -Force -Path $runDirectory | Out-Null

$results = @()
foreach ($target in $normalizedTargets) {
  foreach ($theme in $Themes) {
    foreach ($scaleFactor in $ScaleFactors) {
      $scaleName = $scaleFactor.Replace(".", "_")
      $baseName = "$target-$theme-$scaleName"
      $imagePath = Join-Path $runDirectory "$baseName.png"
      $manifestPath = Join-Path $runDirectory "$baseName.json"
      $errorPath = Join-Path $runDirectory "$baseName.error.txt"

      $processInfo = [Diagnostics.ProcessStartInfo]::new()
      $processInfo.FileName = $executable
      $processInfo.Arguments =
        "--ui-snapshot `"$imagePath`" --ui-snapshot-theme $theme"
      if ($target -ne "welcome" -and $target -ne "workbench") {
        $processInfo.Arguments += " --ui-snapshot-page $target"
      }
      $useMaterialFixture =
        $target -eq "face-inspector" -or $target -eq "material-browser-empty"
      $useOutlinerFixture = $target.StartsWith("outliner")
      $targetMapPath =
        if ($useMaterialFixture) {
          $resolvedMaterialMapPath
        } elseif ($useOutlinerFixture) {
          $resolvedOutlinerMapPath
        } else {
          $resolvedMapPath
        }
      if ($useMaterialFixture) {
        $processInfo.Arguments +=
          " --ui-snapshot-game-path `"$($resolvedMaterialGamePath.Path)`""
      }
      if (
        $target -eq "workbench" -or
        $target.StartsWith("outliner") -or
        $target -eq "entity-browser" -or
        $target -eq "entity-browser-empty" -or
        $target -eq "face-inspector" -or
        $target -eq "material-browser-empty" -or
        $target -eq "plugin-inspector" -or
        $target -eq "supporting" -or
        $target -eq "path-tool-preview" -or
        $target -eq "python-console" -or
        $target -eq "command-palette") {
        $processInfo.Arguments += " `"$($targetMapPath.Path)`""
      }
      $processInfo.WorkingDirectory = Split-Path -Parent $executable
      $processInfo.UseShellExecute = $false
      $processInfo.CreateNoWindow = $true
      $processInfo.RedirectStandardOutput = $true
      $processInfo.RedirectStandardError = $true
      $processInfo.EnvironmentVariables["PATH"] =
        "$($resolvedQtBin.Path);$($processInfo.EnvironmentVariables['PATH'])"
      $processInfo.EnvironmentVariables["QT_QPA_PLATFORM"] = "windows"
      $processInfo.EnvironmentVariables["QT_QPA_PLATFORM_PLUGIN_PATH"] = $platformPluginDir
      $processInfo.EnvironmentVariables["QT_AUTO_SCREEN_SCALE_FACTOR"] = "0"
      $processInfo.EnvironmentVariables["QT_SCALE_FACTOR"] = $scaleFactor

      Write-Host "==> Capture $target theme=$theme scale=$scaleFactor"
      $process = [Diagnostics.Process]::new()
      $process.StartInfo = $processInfo
      [void] $process.Start()
      $stdoutTask = $process.StandardOutput.ReadToEndAsync()
      $stderrTask = $process.StandardError.ReadToEndAsync()
      if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill()
        $process.WaitForExit()
        $stdout = $stdoutTask.GetAwaiter().GetResult()
        $stderr = $stderrTask.GetAwaiter().GetResult()
        throw @"
UI snapshot timed out after $TimeoutSeconds seconds: $imagePath
stdout:
$stdout
stderr:
$stderr
"@
      }

      $stdout = $stdoutTask.GetAwaiter().GetResult()
      $stderr = $stderrTask.GetAwaiter().GetResult()
      if ($process.ExitCode -ne 0) {
        $snapshotError = if (Test-Path -LiteralPath $errorPath -PathType Leaf) {
          Get-Content -LiteralPath $errorPath -Raw
        } else {
          "No snapshot diagnostic file was created."
        }
        throw @"
UI snapshot failed with exit code $($process.ExitCode): $imagePath
snapshot diagnostic:
$snapshotError
stdout:
$stdout
stderr:
$stderr
"@
      }
      if (-not (Test-Path -LiteralPath $imagePath -PathType Leaf)) {
        throw "UI snapshot image was not created: $imagePath"
      }
      if ((Get-Item -LiteralPath $imagePath).Length -lt 1024) {
        throw "UI snapshot image is unexpectedly small: $imagePath"
      }
      if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "UI snapshot manifest was not created: $manifestPath"
      }

      $metadata = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
      Assert-UiSnapshotMetadata `
        -Metadata $metadata `
        -ExpectedTarget $target `
        -ExpectedTheme $theme `
        -ExpectedScale $scaleFactor `
        -ImagePath $imagePath

      $baselinePath = $null
      $comparison = $null
      if ($null -ne $resolvedBaselineDir) {
        $baselinePath = Join-Path $resolvedBaselineDir.Path "$baseName.png"
        if (-not (Test-Path -LiteralPath $baselinePath -PathType Leaf)) {
          throw "UI snapshot baseline was not found: $baselinePath"
        }
        $comparison = Compare-UiSnapshot `
          -ActualPath $imagePath `
          -BaselinePath $baselinePath `
          -DifferenceThreshold $PixelDifferenceThreshold `
          -AllowedChangedPixelRatio $MaxChangedPixelRatio
      }

      $results += [pscustomobject]@{
        Target = $target
        Theme = $theme
        Scale = $scaleFactor
        LogicalSize = "$($metadata.logicalSize.width)x$($metadata.logicalSize.height)"
        PixelSize = "$($metadata.pixelSize.width)x$($metadata.pixelSize.height)"
        Colors = $metadata.sampledColorCount
        LuminanceRange = $metadata.luminanceRange
        MapPath = if ($null -eq $targetMapPath) { $null } else { $targetMapPath.Path }
        GamePath =
          if ($useMaterialFixture) { $resolvedMaterialGamePath.Path } else { $null }
        Image = $imagePath
        Manifest = $manifestPath
        Sha256 = $metadata.sha256
        Baseline = $baselinePath
        ChangedPixelRatio =
          if ($null -eq $comparison) { $null } else { $comparison.ChangedPixelRatio }
      }
    }
  }
}

$contactSheetPath = Join-Path $runDirectory "contact-sheet.png"
New-UiSnapshotContactSheet `
  -Runs $results `
  -ColumnCount $ScaleFactors.Count `
  -Path $contactSheetPath

$reportPath = Join-Path $runDirectory "report.json"
$reportMapPath = if ($null -eq $resolvedMapPath) { $null } else { $resolvedMapPath.Path }
$reportMaterialMapPath =
  if ($null -eq $resolvedMaterialMapPath) { $null } else { $resolvedMaterialMapPath.Path }
$reportOutlinerMapPath =
  if ($null -eq $resolvedOutlinerMapPath) { $null } else { $resolvedOutlinerMapPath.Path }
$reportMaterialGamePath =
  if ($null -eq $resolvedMaterialGamePath) { $null } else { $resolvedMaterialGamePath.Path }
@{
  status = "ok"
  executable = $executable
  qtBin = $resolvedQtBin.Path
  mapPath = $reportMapPath
  outlinerMapPath = $reportOutlinerMapPath
  materialMapPath = $reportMaterialMapPath
  materialGamePath = $reportMaterialGamePath
  contactSheet = $contactSheetPath
  baselineDirectory =
    if ($null -eq $resolvedBaselineDir) { $null } else { $resolvedBaselineDir.Path }
  pixelDifferenceThreshold = $PixelDifferenceThreshold
  maxChangedPixelRatio = $MaxChangedPixelRatio
  runs = $results
} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8

Write-Host ""
$results | Format-Table Target, Theme, Scale, LogicalSize, PixelSize, Colors, LuminanceRange
Write-Host "Contact sheet: $contactSheetPath"
Write-Host "UI theme acceptance passed: $reportPath"
