param(
  [string]$BuildDir = "build-release-codex",
  [switch]$InstallStubgen,
  [switch]$Check
)

$ErrorActionPreference = "Stop"
$python = "C:\Users\Trh\AppData\Local\Programs\Python\Python312\python.exe"
$pythonDirectory = Split-Path -Parent $python
$qtBin = "D:\Qtx\6.11.1\msvc2022_64\bin"
$tool = Join-Path $BuildDir "app\DumpPythonApi\DumpPythonApi.exe"
$output = Join-Path $BuildDir "python-api"
$repositoryStubs = "python\stubs\trenchbroom"

if (!(Test-Path -LiteralPath $python)) {
  throw "Python runtime not found: $python"
}
if (!(Test-Path -LiteralPath $qtBin)) {
  throw "Qt runtime not found: $qtBin"
}
$env:PATH = "$qtBin;$pythonDirectory;$env:PATH"

if ($InstallStubgen) {
  & $python -m pip install --disable-pip-version-check -r python\requirements-python-api-stubs.txt
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
if (!(Test-Path -LiteralPath $tool)) {
  throw "Build DumpPythonApi before generating stubs: $tool"
}
& $tool --output $output --stubs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$generatedStubs = (Resolve-Path -LiteralPath (Join-Path $output "stubs\trenchbroom")).Path
if ($Check) {
  $generatedFiles = Get-ChildItem -LiteralPath $generatedStubs -Recurse -File | ForEach-Object {
    $_.FullName.Substring($generatedStubs.Length).TrimStart('\')
  } | Sort-Object
  $repositoryFiles = if (Test-Path -LiteralPath $repositoryStubs) {
    $repositoryStubs = (Resolve-Path -LiteralPath $repositoryStubs).Path
    Get-ChildItem -LiteralPath $repositoryStubs -Recurse -File | ForEach-Object {
      $_.FullName.Substring($repositoryStubs.Length).TrimStart('\')
    } | Sort-Object
  } else {
    @()
  }
  $differentFiles = Compare-Object $repositoryFiles $generatedFiles
  $contentDiffers = $false
  if ($null -eq $differentFiles) {
    $contentDiffers = $generatedFiles | Where-Object {
      (Get-FileHash -Algorithm SHA256 (Join-Path $generatedStubs $_)).Hash -ne
        (Get-FileHash -Algorithm SHA256 (Join-Path $repositoryStubs $_)).Hash
    }
  }
  if ($null -ne $differentFiles -or $contentDiffers) {
    throw "Generated Python stubs differ from python/stubs/trenchbroom. Run this script without -Check."
  }
  exit 0
}
if (Test-Path -LiteralPath $repositoryStubs) {
  Remove-Item -LiteralPath $repositoryStubs -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $repositoryStubs | Out-Null
Copy-Item -Path (Join-Path $generatedStubs "*") -Destination $repositoryStubs -Recurse -Force
