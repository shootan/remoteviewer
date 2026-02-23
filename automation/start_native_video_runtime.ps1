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
  [switch]$StartClient
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Root)) {
  $Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

$impl = Join-Path $PSScriptRoot "start_native_video_runtime_impl.ps1"
if (-not (Test-Path $impl)) {
  throw "impl script not found: $impl"
}

$stateDir = Join-Path $Root "automation/state"
New-Item -ItemType Directory -Path $stateDir -Force | Out-Null
$sessionPath = Join-Path $stateDir "native-video-session.json"
$effectiveTransport = $Transport
if ([string]::IsNullOrWhiteSpace($effectiveTransport)) {
  if ($Codec -ieq "h264") {
    $effectiveTransport = "udp"
  } else {
    $effectiveTransport = "tcp"
  }
}

$argList = @(
  "-NoProfile",
  "-ExecutionPolicy", "Bypass",
  "-File", $impl,
  "-Root", $Root,
  "-Port", "$Port",
  "-ControlPort", "$ControlPort",
  "-Transport", $effectiveTransport,
  "-UdpMtu", "$UdpMtu",
  "-Fps", "$Fps",
  "-Codec", $Codec,
  "-Bitrate", "$Bitrate",
  "-Keyint", "$Keyint",
  "-EncodeWidth", "$EncodeWidth",
  "-EncodeHeight", "$EncodeHeight",
  "-FpsHint", "$FpsHint",
  "-RemoteHost", $RemoteHost,
  "-HostSeconds", "$HostSeconds",
  "-ClientSeconds", "$ClientSeconds",
  "-TraceEvery", "$TraceEvery",
  "-TraceMax", "$TraceMax",
  "-InputLogEvery", "$InputLogEvery",
  "-TcpSendBufKb", "$TcpSendBufKb",
  "-TcpRecvBufKb", "$TcpRecvBufKb",
  "-Internal"
)
if ($StartClient) { $argList += "-StartClient" }
if ($NoInputChannel) { $argList += "-NoInputChannel" }
if (-not [string]::IsNullOrWhiteSpace($EncoderBackend)) {
  $argList += @("-EncoderBackend", $EncoderBackend)
}
if (-not [string]::IsNullOrWhiteSpace($DecoderBackend)) {
  $argList += @("-DecoderBackend", $DecoderBackend)
}

$launcher = Start-Process -FilePath "powershell" -ArgumentList $argList -WindowStyle Hidden -PassThru

Write-Output "START_LAUNCHER_PID=$($launcher.Id)"
Write-Output "PORT=$Port"
Write-Output "CONTROL_PORT=$ControlPort"
Write-Output "TRANSPORT=$effectiveTransport"
Write-Output "UDP_MTU=$UdpMtu"
Write-Output "CODEC=$Codec"
Write-Output "ENCODE_WIDTH=$EncodeWidth"
Write-Output "ENCODE_HEIGHT=$EncodeHeight"
Write-Output "ENCODER_BACKEND=$EncoderBackend"
Write-Output "DECODER_BACKEND=$DecoderBackend"
Write-Output "SESSION_FILE=$sessionPath"
Write-Output "STATUS=starting_in_background"
