# Proves the capture lifecycle detaches when the client leaves and comes back when one returns.
#
# The sequence a real machine goes through: a session runs, the viewer exits, the host sits
# alone (this is when RDP into that PC used to crawl -- DXGI duplication kept acquiring at
# full rate), then a second viewer connects and must get pictures, not black.
#
# Timing: the host notices a vanished client by control-read silence (10s), then waits the
# idle grace (5s) before detaching. The sleeps below leave margin over both.

param(
  [string]$Viewer = "D:\remote\remote\build-local\apps\native_poc\Release\GNLinkViewer.exe",
  [string]$Stream = "D:\remote\remote\build-local\apps\native_poc\Release\GNLinkStream.exe",
  [int]$Port = 43230
)

$env:REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE = "1"
$env:REMOTE60_NATIVE_START_STREAM_VIEW = "1"

$h = Start-Process $Stream -PassThru -NoNewWindow `
  -ArgumentList "--transport","udp","--codec","h264","--bind-port","$Port","--seconds","90" `
  -RedirectStandardOutput "$env:TEMP\idle_host.txt" -RedirectStandardError "$env:TEMP\idle_host_err.txt"
Start-Sleep -Seconds 3

$v1 = Start-Process $Viewer -PassThru -NoNewWindow `
  -ArgumentList "--transport","udp","--codec","h264","--enable-input-channel", `
                "--host","127.0.0.1","--port","$Port","--seconds","8" `
  -RedirectStandardOutput "$env:TEMP\idle_v1.txt" -RedirectStandardError "$env:TEMP\idle_v1_err.txt"
[void]$v1.WaitForExit(20000); if (-not $v1.HasExited) { $v1.Kill() }

# 10s silence detection + 5s grace + margin.
Start-Sleep -Seconds 20

$midLog = Get-Content "$env:TEMP\idle_host.txt" -ErrorAction SilentlyContinue
$sawInactive = @($midLog | Where-Object { $_ -match 'stream inactive' }).Count
$sawDetach   = @($midLog | Where-Object { $_ -match 'capture detached \(idle\)' }).Count

$v2 = Start-Process $Viewer -PassThru -NoNewWindow `
  -ArgumentList "--transport","udp","--codec","h264","--enable-input-channel", `
                "--host","127.0.0.1","--port","$Port","--seconds","10" `
  -RedirectStandardOutput "$env:TEMP\idle_v2.txt" -RedirectStandardError "$env:TEMP\idle_v2_err.txt"
[void]$v2.WaitForExit(25000); if (-not $v2.HasExited) { $v2.Kill() }
if (-not $h.HasExited) { $h.Kill() }

$hostLog = Get-Content "$env:TEMP\idle_host.txt" -ErrorAction SilentlyContinue
$v2Log   = Get-Content "$env:TEMP\idle_v2.txt" -ErrorAction SilentlyContinue
$sawReattach = @($hostLog | Where-Object { $_ -match 'capture reattached' }).Count
$v2Frames    = @($v2Log | Where-Object { $_ -match '\[present\] seq=' }).Count
$v1Frames    = @((Get-Content "$env:TEMP\idle_v1.txt" -ErrorAction SilentlyContinue) |
                 Where-Object { $_ -match '\[present\] seq=' }).Count

"first session frames  : $v1Frames"
"stream inactive seen  : $sawInactive"
"capture detach seen   : $sawDetach"
"capture reattach seen : $sawReattach"
"second session frames : $v2Frames"
if ($v1Frames -gt 0 -and $sawInactive -gt 0 -and $sawDetach -gt 0 -and $sawReattach -gt 0 -and $v2Frames -gt 0) {
  "VERDICT: capture detaches when idle and the next session still gets video"
} else {
  "VERDICT: INCOMPLETE"
}
