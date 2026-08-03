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
  [string]$BuildDir = "build-native2",
  [ValidateSet("Debug", "Release")]
  [string]$Configuration = "Debug",
  [int]$HostSeconds = 12,
  [int]$ClientSeconds = 8,
  [int]$TraceEvery = 0,
  [int]$TraceMax = 0,
  [switch]$GateAProfile,
  [int]$GateAProfileRetryCount = 0
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

$resolvedBuildDir = $BuildDir
if (-not [System.IO.Path]::IsPathRooted($BuildDir)) {
  $resolvedBuildDir = Join-Path $Root $BuildDir
}
$hostExe = Join-Path $resolvedBuildDir "apps/native_poc/$Configuration/GNLinkStream.exe"
$clientExe = Join-Path $resolvedBuildDir "apps/native_poc/$Configuration/GNLinkViewer.exe"
if (-not (Test-Path $hostExe)) { throw "host exe not found: $hostExe" }
if (-not (Test-Path $clientExe)) { throw "client exe not found: $clientExe" }

# Never kill processes by name: a 2026-07-30 audit run took down the host the user was
# actively streaming from. If the requested ports are busy, name their owner and stop so the
# caller can pick isolated ports instead.
$portOwners = @()
try {
  $portOwners += @(Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue |
    Select-Object -ExpandProperty OwningProcess -Unique)
} catch {}
if ($ControlPort -gt 0) {
  try {
    $portOwners += @(Get-NetTCPConnection -LocalPort $ControlPort -State Listen -ErrorAction SilentlyContinue |
      Select-Object -ExpandProperty OwningProcess -Unique)
  } catch {}
}
$portOwners = @($portOwners | Where-Object { $_ } | Sort-Object -Unique)
if ($portOwners.Count -gt 0) {
  foreach ($ownerPid in $portOwners) {
    $ownerName = try { (Get-Process -Id $ownerPid -ErrorAction Stop).ProcessName } catch { "unknown" }
    Write-Output "PORT_IN_USE port=$Port controlPort=$ControlPort ownerPid=$ownerPid ownerName=$ownerName"
  }
  throw "ports $Port/$ControlPort are in use; pass isolated ports (e.g. -Port 44100 -ControlPort 44101)"
}

$ts = Get-Date -Format "yyyyMMdd-HHmmss"
$logDir = Join-Path $Root ("automation/logs/verify-native-video-" + $ts)
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$hostOut = Join-Path $logDir "host.out.log"
$hostErr = Join-Path $logDir "host.err.log"
$clientOut = Join-Path $logDir "client.out.log"
$clientErr = Join-Path $logDir "client.err.log"

$hostArgs = @("--bind-port", "$Port", "--fps", "$Fps", "--codec", "$Codec", "--seconds", "$HostSeconds")
# Loopback runs bind loopback: binding 0.0.0.0 from a fresh build path summons the Windows
# Firewall consent dialog, which dims the screen and starves WGC for the whole run.
if ($RemoteHost -eq "127.0.0.1" -or $RemoteHost -ieq "localhost") {
  $hostArgs += @("--bind-address", "127.0.0.1")
}
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
  if ($GateAProfile) {
    $env:REMOTE60_NATIVE_H264_NO_PACING = "1"
    $env:REMOTE60_NATIVE_FRAME_GATING_DISABLE = "1"
    $env:REMOTE60_NATIVE_ABR_DISABLE = "1"
  }
} else {
  Remove-Item Env:REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE -ErrorAction SilentlyContinue
  Remove-Item Env:REMOTE60_NATIVE_ENCODER_BACKEND -ErrorAction SilentlyContinue
  Remove-Item Env:REMOTE60_NATIVE_DECODER_BACKEND -ErrorAction SilentlyContinue
  Remove-Item Env:REMOTE60_NATIVE_H264_NO_PACING -ErrorAction SilentlyContinue
  Remove-Item Env:REMOTE60_NATIVE_FRAME_GATING_DISABLE -ErrorAction SilentlyContinue
  Remove-Item Env:REMOTE60_NATIVE_ABR_DISABLE -ErrorAction SilentlyContinue
}

$hostProc = Start-Process -FilePath $hostExe -ArgumentList $hostArgs -WorkingDirectory $Root `
  -RedirectStandardOutput $hostOut -RedirectStandardError $hostErr -PassThru
Start-Sleep -Seconds 2
$clientProc = Start-Process -FilePath $clientExe -ArgumentList $clientArgs -WorkingDirectory $Root `
  -RedirectStandardOutput $clientOut -RedirectStandardError $clientErr -PassThru

# Poll instead of a blocking wait so CPU time and working set can be sampled while both
# processes are still alive; the numbers become unreadable once they exit.
$hostCpuSeconds = 0.0
$clientCpuSeconds = 0.0
$hostPeakWsBytes = 0
$clientPeakWsBytes = 0
$sampleWatch = [System.Diagnostics.Stopwatch]::StartNew()
while (-not $clientProc.HasExited) {
  Start-Sleep -Milliseconds 500
  try {
    $hostProc.Refresh()
    if (-not $hostProc.HasExited) {
      $hostCpuSeconds = $hostProc.TotalProcessorTime.TotalSeconds
      if ($hostProc.WorkingSet64 -gt $hostPeakWsBytes) { $hostPeakWsBytes = $hostProc.WorkingSet64 }
    }
  } catch {}
  try {
    $clientProc.Refresh()
    if (-not $clientProc.HasExited) {
      $clientCpuSeconds = $clientProc.TotalProcessorTime.TotalSeconds
      if ($clientProc.WorkingSet64 -gt $clientPeakWsBytes) { $clientPeakWsBytes = $clientProc.WorkingSet64 }
    }
  } catch {}
}
$sampleWatch.Stop()
$cpuSampleSeconds = [Math]::Max(0.001, $sampleWatch.Elapsed.TotalSeconds)
Start-Sleep -Milliseconds 200
if (-not $hostProc.HasExited) {
  try {
    $hostProc.Refresh()
    $hostCpuSeconds = $hostProc.TotalProcessorTime.TotalSeconds
    if ($hostProc.WorkingSet64 -gt $hostPeakWsBytes) { $hostPeakWsBytes = $hostProc.WorkingSet64 }
  } catch {}
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
$decodedRawMbpsVals = New-Object System.Collections.Generic.List[double]
$decodeRatioVals = New-Object System.Collections.Generic.List[double]
$rawEquivMbpsVals = New-Object System.Collections.Generic.List[double]
$encRatioVals = New-Object System.Collections.Generic.List[double]
$gpuScaleAttemptsVals = New-Object System.Collections.Generic.List[double]
$gpuScaleSuccessVals = New-Object System.Collections.Generic.List[double]
$gpuScaleFailVals = New-Object System.Collections.Generic.List[double]
$gpuScaleCpuFallbackVals = New-Object System.Collections.Generic.List[double]
$decodedFrameVals = New-Object System.Collections.Generic.List[double]
$decodeRecoverySecVals = New-Object System.Collections.Generic.List[double]
$decodeZeroStreakCurrentSec = 0
$decodeZeroStreakMaxSec = 0
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
$udpAssemblyDropPmVals = New-Object System.Collections.Generic.List[double]
$udpSimDropPmVals = New-Object System.Collections.Generic.List[double]
$udpAssemblySampleCount = 0
$udpAssemblyChunksTotal = 0
$udpAssemblyCompletedTotal = 0
$udpAssemblyDroppedTotal = 0
$udpAssemblyMalformedTotal = 0
$udpAssemblyReorderTotal = 0
$udpAssemblyKeyReqTotal = 0
$udpSimDropTotal = 0
$queueWaitTimeoutCount = 0
$queueWaitNoWorkCount = 0
$queueWaitReason0Count = 0
$queueWaitReason1Count = 0
$queueWaitReason2Count = 0
$hostBottleneckStageCode0Count = 0
$hostBottleneckStageCode1Count = 0
$hostBottleneckStageCode2Count = 0
$hostBottleneckStageCode3Count = 0
$hostBottleneckStageCode4Count = 0
$hostBottleneckStageCode5Count = 0
$hostBottleneckStageCode6Count = 0
$hostBottleneckStageCode7Count = 0
$hostBottleneckStageCode8Count = 0
$hostBottleneckStageCode9Count = 0
$encApiPathCode0Count = 0
$encApiPathCode1Count = 0
$encApiPathCode2Count = 0
$encApiPathCode3Count = 0
$encApiPathCode4Count = 0
$encApiPathCode5Count = 0
$encApiPathCode6Count = 0
$queuePushCount = 0
$syntheticKeepaliveTotal = 0
$queuePopCount = 0
$hostSkippedByOverwrite = 0
$d3dPresentSuccessTotal = 0
$d3dPresentFailTotal = 0
$gdiFallbackPresentedTotal = 0
$gdiFallbackRateX1000Vals = New-Object System.Collections.Generic.List[double]
$fallbackInitFailTotal = 0
$fallbackRenderFailTotal = 0
$fallbackNv12ConvertFailTotal = 0
$paintCoalescedTotal = 0
$overwriteBeforePresentTotal = 0
$renderPathD3dNv12Count = 0
$renderPathD3dNv12SurfaceCount = 0
$renderPathGdiNv12FallbackCount = 0
$renderPathGdiBgraCount = 0
$renderPathOtherCount = 0
$fallbackReasonNoneCount = 0
$fallbackReasonD3dInitFailCount = 0
$fallbackReasonNv12ToBgraFailCount = 0
$fallbackReasonOtherCount = 0
$m9EventCount = 0
$m9LastMode = ""
$m9LastAction = ""
$m9LastFromLevel = -1
$m9LastToLevel = -1
$m9ApplyDeferredCount = 0
$congestionStateLast = "unknown"
$congestionTransitionEvents = 0
$congestionTransitionsLast = 0
$congestionRecoveryCountLast = 0
$congestionRecoveryAvgUsLast = 0
$congestionRecoveryMaxUsLast = 0
$congestionRecoveryReqLast = 0
$congestionStaleDropsLast = 0
$congestionHoldLatestDropsLast = 0
$congestionBurstDropsLast = 0
$queueDepthSamplesLast = 0
$queueDepthMaxLast = 0
$queueDepthH0Last = 0
$queueDepthH1Last = 0
$queueDepthH2Last = 0
$queueDepthH3Last = 0
$queueDepthH4pLast = 0
$prevPresentUs = -1
$stageValues = [ordered]@{
  c2eUs = (New-Object System.Collections.Generic.List[double])
  encUs = (New-Object System.Collections.Generic.List[double])
  e2sUs = (New-Object System.Collections.Generic.List[double])
  netUs = (New-Object System.Collections.Generic.List[double])
  r2dUs = (New-Object System.Collections.Generic.List[double])
  decUs = (New-Object System.Collections.Generic.List[double])
  decodeToQueueUs = (New-Object System.Collections.Generic.List[double])
  queueWaitUs = (New-Object System.Collections.Generic.List[double])
  paintUs = (New-Object System.Collections.Generic.List[double])
  uploadYUs = (New-Object System.Collections.Generic.List[double])
  uploadUVUs = (New-Object System.Collections.Generic.List[double])
  drawUs = (New-Object System.Collections.Generic.List[double])
  presentBlockUs = (New-Object System.Collections.Generic.List[double])
  d2pUs = (New-Object System.Collections.Generic.List[double])
  renderUs = (New-Object System.Collections.Generic.List[double])
  totalUs = (New-Object System.Collections.Generic.List[double])
}
$hostUserFeedbackValues = [ordered]@{
  pipeUs = (New-Object System.Collections.Generic.List[double])
  captureToCallbackUs = (New-Object System.Collections.Generic.List[double])
  callbackIntervalUs = (New-Object System.Collections.Generic.List[double])
  captureIntervalUs = (New-Object System.Collections.Generic.List[double])
  captureClockSkewUs = (New-Object System.Collections.Generic.List[double])
  captureD3DWaitUs = (New-Object System.Collections.Generic.List[double])
  captureCopyMapUs = (New-Object System.Collections.Generic.List[double])
  captureMemcpyUs = (New-Object System.Collections.Generic.List[double])
  captureUnmapWaitUs = (New-Object System.Collections.Generic.List[double])
  captureUnmapUs = (New-Object System.Collections.Generic.List[double])
  preEncodePrepUs = (New-Object System.Collections.Generic.List[double])
  scaleUs = (New-Object System.Collections.Generic.List[double])
  scaleD3DWaitUs = (New-Object System.Collections.Generic.List[double])
  scaleCopyMapUs = (New-Object System.Collections.Generic.List[double])
  scaleMemcpyUs = (New-Object System.Collections.Generic.List[double])
  scaleUnmapWaitUs = (New-Object System.Collections.Generic.List[double])
  scaleUnmapUs = (New-Object System.Collections.Generic.List[double])
  nv12Us = (New-Object System.Collections.Generic.List[double])
  selectWaitUs = (New-Object System.Collections.Generic.List[double])
  queueSelectWaitUs = (New-Object System.Collections.Generic.List[double])
  queueToEncodeUs = (New-Object System.Collections.Generic.List[double])
  queueToSendUs = (New-Object System.Collections.Generic.List[double])
  queueGapFrames = (New-Object System.Collections.Generic.List[double])
  captureToQueueUs = (New-Object System.Collections.Generic.List[double])
  queueWaitUs = (New-Object System.Collections.Generic.List[double])
  bottleneckStageCode = (New-Object System.Collections.Generic.List[double])
  bottleneckStageUs = (New-Object System.Collections.Generic.List[double])
  tickWaitUs = (New-Object System.Collections.Generic.List[double])
  queueDepth = (New-Object System.Collections.Generic.List[double])
  queueDepthMax = (New-Object System.Collections.Generic.List[double])
  sendStartUs = (New-Object System.Collections.Generic.List[double])
  sendDoneUs = (New-Object System.Collections.Generic.List[double])
  sendDurUs = (New-Object System.Collections.Generic.List[double])
  sendWaitUs = (New-Object System.Collections.Generic.List[double])
  sendIntervalUs = (New-Object System.Collections.Generic.List[double])
  sendIntervalErrUs = (New-Object System.Collections.Generic.List[double])
  sendToEncodeUs = (New-Object System.Collections.Generic.List[double])
  sendCallCount = (New-Object System.Collections.Generic.List[double])
  sendHeaderUs = (New-Object System.Collections.Generic.List[double])
  sendPayloadUs = (New-Object System.Collections.Generic.List[double])
  sendHeaderCallCount = (New-Object System.Collections.Generic.List[double])
  sendPayloadCallCount = (New-Object System.Collections.Generic.List[double])
  sendChunkCount = (New-Object System.Collections.Generic.List[double])
  sendChunkMaxUs = (New-Object System.Collections.Generic.List[double])
  encApiPathCode = (New-Object System.Collections.Generic.List[double])
  encApiHw = (New-Object System.Collections.Generic.List[double])
  encApiInputUs = (New-Object System.Collections.Generic.List[double])
  encApiDrainUs = (New-Object System.Collections.Generic.List[double])
  encApiNotAcceptingCount = (New-Object System.Collections.Generic.List[double])
  encApiNeedMoreInputCount = (New-Object System.Collections.Generic.List[double])
  encApiStreamChangeCount = (New-Object System.Collections.Generic.List[double])
  encApiOutputErrorCount = (New-Object System.Collections.Generic.List[double])
  encApiAsyncEnabled = (New-Object System.Collections.Generic.List[double])
  encApiAsyncPollCount = (New-Object System.Collections.Generic.List[double])
  encApiAsyncNoEventCount = (New-Object System.Collections.Generic.List[double])
  encApiAsyncNeedInputCount = (New-Object System.Collections.Generic.List[double])
  encApiAsyncHaveOutputCount = (New-Object System.Collections.Generic.List[double])
  c2eUs = (New-Object System.Collections.Generic.List[double])
  encQueueUs = (New-Object System.Collections.Generic.List[double])
  encQueueAlignedUs = (New-Object System.Collections.Generic.List[double])
    captureToAuUs = (New-Object System.Collections.Generic.List[double])
    captureToAuSkewUs = (New-Object System.Collections.Generic.List[double])
    auTsFromOutput = (New-Object System.Collections.Generic.List[double])
    auTsSkewUs = (New-Object System.Collections.Generic.List[double])
    captureToAuTimelineDeltaUs = (New-Object System.Collections.Generic.List[double])
  captureToAuTimelineSkewUs = (New-Object System.Collections.Generic.List[double])
  captureTimelineRelativeUs = (New-Object System.Collections.Generic.List[double])
  auTimelineRelativeUs = (New-Object System.Collections.Generic.List[double])
  captureTimelineOriginUs = (New-Object System.Collections.Generic.List[double])
  auTimelineOriginUs = (New-Object System.Collections.Generic.List[double])
  callbackToSendGapUs = (New-Object System.Collections.Generic.List[double])
  cb2eUs = (New-Object System.Collections.Generic.List[double])
  cb2sUs = (New-Object System.Collections.Generic.List[double])
  capAgeUs = (New-Object System.Collections.Generic.List[double])
  encUs = (New-Object System.Collections.Generic.List[double])
  e2sUs = (New-Object System.Collections.Generic.List[double])
}
$hostCaptureIntervalVals = New-Object System.Collections.Generic.List[double]
$hostCaptureIntervalErrUsVals = New-Object System.Collections.Generic.List[double]
$hostCallbackIntervalVals = New-Object System.Collections.Generic.List[double]
$hostCallbackIntervalErrUsVals = New-Object System.Collections.Generic.List[double]
$clientUserFeedbackValues = [ordered]@{
  totalUs = (New-Object System.Collections.Generic.List[double])
  capGapUs = (New-Object System.Collections.Generic.List[double])
  queueToPaintUs = (New-Object System.Collections.Generic.List[double])
  queueToPresentUs = (New-Object System.Collections.Generic.List[double])
  c2eUs = (New-Object System.Collections.Generic.List[double])
  encUs = (New-Object System.Collections.Generic.List[double])
  e2sUs = (New-Object System.Collections.Generic.List[double])
  netUs = (New-Object System.Collections.Generic.List[double])
  r2dUs = (New-Object System.Collections.Generic.List[double])
  decUs = (New-Object System.Collections.Generic.List[double])
  decodeToQueueUs = (New-Object System.Collections.Generic.List[double])
  queueWaitUs = (New-Object System.Collections.Generic.List[double])
  paintUs = (New-Object System.Collections.Generic.List[double])
  presentBlockUs = (New-Object System.Collections.Generic.List[double])
  d2pUs = (New-Object System.Collections.Generic.List[double])
}
$hostUserFeedbackTopEntries = New-Object System.Collections.Generic.List[object]
$clientUserFeedbackTopEntries = New-Object System.Collections.Generic.List[object]
$hostPrevCaptureUsForInterval = $null
$hostCaptureIntervalTargetUs = if ($Fps -gt 0) { [double](1000000.0 / [double]$Fps) } else { 0.0 }
$hostTraceCaptureSeen = $false
$hostCaptureSourceLast = ""
foreach ($line in $clientLines) {
  if ($line -match 'avgLatencyUs=([0-9]+)') {
    [void]$latencyVals.Add([double]$Matches[1])
  }
  if ($line -match 'decodedFrames=([0-9]+)') {
    $decodedFramesThisSec = [double]$Matches[1]
    [void]$decodedFrameVals.Add($decodedFramesThisSec)
    if ($decodedFramesThisSec -le 0) {
      $decodeZeroStreakCurrentSec += 1
      if ($decodeZeroStreakCurrentSec -gt $decodeZeroStreakMaxSec) {
        $decodeZeroStreakMaxSec = $decodeZeroStreakCurrentSec
      }
    } else {
      if ($decodeZeroStreakCurrentSec -gt 0) {
        [void]$decodeRecoverySecVals.Add([double]$decodeZeroStreakCurrentSec)
        $decodeZeroStreakCurrentSec = 0
      }
    }
  }
  if ($line -match 'rttUs=([0-9]+)') {
    [void]$controlRttVals.Add([double]$Matches[1])
  }
  if ($line -match 'mbps=([0-9]+(?:\\.[0-9]+)?)') {
    [void]$mbpsVals.Add([double]$Matches[1])
  }
  if ($line -match 'decodedRawMbps=([0-9]+(?:\\.[0-9]+)?)') {
    [void]$decodedRawMbpsVals.Add([double]$Matches[1])
  }
  if ($line -match 'decodeRatioX100=([0-9]+)') {
    [void]$decodeRatioVals.Add([double]$Matches[1])
  }
  if ($line -match '\[native-video-client\]\[congestion\] state=([A-Za-z_]+)') {
    $congestionTransitionEvents += 1
    $congestionStateLast = $Matches[1]
  }
  if ($line -match 'congestionState=([A-Za-z_]+)') {
    $congestionStateLast = $Matches[1]
  }
  if ($line -match 'congestionTransitions=([0-9]+)') {
    $congestionTransitionsLast = [int64]$Matches[1]
  }
  if ($line -match 'congestionRecoveryCount=([0-9]+)') {
    $congestionRecoveryCountLast = [int64]$Matches[1]
  }
  if ($line -match 'congestionRecoveryAvgUs=([0-9]+)') {
    $congestionRecoveryAvgUsLast = [int64]$Matches[1]
  }
  if ($line -match 'congestionRecoveryMaxUs=([0-9]+)') {
    $congestionRecoveryMaxUsLast = [int64]$Matches[1]
  }
  if ($line -match 'congestionRecoveryReq=([0-9]+)') {
    $congestionRecoveryReqLast = [int64]$Matches[1]
  }
  if ($line -match 'staleDrops=([0-9]+)') {
    $congestionStaleDropsLast = [int64]$Matches[1]
  }
  if ($line -match 'holdLatestDrops=([0-9]+)') {
    $congestionHoldLatestDropsLast = [int64]$Matches[1]
  }
  if ($line -match 'burstDrops=([0-9]+)') {
    $congestionBurstDropsLast = [int64]$Matches[1]
  }
  if ($line -match 'queueDepthSamples=([0-9]+)') {
    $queueDepthSamplesLast = [int64]$Matches[1]
  }
  if ($line -match 'queueDepthMax=([0-9]+)') {
    $queueDepthMaxLast = [int64]$Matches[1]
  }
  if ($line -match 'queueDepthH0=([0-9]+)') {
    $queueDepthH0Last = [int64]$Matches[1]
  }
  if ($line -match 'queueDepthH1=([0-9]+)') {
    $queueDepthH1Last = [int64]$Matches[1]
  }
  if ($line -match 'queueDepthH2=([0-9]+)') {
    $queueDepthH2Last = [int64]$Matches[1]
  }
  if ($line -match 'queueDepthH3=([0-9]+)') {
    $queueDepthH3Last = [int64]$Matches[1]
  }
  if ($line -match 'queueDepthH4p=([0-9]+)') {
    $queueDepthH4pLast = [int64]$Matches[1]
  }
  if ($line -match 'd3dPresentSuccess=([0-9]+)') {
    $d3dPresentSuccessTotal += [int64]$Matches[1]
  }
  if ($line -match 'd3dPresentFail=([0-9]+)') {
    $d3dPresentFailTotal += [int64]$Matches[1]
  }
  if ($line -match 'gdiFallbackPresented=([0-9]+)') {
    $gdiFallbackPresentedTotal += [int64]$Matches[1]
  }
  if ($line -match 'gdiFallbackRateX1000=([0-9]+)') {
    [void]$gdiFallbackRateX1000Vals.Add([double]$Matches[1])
  }
  if ($line -match 'fallbackInitFail=([0-9]+)') {
    $fallbackInitFailTotal += [int64]$Matches[1]
  }
  if ($line -match 'fallbackRenderFail=([0-9]+)') {
    $fallbackRenderFailTotal += [int64]$Matches[1]
  }
  if ($line -match 'fallbackNv12ConvertFail=([0-9]+)') {
    $fallbackNv12ConvertFailTotal += [int64]$Matches[1]
  }
  if ($line -match 'paintCoalesced=([0-9]+)') {
    $paintCoalescedTotal += [int64]$Matches[1]
  }
  if ($line -match 'overwriteBeforePresent=([0-9]+)') {
    $overwriteBeforePresentTotal += [int64]$Matches[1]
  }
  if ($line -match '\[native-video-client\]\[trace_present\]') {
    if ($line -match 'presentUs=([0-9]+)') {
      $presentUs = [int64]$Matches[1]
      # trace_present is sampled at TraceEvery frames, so adjacent trace timestamps are
      # expected to be about one second apart at 60 fps. They are not consecutive presents
      # and therefore cannot be used as a freeze detector.
      $prevPresentUs = $presentUs
    }
    foreach ($stageName in $stageValues.Keys) {
      if ($line -match ($stageName + '=([0-9]+)')) {
        [void]$stageValues[$stageName].Add([double]$Matches[1])
      }
    }
    if ($line -match 'renderPath=([A-Za-z0-9_]+)') {
      switch ($Matches[1]) {
        "d3d_nv12" { $renderPathD3dNv12Count += 1 }
        "d3d_nv12_surface" {
          $renderPathD3dNv12Count += 1
          $renderPathD3dNv12SurfaceCount += 1
        }
        "gdi_nv12_fallback" { $renderPathGdiNv12FallbackCount += 1 }
        "gdi_bgra" { $renderPathGdiBgraCount += 1 }
        default { $renderPathOtherCount += 1 }
      }
    }
    if ($line -match 'fallbackReason=([A-Za-z0-9_]+)') {
      switch ($Matches[1]) {
        "none" { $fallbackReasonNoneCount += 1 }
        "d3d_init_fail" { $fallbackReasonD3dInitFailCount += 1 }
        "nv12_to_bgra_fail" { $fallbackReasonNv12ToBgraFailCount += 1 }
        default { $fallbackReasonOtherCount += 1 }
      }
    }
  }
  if ($line -match '\[native-video-client\]\[user-feedback\]') {
    $seqVal = 0
    if ($line -match 'seq=([0-9]+)') {
      $seqVal = [int64]$Matches[1]
    }
    foreach ($stageName in $clientUserFeedbackValues.Keys) {
      if ($line -match ($stageName + '=([0-9]+)')) {
        [void]$clientUserFeedbackValues[$stageName].Add([double]$Matches[1])
      }
    }
    # capGapUs is measured between actual consecutive successful presents. A >=1s gap
    # always causes a user-feedback record, so this remains a reliable sparse freeze signal.
    if ($line -match 'capGapUs=([0-9]+)') {
      $gapUs = [double]$Matches[1]
      [void]$presentGapVals.Add($gapUs)
      if ($gapUs -ge 1000000) { $presentGapOver1s += 1 }
      if ($gapUs -ge 3000000) { $presentGapOver3s += 1 }
    }
    if ($line -match 'totalUs=([0-9]+)') {
      [void]$clientUserFeedbackTopEntries.Add([PSCustomObject]@{
        seq = $seqVal
        totalUs = [double]$Matches[1]
      })
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
  if ($line -match '\[native-video-client\] udp-assembly ') {
    $udpAssemblySampleCount += 1
    if ($line -match 'chunks=([0-9]+)') {
      $udpAssemblyChunksTotal += [int64]$Matches[1]
    }
    if ($line -match 'completed=([0-9]+)') {
      $udpAssemblyCompletedTotal += [int64]$Matches[1]
    }
    if ($line -match 'dropped=([0-9]+)') {
      $udpAssemblyDroppedTotal += [int64]$Matches[1]
    }
    if ($line -match 'malformed=([0-9]+)') {
      $udpAssemblyMalformedTotal += [int64]$Matches[1]
    }
    if ($line -match 'reorder=([0-9]+)') {
      $udpAssemblyReorderTotal += [int64]$Matches[1]
    }
    if ($line -match 'keyReq=([0-9]+)') {
      $udpAssemblyKeyReqTotal += [int64]$Matches[1]
    }
    if ($line -match 'dropPm=([0-9]+)') {
      [void]$udpAssemblyDropPmVals.Add([double]$Matches[1])
    }
    if ($line -match 'simDropPm=([0-9]+)') {
      [void]$udpSimDropPmVals.Add([double]$Matches[1])
    }
    if ($line -match 'simDropTotal=([0-9]+)') {
      $udpSimDropTotal += [int64]$Matches[1]
    }
  }
}

foreach ($line in $hostLines) {
  if ($line -match 'capture item source=(.+)$') {
    $hostCaptureSourceLast = $Matches[1].Trim()
  }
  if ($line -match 'rawEquivMbps=([0-9]+(?:\\.[0-9]+)?)') {
    [void]$rawEquivMbpsVals.Add([double]$Matches[1])
  }
  if ($line -match 'encRatioX100=([0-9]+)') {
    [void]$encRatioVals.Add([double]$Matches[1])
  }
  if ($line -match 'gpuScaleAttempts=([0-9]+)') {
    [void]$gpuScaleAttemptsVals.Add([double]$Matches[1])
  }
  if ($line -match 'gpuScaleSuccess=([0-9]+)') {
    [void]$gpuScaleSuccessVals.Add([double]$Matches[1])
  }
  if ($line -match 'gpuScaleFail=([0-9]+)') {
    [void]$gpuScaleFailVals.Add([double]$Matches[1])
  }
  if ($line -match 'gpuScaleCpuFallback=([0-9]+)') {
    [void]$gpuScaleCpuFallbackVals.Add([double]$Matches[1])
  }
  if ($line -match '\[native-video-host\]\[abr\] profile=([a-z]+)') {
    $abrSwitchCount += 1
    $abrLastProfile = $Matches[1]
    switch ($abrLastProfile) {
      "high" { $abrToHighCount += 1 }
      "mid" { $abrToMidCount += 1 }
      "low" { $abrToLowCount += 1 }
    }
  }
  if ($line -match '\[native-video-host\]\[m9\] action=([a-z]+)') {
    $m9EventCount += 1
    $m9LastAction = $Matches[1]
    if ($line -match 'mode=([a-zA-Z0-9_-]+)') {
      $m9LastMode = $Matches[1]
    }
    if ($line -match 'fromLevel=([0-9]+)') {
      $m9LastFromLevel = [int]$Matches[1]
    }
    if ($line -match 'toLevel=([0-9]+)') {
      $m9LastToLevel = [int]$Matches[1]
    }
  }
  if ($line -match '\[native-video-host\]\[m9\] apply-path-deferred=1') {
    $m9ApplyDeferredCount += 1
  }
  if ($line -match '\[native-video-host\]\[control\] keyframe-request seq=') {
    $keyReqHostRecv += 1
  }
  if ($line -match '\[native-video-host\]\[control\] keyframe-request-consumed ') {
    $keyReqHostConsumed += 1
  }
  if ($line -match 'queueWaitTimeoutCount=([0-9]+)') {
    $queueWaitTimeoutCount = [int64]$Matches[1]
  }
  if ($line -match 'queueWaitNoWorkCount=([0-9]+)') {
    $queueWaitNoWorkCount = [int64]$Matches[1]
  }
  if ($line -match 'queuePushCount=([0-9]+)') {
    $queuePushCount = [int64]$Matches[1]
  }
  if ($line -match 'syntheticKeepaliveTotal=([0-9]+)') {
    $syntheticKeepaliveTotal = [int64]$Matches[1]
  }
  if ($line -match 'queuePopCount=([0-9]+)') {
    $queuePopCount = [int64]$Matches[1]
  }
  if ($line -match 'skippedByOverwrite=([0-9]+)') {
    $hostSkippedByOverwrite = [int64]$Matches[1]
  }
  if ($line -match '\[native-video-host\]\[user-feedback\]') {
    $seqVal = 0
    $captureUs = $null
    $frameCaptureUs = $null
    $callbackIntervalUs = $null
    $captureIntervalUs = $null
    $sendUs = $null
    $pipeUsFromLine = $null
    $sendStartUsAbs = $null
    $sendDoneUsAbs = $null
      $topEntry = @{
        seq = 0
        pipeUs = 0
      callbackIntervalUs = $null
      captureIntervalUs = $null
      sendCallCount = $null
      sendHeaderUs = $null
      sendPayloadUs = $null
      sendHeaderCallCount = $null
      sendPayloadCallCount = $null
      sendChunkCount = $null
      sendChunkMaxUs = $null
      encApiPathCode = $null
      encApiHw = $null
      encApiInputUs = $null
      encApiDrainUs = $null
      encApiNotAcceptingCount = $null
      encApiNeedMoreInputCount = $null
      encApiStreamChangeCount = $null
      encApiOutputErrorCount = $null
      encApiAsyncEnabled = $null
      encApiAsyncPollCount = $null
      encApiAsyncNoEventCount = $null
      encApiAsyncNeedInputCount = $null
      encApiAsyncHaveOutputCount = $null
      sendWaitUs = $null
      sendIntervalUs = $null
      sendIntervalErrUs = $null
      sendToEncodeUs = $null
      tickWaitUs = $null
      captureToAuTimelineSkewUs = $null
      captureToAuTimelineDeltaUs = $null
      captureToAuSkewUs = $null
      auTsFromOutput = $null
      auTsSkewUs = $null
      captureTimelineRelativeUs = $null
      auTimelineRelativeUs = $null
      captureTimelineOriginUs = $null
      auTimelineOriginUs = $null
      captureToAuUs = $null
      encodeInputUs = $null
      auCaptureUs = $null
      captureToQueueUs = $null
      captureClockSkewUs = $null
      preEncodePrepUs = $null
      scaleUs = $null
      nv12Us = $null
        queueToEncodeUs = $null
        queueToSendUs = $null
        encQueueAlignedUs = $null
        bottleneckStageCode = $null
        bottleneckStageUs = $null
        queueWaitReason = $null
      }
    foreach ($pair in [regex]::Matches($line, '([A-Za-z_][A-Za-z0-9_]*)=([0-9]+)')) {
      $key = $pair.Groups[1].Value
      $value = [double]$pair.Groups[2].Value
      if ($key -eq 'seq') {
        $seqVal = [int64]$value
      } elseif ($key -eq 'captureUs') {
        $captureUs = $value
      } elseif ($key -eq 'frameCaptureUs') {
        $frameCaptureUs = $value
      } elseif ($key -eq 'callbackIntervalUs') {
        $callbackIntervalUs = $value
      } elseif ($key -eq 'captureIntervalUs') {
        $captureIntervalUs = $value
      } elseif ($key -eq 'sendUs') {
        $sendUs = $value
      } elseif ($key -eq 'pipeUs') {
        $pipeUsFromLine = $value
      } elseif ($key -eq 'sendStartUs') {
        $sendStartUsAbs = $value
      } elseif ($key -eq 'sendDoneUs') {
        $sendDoneUsAbs = $value
      } elseif ($key -eq 'queueWaitReason') {
        $reason = [int]$value
        switch ($reason) {
          0 { $queueWaitReason0Count += 1 }
          1 { $queueWaitReason1Count += 1 }
          2 { $queueWaitReason2Count += 1 }
        }
      } elseif ($key -eq 'bottleneckStageCode') {
        $stageCode = [int]$value
        switch ($stageCode) {
          0 { $hostBottleneckStageCode0Count += 1 }
          1 { $hostBottleneckStageCode1Count += 1 }
          2 { $hostBottleneckStageCode2Count += 1 }
          3 { $hostBottleneckStageCode3Count += 1 }
          4 { $hostBottleneckStageCode4Count += 1 }
          5 { $hostBottleneckStageCode5Count += 1 }
          6 { $hostBottleneckStageCode6Count += 1 }
          7 { $hostBottleneckStageCode7Count += 1 }
          8 { $hostBottleneckStageCode8Count += 1 }
          9 { $hostBottleneckStageCode9Count += 1 }
        }
      } elseif ($key -eq 'encApiPathCode') {
        $pathCode = [int]$value
        switch ($pathCode) {
          0 { $encApiPathCode0Count += 1 }
          1 { $encApiPathCode1Count += 1 }
          2 { $encApiPathCode2Count += 1 }
          3 { $encApiPathCode3Count += 1 }
          4 { $encApiPathCode4Count += 1 }
          5 { $encApiPathCode5Count += 1 }
          6 { $encApiPathCode6Count += 1 }
        }
      }
      if ($hostUserFeedbackValues.Keys -contains $key -and $key -ne 'sendStartUs' -and $key -ne 'sendDoneUs') {
        [void]$hostUserFeedbackValues[$key].Add($value)
      }
      if ($topEntry.ContainsKey($key)) {
        $topEntry[$key] = $value
      }
    }
    if (-not $hostTraceCaptureSeen) {
      $captureRefUs = $null
      if ($captureUs -ne $null) {
        $captureRefUs = $captureUs
      } elseif ($frameCaptureUs -ne $null) {
        $captureRefUs = $frameCaptureUs
      }
      if ($captureRefUs -ne $null) {
        if ($hostPrevCaptureUsForInterval -ne $null -and $captureRefUs -ge $hostPrevCaptureUsForInterval) {
          $captureIntervalUs = [double]($captureRefUs - $hostPrevCaptureUsForInterval)
          [void]$hostCaptureIntervalVals.Add($captureIntervalUs)
          if ($hostCaptureIntervalTargetUs -gt 0) {
            [void]$hostCaptureIntervalErrUsVals.Add([double][Math]::Abs($captureIntervalUs - $hostCaptureIntervalTargetUs))
          }
        }
        $hostPrevCaptureUsForInterval = $captureRefUs
      }
    }
    if ($callbackIntervalUs -ne $null) {
      [void]$hostCallbackIntervalVals.Add([double]$callbackIntervalUs)
      if ($hostCaptureIntervalTargetUs -gt 0) {
        [void]$hostCallbackIntervalErrUsVals.Add([double][Math]::Abs($callbackIntervalUs - $hostCaptureIntervalTargetUs))
      }
    }
    if ($captureUs -ne $null -and $sendStartUsAbs -ne $null) {
      [void]$hostUserFeedbackValues['sendStartUs'].Add([double][Math]::Max(0.0, $sendStartUsAbs - $captureUs))
    }
    if ($captureUs -ne $null -and $sendDoneUsAbs -ne $null) {
      [void]$hostUserFeedbackValues['sendDoneUs'].Add([double][Math]::Max(0.0, $sendDoneUsAbs - $captureUs))
    }
    if ($pipeUsFromLine -eq $null -and $captureUs -ne $null -and $sendUs -ne $null) {
      $pipeUsFromLine = [Math]::Max(0.0, $sendUs - $captureUs)
      [void]$hostUserFeedbackValues['pipeUs'].Add([double]$pipeUsFromLine)
    }
    if ($pipeUsFromLine -ne $null) {
      $topEntry.seq = $seqVal
      $topEntry.pipeUs = [double]$pipeUsFromLine
      [void]$hostUserFeedbackTopEntries.Add([PSCustomObject]$topEntry)
    }
  } elseif ($line -match '\[native-video-host\]\[trace\]') {
    $seqVal = 0
    $captureUs = $null
    $frameCaptureUs = $null
    $callbackIntervalUs = $null
    $captureIntervalUs = $null
    $sendUs = $null
    $pipeUsFromLine = $null
    $sendStartUsAbs = $null
    $sendDoneUsAbs = $null
    $topEntry = @{
      seq = 0
      pipeUs = 0
      callbackIntervalUs = $null
      captureIntervalUs = $null
      sendCallCount = $null
      sendHeaderUs = $null
      sendPayloadUs = $null
      sendHeaderCallCount = $null
      sendPayloadCallCount = $null
      sendChunkCount = $null
      sendChunkMaxUs = $null
      encApiPathCode = $null
      encApiHw = $null
      encApiInputUs = $null
      encApiDrainUs = $null
      encApiNotAcceptingCount = $null
      encApiNeedMoreInputCount = $null
      encApiStreamChangeCount = $null
      encApiOutputErrorCount = $null
      encApiAsyncEnabled = $null
      encApiAsyncPollCount = $null
      encApiAsyncNoEventCount = $null
      encApiAsyncNeedInputCount = $null
      encApiAsyncHaveOutputCount = $null
      sendWaitUs = $null
      sendIntervalUs = $null
      sendIntervalErrUs = $null
      sendToEncodeUs = $null
      tickWaitUs = $null
      captureToAuTimelineSkewUs = $null
      captureToAuTimelineDeltaUs = $null
      captureToAuSkewUs = $null
      auTsFromOutput = $null
      auTsSkewUs = $null
      captureTimelineRelativeUs = $null
      auTimelineRelativeUs = $null
      captureTimelineOriginUs = $null
      auTimelineOriginUs = $null
      captureToAuUs = $null
      encodeInputUs = $null
      auCaptureUs = $null
      captureToQueueUs = $null
      captureClockSkewUs = $null
      preEncodePrepUs = $null
      scaleUs = $null
      nv12Us = $null
        queueToEncodeUs = $null
        queueToSendUs = $null
        encQueueAlignedUs = $null
        bottleneckStageCode = $null
        bottleneckStageUs = $null
        queueWaitReason = $null
      }
    foreach ($pair in [regex]::Matches($line, '([A-Za-z_][A-Za-z0-9_]*)=([0-9]+)')) {
      $key = $pair.Groups[1].Value
      $value = [double]$pair.Groups[2].Value
      if ($key -eq 'seq') {
        $seqVal = [int64]$value
      } elseif ($key -eq 'captureUs') {
        $captureUs = $value
      } elseif ($key -eq 'frameCaptureUs') {
        $frameCaptureUs = $value
      } elseif ($key -eq 'callbackIntervalUs') {
        $callbackIntervalUs = $value
      } elseif ($key -eq 'captureIntervalUs') {
        $captureIntervalUs = $value
      } elseif ($key -eq 'sendUs') {
        $sendUs = $value
      } elseif ($key -eq 'sendStartUs') {
        $sendStartUsAbs = $value
      } elseif ($key -eq 'sendDoneUs') {
        $sendDoneUsAbs = $value
      } elseif ($key -eq 'queueWaitReason') {
        $reason = [int]$value
        switch ($reason) {
          0 { $queueWaitReason0Count += 1 }
          1 { $queueWaitReason1Count += 1 }
          2 { $queueWaitReason2Count += 1 }
        }
      } elseif ($key -eq 'bottleneckStageCode') {
        $stageCode = [int]$value
        switch ($stageCode) {
          0 { $hostBottleneckStageCode0Count += 1 }
          1 { $hostBottleneckStageCode1Count += 1 }
          2 { $hostBottleneckStageCode2Count += 1 }
          3 { $hostBottleneckStageCode3Count += 1 }
          4 { $hostBottleneckStageCode4Count += 1 }
          5 { $hostBottleneckStageCode5Count += 1 }
          6 { $hostBottleneckStageCode6Count += 1 }
          7 { $hostBottleneckStageCode7Count += 1 }
          8 { $hostBottleneckStageCode8Count += 1 }
          9 { $hostBottleneckStageCode9Count += 1 }
        }
      } elseif ($key -eq 'encApiPathCode') {
        $pathCode = [int]$value
        switch ($pathCode) {
          0 { $encApiPathCode0Count += 1 }
          1 { $encApiPathCode1Count += 1 }
          2 { $encApiPathCode2Count += 1 }
          3 { $encApiPathCode3Count += 1 }
          4 { $encApiPathCode4Count += 1 }
          5 { $encApiPathCode5Count += 1 }
          6 { $encApiPathCode6Count += 1 }
        }
      }
      if ($hostUserFeedbackValues.Keys -contains $key -and $key -ne 'sendStartUs' -and $key -ne 'sendDoneUs') {
        [void]$hostUserFeedbackValues[$key].Add($value)
      }
      if ($topEntry.ContainsKey($key)) {
        $topEntry[$key] = $value
      }
    }
    $captureRefUs = $null
    if ($captureUs -ne $null) {
      $captureRefUs = $captureUs
    } elseif ($frameCaptureUs -ne $null) {
      $captureRefUs = $frameCaptureUs
    }
    if ($captureRefUs -ne $null) {
      if ($hostPrevCaptureUsForInterval -ne $null -and $captureRefUs -ge $hostPrevCaptureUsForInterval) {
        $captureIntervalUs = [double]($captureRefUs - $hostPrevCaptureUsForInterval)
        [void]$hostCaptureIntervalVals.Add($captureIntervalUs)
        if ($hostCaptureIntervalTargetUs -gt 0) {
          [void]$hostCaptureIntervalErrUsVals.Add([double][Math]::Abs($captureIntervalUs - $hostCaptureIntervalTargetUs))
        }
      }
      $hostPrevCaptureUsForInterval = $captureRefUs
      $hostTraceCaptureSeen = $true
    }
    if ($callbackIntervalUs -ne $null) {
      [void]$hostCallbackIntervalVals.Add([double]$callbackIntervalUs)
      if ($hostCaptureIntervalTargetUs -gt 0) {
        [void]$hostCallbackIntervalErrUsVals.Add([double][Math]::Abs($callbackIntervalUs - $hostCaptureIntervalTargetUs))
      }
    }
    if ($captureUs -ne $null -and $sendStartUsAbs -ne $null) {
      [void]$hostUserFeedbackValues['sendStartUs'].Add([double][Math]::Max(0.0, $sendStartUsAbs - $captureUs))
    }
    if ($captureUs -ne $null -and $sendDoneUsAbs -ne $null) {
      [void]$hostUserFeedbackValues['sendDoneUs'].Add([double][Math]::Max(0.0, $sendDoneUsAbs - $captureUs))
    }
    if ($captureUs -ne $null -and $sendUs -ne $null) {
      $pipeUsFromLine = [Math]::Max(0.0, $sendUs - $captureUs)
      [void]$hostUserFeedbackValues['pipeUs'].Add([double]$pipeUsFromLine)
    }
    if ($pipeUsFromLine -ne $null) {
      $topEntry.seq = $seqVal
      $topEntry.pipeUs = [double]$pipeUsFromLine
      [void]$hostUserFeedbackTopEntries.Add([PSCustomObject]$topEntry)
    }
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
$decRaw = Stats-Summary -vals $decodedRawMbpsVals
$decRatio = Stats-Summary -vals $decodeRatioVals
$encRaw = Stats-Summary -vals $rawEquivMbpsVals
$encRatio = Stats-Summary -vals $encRatioVals
$gpuScaleAttempts = Stats-Summary -vals $gpuScaleAttemptsVals
$gpuScaleSuccess = Stats-Summary -vals $gpuScaleSuccessVals
$gpuScaleFail = Stats-Summary -vals $gpuScaleFailVals
$gpuScaleCpuFallback = Stats-Summary -vals $gpuScaleCpuFallbackVals
$dec = Stats-Summary -vals $decodedFrameVals
$decodeRecoverySec = Stats-Summary -vals $decodeRecoverySecVals
$udpAssemblyDropPm = Stats-Summary -vals $udpAssemblyDropPmVals
$udpSimDropPm = Stats-Summary -vals $udpSimDropPmVals
$presentGap = Stats-Summary -vals $presentGapVals
$gdiFallbackRate = Stats-Summary -vals $gdiFallbackRateX1000Vals
$stageStats = [ordered]@{}
foreach ($stageName in $stageValues.Keys) {
  $stageStats[$stageName] = Stats-Summary -vals $stageValues[$stageName]
}
$clientUserFeedbackStats = [ordered]@{}
foreach ($stageName in $clientUserFeedbackValues.Keys) {
  $clientUserFeedbackStats[$stageName] = Stats-Summary -vals $clientUserFeedbackValues[$stageName]
}
$hostUserFeedbackStats = [ordered]@{}
foreach ($stageName in $hostUserFeedbackValues.Keys) {
  $hostUserFeedbackStats[$stageName] = Stats-Summary -vals $hostUserFeedbackValues[$stageName]
}
# Trace records are intentionally sparse; subtracting adjacent trace capture timestamps
# measures the logging cadence (roughly one second), not the frame cadence. The host embeds
# the real consecutive callback interval in every trace, so use that for capture cadence.
$hostCaptureIntervalStats = Stats-Summary -vals $hostCallbackIntervalVals
$hostCaptureIntervalErrStats = Stats-Summary -vals $hostCallbackIntervalErrUsVals
$hostCallbackIntervalStats = Stats-Summary -vals $hostCallbackIntervalVals
$hostCallbackIntervalErrStats = Stats-Summary -vals $hostCallbackIntervalErrUsVals

# Normalize sorted top-entry views to arrays to avoid StrictMode failures when sort output is a single object.
$clientUserFeedbackTopEntriesArray = @()
$hostUserFeedbackTopEntriesArray = @()
if ($clientUserFeedbackTopEntries.Count -gt 0) {
  $clientUserFeedbackTopEntriesArray = @($clientUserFeedbackTopEntries | Sort-Object -Property totalUs -Descending)
}
if ($hostUserFeedbackTopEntries.Count -gt 0) {
  $hostUserFeedbackTopEntriesArray = @($hostUserFeedbackTopEntries | Sort-Object -Property pipeUs -Descending)
}

$bottleneckStage = "N/A"
$bottleneckAvgUs = 0
foreach ($stageName in @("c2eUs", "encUs", "e2sUs", "netUs", "r2dUs", "decUs", "decodeToQueueUs", "queueWaitUs", "paintUs", "uploadYUs", "uploadUVUs", "drawUs", "presentBlockUs", "d2pUs")) {
  $s = $stageStats[$stageName]
  if ($s.count -gt 0 -and $s.avg -ge $bottleneckAvgUs) {
    $bottleneckStage = $stageName
    $bottleneckAvgUs = $s.avg
  }
}
$clientUserFeedbackBottleneckStage = "N/A"
$clientUserFeedbackBottleneckAvgUs = 0
foreach ($stageName in @("totalUs", "capGapUs", "queueToPaintUs", "queueToPresentUs", "decodeToQueueUs", "queueWaitUs", "paintUs", "presentBlockUs", "d2pUs", "r2dUs", "decUs", "netUs", "c2eUs", "encUs", "e2sUs")) {
  $s = $clientUserFeedbackStats[$stageName]
  if ($s.count -gt 0 -and $s.avg -ge $clientUserFeedbackBottleneckAvgUs) {
    $clientUserFeedbackBottleneckStage = $stageName
    $clientUserFeedbackBottleneckAvgUs = $s.avg
  }
}
$hostUserFeedbackBottleneckStage = "N/A"
$hostUserFeedbackBottleneckAvgUs = 0
foreach ($stageName in @(
  "pipeUs",
  "captureToCallbackUs",
  "callbackIntervalUs",
  "captureIntervalUs",
  "preEncodePrepUs",
  "scaleUs",
  "nv12Us",
  "selectWaitUs",
  "queueSelectWaitUs",
  "queueToEncodeUs",
  "queueToSendUs",
  "captureToQueueUs",
  "captureClockSkewUs",
  "queueWaitUs",
  "queueDepth",
  "queueDepthMax",
  "sendDurUs",
  "sendWaitUs",
  "sendToEncodeUs",
  "sendIntervalUs",
  "sendIntervalErrUs",
  "sendDoneUs",
  "sendHeaderUs",
  "sendPayloadUs",
  "queueGapFrames",
  "tickWaitUs",
  "c2eUs",
  "encQueueAlignedUs",
  "captureToAuUs",
  "captureToAuTimelineSkewUs",
  "callbackToSendGapUs",
  "cb2sUs",
  "cb2eUs",
  "capAgeUs",
  "encUs",
  "e2sUs"
)) {
  $s = $hostUserFeedbackStats[$stageName]
  if ($s.count -gt 0 -and $s.avg -ge $hostUserFeedbackBottleneckAvgUs) {
    $hostUserFeedbackBottleneckStage = $stageName
    $hostUserFeedbackBottleneckAvgUs = $s.avg
  }
}

$decodedOk = ($Codec -ine "h264" -or $dec.max -gt 0)
$overallOk = ($clientRc -eq 0 -and $lat.count -gt 0 -and $decodedOk)
$gateADecodedFpsTarget = [Math]::Max(20, [int][Math]::Floor([Math]::Max(1, $Fps) * 0.90))
$gateARecoveryMaxTargetUs = 1000000
$gateAPresentGapOver1sTarget = 0
$gateADecodedFpsOk = ($dec.count -gt 0 -and [double]$dec.avg -ge [double]$gateADecodedFpsTarget)
$gateARecoveryOk = ($congestionRecoveryMaxUsLast -le $gateARecoveryMaxTargetUs)
$gateAPresentGapOk = ($presentGapOver1s -le $gateAPresentGapOver1sTarget)
$gateAFreezeOk = ($gateARecoveryOk -and $gateAPresentGapOk)
$gateAPass = ($clientRc -eq 0 -and $gateADecodedFpsOk -and $gateAFreezeOk)

$m7Profile = "none"
$m7DecodedFpsTarget = 0
$m7LatencyP95TargetUs = 0
if ($Codec -ieq "h264") {
  if ($EncodeWidth -ge 1920 -or $EncodeHeight -ge 1080) {
    $m7Profile = "1080p$($Fps)"
    $m7DecodedFpsTarget = [Math]::Max(20, [int][Math]::Floor([Math]::Max(1, $Fps) * 0.90))
    $m7LatencyP95TargetUs = 70000
  } elseif ($EncodeWidth -ge 1280 -or $EncodeHeight -ge 720) {
    $m7Profile = "720p$($Fps)"
    $m7DecodedFpsTarget = [Math]::Max(20, [int][Math]::Floor([Math]::Max(1, $Fps) * 0.90))
    $m7LatencyP95TargetUs = 55000
  }
}
$m7Applicable = ($m7DecodedFpsTarget -gt 0 -and $m7LatencyP95TargetUs -gt 0)
$m7DecodedFpsOk = (-not $m7Applicable -or ($dec.count -gt 0 -and [double]$dec.avg -ge [double]$m7DecodedFpsTarget))
$m7LatencyP95Ok = (-not $m7Applicable -or ($lat.count -gt 0 -and [double]$lat.p95 -le [double]$m7LatencyP95TargetUs))
$m7PresentGapOk = ($presentGapOver1s -le 0)
$m7Pass = ($overallOk -and $m7Applicable -and $m7DecodedFpsOk -and $m7LatencyP95Ok -and $m7PresentGapOk)
$captureInputExpectedPushMin = [Math]::Max(30, [int][Math]::Floor([double]([Math]::Max(1, $HostSeconds) * [Math]::Max(1, $Fps)) * 0.35))
$captureInputEffectivePushCount = [int64]$queuePushCount + [int64]$syntheticKeepaliveTotal
$captureInputStallReasons = New-Object System.Collections.Generic.List[string]
if ($captureInputEffectivePushCount -gt 0 -and $captureInputEffectivePushCount -lt $captureInputExpectedPushMin) {
  [void]$captureInputStallReasons.Add("queue_push_low")
}
if ($mb.count -gt 0 -and [double]$mb.avg -le 0.05) {
  [void]$captureInputStallReasons.Add("mbps_near_zero")
}
$captureInputStallDetected = ($captureInputStallReasons.Count -gt 0)
$captureInputStallReason = if ($captureInputStallReasons.Count -gt 0) {
  [string]::Join(",", $captureInputStallReasons.ToArray())
} else {
  "none"
}
$gateACaptureSourceMonitorOk = [string]::IsNullOrWhiteSpace($hostCaptureSourceLast) -or
  $hostCaptureSourceLast.Contains("MonitorFromWindow") -or
  $hostCaptureSourceLast.Contains("MonitorFromPoint") -or
  $hostCaptureSourceLast.Contains("EnumDisplayMonitors")
$gateAShellWindowFallbackDetected = (-not [string]::IsNullOrWhiteSpace($hostCaptureSourceLast)) -and
  $hostCaptureSourceLast.Contains("GetShellWindow")
$iconGreen = ([char]0xD83D) + ([char]0xDFE2)
$iconOrange = ([char]0xD83D) + ([char]0xDFE0)
$iconRedX = [string][char]0x274C
$m7Status = "AMBIGUOUS"
$m7StatusIcon = $iconOrange
$m7StatusReasons = New-Object System.Collections.Generic.List[string]
if (-not $overallOk) {
  $m7Status = "FAIL"
  $m7StatusIcon = $iconRedX
  [void]$m7StatusReasons.Add("overall_not_ok")
} else {
  if ($captureInputStallDetected) {
    $m7Status = "FAIL"
    $m7StatusIcon = $iconRedX
    [void]$m7StatusReasons.Add("capture_input_stall")
    [void]$m7StatusReasons.Add($captureInputStallReason)
  } elseif ($GateAProfile -and $gateAShellWindowFallbackDetected -and $dec.count -eq 0) {
    $m7Status = "FAIL"
    $m7StatusIcon = $iconRedX
    [void]$m7StatusReasons.Add("capture_source_shellwindow")
  } else {
    if (-not $m7Applicable) {
      [void]$m7StatusReasons.Add("profile_not_applicable")
    }
    if (-not $m7DecodedFpsOk) {
      [void]$m7StatusReasons.Add("decoded_fps_below_target")
    }
    if (-not $m7LatencyP95Ok) {
      [void]$m7StatusReasons.Add("latency_p95_above_target")
    }
    if (-not $m7PresentGapOk) {
      [void]$m7StatusReasons.Add("present_gap_over_1s")
    }
    if ($m7Pass) {
      $m7Status = "SUCCESS"
      $m7StatusIcon = $iconGreen
      $m7StatusReasons.Clear()
      [void]$m7StatusReasons.Add("ok")
    } elseif ($presentGapOver3s -gt 0 -or ($m7Applicable -and $lat.count -gt 0 -and [double]$lat.p95 -gt ([double]$m7LatencyP95TargetUs * 1.5))) {
      $m7Status = "FAIL"
      $m7StatusIcon = $iconRedX
    }
  }
}
$m7StatusReason = if ($m7StatusReasons.Count -gt 0) { [string]::Join(",", $m7StatusReasons.ToArray()) } else { "ok" }

if ($GateAProfile -and $gateAShellWindowFallbackDetected -and $dec.count -eq 0 -and $GateAProfileRetryCount -lt 2) {
  Write-Output "GATE_A_PROFILE_RETRYING=True"
  Write-Output "GATE_A_PROFILE_RETRY_COUNT=$GateAProfileRetryCount"
  Write-Output "HOST_CAPTURE_SOURCE_LAST=$hostCaptureSourceLast"
  & $PSCommandPath `
    -Root $Root `
    -RemoteHost $RemoteHost `
    -Port $Port `
    -ControlPort $ControlPort `
    -Transport $Transport `
    -UdpMtu $UdpMtu `
    -Fps $Fps `
    -Codec $Codec `
    -Bitrate $Bitrate `
    -Keyint $Keyint `
    -EncodeWidth $EncodeWidth `
    -EncodeHeight $EncodeHeight `
    -EncoderBackend $EncoderBackend `
    -DecoderBackend $DecoderBackend `
    -FpsHint $FpsHint `
    -BuildDir $BuildDir `
    -Configuration $Configuration `
    -HostSeconds $HostSeconds `
    -ClientSeconds $ClientSeconds `
    -TraceEvery $TraceEvery `
    -TraceMax $TraceMax `
    -GateAProfile `
    -GateAProfileRetryCount ($GateAProfileRetryCount + 1) `
    -NoInputChannel:$NoInputChannel.IsPresent
  exit $LASTEXITCODE
}

$gitCommit = ""
try { $gitCommit = (& git -C $Root rev-parse --short HEAD 2>$null | Select-Object -First 1) } catch {}
$logicalCores = [Environment]::ProcessorCount
$hostCpuSingleCorePct = [Math]::Round(100.0 * $hostCpuSeconds / $cpuSampleSeconds, 2)
$clientCpuSingleCorePct = [Math]::Round(100.0 * $clientCpuSeconds / $cpuSampleSeconds, 2)

# Machine-readable record of what exactly ran; numbers without this are not comparable.
$runMetadata = [ordered]@{
  timestamp = $ts
  gitCommit = "$gitCommit"
  configuration = $Configuration
  buildDir = $resolvedBuildDir
  os = [System.Environment]::OSVersion.VersionString
  logicalCores = $logicalCores
  codec = $Codec
  transport = $effectiveTransport
  port = $Port
  controlPort = $ControlPort
  fps = $Fps
  bitrate = $Bitrate
  keyint = $Keyint
  encodeWidth = $EncodeWidth
  encodeHeight = $EncodeHeight
  encoderBackend = $EncoderBackend
  decoderBackend = $DecoderBackend
  hostSeconds = $HostSeconds
  clientSeconds = $ClientSeconds
  gateAProfile = $GateAProfile.IsPresent
  hostPid = $hostProc.Id
  clientPid = $clientProc.Id
}
try {
  $runMetadata | ConvertTo-Json | Out-File -FilePath (Join-Path $logDir "run-metadata.json") -Encoding utf8
} catch {}

Write-Output "LOG_DIR=$logDir"
Write-Output "CONFIGURATION=$Configuration"
Write-Output "GIT_COMMIT=$gitCommit"
Write-Output "HOST_CPU_SECONDS=$([Math]::Round($hostCpuSeconds, 3))"
Write-Output "HOST_CPU_SINGLE_CORE_PCT=$hostCpuSingleCorePct"
Write-Output "HOST_PEAK_WS_MB=$([Math]::Round($hostPeakWsBytes / 1MB, 1))"
Write-Output "CLIENT_CPU_SECONDS=$([Math]::Round($clientCpuSeconds, 3))"
Write-Output "CLIENT_CPU_SINGLE_CORE_PCT=$clientCpuSingleCorePct"
Write-Output "CLIENT_PEAK_WS_MB=$([Math]::Round($clientPeakWsBytes / 1MB, 1))"
Write-Output "CPU_SAMPLE_SECONDS=$([Math]::Round($cpuSampleSeconds, 3))"
Write-Output "CODEC=$Codec"
Write-Output "TRANSPORT=$effectiveTransport"
Write-Output "GATE_A_PROFILE_APPLIED=$($GateAProfile.IsPresent)"
Write-Output "GATE_A_PROFILE_H264_NO_PACING=$(if ($env:REMOTE60_NATIVE_H264_NO_PACING) { $env:REMOTE60_NATIVE_H264_NO_PACING } else { 0 })"
Write-Output "GATE_A_PROFILE_FRAME_GATING_DISABLE=$(if ($env:REMOTE60_NATIVE_FRAME_GATING_DISABLE) { $env:REMOTE60_NATIVE_FRAME_GATING_DISABLE } else { 0 })"
Write-Output "GATE_A_PROFILE_ABR_DISABLE=$(if ($env:REMOTE60_NATIVE_ABR_DISABLE) { $env:REMOTE60_NATIVE_ABR_DISABLE } else { 0 })"
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
Write-Output "DECODE_ZERO_STREAK_MAX_SEC=$decodeZeroStreakMaxSec"
Write-Output "DECODE_RECOVERY_COUNT=$($decodeRecoverySec.count)"
Write-Output "DECODE_RECOVERY_AVG_SEC=$($decodeRecoverySec.avg)"
Write-Output "DECODE_RECOVERY_P95_SEC=$($decodeRecoverySec.p95)"
Write-Output "DECODE_RECOVERY_MAX_SEC=$($decodeRecoverySec.max)"
Write-Output "PRESENT_GAP_COUNT=$($presentGap.count)"
Write-Output "PRESENT_GAP_AVG_US=$($presentGap.avg)"
Write-Output "PRESENT_GAP_P95_US=$($presentGap.p95)"
Write-Output "PRESENT_GAP_MAX_US=$($presentGap.max)"
Write-Output "PRESENT_GAP_OVER_1S=$presentGapOver1s"
Write-Output "PRESENT_GAP_OVER_3S=$presentGapOver3s"
Write-Output "CONGESTION_STATE_LAST=$congestionStateLast"
Write-Output "CONGESTION_TRANSITION_EVENTS=$congestionTransitionEvents"
Write-Output "CONGESTION_TRANSITIONS_LAST=$congestionTransitionsLast"
Write-Output "CONGESTION_RECOVERY_COUNT_LAST=$congestionRecoveryCountLast"
Write-Output "CONGESTION_RECOVERY_AVG_US_LAST=$congestionRecoveryAvgUsLast"
Write-Output "CONGESTION_RECOVERY_MAX_US_LAST=$congestionRecoveryMaxUsLast"
Write-Output "CONGESTION_RECOVERY_REQ_LAST=$congestionRecoveryReqLast"
Write-Output "CONGESTION_STALE_DROPS_LAST=$congestionStaleDropsLast"
Write-Output "CONGESTION_HOLD_LATEST_DROPS_LAST=$congestionHoldLatestDropsLast"
Write-Output "CONGESTION_BURST_DROPS_LAST=$congestionBurstDropsLast"
Write-Output "QUEUE_DEPTH_SAMPLES_LAST=$queueDepthSamplesLast"
Write-Output "QUEUE_DEPTH_MAX_LAST=$queueDepthMaxLast"
Write-Output "QUEUE_DEPTH_H0_LAST=$queueDepthH0Last"
Write-Output "QUEUE_DEPTH_H1_LAST=$queueDepthH1Last"
Write-Output "QUEUE_DEPTH_H2_LAST=$queueDepthH2Last"
Write-Output "QUEUE_DEPTH_H3_LAST=$queueDepthH3Last"
Write-Output "QUEUE_DEPTH_H4P_LAST=$queueDepthH4pLast"
Write-Output "GATE_A_TARGET_DECODED_FPS=$gateADecodedFpsTarget"
Write-Output "GATE_A_TARGET_RECOVERY_MAX_US=$gateARecoveryMaxTargetUs"
Write-Output "GATE_A_TARGET_PRESENT_GAP_OVER_1S=$gateAPresentGapOver1sTarget"
Write-Output "GATE_A_DECODED_FPS_OK=$gateADecodedFpsOk"
Write-Output "GATE_A_RECOVERY_OK=$gateARecoveryOk"
Write-Output "GATE_A_PRESENT_GAP_OK=$gateAPresentGapOk"
Write-Output "GATE_A_FREEZE_OK=$gateAFreezeOk"
Write-Output "GATE_A_PASS=$gateAPass"
Write-Output "M7_PROFILE=$m7Profile"
Write-Output "M7_TARGET_DECODED_FPS=$m7DecodedFpsTarget"
Write-Output "M7_TARGET_LAT_P95_US=$m7LatencyP95TargetUs"
Write-Output "M7_DECODED_FPS_OK=$m7DecodedFpsOk"
Write-Output "M7_LAT_P95_OK=$m7LatencyP95Ok"
Write-Output "M7_PRESENT_GAP_OK=$m7PresentGapOk"
Write-Output "M7_PASS=$m7Pass"
Write-Output "M7_STATUS=$m7Status"
Write-Output "M7_STATUS_ICON=$m7StatusIcon"
Write-Output "M7_STATUS_REASON=$m7StatusReason"
Write-Output "CAPTURE_INPUT_STALL_DETECTED=$captureInputStallDetected"
Write-Output "CAPTURE_INPUT_STALL_REASON=$captureInputStallReason"
Write-Output "HOST_QUEUE_PUSH_EXPECTED_MIN=$captureInputExpectedPushMin"
Write-Output "HOST_CAPTURE_EFFECTIVE_PUSH_COUNT=$captureInputEffectivePushCount"
Write-Output "HOST_CAPTURE_SOURCE_LAST=$hostCaptureSourceLast"
Write-Output "GATE_A_CAPTURE_SOURCE_MONITOR_OK=$gateACaptureSourceMonitorOk"
Write-Output "GATE_A_SHELLWINDOW_FALLBACK_DETECTED=$gateAShellWindowFallbackDetected"
Write-Output "TS_SOURCE_MFT=$tsSourceMft"
Write-Output "TS_SOURCE_INPUT_FALLBACK=$tsSourceInputFallback"
Write-Output "TS_SOURCE_HEADER_FALLBACK=$tsSourceHeaderFallback"
Write-Output "ABR_SWITCH_COUNT=$abrSwitchCount"
Write-Output "ABR_TO_HIGH_COUNT=$abrToHighCount"
Write-Output "ABR_TO_MID_COUNT=$abrToMidCount"
Write-Output "ABR_TO_LOW_COUNT=$abrToLowCount"
Write-Output "ABR_LAST_PROFILE=$abrLastProfile"
Write-Output "M9_EVENT_COUNT=$m9EventCount"
Write-Output "M9_LAST_MODE=$m9LastMode"
Write-Output "M9_LAST_ACTION=$m9LastAction"
Write-Output "M9_LAST_FROM_LEVEL=$m9LastFromLevel"
Write-Output "M9_LAST_TO_LEVEL=$m9LastToLevel"
Write-Output "M9_APPLY_DEFERRED_COUNT=$m9ApplyDeferredCount"
Write-Output "KEYREQ_CLIENT_SENT=$keyReqClientSent"
Write-Output "KEYREQ_HOST_RECV=$keyReqHostRecv"
Write-Output "KEYREQ_HOST_CONSUMED=$keyReqHostConsumed"
Write-Output "UDP_ASSEMBLY_SAMPLE_COUNT=$udpAssemblySampleCount"
Write-Output "UDP_ASSEMBLY_CHUNKS_TOTAL=$udpAssemblyChunksTotal"
Write-Output "UDP_ASSEMBLY_COMPLETED_TOTAL=$udpAssemblyCompletedTotal"
Write-Output "UDP_ASSEMBLY_DROPPED_TOTAL=$udpAssemblyDroppedTotal"
Write-Output "UDP_ASSEMBLY_MALFORMED_TOTAL=$udpAssemblyMalformedTotal"
Write-Output "UDP_ASSEMBLY_REORDER_TOTAL=$udpAssemblyReorderTotal"
Write-Output "UDP_ASSEMBLY_KEYREQ_TOTAL=$udpAssemblyKeyReqTotal"
Write-Output "UDP_ASSEMBLY_DROP_PM_COUNT=$($udpAssemblyDropPm.count)"
Write-Output "UDP_ASSEMBLY_DROP_PM_AVG=$($udpAssemblyDropPm.avg)"
Write-Output "UDP_ASSEMBLY_DROP_PM_P95=$($udpAssemblyDropPm.p95)"
Write-Output "UDP_ASSEMBLY_DROP_PM_MAX=$($udpAssemblyDropPm.max)"
Write-Output "UDP_SIM_DROP_TOTAL=$udpSimDropTotal"
Write-Output "UDP_SIM_DROP_PM_COUNT=$($udpSimDropPm.count)"
Write-Output "UDP_SIM_DROP_PM_AVG=$($udpSimDropPm.avg)"
Write-Output "UDP_SIM_DROP_PM_P95=$($udpSimDropPm.p95)"
Write-Output "UDP_SIM_DROP_PM_MAX=$($udpSimDropPm.max)"
Write-Output "HOST_QUEUE_WAIT_TIMEOUT_COUNT=$queueWaitTimeoutCount"
Write-Output "HOST_QUEUE_WAIT_NOWORK_COUNT=$queueWaitNoWorkCount"
Write-Output "HOST_QUEUE_PUSH_COUNT=$queuePushCount"
Write-Output "HOST_SYNTHETIC_KEEPALIVE_TOTAL=$syntheticKeepaliveTotal"
Write-Output "HOST_QUEUE_POP_COUNT=$queuePopCount"
Write-Output "HOST_QUEUE_SKIPPED_BY_OVERWRITE=$hostSkippedByOverwrite"
Write-Output "HOST_CAPTURE_INTERVAL_COUNT=$($hostCaptureIntervalStats.count)"
Write-Output "HOST_CAPTURE_INTERVAL_TARGET_US=$hostCaptureIntervalTargetUs"
Write-Output "HOST_CAPTURE_INTERVAL_AVG_US=$($hostCaptureIntervalStats.avg)"
Write-Output "HOST_CAPTURE_INTERVAL_P95_US=$($hostCaptureIntervalStats.p95)"
Write-Output "HOST_CAPTURE_INTERVAL_MAX_US=$($hostCaptureIntervalStats.max)"
Write-Output "HOST_CAPTURE_INTERVAL_ERR_COUNT=$($hostCaptureIntervalErrStats.count)"
Write-Output "HOST_CAPTURE_INTERVAL_ERR_AVG_US=$($hostCaptureIntervalErrStats.avg)"
Write-Output "HOST_CALLBACK_INTERVAL_COUNT=$($hostCallbackIntervalStats.count)"
Write-Output "HOST_CALLBACK_INTERVAL_TARGET_US=$hostCaptureIntervalTargetUs"
Write-Output "HOST_CALLBACK_INTERVAL_AVG_US=$($hostCallbackIntervalStats.avg)"
Write-Output "HOST_CALLBACK_INTERVAL_P95_US=$($hostCallbackIntervalStats.p95)"
Write-Output "HOST_CALLBACK_INTERVAL_MAX_US=$($hostCallbackIntervalStats.max)"
Write-Output "HOST_CALLBACK_INTERVAL_ERR_COUNT=$($hostCallbackIntervalErrStats.count)"
Write-Output "HOST_CALLBACK_INTERVAL_ERR_AVG_US=$($hostCallbackIntervalErrStats.avg)"
$hostWaitReasonTotal = $queueWaitReason0Count + $queueWaitReason1Count + $queueWaitReason2Count
$hostBottleneckStageTotal = $hostBottleneckStageCode0Count + $hostBottleneckStageCode1Count + $hostBottleneckStageCode2Count + $hostBottleneckStageCode3Count + $hostBottleneckStageCode4Count + $hostBottleneckStageCode5Count + $hostBottleneckStageCode6Count + $hostBottleneckStageCode7Count + $hostBottleneckStageCode8Count + $hostBottleneckStageCode9Count
$encApiPathCodeTotal = $encApiPathCode0Count + $encApiPathCode1Count + $encApiPathCode2Count + $encApiPathCode3Count + $encApiPathCode4Count + $encApiPathCode5Count + $encApiPathCode6Count
Write-Output "HOST_QUEUE_WAIT_REASON_0_COUNT=$queueWaitReason0Count"
Write-Output "HOST_QUEUE_WAIT_REASON_1_COUNT=$queueWaitReason1Count"
Write-Output "HOST_QUEUE_WAIT_REASON_2_COUNT=$queueWaitReason2Count"
Write-Output "HOST_QUEUE_WAIT_REASON_TOTAL=$hostWaitReasonTotal"
Write-Output "HOST_BOTTLENECK_STAGE_CODE_0_NONE_COUNT=$hostBottleneckStageCode0Count"
Write-Output "HOST_BOTTLENECK_STAGE_CODE_1_QUEUE_WAIT_COUNT=$hostBottleneckStageCode1Count"
Write-Output "HOST_BOTTLENECK_STAGE_CODE_2_QUEUE_TO_ENCODE_COUNT=$hostBottleneckStageCode2Count"
Write-Output "HOST_BOTTLENECK_STAGE_CODE_3_PRE_ENCODE_PREP_COUNT=$hostBottleneckStageCode3Count"
Write-Output "HOST_BOTTLENECK_STAGE_CODE_4_SCALE_COUNT=$hostBottleneckStageCode4Count"
Write-Output "HOST_BOTTLENECK_STAGE_CODE_5_BGRA_TO_NV12_COUNT=$hostBottleneckStageCode5Count"
Write-Output "HOST_BOTTLENECK_STAGE_CODE_6_ENCODER_COUNT=$hostBottleneckStageCode6Count"
Write-Output "HOST_BOTTLENECK_STAGE_CODE_7_QUEUE_TO_SEND_COUNT=$hostBottleneckStageCode7Count"
Write-Output "HOST_BOTTLENECK_STAGE_CODE_8_SEND_IO_COUNT=$hostBottleneckStageCode8Count"
Write-Output "HOST_BOTTLENECK_STAGE_CODE_9_SEND_INTERVAL_JITTER_COUNT=$hostBottleneckStageCode9Count"
Write-Output "HOST_BOTTLENECK_STAGE_CODE_TOTAL=$hostBottleneckStageTotal"
Write-Output "HOST_ENC_API_PATH_CODE_0_UNKNOWN_COUNT=$encApiPathCode0Count"
Write-Output "HOST_ENC_API_PATH_CODE_1_AMF_COUNT=$encApiPathCode1Count"
Write-Output "HOST_ENC_API_PATH_CODE_2_NVENC_COUNT=$encApiPathCode2Count"
Write-Output "HOST_ENC_API_PATH_CODE_3_QSV_COUNT=$encApiPathCode3Count"
Write-Output "HOST_ENC_API_PATH_CODE_4_MFT_COUNT=$encApiPathCode4Count"
Write-Output "HOST_ENC_API_PATH_CODE_5_CLSID_COUNT=$encApiPathCode5Count"
Write-Output "HOST_ENC_API_PATH_CODE_6_OTHER_COUNT=$encApiPathCode6Count"
Write-Output "HOST_ENC_API_PATH_CODE_TOTAL=$encApiPathCodeTotal"
Write-Output "D3D_PRESENT_SUCCESS_TOTAL=$d3dPresentSuccessTotal"
Write-Output "D3D_PRESENT_FAIL_TOTAL=$d3dPresentFailTotal"
Write-Output "GDI_FALLBACK_PRESENTED_TOTAL=$gdiFallbackPresentedTotal"
Write-Output "GDI_FALLBACK_RATE_X1000_COUNT=$($gdiFallbackRate.count)"
Write-Output "GDI_FALLBACK_RATE_X1000_AVG=$($gdiFallbackRate.avg)"
Write-Output "GDI_FALLBACK_RATE_X1000_P95=$($gdiFallbackRate.p95)"
Write-Output "GDI_FALLBACK_RATE_X1000_MAX=$($gdiFallbackRate.max)"
Write-Output "FALLBACK_INIT_FAIL_TOTAL=$fallbackInitFailTotal"
Write-Output "FALLBACK_RENDER_FAIL_TOTAL=$fallbackRenderFailTotal"
Write-Output "FALLBACK_NV12_TO_BGRA_FAIL_TOTAL=$fallbackNv12ConvertFailTotal"
Write-Output "PAINT_COALESCED_TOTAL=$paintCoalescedTotal"
Write-Output "OVERWRITE_BEFORE_PRESENT_TOTAL=$overwriteBeforePresentTotal"
Write-Output "RENDER_PATH_D3D_NV12_COUNT=$renderPathD3dNv12Count"
Write-Output "RENDER_PATH_D3D_NV12_SURFACE_COUNT=$renderPathD3dNv12SurfaceCount"
Write-Output "RENDER_PATH_GDI_NV12_FALLBACK_COUNT=$renderPathGdiNv12FallbackCount"
Write-Output "RENDER_PATH_GDI_BGRA_COUNT=$renderPathGdiBgraCount"
Write-Output "RENDER_PATH_OTHER_COUNT=$renderPathOtherCount"
Write-Output "FALLBACK_REASON_NONE_COUNT=$fallbackReasonNoneCount"
Write-Output "FALLBACK_REASON_D3D_INIT_FAIL_COUNT=$fallbackReasonD3dInitFailCount"
Write-Output "FALLBACK_REASON_NV12_TO_BGRA_FAIL_COUNT=$fallbackReasonNv12ToBgraFailCount"
Write-Output "FALLBACK_REASON_OTHER_COUNT=$fallbackReasonOtherCount"
Write-Output "MBPS_COUNT=$($mb.count)"
Write-Output "MBPS_AVG=$($mb.avg)"
Write-Output "MBPS_P95=$($mb.p95)"
Write-Output "MBPS_MAX=$($mb.max)"
Write-Output "DECODED_RAW_MBPS_COUNT=$($decRaw.count)"
Write-Output "DECODED_RAW_MBPS_AVG=$($decRaw.avg)"
Write-Output "DECODED_RAW_MBPS_P95=$($decRaw.p95)"
Write-Output "DECODED_RAW_MBPS_MAX=$($decRaw.max)"
Write-Output "DECODE_RATIO_X100_COUNT=$($decRatio.count)"
Write-Output "DECODE_RATIO_X100_AVG=$($decRatio.avg)"
Write-Output "DECODE_RATIO_X100_P95=$($decRatio.p95)"
Write-Output "DECODE_RATIO_X100_MAX=$($decRatio.max)"
Write-Output "ENC_RAW_EQUIV_MBPS_COUNT=$($encRaw.count)"
Write-Output "ENC_RAW_EQUIV_MBPS_AVG=$($encRaw.avg)"
Write-Output "ENC_RAW_EQUIV_MBPS_P95=$($encRaw.p95)"
Write-Output "ENC_RAW_EQUIV_MBPS_MAX=$($encRaw.max)"
Write-Output "ENC_RATIO_X100_COUNT=$($encRatio.count)"
Write-Output "ENC_RATIO_X100_AVG=$($encRatio.avg)"
Write-Output "ENC_RATIO_X100_P95=$($encRatio.p95)"
Write-Output "ENC_RATIO_X100_MAX=$($encRatio.max)"
Write-Output "GPU_SCALE_ATTEMPTS_COUNT=$($gpuScaleAttempts.count)"
Write-Output "GPU_SCALE_ATTEMPTS_AVG=$($gpuScaleAttempts.avg)"
Write-Output "GPU_SCALE_ATTEMPTS_P95=$($gpuScaleAttempts.p95)"
Write-Output "GPU_SCALE_ATTEMPTS_MAX=$($gpuScaleAttempts.max)"
Write-Output "GPU_SCALE_SUCCESS_COUNT=$($gpuScaleSuccess.count)"
Write-Output "GPU_SCALE_SUCCESS_AVG=$($gpuScaleSuccess.avg)"
Write-Output "GPU_SCALE_SUCCESS_P95=$($gpuScaleSuccess.p95)"
Write-Output "GPU_SCALE_SUCCESS_MAX=$($gpuScaleSuccess.max)"
Write-Output "GPU_SCALE_FAIL_COUNT=$($gpuScaleFail.count)"
Write-Output "GPU_SCALE_FAIL_AVG=$($gpuScaleFail.avg)"
Write-Output "GPU_SCALE_FAIL_P95=$($gpuScaleFail.p95)"
Write-Output "GPU_SCALE_FAIL_MAX=$($gpuScaleFail.max)"
Write-Output "GPU_SCALE_CPU_FALLBACK_COUNT=$($gpuScaleCpuFallback.count)"
Write-Output "GPU_SCALE_CPU_FALLBACK_AVG=$($gpuScaleCpuFallback.avg)"
Write-Output "GPU_SCALE_CPU_FALLBACK_P95=$($gpuScaleCpuFallback.p95)"
Write-Output "GPU_SCALE_CPU_FALLBACK_MAX=$($gpuScaleCpuFallback.max)"
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
foreach ($stageName in $hostUserFeedbackStats.Keys) {
  $upper = "UF_H_" + $stageName.ToUpperInvariant()
  $s = $hostUserFeedbackStats[$stageName]
  Write-Output "USER_FEEDBACK_${upper}_COUNT=$($s.count)"
  Write-Output "USER_FEEDBACK_${upper}_AVG_US=$($s.avg)"
  Write-Output "USER_FEEDBACK_${upper}_P95_US=$($s.p95)"
  Write-Output "USER_FEEDBACK_${upper}_MAX_US=$($s.max)"
}
foreach ($stageName in $clientUserFeedbackStats.Keys) {
  $upper = "UF_C_" + $stageName.ToUpperInvariant()
  $s = $clientUserFeedbackStats[$stageName]
  Write-Output "USER_FEEDBACK_${upper}_COUNT=$($s.count)"
  Write-Output "USER_FEEDBACK_${upper}_AVG_US=$($s.avg)"
  Write-Output "USER_FEEDBACK_${upper}_P95_US=$($s.p95)"
  Write-Output "USER_FEEDBACK_${upper}_MAX_US=$($s.max)"
}
Write-Output "USER_FEEDBACK_HOST_BOTTLENECK_STAGE=$hostUserFeedbackBottleneckStage"
Write-Output "USER_FEEDBACK_HOST_BOTTLENECK_AVG_US=$hostUserFeedbackBottleneckAvgUs"
Write-Output "USER_FEEDBACK_CLIENT_BOTTLENECK_STAGE=$clientUserFeedbackBottleneckStage"
Write-Output "USER_FEEDBACK_CLIENT_BOTTLENECK_AVG_US=$clientUserFeedbackBottleneckAvgUs"
if ($hostUserFeedbackTopEntriesArray.Count -gt 0) {
  for ($i = 0; $i -lt [Math]::Min(3, $hostUserFeedbackTopEntriesArray.Count); $i++) {
    $entry = $hostUserFeedbackTopEntriesArray[$i]
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_SEQ=$($entry.seq)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_PIPE_US=$($entry.pipeUs)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_CALLBACK_INTERVAL_US=$($entry.callbackIntervalUs)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_CAPTURE_INTERVAL_US=$($entry.captureIntervalUs)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_CAPTURE_TO_AU_TIMELINE_SKEW_US=$($entry.captureToAuTimelineSkewUs)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_CAPTURE_TO_AU_TIMELINE_DELTA_US=$($entry.captureToAuTimelineDeltaUs)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_CAPTURE_TO_AU_SKEW_US=$($entry.captureToAuSkewUs)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_AU_TS_FROM_OUTPUT=$($entry.auTsFromOutput)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_AU_TS_SKEW_US=$($entry.auTsSkewUs)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_CAPTURE_TIMELINE_RELATIVE_US=$($entry.captureTimelineRelativeUs)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_AU_TIMELINE_RELATIVE_US=$($entry.auTimelineRelativeUs)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_CAPTURE_TIMELINE_ORIGIN_US=$($entry.captureTimelineOriginUs)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_AU_TIMELINE_ORIGIN_US=$($entry.auTimelineOriginUs)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_CAPTURE_TO_AU_US=$($entry.captureToAuUs)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_ENCODE_INPUT_US=$($entry.encodeInputUs)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_AU_CAPTURE_US=$($entry.auCaptureUs)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_CAPTURE_TO_QUEUE_US=$($entry.captureToQueueUs)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_CAPTURE_CLOCK_SKEW_US=$($entry.captureClockSkewUs)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_QUEUE_TO_ENCODE_US=$($entry.queueToEncodeUs)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_QUEUE_TO_SEND_US=$($entry.queueToSendUs)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_BOTTLENECK_STAGE_CODE=$($entry.bottleneckStageCode)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_BOTTLENECK_STAGE_US=$($entry.bottleneckStageUs)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_QUEUE_WAIT_REASON=$($entry.queueWaitReason)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_PRE_ENCODE_PREP_US=$($entry.preEncodePrepUs)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_SCALE_US=$($entry.scaleUs)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_NV12_US=$($entry.nv12Us)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_ENC_QUEUE_ALIGNED_US=$($entry.encQueueAlignedUs)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_SEND_CALL_COUNT=$($entry.sendCallCount)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_SEND_HEADER_US=$($entry.sendHeaderUs)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_SEND_PAYLOAD_US=$($entry.sendPayloadUs)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_SEND_HEADER_CALL_COUNT=$($entry.sendHeaderCallCount)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_SEND_PAYLOAD_CALL_COUNT=$($entry.sendPayloadCallCount)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_SEND_INTERVAL_US=$($entry.sendIntervalUs)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_SEND_INTERVAL_ERR_US=$($entry.sendIntervalErrUs)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_SEND_CHUNK_COUNT=$($entry.sendChunkCount)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_SEND_CHUNK_MAX_US=$($entry.sendChunkMaxUs)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_SEND_WAIT_US=$($entry.sendWaitUs)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_SEND_TO_ENCODE_US=$($entry.sendToEncodeUs)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_TICK_WAIT_US=$($entry.tickWaitUs)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_ENC_API_PATH_CODE=$($entry.encApiPathCode)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_ENC_API_HW=$($entry.encApiHw)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_ENC_API_INPUT_US=$($entry.encApiInputUs)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_ENC_API_DRAIN_US=$($entry.encApiDrainUs)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_ENC_API_NOT_ACCEPTING_COUNT=$($entry.encApiNotAcceptingCount)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_ENC_API_NEED_MORE_INPUT_COUNT=$($entry.encApiNeedMoreInputCount)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_ENC_API_STREAM_CHANGE_COUNT=$($entry.encApiStreamChangeCount)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_ENC_API_OUTPUT_ERROR_COUNT=$($entry.encApiOutputErrorCount)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_ENC_API_ASYNC_ENABLED=$($entry.encApiAsyncEnabled)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_ENC_API_ASYNC_POLL_COUNT=$($entry.encApiAsyncPollCount)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_ENC_API_ASYNC_NO_EVENT_COUNT=$($entry.encApiAsyncNoEventCount)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_ENC_API_ASYNC_NEED_INPUT_COUNT=$($entry.encApiAsyncNeedInputCount)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_ENC_API_ASYNC_HAVE_OUTPUT_COUNT=$($entry.encApiAsyncHaveOutputCount)"
  }
}
if ($clientUserFeedbackTopEntriesArray.Count -gt 0) {
  for ($i = 0; $i -lt [Math]::Min(3, $clientUserFeedbackTopEntriesArray.Count); $i++) {
    $entry = $clientUserFeedbackTopEntriesArray[$i]
    Write-Output "USER_FEEDBACK_CLIENT_TOP${i}_SEQ=$($entry.seq)"
    Write-Output "USER_FEEDBACK_CLIENT_TOP${i}_TOTAL_US=$($entry.totalUs)"
  }
}
Write-Output "BOTTLENECK_STAGE=$bottleneckStage"
Write-Output "BOTTLENECK_AVG_US=$bottleneckAvgUs"
Write-Output "OVERALL_OK=$overallOk"
