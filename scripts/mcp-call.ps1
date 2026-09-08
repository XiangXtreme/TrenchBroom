param(
  [string] $Url = "",
  [string] $Tool = "",
  [string] $ArgumentsJson = "{}",
  [string] $ArgumentsPath = "",
  [string] $Method = "",
  [string] $ParamsJson = "{}",
  [string] $ParamsPath = "",
  [string] $ResourceUri = "",
  [int] $TimeoutSec = 10,
  [switch] $Initialize,
  [switch] $ListTools,
  [switch] $NoHandshake,
  [switch] $Launch,
  [string] $TrenchBroomExe = "",
  [string] $MapPath = "",
  [switch] $KeepOpen,
  [switch] $RawResponse,
  [switch] $RawStructured,
  [switch] $Quiet
)

$ErrorActionPreference = "Stop"

function ConvertTo-CompactJson {
  param([object] $Value)

  return $Value | ConvertTo-Json -Depth 100 -Compress
}

function ConvertTo-PrettyJson {
  param([object] $Value)

  return $Value | ConvertTo-Json -Depth 100
}

function Write-Status {
  param(
    [string] $Message,
    [ConsoleColor] $ForegroundColor = [ConsoleColor]::Gray
  )

  if (-not $Quiet -and -not $RawResponse -and -not $RawStructured) {
    Write-Host $Message -ForegroundColor $ForegroundColor
  }
}

function Get-PropertyValue {
  param(
    [object] $Object,
    [string] $Name
  )

  if ($null -eq $Object) {
    return $null
  }

  $property = $Object.PSObject.Properties[$Name]
  if ($null -eq $property) {
    return $null
  }

  return $property.Value
}

function Read-JsonValue {
  param(
    [string] $Json,
    [string] $Path,
    [object] $DefaultValue
  )

  if (-not [string]::IsNullOrWhiteSpace($Path)) {
    $Json = Get-Content -Path $Path -Raw
  }

  if ([string]::IsNullOrWhiteSpace($Json)) {
    return $DefaultValue
  }

  return $Json | ConvertFrom-Json
}

function Get-McpUrlPort {
  param([string] $Url)

  try {
    $uri = [Uri] $Url
    return $uri.Port
  } catch {
    return $null
  }
}

function Get-McpPortOwners {
  param([int] $Port)

  if ($Port -le 0) {
    return @()
  }

  try {
    return @(Get-NetTCPConnection -LocalPort $Port -ErrorAction Stop |
      Select-Object -ExpandProperty OwningProcess -Unique)
  } catch {
    return @()
  }
}

function Format-McpPortOwners {
  param([int[]] $Owners)

  $realOwners = @($Owners | Where-Object { $_ -ne 0 })
  $labels = @()
  if ($realOwners.Count -gt 0) {
    $labels += ($realOwners -join ', ')
  }
  if ($Owners -contains 0) {
    $labels += "0 (system reservation)"
  }
  return $labels -join ', '
}

function Test-McpPortOwnerMatch {
  param(
    [int[]] $Owners,
    [int] $ProcessId
  )

  return @($Owners | Where-Object { $_ -ne 0 }) -contains $ProcessId
}

function Invoke-McpRequest {
  param(
    [string] $Url,
    [string] $Method,
    [object] $Params,
    [int] $TimeoutSec
  )

  $script:NextRequestId++
  $request = [ordered] @{
    jsonrpc = "2.0"
    id = $script:NextRequestId
    method = $Method
  }

  if ($null -ne $Params) {
    $request.params = $Params
  }

  $headers = @{
    Accept = "application/json, text/event-stream"
    "MCP-Protocol-Version" = "2025-06-18"
  }

  $response = Invoke-RestMethod `
    -Method Post `
    -Uri $Url `
    -Headers $headers `
    -ContentType "application/json" `
    -Body (ConvertTo-CompactJson $request) `
    -TimeoutSec $TimeoutSec

  $error = Get-PropertyValue $response "error"
  if ($null -ne $error) {
    throw "$Method failed: $($error.message)"
  }

  return $response
}

$configPath = Join-Path $env:APPDATA "TrenchBroom\MCP\config.json"
if (Test-Path $configPath) {
  $config = Get-Content -Path $configPath -Raw | ConvertFrom-Json
  if ([string]::IsNullOrWhiteSpace($Url)) {
    $hostName = if ([string]::IsNullOrWhiteSpace($config.httpHost)) { "127.0.0.1" } else { $config.httpHost }
    $port = if ($null -eq $config.httpPort) { 37666 } else { $config.httpPort }
    $Url = "http://$hostName`:$port/mcp"
  }
} elseif ([string]::IsNullOrWhiteSpace($Url)) {
  $Url = "http://127.0.0.1:37666/mcp"
}

if ([string]::IsNullOrWhiteSpace($TrenchBroomExe)) {
  $TrenchBroomExe = Join-Path "build-release-codex" "app\TrenchBroom\TrenchBroom.exe"
}
if ([string]::IsNullOrWhiteSpace($MapPath)) {
  $MapPath = Join-Path "build-release-codex" "app\TrenchBroom\map_test\unnamed.map"
}

$launchedProcess = $null
try {
  if ($Launch) {
    $resolvedExe = (Resolve-Path $TrenchBroomExe).Path
    $resolvedMap = (Resolve-Path $MapPath).Path
    $launchedProcess = Start-Process `
      -FilePath $resolvedExe `
      -ArgumentList $resolvedMap `
      -WorkingDirectory (Split-Path $resolvedExe) `
      -WindowStyle Hidden `
      -PassThru
    Write-Status "Started TrenchBroom PID $($launchedProcess.Id)" Cyan
    Start-Sleep -Seconds 3
  }

  Write-Status "MCP HTTP URL: $Url" Cyan
  $mcpPort = Get-McpUrlPort -Url $Url
  $mcpPortOwners = Get-McpPortOwners -Port $mcpPort
  if ($mcpPortOwners.Count -gt 0) {
    Write-Status "MCP port $mcpPort owner PID(s): $(Format-McpPortOwners $mcpPortOwners)" Cyan
  }
  $script:NextRequestId = 0

  if ($Initialize) {
    $requestMethod = "initialize"
    $requestParams = [ordered] @{
      protocolVersion = "2025-06-18"
      capabilities = @{}
      clientInfo = [ordered] @{ name = "trenchbroom-mcp-call"; version = "0.1.0" }
    }
  } elseif ($ListTools) {
    $requestMethod = "tools/list"
    $requestParams = @{}
  } elseif (-not [string]::IsNullOrWhiteSpace($ResourceUri)) {
    $requestMethod = "resources/read"
    $requestParams = [ordered] @{ uri = $ResourceUri }
  } elseif (-not [string]::IsNullOrWhiteSpace($Method)) {
    $requestMethod = $Method
    $requestParams = Read-JsonValue -Json $ParamsJson -Path $ParamsPath -DefaultValue @{}
  } else {
    if ([string]::IsNullOrWhiteSpace($Tool)) {
      $Tool = "tb_inspect"
      $ArgumentsJson = '{"view":"status"}'
    }
    $requestMethod = "tools/call"
    $arguments = Read-JsonValue -Json $ArgumentsJson -Path $ArgumentsPath -DefaultValue @{}
    $requestParams = [ordered] @{ name = $Tool; arguments = $arguments }
  }

  if (-not $NoHandshake -and $requestMethod -ne "initialize") {
    Invoke-McpRequest `
      -Url $Url `
      -Method "initialize" `
      -Params ([ordered] @{
        protocolVersion = "2025-06-18"
        capabilities = @{}
        clientInfo = [ordered] @{ name = "trenchbroom-mcp-call"; version = "0.1.0" }
      }) `
      -TimeoutSec $TimeoutSec | Out-Null
  }

  $response = Invoke-McpRequest `
    -Url $Url `
    -Method $requestMethod `
    -Params $requestParams `
    -TimeoutSec $TimeoutSec

  if ($RawResponse) {
    ConvertTo-PrettyJson $response
    return
  }

  $result = Get-PropertyValue $response "result"
  $isToolError = [bool] (Get-PropertyValue $result "isError")
  if ($isToolError) {
    $structured = Get-PropertyValue $result "structuredContent"
    $message = Get-PropertyValue $structured "message"
    if ([string]::IsNullOrWhiteSpace($message)) {
      $message = Get-PropertyValue ((Get-PropertyValue $result "content") | Select-Object -First 1) "text"
    }
    if ($null -ne $structured) {
      ConvertTo-PrettyJson $structured
    }
    throw "Tool returned error: $message"
  }

  $structuredContent = Get-PropertyValue $result "structuredContent"
  if ($null -ne $structuredContent -and -not $RawStructured) {
    $statusProcessId = Get-PropertyValue $structuredContent "processId"
    if ($null -ne $statusProcessId -and $mcpPortOwners.Count -gt 0) {
      $matchesOwner = Test-McpPortOwnerMatch -Owners $mcpPortOwners -ProcessId ([int] $statusProcessId)
      $color = if ($matchesOwner) { [ConsoleColor]::Green } else { [ConsoleColor]::Yellow }
      Write-Status "tb_inspect.processId=$statusProcessId; portOwnerMatch=$matchesOwner" $color
    }
  }
  if ($RawStructured -and $null -ne $structuredContent) {
    ConvertTo-PrettyJson $structuredContent
    return
  }

  if ($null -ne $structuredContent) {
    Write-Status "$requestMethod`: ok" Green
    ConvertTo-PrettyJson $structuredContent
  } else {
    Write-Status "$requestMethod`: ok" Green
    ConvertTo-PrettyJson $result
  }
} finally {
  if ($Launch -and -not $KeepOpen -and $null -ne $launchedProcess) {
    $process = Get-Process -Id $launchedProcess.Id -ErrorAction SilentlyContinue
    if ($null -ne $process) {
      Stop-Process -Id $launchedProcess.Id -Force
      Write-Status "Stopped TrenchBroom PID $($launchedProcess.Id)" Cyan
    }
  }
}
