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
$decodedRawMbpsVals = New-Object System.Collections.Generic.List[double]
$decodeRatioVals = New-Object System.Collections.Generic.List[double]
$rawEquivMbpsVals = New-Object System.Collections.Generic.List[double]
$encRatioVals = New-Object System.Collections.Generic.List[double]
$gpuScaleAttemptsVals = New-Object System.Collections.Generic.List[double]
$gpuScaleSuccessVals = New-Object System.Collections.Generic.List[double]
$gpuScaleFailVals = New-Object System.Collections.Generic.List[double]
$gpuScaleCpuFallbackVals = New-Object System.Collections.Generic.List[double]
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
$renderPathGdiNv12FallbackCount = 0
$renderPathGdiBgraCount = 0
$renderPathOtherCount = 0
$fallbackReasonNoneCount = 0
$fallbackReasonD3dInitFailCount = 0
$fallbackReasonNv12ToBgraFailCount = 0
$fallbackReasonOtherCount = 0
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
  selectWaitUs = (New-Object System.Collections.Generic.List[double])
  queueGapFrames = (New-Object System.Collections.Generic.List[double])
  c2eUs = (New-Object System.Collections.Generic.List[double])
  encQueueUs = (New-Object System.Collections.Generic.List[double])
  captureToAuUs = (New-Object System.Collections.Generic.List[double])
  callbackToSendGapUs = (New-Object System.Collections.Generic.List[double])
  cb2eUs = (New-Object System.Collections.Generic.List[double])
  capAgeUs = (New-Object System.Collections.Generic.List[double])
  encUs = (New-Object System.Collections.Generic.List[double])
  e2sUs = (New-Object System.Collections.Generic.List[double])
}
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
  if ($line -match 'decodedRawMbps=([0-9]+(?:\\.[0-9]+)?)') {
    [void]$decodedRawMbpsVals.Add([double]$Matches[1])
  }
  if ($line -match 'decodeRatioX100=([0-9]+)') {
    [void]$decodeRatioVals.Add([double]$Matches[1])
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
      if ($prevPresentUs -ge 0 -and $presentUs -ge $prevPresentUs) {
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
    if ($line -match 'renderPath=([A-Za-z0-9_]+)') {
      switch ($Matches[1]) {
        "d3d_nv12" { $renderPathD3dNv12Count += 1 }
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
}

foreach ($line in $hostLines) {
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
  if ($line -match '\[native-video-host\]\[control\] keyframe-request seq=') {
    $keyReqHostRecv += 1
  }
  if ($line -match '\[native-video-host\]\[control\] keyframe-request-consumed ') {
    $keyReqHostConsumed += 1
  }
if ($line -match '\[native-video-host\]\[user-feedback\]') {
  $seqVal = 0
  if ($line -match 'seq=([0-9]+)') {
    $seqVal = [int64]$Matches[1]
  }
    foreach ($stageName in $hostUserFeedbackValues.Keys) {
      if ($line -match ($stageName + '=([0-9]+)')) {
        [void]$hostUserFeedbackValues[$stageName].Add([double]$Matches[1])
      }
    }
    if ($line -match 'pipeUs=([0-9]+)') {
      [void]$hostUserFeedbackTopEntries.Add([PSCustomObject]@{
        seq = $seqVal
        pipeUs = [double]$Matches[1]
      })
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

if ($clientUserFeedbackTopEntries.Count -gt 0) {
  $clientUserFeedbackTopEntries = $clientUserFeedbackTopEntries | Sort-Object -Property totalUs -Descending
}
if ($hostUserFeedbackTopEntries.Count -gt 0) {
  $hostUserFeedbackTopEntries = $hostUserFeedbackTopEntries | Sort-Object -Property pipeUs -Descending
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
foreach ($stageName in @("pipeUs", "captureToCallbackUs", "selectWaitUs", "queueGapFrames", "c2eUs", "encQueueUs", "captureToAuUs", "callbackToSendGapUs", "cb2eUs", "capAgeUs", "encUs", "e2sUs")) {
  $s = $hostUserFeedbackStats[$stageName]
  if ($s.count -gt 0 -and $s.avg -ge $hostUserFeedbackBottleneckAvgUs) {
    $hostUserFeedbackBottleneckStage = $stageName
    $hostUserFeedbackBottleneckAvgUs = $s.avg
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
if ($hostUserFeedbackTopEntries.Count -gt 0) {
  for ($i = 0; $i -lt [Math]::Min(3, $hostUserFeedbackTopEntries.Count); $i++) {
    $entry = $hostUserFeedbackTopEntries[$i]
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_SEQ=$($entry.seq)"
    Write-Output "USER_FEEDBACK_HOST_TOP${i}_PIPE_US=$($entry.pipeUs)"
  }
}
if ($clientUserFeedbackTopEntries.Count -gt 0) {
  for ($i = 0; $i -lt [Math]::Min(3, $clientUserFeedbackTopEntries.Count); $i++) {
    $entry = $clientUserFeedbackTopEntries[$i]
    Write-Output "USER_FEEDBACK_CLIENT_TOP${i}_SEQ=$($entry.seq)"
    Write-Output "USER_FEEDBACK_CLIENT_TOP${i}_TOTAL_US=$($entry.totalUs)"
  }
}
Write-Output "BOTTLENECK_STAGE=$bottleneckStage"
Write-Output "BOTTLENECK_AVG_US=$bottleneckAvgUs"
Write-Output "OVERALL_OK=$overallOk"
