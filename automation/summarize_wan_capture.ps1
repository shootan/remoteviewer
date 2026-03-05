param(
  [Parameter(Mandatory = $true)]
  [string]$HostInput,
  [Parameter(Mandatory = $true)]
  [string]$ClientInput,
  [int]$DecodedFpsGate = 20
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-LogPath {
  param([string]$InputPath, [string]$PreferredName)
  $resolved = (Resolve-Path -LiteralPath $InputPath).Path
  $item = Get-Item -LiteralPath $resolved
  if ($item.PSIsContainer) {
    $candidate = Join-Path $resolved $PreferredName
    if (Test-Path -LiteralPath $candidate) {
      return (Resolve-Path -LiteralPath $candidate).Path
    }
    throw "log file not found in directory: $candidate"
  }
  return $resolved
}

function Add-Double {
  param(
    [System.Collections.Generic.List[double]]$List,
    [string]$Value
  )
  if ([string]::IsNullOrWhiteSpace($Value)) { return }
  $parsed = 0.0
  if ([double]::TryParse($Value, [ref]$parsed)) {
    $List.Add($parsed)
  }
}

function Average {
  param([System.Collections.Generic.List[double]]$Values)
  if ($Values.Count -eq 0) { return 0.0 }
  $sum = 0.0
  foreach ($v in $Values) { $sum += $v }
  return ($sum / $Values.Count)
}

function Maximum {
  param([System.Collections.Generic.List[double]]$Values)
  if ($Values.Count -eq 0) { return 0.0 }
  $max = $Values[0]
  foreach ($v in $Values) {
    if ($v -gt $max) { $max = $v }
  }
  return $max
}

function Parse-KeyValues {
  param([string]$Line)
  $map = @{}
  if ($null -eq $Line) { return $map }
  $matches = [regex]::Matches($Line, '([A-Za-z0-9_]+)=([^\s]+)')
  foreach ($m in $matches) {
    $map[$m.Groups[1].Value] = $m.Groups[2].Value
  }
  return $map
}

$hostLog = Resolve-LogPath -InputPath $HostInput -PreferredName "host.out.log"
$clientLog = Resolve-LogPath -InputPath $ClientInput -PreferredName "client.out.log"

$hostLines = Get-Content -LiteralPath $hostLog
$clientLines = Get-Content -LiteralPath $clientLog

$hostEncodedFrames = New-Object System.Collections.Generic.List[double]
$hostMbps = New-Object System.Collections.Generic.List[double]
$clientDecodedFrames = New-Object System.Collections.Generic.List[double]
$clientLatencyAvgUs = New-Object System.Collections.Generic.List[double]
$clientLatencyMaxUs = New-Object System.Collections.Generic.List[double]
$clientMbps = New-Object System.Collections.Generic.List[double]
$clientDropPm = New-Object System.Collections.Generic.List[double]
$clientPresentGapOver1s = New-Object System.Collections.Generic.List[double]
$clientCongestionTransitions = New-Object System.Collections.Generic.List[double]

$m9Enabled = "unknown"
$m9Mode = "unknown"
$m9EventCount = 0
$m9ActionUp = 0
$m9ActionDown = 0
$m9ActionHold = 0

foreach ($line in $hostLines) {
  if ($line.Contains("[native-video-host] encodedFrames=")) {
    $kv = Parse-KeyValues -Line $line
    if ($kv.ContainsKey("encodedFrames")) { Add-Double -List $hostEncodedFrames -Value $kv["encodedFrames"] }
    if ($kv.ContainsKey("mbps")) { Add-Double -List $hostMbps -Value $kv["mbps"] }
  }
  if ($line.Contains("[native-video-host] h264 pacing=")) {
    $kv = Parse-KeyValues -Line $line
    if ($kv.ContainsKey("m9")) { $m9Enabled = $kv["m9"] }
    if ($kv.ContainsKey("m9Mode")) { $m9Mode = $kv["m9Mode"] }
  }
  if ($line.Contains("[native-video-host][m9]")) {
    $m9EventCount += 1
    $kv = Parse-KeyValues -Line $line
    if ($kv.ContainsKey("action")) {
      $action = $kv["action"].ToLowerInvariant()
      if ($action -eq "up") { $m9ActionUp += 1 }
      elseif ($action -eq "down") { $m9ActionDown += 1 }
      else { $m9ActionHold += 1 }
    }
  }
}

foreach ($line in $clientLines) {
  if ($line.Contains("[native-video-client] recvFrames=")) {
    $kv = Parse-KeyValues -Line $line
    if ($kv.ContainsKey("decodedFrames")) { Add-Double -List $clientDecodedFrames -Value $kv["decodedFrames"] }
    if ($kv.ContainsKey("avgLatencyUs")) { Add-Double -List $clientLatencyAvgUs -Value $kv["avgLatencyUs"] }
    if ($kv.ContainsKey("maxLatencyUs")) { Add-Double -List $clientLatencyMaxUs -Value $kv["maxLatencyUs"] }
    if ($kv.ContainsKey("mbps")) { Add-Double -List $clientMbps -Value $kv["mbps"] }
    if ($kv.ContainsKey("presentGapOver1s")) { Add-Double -List $clientPresentGapOver1s -Value $kv["presentGapOver1s"] }
    if ($kv.ContainsKey("congestionTransitions")) { Add-Double -List $clientCongestionTransitions -Value $kv["congestionTransitions"] }
  }
  if ($line.Contains("[native-video-client] udp-assembly")) {
    $kv = Parse-KeyValues -Line $line
    if ($kv.ContainsKey("dropPm")) { Add-Double -List $clientDropPm -Value $kv["dropPm"] }
  }
}

$hostEncodedAvg = [Math]::Round((Average -Values $hostEncodedFrames), 2)
$hostMbpsAvg = [Math]::Round((Average -Values $hostMbps), 3)
$clientDecodedAvg = [Math]::Round((Average -Values $clientDecodedFrames), 2)
$clientLatAvgUsAvg = [Math]::Round((Average -Values $clientLatencyAvgUs), 2)
$clientLatMaxUsMax = [Math]::Round((Maximum -Values $clientLatencyMaxUs), 2)
$clientMbpsAvg = [Math]::Round((Average -Values $clientMbps), 3)
$clientDropPmAvg = [Math]::Round((Average -Values $clientDropPm), 2)
$clientDropPmMax = [Math]::Round((Maximum -Values $clientDropPm), 2)
$clientTransitionsLast = if ($clientCongestionTransitions.Count -gt 0) {
  [int]$clientCongestionTransitions[$clientCongestionTransitions.Count - 1]
} else {
  0
}

$gateDecodedOk = if ($clientDecodedAvg -ge $DecodedFpsGate) { "True" } else { "False" }
$gatePresentGapOk = "Unknown"
if ($clientPresentGapOver1s.Count -gt 0) {
  $pgMax = Maximum -Values $clientPresentGapOver1s
  if ($pgMax -le 0.0) { $gatePresentGapOk = "True" } else { $gatePresentGapOk = "False" }
}
$gatePass = if ($gatePresentGapOk -eq "Unknown") { $gateDecodedOk } else {
  if ($gateDecodedOk -eq "True" -and $gatePresentGapOk -eq "True") { "True" } else { "False" }
}

Write-Output "HOST_LOG=$hostLog"
Write-Output "CLIENT_LOG=$clientLog"
Write-Output "HOST_STATS_SAMPLE_COUNT=$($hostEncodedFrames.Count)"
Write-Output "CLIENT_STATS_SAMPLE_COUNT=$($clientDecodedFrames.Count)"
Write-Output "HOST_ENCODED_FPS_AVG=$hostEncodedAvg"
Write-Output "HOST_MBPS_AVG=$hostMbpsAvg"
Write-Output "CLIENT_DECODED_FPS_AVG=$clientDecodedAvg"
Write-Output "CLIENT_LATENCY_AVG_US_AVG=$clientLatAvgUsAvg"
Write-Output "CLIENT_LATENCY_MAX_US_MAX=$clientLatMaxUsMax"
Write-Output "CLIENT_MBPS_AVG=$clientMbpsAvg"
Write-Output "CLIENT_UDP_DROP_PM_AVG=$clientDropPmAvg"
Write-Output "CLIENT_UDP_DROP_PM_MAX=$clientDropPmMax"
Write-Output "CLIENT_CONGESTION_TRANSITIONS_LAST=$clientTransitionsLast"
Write-Output "M9_ENABLED=$m9Enabled"
Write-Output "M9_MODE=$m9Mode"
Write-Output "M9_EVENT_COUNT=$m9EventCount"
Write-Output "M9_ACTION_UP_COUNT=$m9ActionUp"
Write-Output "M9_ACTION_DOWN_COUNT=$m9ActionDown"
Write-Output "M9_ACTION_HOLD_OTHER_COUNT=$m9ActionHold"
Write-Output "GATE_A_TARGET_DECODED_FPS=$DecodedFpsGate"
Write-Output "GATE_A_DECODED_FPS_OK=$gateDecodedOk"
Write-Output "GATE_A_PRESENT_GAP_OK=$gatePresentGapOk"
Write-Output "GATE_A_PASS=$gatePass"
