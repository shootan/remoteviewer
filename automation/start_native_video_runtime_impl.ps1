param(
  [string]$Root = "",
  [int]$Port = 43000,
  [int]$ControlPort = 0,
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
  [string]$RemoteHost = "127.0.0.1",
  [int]$HostSeconds = 0,
  [int]$ClientSeconds = 0,
  [int]$TraceEvery = 0,
  [int]$TraceMax = 0,
  [int]$InputLogEvery = 120,
  [int]$TcpSendBufKb = 0,
  [int]$TcpRecvBufKb = 0,
  [switch]$NoInputChannel,
  [switch]$StartClient,
  [switch]$Internal
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not $Internal) {
  throw "Do not run start_native_video_runtime_impl.ps1 directly. Use automation/start_native_video_runtime.ps1."
}

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
if ($StartClient -and -not (Test-Path $clientExe)) { throw "client exe not found: $clientExe" }

$stateDir = Join-Path $Root "automation/state"
New-Item -ItemType Directory -Path $stateDir -Force | Out-Null
$sessionPath = Join-Path $stateDir "native-video-session.json"

function Is-Alive {
  param([int]$ProcessId)
  if ($ProcessId -le 0) { return $false }
  try {
    $p = Get-Process -Id $ProcessId -ErrorAction Stop
    return ($null -ne $p -and -not $p.HasExited)
  } catch {
    return $false
  }
}

function Stop-IfAlive {
  param([int]$TargetProcessId)
  if ($TargetProcessId -le 0) { return }
  if (-not (Is-Alive -ProcessId $TargetProcessId)) { return }
  try { Stop-Process -Id $TargetProcessId -Force -ErrorAction SilentlyContinue } catch {}
}

if (Test-Path $sessionPath) {
  try {
    $prev = Get-Content $sessionPath -Raw | ConvertFrom-Json
    if ($prev.hostPid) { Stop-IfAlive -TargetProcessId ([int]$prev.hostPid) }
    if ($prev.clientPid) { Stop-IfAlive -TargetProcessId ([int]$prev.clientPid) }
  } catch {}
}

Get-Process remote60_native_video_host_poc -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Get-Process remote60_native_video_client_poc -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

$ts = Get-Date -Format "yyyyMMdd-HHmmss"
$logDir = Join-Path $Root ("automation/logs/native-video-live-" + $ts)
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$hostOut = Join-Path $logDir "host.out.log"
$hostErr = Join-Path $logDir "host.err.log"
$clientOut = Join-Path $logDir "client.out.log"
$clientErr = Join-Path $logDir "client.err.log"

$hostArgs = @("--bind-port", "$Port", "--fps", "$Fps", "--codec", "$Codec")
if (-not [string]::IsNullOrWhiteSpace($effectiveTransport)) { $hostArgs += @("--transport", "$effectiveTransport") }
if ($UdpMtu -gt 0) { $hostArgs += @("--udp-mtu", "$UdpMtu") }
if ($Codec -ieq "h264") { $hostArgs += @("--bitrate", "$Bitrate", "--keyint", "$Keyint") }
if ($EncodeWidth -gt 0 -and $EncodeHeight -gt 0) {
  $hostArgs += @("--encode-width", "$EncodeWidth", "--encode-height", "$EncodeHeight")
}
if ($HostSeconds -gt 0) { $hostArgs += @("--seconds", "$HostSeconds") }
if ($ControlPort -gt 0) { $hostArgs += @("--control-port", "$ControlPort") }
if ($TraceEvery -gt 0) { $hostArgs += @("--trace-every", "$TraceEvery") }
if ($TraceMax -gt 0) { $hostArgs += @("--trace-max", "$TraceMax") }
if ($InputLogEvery -gt 0) { $hostArgs += @("--input-log-every", "$InputLogEvery") }
if ($TcpSendBufKb -gt 0) { $hostArgs += @("--tcp-sendbuf-kb", "$TcpSendBufKb") }

if ($Codec -ieq "h264") {
  $env:REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE = "1"
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
} else {
  Remove-Item Env:REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE -ErrorAction SilentlyContinue
  Remove-Item Env:REMOTE60_NATIVE_ENCODER_BACKEND -ErrorAction SilentlyContinue
  Remove-Item Env:REMOTE60_NATIVE_DECODER_BACKEND -ErrorAction SilentlyContinue
}

$hostProc = Start-Process -FilePath $hostExe -ArgumentList $hostArgs -WorkingDirectory $Root `
  -RedirectStandardOutput $hostOut -RedirectStandardError $hostErr -PassThru

Start-Sleep -Milliseconds 400
$hostProc.Refresh()
if ($hostProc.HasExited) {
  throw "native video host exited early (code=$($hostProc.ExitCode))"
}

$clientProc = $null
if ($StartClient) {
  $clientArgs = @("--host", $RemoteHost, "--port", "$Port", "--codec", "$Codec")
  if (-not [string]::IsNullOrWhiteSpace($effectiveTransport)) { $clientArgs += @("--transport", "$effectiveTransport") }
  if ($UdpMtu -gt 0) { $clientArgs += @("--udp-mtu", "$UdpMtu") }
  if ($Codec -ieq "h264") { $clientArgs += @("--fps-hint", "$FpsHint") }
  if ($ClientSeconds -gt 0) { $clientArgs += @("--seconds", "$ClientSeconds") }
  if ($ControlPort -gt 0) { $clientArgs += @("--control-port", "$ControlPort") }
  if ($TraceEvery -gt 0) { $clientArgs += @("--trace-every", "$TraceEvery") }
  if ($TraceMax -gt 0) { $clientArgs += @("--trace-max", "$TraceMax") }
  if ($InputLogEvery -gt 0) { $clientArgs += @("--input-log-every", "$InputLogEvery") }
  if ($TcpRecvBufKb -gt 0) { $clientArgs += @("--tcp-recvbuf-kb", "$TcpRecvBufKb") }
  if ($TcpSendBufKb -gt 0) { $clientArgs += @("--tcp-sendbuf-kb", "$TcpSendBufKb") }
  if ($NoInputChannel) { $clientArgs += "--no-input-channel" }
  $clientProc = Start-Process -FilePath $clientExe -ArgumentList $clientArgs -WorkingDirectory $Root `
    -RedirectStandardOutput $clientOut -RedirectStandardError $clientErr -PassThru
}

$session = [ordered]@{
  startedAt = (Get-Date -Format o)
  remoteHost = $RemoteHost
  port = $Port
  controlPort = $ControlPort
  transport = $effectiveTransport
  udpMtu = $UdpMtu
  fps = $Fps
  codec = $Codec
  bitrate = $Bitrate
  keyint = $Keyint
  encodeWidth = $EncodeWidth
  encodeHeight = $EncodeHeight
  encoderBackend = $EncoderBackend
  decoderBackend = $DecoderBackend
  fpsHint = $FpsHint
  noInputChannel = [bool]$NoInputChannel
  hostPid = $hostProc.Id
  clientPid = if ($clientProc) { $clientProc.Id } else { 0 }
  startClient = [bool]$StartClient
  logDir = $logDir
  hostOut = $hostOut
  hostErr = $hostErr
  clientOut = $clientOut
  clientErr = $clientErr
}
($session | ConvertTo-Json -Depth 4) | Set-Content -Path $sessionPath -Encoding ascii
