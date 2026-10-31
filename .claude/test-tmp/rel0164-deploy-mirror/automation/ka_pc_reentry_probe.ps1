# ka-pc-impl r1 (KA ③) step 1: two viewer sessions, one after the other, in ONE viewer process.
#
# A test host (this build's GNLinkStream, bound to 127.0.0.1 only -- no firewall prompt, nothing reachable from
# outside) and the test viewer GNLinkViewerKaTest (REMOTE60_VIEWER_TEST_SESSIONS=2), its window placed beyond the
# right edge of the virtual screen and shown without focus: nothing covers the user's monitors.
# Pass = each session connects, decodes and presents, and the second one does so after the first has torn down.
#
#   powershell -File ka_pc_reentry_probe.ps1 -Build <dir with Release exes> -Out <folder under .claude\test-tmp>
param(
  [Parameter(Mandatory = $true)][string]$Build,
  [Parameter(Mandatory = $true)][string]$Out,
  [int]$Port = 47611,
  [int]$Seconds = 10
)
$ErrorActionPreference = 'Stop'
$anchor = 'D:\remote\remote\.claude\test-tmp'
$outFull = [IO.Path]::GetFullPath($Out)
if ([IO.Path]::GetDirectoryName($outFull) -ne $anchor) { throw "Out must be a folder directly under $anchor" }
New-Item -ItemType Directory -Force $outFull | Out-Null

Add-Type -AssemblyName System.Windows.Forms
$vs = [System.Windows.Forms.SystemInformation]::VirtualScreen
$env:REMOTE60_VIEWER_TEST_WINDOW_X = [string]($vs.Right + 200)
$env:REMOTE60_VIEWER_TEST_WINDOW_Y = [string]($vs.Top + 50)
$env:REMOTE60_VIEWER_TEST_SESSIONS = '2'
$env:REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE = '1'

$hostLog = Join-Path $outFull 'host.log'
$viewerLog = Join-Path $outFull 'viewer.log'
$h = Start-Process (Join-Path $Build 'GNLinkStream.exe') -PassThru -NoNewWindow `
  -ArgumentList '--transport', 'udp', '--codec', 'h264', '--bind-address', '127.0.0.1', '--bind-port', "$Port", '--seconds', '60' `
  -RedirectStandardOutput $hostLog -RedirectStandardError (Join-Path $outFull 'host.err.log')
Start-Sleep -Seconds 3
$v = Start-Process (Join-Path $Build 'GNLinkViewerKaTest.exe') -PassThru -NoNewWindow `
  -ArgumentList '--transport', 'udp', '--codec', 'h264', '--enable-input-channel', '--initial-view', 'stream', `
                '--host', '127.0.0.1', '--port', "$Port", '--seconds', "$Seconds" `
  -RedirectStandardOutput $viewerLog -RedirectStandardError (Join-Path $outFull 'viewer.err.log')
$done = $v.WaitForExit(($Seconds * 2 + 40) * 1000)
if (-not $done) { $v.Kill(); "viewer did not finish in time (killed: its own test process)" }
"viewer exit=" + $v.ExitCode
if (-not $h.HasExited) { $h.Kill() }   # the test host this script started
$text = Get-Content $viewerLog -Raw
$s1 = $text.IndexOf('[viewer][ka-test] session 1/2 begins'); $e1 = $text.IndexOf('[viewer][ka-test] session 1/2 ended')
$s2 = $text.IndexOf('[viewer][ka-test] session 2/2 begins'); $e2 = $text.IndexOf('[viewer][ka-test] session 2/2 ended')
"markers s1=$s1 e1=$e1 s2=$s2 e2=$e2"
foreach ($seg in @(@{n=1; a=$s1; b=$e1}, @{n=2; a=$s2; b=$e2})) {
  if ($seg.a -lt 0 -or $seg.b -lt 0) { "session $($seg.n): MISSING"; continue }
  $part = $text.Substring($seg.a, $seg.b - $seg.a)
  $connected = ([regex]::Matches($part, 'connected host=127\.0\.0\.1')).Count
  $presents = ([regex]::Matches($part, '\[present\]')).Count
  $decoded = ([regex]::Matches($part, 'decoded|decode ok|frames=')).Count
  "session $($seg.n): connected=$connected presentLines=$presents decodeHints=$decoded"
}
