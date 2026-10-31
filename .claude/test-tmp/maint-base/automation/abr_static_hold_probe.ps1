# Proves ABR holds its profile through a static desktop instead of mistaking the sparse frame
# cadence for congestion and demoting.
#
# The symptom: 12 Mbps looked sharp, then went soft "for no reason" while the user was just
# reading a still screen. Frame gating sends only a few frames of a static desktop, and the
# client's relative-lag metric over 2-4 samples read as latency the network never had, so ABR
# demoted -- then recovered on motion, then demoted again. The gate: a second whose host send
# cadence is sparse (or in static gating mode) carries no evidence and must not move the profile.
#
# The check: over a ~40s idle session, ABR must not demote below high. A demote on a still
# screen is exactly the bug.
#
# ENVIRONMENT CAVEAT: run this on a genuinely idle host. If the desktop is being used (or
# watched over a live remote session, which moves the cursor), frame gating stays in motion
# mode and the sparse path this guards is never exercised -- the probe then shows zero demotes
# because nothing triggered ABR at all, not because the gate held. "static-gating transitions"
# near zero across the run means the probe did not reach the sparse case; treat that as
# inconclusive and rely on the real-device test.

param(
  [string]$Viewer = "D:\remote\remote\build-local\apps\native_poc\Release\GNLinkViewer.exe",
  [string]$Stream = "D:\remote\remote\build-local\apps\native_poc\Release\GNLinkStream.exe",
  [int]$Port = 43300
)

$env:REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE = "1"
$env:REMOTE60_NATIVE_START_STREAM_VIEW = "1"
$env:REMOTE60_NATIVE_STATS_PRINT_EVERY_SEC = "1"

# Start at 12 Mbps (high) via the viewer's runtime tune, then leave the screen alone.
$h = Start-Process $Stream -PassThru -NoNewWindow `
  -ArgumentList "--transport","udp","--codec","h264","--bind-port","$Port", `
                "--bitrate","12000000","--fps","30","--seconds","55" `
  -RedirectStandardOutput "$env:TEMP\hold_host.txt" -RedirectStandardError "$env:TEMP\hold_host_err.txt"
Start-Sleep -Seconds 3

$v = Start-Process $Viewer -PassThru -NoNewWindow `
  -ArgumentList "--transport","udp","--codec","h264","--enable-input-channel", `
                "--host","127.0.0.1","--port","$Port","--seconds","45" `
  -RedirectStandardOutput "$env:TEMP\hold_view.txt" -RedirectStandardError "$env:TEMP\hold_view_err.txt"
[void]$v.WaitForExit(60000); if (-not $v.HasExited) { $v.Kill() }
if (-not $h.HasExited) { $h.Kill() }

$hostLog = Get-Content "$env:TEMP\hold_host.txt" -ErrorAction SilentlyContinue
$demotes = @($hostLog | Where-Object { $_ -match '\[abr\] profile=(mid|low)' })
$staticSecs = @($hostLog | Where-Object { $_ -match 'frame-gating mode=static' }).Count
$gaveEvidence = @($hostLog | Where-Object { $_ -match 'frame-gating mode=motion' }).Count

"static-gating transitions : $staticSecs"
"motion transitions        : $gaveEvidence"
"abr demotes (mid/low)     : $($demotes.Count)"
if ($demotes.Count -gt 0) { "  first demote: " + ($demotes[0] -replace '.*\[abr\]', '[abr]') }
if ($demotes.Count -gt 0) {
  "VERDICT: FAIL -- ABR demoted on a still screen"
} elseif ($staticSecs -eq 0) {
  "VERDICT: INCONCLUSIVE -- host never entered static gating (desktop in use?); sparse path untested"
} else {
  "VERDICT: profile held through the static screen; no false demote"
}