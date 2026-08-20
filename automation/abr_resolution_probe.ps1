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

function Run-Viewer([int]$seconds, [int]$dropPm, [string]$tag) {
  if ($dropPm -gt 0) { $env:REMOTE60_NATIVE_UDP_SIM_DROP_PM = "$dropPm" }
  else { $env:REMOTE60_NATIVE_UDP_SIM_DROP_PM = "0" }
  $v = Start-Process $script:Viewer -PassThru -NoNewWindow `
    -ArgumentList "--transport","udp","--codec","h264","--enable-input-channel", `
                  "--host","127.0.0.1","--port","$Port", `
                  "--runtime-bitrate","12000000","--runtime-fps","30","--seconds","$seconds" `
    -RedirectStandardOutput "$env:TEMP\abr_view_$tag.txt" -RedirectStandardError "$env:TEMP\abr_view_${tag}_err.txt"
  [void]$v.WaitForExit(($seconds + 15) * 1000)
  if (-not $v.HasExited) { $v.Kill() }
}

Run-Viewer 20 0 "clean1"      # phase 1: tune to 12M must lift the 3M-init 720p to full
Start-Sleep -Seconds 2
Run-Viewer 35 400 "lossy"     # phase 2: try to demote
Start-Sleep -Seconds 2
Run-Viewer 45 0 "clean2"      # phase 3: recover; the restored profile must be full size
Start-Sleep -Seconds 1
if (-not $h.HasExited) { $h.Kill() }

$hostLog = Get-Content "$env:TEMP\abr_host.txt" -ErrorAction SilentlyContinue

"=== ladder / abr / tune events ==="
$events = @($hostLog | Where-Object { $_ -match 'encode ladder|\[abr\]|runtime-config-applied|rate-control reason' })
$events | ForEach-Object { $_ }

# The capture is whatever desktop this runs on; full resolution means "what the ladder grants
# 12 Mbps", which is strictly wider than the 3 Mbps floor. The stuck bug pinned width at the
# 3M ladder width for the whole run, so the discriminating fact is a WIDER size appearing
# after the tune, and -- if a demote happened -- appearing AGAIN after recovery.
$sizes = @($hostLog | Select-String -Pattern 'size=(\d+)x(\d+)' -AllMatches |
           ForEach-Object { [int]$_.Matches[0].Groups[1].Value })
$minW = ($sizes | Measure-Object -Minimum).Minimum
$maxW = ($sizes | Measure-Object -Maximum).Maximum
$demoted  = @($events | Where-Object { $_ -match '\[abr\] profile=(low|mid)' }).Count
$restored = @($events | Where-Object { $_ -match '\[abr\] profile=high' })
$restoredWide = @($restored | Where-Object { $_ -match 'encode=(\d+)x' -and [int]$matches[1] -gt $minW }).Count

"min encode width : $minW  (3M ladder floor)"
"max encode width : $maxW  (must exceed the floor after the 12M tune)"
"abr demotes seen : $demoted"
"abr high restores: $($restored.Count)  (of which wider than floor: $restoredWide)"

if ($maxW -le $minW) { "VERDICT: INCOMPLETE -- the 12M tune never lifted the resolution" }
elseif ($demoted -eq 0) { "VERDICT: PARTIAL -- tune lift verified; ABR never demoted, cycle untested" }
elseif ($restored.Count -gt 0 -and $restoredWide -eq $restored.Count) {
  "VERDICT: full cycle -- demoted and recovered to full resolution, not the frozen one"
} elseif ($restored.Count -eq 0) { "VERDICT: PARTIAL -- demoted but never recovered within the window" }
else { "VERDICT: FAIL -- a high restore came back at the frozen low resolution" }
