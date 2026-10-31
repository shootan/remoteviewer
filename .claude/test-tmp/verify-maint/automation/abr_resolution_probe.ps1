# Reproduces the "12 Mbps but the text is smudged" session and asserts the fix.
#
# The failure being guarded: the host initializes its encoder at a low bitrate, the resolution
# ladder correctly picks ~720p for it, and that size used to be frozen into every ABR profile.
# A later demote-and-recover cycle then restored the bitrate but re-applied the frozen 720p --
# permanently, at any bitrate. The fix derives the size from the ladder at each transition.
#
# Three phases against one host that starts at 3 Mbps (the freeze scenario):
#   1. clean viewer asks for 12 Mbps -> the tune path must lift encode to full resolution
#   2. lossy viewer (40% simulated drop) tries to pressure ABR into a demote
#   3. clean viewer again -> after recovery, the [abr] line must carry full resolution
# Phase 2 is best-effort: simulated loss raises delivered-frame latency through reassembly
# waits, but if ABR never demotes the probe says so instead of passing vacuously.

param(
  [string]$Viewer = "D:\remote\remote\build-local\apps\native_poc\Release\GNLinkViewer.exe",
  [string]$Stream = "D:\remote\remote\build-local\apps\native_poc\Release\GNLinkStream.exe",
  [int]$Port = 43270
)

$env:REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE = "1"
$env:REMOTE60_NATIVE_START_STREAM_VIEW = "1"
$env:REMOTE60_NATIVE_STATS_PRINT_EVERY_SEC = "1"

$h = Start-Process $Stream -PassThru -NoNewWindow `
  -ArgumentList "--transport","udp","--codec","h264","--bind-port","$Port", `
                "--bitrate","3000000","--fps","30","--seconds","180" `
  -RedirectStandardOutput "$env:TEMP\abr_host.txt" -RedirectStandardError "$env:TEMP\abr_host_err.txt"
Start-Sleep -Seconds 3

function Run-Viewer([int]$seconds, [int]$dropPm, [bool]$tune, [string]$tag) {
  if ($dropPm -gt 0) { $env:REMOTE60_NATIVE_UDP_SIM_DROP_PM = "$dropPm" }
  else { $env:REMOTE60_NATIVE_UDP_SIM_DROP_PM = "0" }
  $viewerArgs = @("--transport","udp","--codec","h264","--enable-input-channel",
                  "--host","127.0.0.1","--port","$Port","--seconds","$seconds")
  if ($tune) { $viewerArgs += @("--runtime-bitrate","12000000","--runtime-fps","30") }
  $v = Start-Process $script:Viewer -PassThru -NoNewWindow -ArgumentList $viewerArgs `
    -RedirectStandardOutput "$env:TEMP\abr_view_$tag.txt" -RedirectStandardError "$env:TEMP\abr_view_${tag}_err.txt"
  [void]$v.WaitForExit(($seconds + 15) * 1000)
  if (-not $v.HasExited) { $v.Kill() }
}

Run-Viewer 20 0 $true "clean1"     # phase 1: tune to 12M must lift the 3M-init 720p to full
Start-Sleep -Seconds 2
Run-Viewer 35 400 $true "lossy"    # phase 2: try to demote
Start-Sleep -Seconds 2
# Phase 3 sends NO tune on purpose: a tune applies the full profile directly and would let
# the probe pass without the ABR recovery path -- the very code under test -- ever running.
Run-Viewer 45 0 $false "clean2"
Start-Sleep -Seconds 1
if (-not $h.HasExited) { $h.Kill() }

$hostLog = Get-Content "$env:TEMP\abr_host.txt" -ErrorAction SilentlyContinue

"=== ladder / abr / tune events ==="
$events = @($hostLog | Where-Object { $_ -match 'encode ladder|\[abr\]|runtime-config-applied|rate-control reason' })
$events | ForEach-Object { $_ }

# The lift is proven by the ladder's own event line, not by sampling the stats: the tune
# lands within the first second of the session, so the reduced size never survives long
# enough to appear in a once-per-second stats line.
$lift = @($events | Where-Object {
  $_ -match 'encode ladder (\d+)x\d+ -> (\d+)x\d+ for 12000kbps' -and
  [int]$matches[2] -gt [int]$matches[1] }).Count
$floorW = 1300  # nothing the 3-4M ladder produces is wider than this
$demoted  = @($events | Where-Object { $_ -match '\[abr\] profile=low' }).Count
$restored = @($events | Where-Object { $_ -match '\[abr\] profile=(mid|high)' })
$restoredWide = @($restored | Where-Object { $_ -match 'encode=(\d+)x' -and [int]$matches[1] -gt $floorW }).Count

"tune lift events : $lift  (3M-born size raised for 12M)"
"abr demotes seen : $demoted"
"abr mid/high     : $($restored.Count)  (of which full-size: $restoredWide)"

if ($lift -eq 0) { "VERDICT: FAIL -- the 12M tune never lifted the resolution" }
elseif ($demoted -eq 0) {
  "VERDICT: PARTIAL -- tune lift verified; ABR never demoted (expected on loopback: it"
  "demotes on latency, and simulated loss adds none here). Run on a real network, or rely"
  "on the unit tests that pin the transition arithmetic."
}
elseif ($restored.Count -gt 0 -and $restoredWide -eq $restored.Count) {
  "VERDICT: full cycle -- demoted and recovered to full resolution, not the frozen one"
} elseif ($restored.Count -eq 0) { "VERDICT: PARTIAL -- demoted but never recovered within the window" }
else { "VERDICT: FAIL -- a recovery came back at the frozen low resolution" }
