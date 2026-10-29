param(
  [Parameter(Mandatory = $true)][string]$BuildDir,  # absolute: remote60_ka_standby_holder_tool.exe + GNLinkViewerKaTest.exe
  [Parameter(Mandatory = $true)][string]$ServerJs,  # absolute ...\apps\directory\server.js
  [switch]$Negative                                 # the holder told to launch the viewer as r4 did (no FORCE in its block)
)
# rel-0164 old-APK lane r5 (verifier oa-s / oa-p 2026-10-30 00:14): the REAL standby holder starts the REAL managed viewer
# (GNLinkViewerKaTest --managed-standby) -- and the viewer must get past its codec check (h264 needs
# REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1, which the holder now writes into the viewer's own environment block).
# No device, no Host, no GPU stream: a loopback directory (server.js, its own ports 18521 / 18522, relay off, one test
# account) so the holder can sign in; the Init names a host id that does not exist (--host-id), so the viewer never
# connects anywhere -- it only has to start, take the Init and stay up until its Release. This script REMOVES the variable
# from its own environment first: the viewer can only have it from the holder.
#   PASS: no "unsupported codec" in viewer.log, the viewer did not exit on its own (>= 6 s after Init), ended after Release
#   -Negative: the same with --omit-viewer-encoded-env 1 -- the r4 failure must reproduce (exit on its own, the line)
$ErrorActionPreference = 'Stop'
foreach ($pv in @(@('BuildDir', $BuildDir), @('ServerJs', $ServerJs))) {
  if (-not [IO.Path]::IsPathRooted($pv[1]) -or -not (Test-Path -LiteralPath $pv[1])) { "FAIL  SETUP -$($pv[0]) must be an existing absolute path ($($pv[1]))`nRESULT: INVALID"; exit 2 }
}
$holder = "$BuildDir\remote60_ka_standby_holder_tool.exe"
$viewer = "$BuildDir\GNLinkViewerKaTest.exe"
foreach ($f in $holder, $viewer) { if (-not (Test-Path -LiteralPath $f)) { "FAIL  SETUP $f is not built`nRESULT: INVALID"; exit 2 } }
$wt = (Resolve-Path "$PSScriptRoot\..\..").Path
$root = "$wt\.claude\oa_runs\holder-check-$(if ($Negative) { 'neg' } else { 'pos' })-$(Get-Date -Format yyyyMMddHHmmss)-$PID"
New-Item -ItemType Directory $root | Out-Null
New-Item -ItemType Directory "$root\dir", "$root\standby" | Out-Null
"holder $((Get-FileHash $holder).Hash.ToLower()) viewer $((Get-FileHash $viewer).Hash.ToLower()) server $((Get-FileHash $ServerJs).Hash.ToLower().Substring(0,16))"
Remove-Item env:REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE, env:REMOTE60_NATIVE_ENCODED_EXPERIMENT -ErrorAction SilentlyContinue
"caller env FORCE: $(if ($env:REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE) { 'SET' } else { 'absent' })"

$chars = 'abcdefghijkmnpqrstuvwxyz23456789'
$pw = -join (1..16 | ForEach-Object { $chars[(Get-Random -Maximum $chars.Length)] })
"oacheck $pw" | Out-File "$root\account.txt" -Encoding ascii
$dirEnv = @{ REMOTE60_DIR_DATA = "$root\dir\directory-data.json"; REMOTE60_DIR_PORT = '18521'; REMOTE60_DIR_UDP_PORT = '18522';
  REMOTE60_LOG_DIR = "$root\dir\logs"; REMOTE60_RELAY_ENABLED = '0' }
foreach ($k in $dirEnv.Keys) { Set-Item "env:$k" $dirEnv[$k] }
$node = $null; $hp = $null
try {
  & node $ServerJs --add-account oacheck $pw *> "$root\dir\add_account.txt"
  $node = Start-Process -FilePath node -ArgumentList @($ServerJs) -PassThru -NoNewWindow -RedirectStandardOutput "$root\dir\server.stdout.txt" -RedirectStandardError "$root\dir\server.stderr.txt"
  $null = $node.Handle
  $up = $false
  for ($i = 0; $i -lt 40 -and -not $up; ++$i) {
    try { $c = New-Object Net.Sockets.TcpClient; $c.Connect('127.0.0.1', 18521); $c.Close(); $up = $true } catch { Start-Sleep -Milliseconds 250 }
  }
  if (-not $up) { "FAIL  SETUP the loopback directory did not listen on 18521`nRESULT: INVALID"; exit 2 }
  $args0 = @('--url', 'http://127.0.0.1:18521', '--host-name', 'oa-check', '--host-id', 'oa-check-no-such-host',
    '--account-file', "$root\account.txt", '--viewer', $viewer, '--root', "$root\standby", '--stop-file', "$root\standby\stop",
    '--max-sec', '120')
  if ($Negative) { $args0 += @('--omit-viewer-encoded-env', '1') }
  $hp = Start-Process -FilePath $holder -ArgumentList $args0 -PassThru -NoNewWindow -RedirectStandardOutput "$root\standby\holder.stdout.txt" -RedirectStandardError "$root\standby\holder.stderr.txt"
  $null = $hp.Handle
  $state = "$root\standby\standby_state.txt"
  $init = $false
  for ($i = 0; $i -lt 120 -and -not $init -and -not $hp.HasExited; ++$i) {
    Start-Sleep -Milliseconds 250
    $init = (Test-Path $state) -and (Select-String -Path $state -SimpleMatch -Pattern 'Init sent' -Quiet)
  }
  Start-Sleep -Seconds 6  # the viewer must stay up after its Init
  $diedEarly = (Test-Path $state) -and (Select-String -Path $state -SimpleMatch -Pattern 'the viewer EXITED on its own' -Quiet)
  New-Item -ItemType File "$root\standby\stop" | Out-Null
  if (-not $hp.WaitForExit(40000)) { $hp.Kill(); $null = $hp.WaitForExit(10000); "  holder killed by its handle (did not end in 40 s)" }
  $rc = $hp.ExitCode
} finally {
  if ($node -and -not $node.HasExited) { $node.Kill(); $null = $node.WaitForExit(10000) }
  foreach ($k in $dirEnv.Keys) { Remove-Item "env:$k" -ErrorAction SilentlyContinue }
}
$vlog = "$root\standby\viewer\viewer.log"
$codecRefused = (Test-Path $vlog) -and (Select-String -Path $vlog -SimpleMatch -Pattern 'unsupported codec' -Quiet)
$released = (Test-Path $state) -and (Select-String -Path $state -SimpleMatch -Pattern 'viewer ended after Release' -Quiet)
$envLine = if (Test-Path $state) { (Select-String -Path $state -SimpleMatch -Pattern 'viewer env:' | Select-Object -First 1).Line } else { '' }
"run $root"
"  init=$init diedEarly=$diedEarly codecRefused=$codecRefused released=$released holderRc=$rc"
"  $envLine"
if (Test-Path $vlog) { "  viewer.log head: $((Get-Content $vlog -TotalCount 3) -join ' || ')" }
$ok = if ($Negative) { $init -and $diedEarly -and $codecRefused } else { $init -and -not $diedEarly -and -not $codecRefused -and $released }
$what = if ($Negative) { 'NEGATIVE (r4 launch): the viewer refuses h264 and exits on its own' } else {
  'the real holder starts the real viewer past its codec check; it stays up after Init and ends after Release' }
"$(if ($ok) { 'PASS' } else { 'FAIL' })  $what"
"RESULT: $(if ($ok) { 'PASS' } else { 'FAIL' })"
exit $(if ($ok) { 0 } else { 1 })
