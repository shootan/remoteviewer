param(
  [Parameter(Mandatory = $true)]
  [ValidateSet("host", "client")]
  [string]$Role,
  [Parameter(Mandatory = $true)]
  [string]$ConfigPath,
  [string]$ExeDir = "",
  [string]$RemoteHost = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-IntValue {
  param($Obj, [string]$Name, [int]$Default)
  if ($null -eq $Obj) { return $Default }
  if ($Obj.PSObject.Properties.Name -contains $Name) {
    try { return [int]$Obj.$Name } catch { return $Default }
  }
  return $Default
}

function Get-StringValue {
  param($Obj, [string]$Name, [string]$Default)
  if ($null -eq $Obj) { return $Default }
  if ($Obj.PSObject.Properties.Name -contains $Name) {
    $v = [string]$Obj.$Name
    if (-not [string]::IsNullOrWhiteSpace($v)) { return $v }
  }
  return $Default
}

function Get-BoolValue {
  param($Obj, [string]$Name, [bool]$Default)
  if ($null -eq $Obj) { return $Default }
  if ($Obj.PSObject.Properties.Name -contains $Name) {
    try { return [bool]$Obj.$Name } catch { return $Default }
  }
  return $Default
}

$resolvedConfig = Resolve-Path -LiteralPath $ConfigPath
$cfg = Get-Content -LiteralPath $resolvedConfig -Raw | ConvertFrom-Json

if ([string]::IsNullOrWhiteSpace($ExeDir)) {
  $ExeDir = Split-Path -Parent $PSCommandPath
}
$exeDirResolved = (Resolve-Path -LiteralPath $ExeDir).Path

$codec = Get-StringValue $cfg "codec" "h264"
$transport = Get-StringValue $cfg "transport" ""
if ([string]::IsNullOrWhiteSpace($transport)) {
  if ($codec -ieq "h264") {
    $transport = "udp"
  } else {
    $transport = "tcp"
  }
}
$port = Get-IntValue $cfg "port" 43000
$controlPort = Get-IntValue $cfg "controlPort" 0
$udpMtu = Get-IntValue $cfg "udpMtu" 1200
$tcpSendBufKb = Get-IntValue $cfg "tcpSendBufKb" 0
$tcpRecvBufKb = Get-IntValue $cfg "tcpRecvBufKb" 0
$bitrate = Get-IntValue $cfg "bitrate" 1100000
$keyint = Get-IntValue $cfg "keyint" 15
$fps = Get-IntValue $cfg "fps" 60
$fpsHint = Get-IntValue $cfg "fpsHint" $fps
$encodeWidth = Get-IntValue $cfg "encodeWidth" 0
$encodeHeight = Get-IntValue $cfg "encodeHeight" 0
$seconds = Get-IntValue $cfg "seconds" 0
$noInputChannel = Get-BoolValue $cfg "noInputChannel" $true
$forceEncodedExp = Get-BoolValue $cfg "forceEncodedExperiment" $true
$encoderBackend = Get-StringValue $cfg "encoderBackend" ""
$decoderBackend = Get-StringValue $cfg "decoderBackend" ""
$h264NoPacing = Get-BoolValue $cfg "h264NoPacing" $false
$guardStalePreEncode = Get-BoolValue $cfg "guardStalePreEncode" $false
$capturePoolBuffers = Get-IntValue $cfg "capturePoolBuffers" 0
$guardStaleEncoded = Get-BoolValue $cfg "guardStaleEncoded" $false

# Ensure old shell env doesn't leak into this run.
@(
  "REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE",
  "REMOTE60_NATIVE_ENCODER_BACKEND",
  "REMOTE60_NATIVE_DECODER_BACKEND",
  "REMOTE60_NATIVE_H264_NO_PACING",
  "REMOTE60_NATIVE_GUARD_STALE_PREENCODE",
  "REMOTE60_NATIVE_CAPTURE_POOL_BUFFERS",
  "REMOTE60_NATIVE_GUARD_STALE_ENCODED"
) | ForEach-Object {
  Remove-Item ("Env:" + $_) -ErrorAction SilentlyContinue
}

if ($codec -ieq "h264" -and $forceEncodedExp) {
  $env:REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE = "1"
}
if (-not [string]::IsNullOrWhiteSpace($encoderBackend)) {
  $env:REMOTE60_NATIVE_ENCODER_BACKEND = $encoderBackend
}
if (-not [string]::IsNullOrWhiteSpace($decoderBackend)) {
  $env:REMOTE60_NATIVE_DECODER_BACKEND = $decoderBackend
}
if ($h264NoPacing) {
  $env:REMOTE60_NATIVE_H264_NO_PACING = "1"
}
if ($guardStalePreEncode) {
  $env:REMOTE60_NATIVE_GUARD_STALE_PREENCODE = "1"
}
if ($capturePoolBuffers -ge 1) {
  $env:REMOTE60_NATIVE_CAPTURE_POOL_BUFFERS = "$capturePoolBuffers"
}
if ($guardStaleEncoded) {
  $env:REMOTE60_NATIVE_GUARD_STALE_ENCODED = "1"
}

if ($Role -eq "host") {
  $exe = Join-Path $exeDirResolved "remote60_native_video_host_poc.exe"
  if (-not (Test-Path -LiteralPath $exe)) { throw "host exe not found: $exe" }

  $args = @("--bind-port", "$port", "--codec", $codec, "--fps", "$fps", "--transport", $transport)
  if ($udpMtu -gt 0) { $args += @("--udp-mtu", "$udpMtu") }
  if ($tcpSendBufKb -gt 0) { $args += @("--tcp-sendbuf-kb", "$tcpSendBufKb") }
  if ($seconds -gt 0) { $args += @("--seconds", "$seconds") }
  if ($controlPort -gt 0) { $args += @("--control-port", "$controlPort") }
  if ($codec -ieq "h264") {
    $args += @("--bitrate", "$bitrate", "--keyint", "$keyint")
    if ($encodeWidth -gt 0 -and $encodeHeight -gt 0) {
      $args += @("--encode-width", "$encodeWidth", "--encode-height", "$encodeHeight")
    }
  }

  Write-Output "ROLE=host"
  Write-Output "EXE=$exe"
  Write-Output ("ARGS=" + ($args -join " "))
  & $exe @args
  exit $LASTEXITCODE
}

$exe = Join-Path $exeDirResolved "remote60_native_video_client_poc.exe"
if (-not (Test-Path -LiteralPath $exe)) { throw "client exe not found: $exe" }

$effectiveHost = $RemoteHost
if ([string]::IsNullOrWhiteSpace($effectiveHost)) {
  $effectiveHost = Get-StringValue $cfg "remoteHost" "127.0.0.1"
}

$args = @("--host", $effectiveHost, "--port", "$port", "--codec", $codec, "--transport", $transport, "--fps-hint", "$fpsHint")
if ($udpMtu -gt 0) { $args += @("--udp-mtu", "$udpMtu") }
if ($tcpRecvBufKb -gt 0) { $args += @("--tcp-recvbuf-kb", "$tcpRecvBufKb") }
if ($tcpSendBufKb -gt 0) { $args += @("--tcp-sendbuf-kb", "$tcpSendBufKb") }
if ($seconds -gt 0) { $args += @("--seconds", "$seconds") }
if ($controlPort -gt 0) { $args += @("--control-port", "$controlPort") }
if ($noInputChannel) { $args += "--no-input-channel" }

Write-Output "ROLE=client"
Write-Output "EXE=$exe"
Write-Output ("ARGS=" + ($args -join " "))
& $exe @args
exit $LASTEXITCODE
