param(
  [string]$Root = "",
  [string]$RemoteHost = "127.0.0.1",
  [int]$Port = 43000,
  [int]$ControlPort = 43001,
  [string]$Transport = "",
  [int]$UdpMtu = 1200,
  [int]$Fps = 30,
  [string]$Codec = "raw",
  [int]$Bitrate = 1100000,
  [int]$Keyint = 15,
  [int]$EncodeWidth = 0,
  [int]$EncodeHeight = 0,
  [string]$EncoderBackend = "",
  [string]$DecoderBackend = "",
  [int]$FpsHint = 30,
  [switch]$NoInputChannel,
  [int]$HostSeconds = 12,
  [int]$ClientSeconds = 8,
  [int]$TraceEvery = 0,
  [int]$TraceMax = 0
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Root)) {
  $Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}
$effectiveTransport = $Transport
if ([string]::IsNullOrWhiteSpace($effectiveTransport)) {
  if ($Codec -ieq "h264") {
    $effectiveTransport = "udp"
  } else {
    $effectiveTransport = "tcp"
  }
}

$hostExe = Join-Path $Root "build-native2/apps/native_poc/Debug/remote60_native_video_host_poc.exe"
$clientExe = Join-Path $Root "build-native2/apps/native_poc/Debug/remote60_native_video_client_poc.exe"
if (-not (Test-Path $hostExe)) { throw "host exe not found: $hostExe" }
if (-not (Test-Path $clientExe)) { throw "client exe not found: $clientExe" }

Get-Process remote60_native_video_host_poc -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Get-Process remote60_native_video_client_poc -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

$ts = Get-Date -Format "yyyyMMdd-HHmmss"
$logDir = Join-Path $Root ("automation/logs/verify-native-video-" + $ts)
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$hostOut = Join-Path $logDir "host.out.log"
$hostErr = Join-Path $logDir "host.err.log"
$clientOut = Join-Path $logDir "client.out.log"
$clientErr = Join-Path $logDir "client.err.log"

$hostArgs = @("--bind-port", "$Port", "--fps", "$Fps", "--codec", "$Codec", "--seconds", "$HostSeconds")
if (-not [string]::IsNullOrWhiteSpace($effectiveTransport)) { $hostArgs += @("--transport", "$effectiveTransport") }
if ($UdpMtu -gt 0) { $hostArgs += @("--udp-mtu", "$UdpMtu") }
if ($Codec -ieq "h264") {
  $hostArgs += @("--bitrate", "$Bitrate", "--keyint", "$Keyint")
  if ($EncodeWidth -gt 0 -and $EncodeHeight -gt 0) {
    $hostArgs += @("--encode-width", "$EncodeWidth", "--encode-height", "$EncodeHeight")
  }
  if (-not [string]::IsNullOrWhiteSpace($EncoderBackend)) {
    $env:REMOTE60_NATIVE_ENCODER_BACKEND = $EncoderBackend
  } else {
    Remove-Item Env:REMOTE60_NATIVE_ENCODER_BACKEND -ErrorAction SilentlyContinue
  }
  if (-not [string]::IsNullOrWhiteSpace($DecoderBackend)) {
    $env:REMOTE60_NATIVE_DECODER_BACKEND = $DecoderBackend
  } else {
    Remove-Item Env:REMOTE60_NATIVE_DECODER_BACKEND -ErrorAction SilentlyContinue
  }
}
if ($ControlPort -gt 0) { $hostArgs += @("--control-port", "$ControlPort") }
if ($TraceEvery -gt 0) { $hostArgs += @("--trace-every", "$TraceEvery") }
if ($TraceMax -gt 0) { $hostArgs += @("--trace-max", "$TraceMax") }

$clientArgs = @("--host", $RemoteHost, "--port", "$Port", "--codec", "$Codec", "--seconds", "$ClientSeconds")
if (-not [string]::IsNullOrWhiteSpace($effectiveTransport)) { $clientArgs += @("--transport", "$effectiveTransport") }
if ($UdpMtu -gt 0) { $clientArgs += @("--udp-mtu", "$UdpMtu") }
if ($Codec -ieq "h264") { $clientArgs += @("--fps-hint", "$FpsHint") }
if ($NoInputChannel) { $clientArgs += "--no-input-channel" }
if ($ControlPort -gt 0) { $clientArgs += @("--control-port", "$ControlPort") }
if ($TraceEvery -gt 0) { $clientArgs += @("--trace-every", "$TraceEvery") }
if ($TraceMax -gt 0) { $clientArgs += @("--trace-max", "$TraceMax") }

if ($Codec -ieq "h264") {
  $env:REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE = "1"
} else {
  Remove-Item Env:REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE -ErrorAction SilentlyContinue
  Remove-Item Env:REMOTE60_NATIVE_ENCODER_BACKEND -ErrorAction SilentlyContinue
  Remove-Item Env:REMOTE60_NATIVE_DECODER_BACKEND -ErrorAction SilentlyContinue
}

$hostProc = Start-Process -FilePath $hostExe -ArgumentList $hostArgs -WorkingDirectory $Root `
  -RedirectStandardOutput $hostOut -RedirectStandardError $hostErr -PassThru
Start-Sleep -Seconds 2
$clientProc = Start-Process -FilePath $clientExe -ArgumentList $clientArgs -WorkingDirectory $Root `
  -RedirectStandardOutput $clientOut -RedirectStandardError $clientErr -PassThru

$clientProc.WaitForExit()
Start-Sleep -Milliseconds 200
if (-not $hostProc.HasExited) {
  try { Stop-Process -Id $hostProc.Id -Force -ErrorAction SilentlyContinue } catch {}
}

try { $hostProc.Refresh() } catch {}
try { $clientProc.Refresh() } catch {}
$hostRc = if ($hostProc.HasExited) { [int]$hostProc.ExitCode } else { -999 }
$clientRc = if ($clientProc.HasExited) { [int]$clientProc.ExitCode } else { -999 }

$clientLines = @()
if (Test-Path $clientOut) { $clientLines = Get-Content $clientOut }
$hostLines = @()
if (Test-Path $hostOut) { $hostLines = Get-Content $hostOut }

$latencyVals = New-Object System.Collections.Generic.List[double]
$controlRttVals = New-Object System.Collections.Generic.List[double]
$mbpsVals = New-Object System.Collections.Generic.List[double]
$decodedFrameVals = New-Object System.Collections.Generic.List[double]
$presentGapVals = New-Object System.Collections.Generic.List[double]
$presentGapOver1s = 0
$presentGapOver3s = 0
$tsSourceMft = 0
$tsSourceInputFallback = 0
$tsSourceHeaderFallback = 0
$abrSwitchCount = 0
$abrToHighCount = 0
$abrToMidCount = 0
$abrToLowCount = 0
$abrLastProfile = ""
$keyReqClientSent = 0
$keyReqHostRecv = 0
$keyReqHostConsumed = 0
$prevPresentUs = 0
$stageValues = [ordered]@{
  c2eUs = (New-Object System.Collections.Generic.List[double])
  encUs = (New-Object System.Collections.Generic.List[double])
  e2sUs = (New-Object System.Collections.Generic.List[double])
  netUs = (New-Object System.Collections.Generic.List[double])
  r2dUs = (New-Object System.Collections.Generic.List[double])
  decUs = (New-Object System.Collections.Generic.List[double])
  d2pUs = (New-Object System.Collections.Generic.List[double])
  renderUs = (New-Object System.Collections.Generic.List[double])
  totalUs = (New-Object System.Collections.Generic.List[double])
}
foreach ($line in $clientLines) {
  if ($line -match 'avgLatencyUs=([0-9]+)') {
    [void]$latencyVals.Add([double]$Matches[1])
  }
  if ($line -match 'decodedFrames=([0-9]+)') {
    [void]$decodedFrameVals.Add([double]$Matches[1])
  }
  if ($line -match 'rttUs=([0-9]+)') {
    [void]$controlRttVals.Add([double]$Matches[1])
  }
  if ($line -match 'mbps=([0-9]+(?:\\.[0-9]+)?)') {
    [void]$mbpsVals.Add([double]$Matches[1])
  }
  if ($line -match '\[native-video-client\]\[trace_present\]') {
    if ($line -match 'presentUs=([0-9]+)') {
      $presentUs = [int64]$Matches[1]
      if ($prevPresentUs -gt 0 -and $presentUs -ge $prevPresentUs) {
        $gapUs = [double]($presentUs - $prevPresentUs)
        [void]$presentGapVals.Add($gapUs)
        if ($gapUs -ge 1000000) { $presentGapOver1s += 1 }
        if ($gapUs -ge 3000000) { $presentGapOver3s += 1 }
      }
      $prevPresentUs = $presentUs
    }
    foreach ($stageName in $stageValues.Keys) {
      if ($line -match ($stageName + '=([0-9]+)')) {
        [void]$stageValues[$stageName].Add([double]$Matches[1])
      }
    }
  }
  if ($line -match '\[native-video-client\]\[trace_recv\]') {
    if ($line -match 'tsSource=([a-z_]+)') {
      switch ($Matches[1]) {
        "mft" { $tsSourceMft += 1 }
        "input_fallback" { $tsSourceInputFallback += 1 }
        "header_fallback" { $tsSourceHeaderFallback += 1 }
      }
    }
  }
  if ($line -match '\[native-video-client\]\[control\] keyframe-request seq=') {
    $keyReqClientSent += 1
  }
}

foreach ($line in $hostLines) {
  if ($line -match '\[native-video-host\]\[abr\] profile=([a-z]+)') {
    $abrSwitchCount += 1
    $abrLastProfile = $Matches[1]
    switch ($abrLastProfile) {
      "high" { $abrToHighCount += 1 }
      "mid" { $abrToMidCount += 1 }
      "low" { $abrToLowCount += 1 }
    }
  }
  if ($line -match '\[native-video-host\]\[control\] keyframe-request seq=') {
    $keyReqHostRecv += 1
  }
  if ($line -match '\[native-video-host\]\[control\] keyframe-request-consumed ') {
    $keyReqHostConsumed += 1
  }
}

function Stats-Summary {
  param([System.Collections.Generic.List[double]]$vals)
  if ($vals.Count -eq 0) {
    return [ordered]@{ count=0; avg=0; p95=0; max=0 }
  }
  $arr = @($vals.ToArray() | Sort-Object)
  $sum = 0.0
  foreach ($v in $arr) { $sum += $v }
  $avg = $sum / $arr.Count
  $p95Index = [Math]::Min($arr.Count - 1, [Math]::Floor(($arr.Count - 1) * 0.95))
  $p95 = $arr[$p95Index]
  $max = $arr[$arr.Count - 1]
  return [ordered]@{ count=$arr.Count; avg=[Math]::Round($avg,2); p95=$p95; max=$max }
}

$lat = Stats-Summary -vals $latencyVals
$ctl = Stats-Summary -vals $controlRttVals
$mb = Stats-Summary -vals $mbpsVals
$dec = Stats-Summary -vals $decodedFrameVals
$presentGap = Stats-Summary -vals $presentGapVals
$stageStats = [ordered]@{}
foreach ($stageName in $stageValues.Keys) {
  $stageStats[$stageName] = Stats-Summary -vals $stageValues[$stageName]
}

$bottleneckStage = "N/A"
$bottleneckAvgUs = 0
foreach ($stageName in @("c2eUs", "encUs", "e2sUs", "netUs", "r2dUs", "decUs", "d2pUs")) {
  $s = $stageStats[$stageName]
  if ($s.count -gt 0 -and $s.avg -ge $bottleneckAvgUs) {
    $bottleneckStage = $stageName
    $bottleneckAvgUs = $s.avg
  }
}

$decodedOk = ($Codec -ine "h264" -or $dec.max -gt 0)
$overallOk = ($clientRc -eq 0 -and $lat.count -gt 0 -and $decodedOk)

Write-Output "LOG_DIR=$logDir"
Write-Output "CODEC=$Codec"
Write-Output "TRANSPORT=$effectiveTransport"
Write-Output "HOST_RC=$hostRc"
Write-Output "CLIENT_RC=$clientRc"
Write-Output "LAT_COUNT=$($lat.count)"
Write-Output "LAT_AVG_US=$($lat.avg)"
Write-Output "LAT_P95_US=$($lat.p95)"
Write-Output "LAT_MAX_US=$($lat.max)"
Write-Output "DEC_COUNT=$($dec.count)"
Write-Output "DEC_AVG=$($dec.avg)"
Write-Output "DEC_P95=$($dec.p95)"
Write-Output "DEC_MAX=$($dec.max)"
Write-Output "PRESENT_GAP_COUNT=$($presentGap.count)"
Write-Output "PRESENT_GAP_AVG_US=$($presentGap.avg)"
Write-Output "PRESENT_GAP_P95_US=$($presentGap.p95)"
Write-Output "PRESENT_GAP_MAX_US=$($presentGap.max)"
Write-Output "PRESENT_GAP_OVER_1S=$presentGapOver1s"
Write-Output "PRESENT_GAP_OVER_3S=$presentGapOver3s"
Write-Output "TS_SOURCE_MFT=$tsSourceMft"
Write-Output "TS_SOURCE_INPUT_FALLBACK=$tsSourceInputFallback"
Write-Output "TS_SOURCE_HEADER_FALLBACK=$tsSourceHeaderFallback"
Write-Output "ABR_SWITCH_COUNT=$abrSwitchCount"
Write-Output "ABR_TO_HIGH_COUNT=$abrToHighCount"
Write-Output "ABR_TO_MID_COUNT=$abrToMidCount"
Write-Output "ABR_TO_LOW_COUNT=$abrToLowCount"
Write-Output "ABR_LAST_PROFILE=$abrLastProfile"
Write-Output "KEYREQ_CLIENT_SENT=$keyReqClientSent"
Write-Output "KEYREQ_HOST_RECV=$keyReqHostRecv"
Write-Output "KEYREQ_HOST_CONSUMED=$keyReqHostConsumed"
Write-Output "MBPS_COUNT=$($mb.count)"
Write-Output "MBPS_AVG=$($mb.avg)"
Write-Output "MBPS_P95=$($mb.p95)"
Write-Output "MBPS_MAX=$($mb.max)"
Write-Output "CTRL_RTT_COUNT=$($ctl.count)"
Write-Output "CTRL_RTT_AVG_US=$($ctl.avg)"
Write-Output "CTRL_RTT_P95_US=$($ctl.p95)"
Write-Output "CTRL_RTT_MAX_US=$($ctl.max)"
foreach ($stageName in $stageStats.Keys) {
  $upper = $stageName.ToUpperInvariant()
  $s = $stageStats[$stageName]
  Write-Output "STAGE_${upper}_COUNT=$($s.count)"
  Write-Output "STAGE_${upper}_AVG_US=$($s.avg)"
  Write-Output "STAGE_${upper}_P95_US=$($s.p95)"
  Write-Output "STAGE_${upper}_MAX_US=$($s.max)"
}
Write-Output "BOTTLENECK_STAGE=$bottleneckStage"
Write-Output "BOTTLENECK_AVG_US=$bottleneckAvgUs"
Write-Output "OVERALL_OK=$overallOk"
