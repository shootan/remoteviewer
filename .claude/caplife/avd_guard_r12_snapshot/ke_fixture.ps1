param(
  [Parameter(Mandatory = $true)][string]$Root,
  [Parameter(Mandatory = $true)][string]$RunId,
  [int]$HostSec = 2400,
  [int]$ReadyTimeoutSec = 60,
  [int]$StopWaitSec = 180,  # how long a harness gets to end by itself after its stop
  # what starts a host's harness and the directory -- the product files by default; the orchestration test
  # (ke_orchestration_test.ps1) passes stand-ins here (no host, no node, no AVD)
  [string[]]$HarnessCmd = @(),
  [string[]]$NodeCmd = @(),
  # AVD r10: the hosts this plan uses (ke_slot.ps1 passes them; an E2-only plan: a)
  [string]$Hosts = 'a,b,c'
)
# keepalive-r2 ② e (kept model A) AVD fixture: a REAL server.js on loopback (its relay on, the test account only) and
# three pinned TEST hosts (ke-pc-a ff00ff, ke-pc-b 00ffff, ke-pc-c ffff00), each through mobile_abr_session_e2e_test
# --scenario directoryhost: its own harness / target window / staged copy / LOCALAPPDATA / machine id, bound to
# 127.0.0.1, and the shared injection / power guard (input none + power off, both whole OFF lines before any client).
# The older host: NOT RUN (no build of it passes the guard). Ports 18410 / 18411 / 18443; the AVD reaches them at 10.0.2.2.
#
# AVD guard r2 (Codex codex_avd_guard_src_r1.md N1 / N2) -- this script is the SUPERVISOR of what it starts:
#   * $Root is a NEW run folder ke_slot.ps1 made (checked here: directly under the anchor, no link, nothing of a host);
#   * every child is started here and held by its own Process object (its handle taken at once) -- never a pid file;
#   * a host counts as ready only when ITS harness wrote a readiness naming this run's id and its own pid; a failed /
#     missing / foreign readiness, a timeout or a harness that ended first is a failure: nothing more is started, what
#     was started is stopped (by its own handle), fixture_ready.txt is never written, exit 2;
#   * all ready: fixture_ready.txt (atomically). Then it waits for $Root\stop_fixture (ke_slot.ps1 makes it after the
#     AVD), noting a child that ends early (a late host INVALID / FAIL) in fixture_failed.txt;
#   * the end: each harness asked to stop (its stop file), waited for by its own handle (killed by that handle only if
#     it does not end), its exit code kept; the node killed by its handle; fixture_result.txt; exit 0 only when every
#     harness ended 0 by itself and none ended early.
$ErrorActionPreference = 'Stop'
# -File passes a list as one comma-joined string: split it back
if ($HarnessCmd.Count -eq 1) { $HarnessCmd = @($HarnessCmd[0] -split ',') }
if ($NodeCmd.Count -eq 1) { $NodeCmd = @($NodeCmd[0] -split ',') }
$wt = 'C:\work\gnlink-wt\feat-apk-0233'
$e = "$wt\.claude\e2e"
$anchor = "$e\runs"
function Is-Link([string]$p) { (Test-Path -LiteralPath $p) -and (((Get-Item -LiteralPath $p -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) }

# ---- the run folder: exactly a folder ke_slot made under the anchor, no link on its path, no host in it yet
$full = [IO.Path]::GetFullPath($Root)
if ((Split-Path $full -Parent) -ne $anchor -or -not (Test-Path -LiteralPath $full -PathType Container)) {
  "FAIL  SETUP fixture: $Root is not a run folder directly under $anchor`nRESULT: INVALID"; exit 2
}
$p = $full
while ($p -and $p.Length -gt 3) {
  if (Is-Link $p) { "FAIL  SETUP fixture: a link on the run folder's path ($p)`nRESULT: INVALID"; exit 2 }
  $p = Split-Path $p -Parent
}
if ($RunId -notmatch '^[A-Za-z0-9_-]{1,48}$') { "FAIL  SETUP fixture: the run id must be plain ASCII`nRESULT: INVALID"; exit 2 }
foreach ($n in 'a', 'b', 'c', 'dir', 'fixture.txt', 'fixture_ready.txt', 'fixture_result.txt', 'fixture_account.txt') {
  if (Test-Path -LiteralPath "$full\$n") { "FAIL  SETUP fixture: $full\$n already exists (an earlier run's -- not reused)`nRESULT: INVALID"; exit 2 }
}
$Root = $full
function Say([string]$line) { $line | Out-File "$Root\fixture.txt" -Append -Encoding utf8 }
Say "fixture run=$RunId start $(Get-Date -Format s) (qwinsta: $((qwinsta | Select-String '^>').Line.Trim()))"
Say "  old host: NOT RUN (no build of it passes the injection / power guard)"

$children = [ordered]@{}   # name -> Process (its handle held)
$failed = New-Object System.Collections.ArrayList
$stopped = $false
# the orchestration test's switches (ke_orchestration_test.ps1 only; the stand-in harness never touches a host):
#   KE_FIXTURE_TEST_BAD_EXE_FOR=<host>   that host's harness is started from a path that does not exist (a launch throw)
#   KE_FIXTURE_TEST_CRASH_AFTER_READY=1  the fixture ends at once right after its readiness, with NO cleanup of its own
#   KE_FIXTURE_TEST_STOP_THROW_FOR=<n>   stopping that child throws (one cleanup error: the others are still ended)
function Start-Owned([string]$name, [string]$exe, [string[]]$argv, [string]$out, [hashtable]$envVars) {
  foreach ($k in $envVars.Keys) { Set-Item "env:$k" $envVars[$k] }
  try {
    if ($env:KE_FIXTURE_TEST_BAD_EXE_FOR -eq $name) { $exe = "$exe.no-such-file" }
    $pr = Start-Process -FilePath $exe -ArgumentList $argv -PassThru -NoNewWindow -RedirectStandardOutput "$out.stdout.txt" -RedirectStandardError "$out.stderr.txt"
  } finally {
    foreach ($k in $envVars.Keys) { Remove-Item "env:$k" -ErrorAction SilentlyContinue }
  }
  $null = $pr.Handle  # the handle, taken now: its exit code stays readable after it ends
  $script:children[$name] = $pr
  Say "  started $name pid $($pr.Id) at $($pr.StartTime.ToString('s'))"
  return $pr
}
# G1 (Codex codex_avd_guard_src_r2.md): what is known dead is never taken as ready -- every child started so far alive
function Dead-Child() {
  foreach ($name in @($script:children.Keys)) {
    if ($script:children[$name].HasExited) { return "$name ended (exit $($script:children[$name].ExitCode))" }
  }
  return $null
}
# G2: every child is ended, each on its own (an error with one never skips the others), and the result written
function Stop-All([string]$why) {
  if ($script:stopped) { return }
  $script:stopped = $true
  try { Say "  stopping ($why)" } catch {}
  $result = [ordered]@{}
  foreach ($name in @($script:children.Keys)) {
    $pr = $script:children[$name]
    try {
      if ($env:KE_FIXTURE_TEST_STOP_THROW_FOR -eq $name) { throw "test: stopping $name throws" }
      if ($name -ne 'node' -and -not $pr.HasExited) {
        New-Item -ItemType File "$Root\$name\stop" -Force | Out-Null  # the harness's own stop: the guard's end runs
      }
      $graceMs = if ($name -eq 'node') { 0 } else { $StopWaitSec * 1000 }
      if ($pr.WaitForExit($graceMs)) {
        $result[$name] = "exit $($pr.ExitCode)"
        if ($name -ne 'node' -and $pr.ExitCode -ne 0) { [void]$script:failed.Add("$name exit $($pr.ExitCode)") }
      } else {
        $pr.Kill()
        $ok = $pr.WaitForExit(15000)
        $result[$name] = if ($name -eq 'node') { if ($ok) { 'ended' } else { 'end NOT confirmed' } } else {
          "killed by its handle (it did not end by itself)$(if (-not $ok) { '; its end NOT confirmed' })" }
        if ($name -ne 'node') { [void]$script:failed.Add("$name did not end by itself") }
        if (-not $ok) { [void]$script:failed.Add("$name end not confirmed") }
      }
    } catch {
      [void]$script:failed.Add("stopping $name failed: $_")
      $result[$name] = "stop error ($_)"
      try {
        if (-not $pr.HasExited) { $pr.Kill() }
        $ok = $pr.WaitForExit(15000)
        $result[$name] += if ($ok) { '; killed by its handle' } else { '; its end NOT confirmed' }
      } catch { $result[$name] += "; kill failed ($_)" }
    }
  }
  $lines = @("fixture run=$RunId result") + @($result.Keys | ForEach-Object { "  $_ $($result[$_])" }) +
           @("  failures: $(if ($script:failed.Count) { $script:failed -join '; ' } else { 'none' })")
  try { $lines | Out-File "$Root\fixture_result.txt" -Encoding utf8 } catch {}
  try { $lines | ForEach-Object { Say $_ } } catch {}
}

$exitCode = 2
try {
  # ---- the directory
  New-Item -ItemType Directory "$Root\dir" | Out-Null
  $chars = 'abcdefghijkmnpqrstuvwxyz23456789'
  $pw = -join (1..16 | ForEach-Object { $chars[(Get-Random -Maximum $chars.Length)] })
  "ketest $pw" | Out-File "$Root\fixture_account.txt" -Encoding ascii
  $dirEnv = @{ REMOTE60_DIR_DATA = "$Root\dir\directory-data.json"; REMOTE60_DIR_PORT = '18410'; REMOTE60_DIR_UDP_PORT = '18411';
    REMOTE60_LOG_DIR = "$Root\dir\logs"; REMOTE60_RELAY_ENABLED = '1'; REMOTE60_RELAY_IP = '10.0.2.2';
    # the relay as the HOSTS dial it (they run on this PC, where 10.0.2.2 is not reachable)
    REMOTE60_RELAY_HOST_FACING_IP = '127.0.0.1'; REMOTE60_RELAY_PORT = '18443'; REMOTE60_RELAY_ALLOW_IPS = '*';
    REMOTE60_RELAY_ALLOW_ACCOUNTS = 'ketest' }
  # AVD r8 (Codex design r3 D2): the directory may be another worktree's (KE_SERVER_JS: an absolute ...\server.js that
  # exists), and its relay trace is on only with KE_RELAY_TRACE=1 (REMOTE60_RELAY_TRACE, the fixture's own server).
  if ($env:KE_RELAY_TRACE -eq '1') { $dirEnv['REMOTE60_RELAY_TRACE'] = '1' }
  if ($NodeCmd.Count -eq 0) {
    $server = "$wt\apps\directory\server.js"
    if ($env:KE_SERVER_JS) {
      if (-not ([IO.Path]::IsPathRooted($env:KE_SERVER_JS)) -or -not $env:KE_SERVER_JS.EndsWith('\server.js') -or
          -not (Test-Path -LiteralPath $env:KE_SERVER_JS -PathType Leaf)) { throw "KE_SERVER_JS is not an existing ...\server.js ($($env:KE_SERVER_JS))" }
      $server = $env:KE_SERVER_JS
    }
    foreach ($k in $dirEnv.Keys) { Set-Item "env:$k" $dirEnv[$k] }
    & node $server --add-account ketest $pw *> "$Root\dir\add_account.txt"
    foreach ($k in $dirEnv.Keys) { Remove-Item "env:$k" -ErrorAction SilentlyContinue }
    $NodeCmd = @('node', $server)
    Say "  directory server.js $server $((Get-FileHash $server).Hash.ToLower().Substring(0,16)) relayTrace=$($dirEnv['REMOTE60_RELAY_TRACE'])"
  }
  $node = Start-Owned 'node' $NodeCmd[0] @($NodeCmd | Select-Object -Skip 1) "$Root\dir\server" $dirEnv
  Start-Sleep 2
  if ($node.HasExited) { throw "the directory ended at once (exit $($node.ExitCode))" }

  # ---- the hosts, one after another; any failure -- or any child found dead -- stops the rest from starting
  $b = "$wt\build\apps\native_poc\Release"
  if ($HarnessCmd.Count -eq 0) {
    $HarnessCmd = @("$b\remote60_mobile_abr_session_e2e_test.exe")
    Say "  harness $((Get-FileHash $HarnessCmd[0]).Hash.ToLower()) test host $((Get-FileHash "$b\GNLinkStreamPinnedSelectTest.exe").Hash.ToLower())"
  }
  if ($Hosts -notmatch '^[abc](,[abc])*$') { throw "-Hosts must be a list of a / b / c ($Hosts)" }
  $wanted = @($Hosts -split ',')
  $hostsDef = @(@(
    @{ n = 'a'; name = 'ke-pc-a'; mark = 'ff00ff' },
    @{ n = 'b'; name = 'ke-pc-b'; mark = '00ffff' },
    @{ n = 'c'; name = 'ke-pc-c'; mark = 'ffff00' }) | Where-Object { $wanted -contains $_.n })
  Say "  hosts: $(($hostsDef | ForEach-Object { $_.name }) -join ', ') (asked: $Hosts)"
  foreach ($h in $hostsDef) {
    $dead = Dead-Child
    if ($dead) { throw "before host $($h.n): $dead" }
    $out = "$Root\$($h.n)"
    New-Item -ItemType Directory $out | Out-Null   # new (refused above if it existed)
    '' | Out-File "$out\fault.txt" -Encoding ascii -NoNewline
    $hid = "$RunId-$($h.n)"
    $argv = @($HarnessCmd | Select-Object -Skip 1) + @('--scenario', 'directoryhost', '--run-id', $hid,
      '--dir-url', 'http://127.0.0.1:18410', '--dir-id', 'ketest', '--dir-pw', $pw, '--host-name', $h.name, '--target-mark', $h.mark,
      '--host-exe', 'GNLinkStreamPinnedSelectTest.exe', '--external-sec', "$HostSec", '--watchdog-sec', "$($HostSec + 400)", '--out', $out)
    $pr = Start-Owned $h.n $HarnessCmd[0] $argv "$out\harness" @{ REMOTE60_ALLOW_HOST_E2E = '1';
      REMOTE60_PINNED_TEST_FAULT_FILE = "$out\fault.txt"; REMOTE60_PINNED_TEST_MACHINE_ID = "ke-fixture-machine-$($h.n)" }
    $deadline = (Get-Date).AddSeconds($ReadyTimeoutSec)
    $ready = $null
    while ((Get-Date) -lt $deadline -and -not $pr.HasExited) {
      if (Test-Path -LiteralPath "$out\directoryhost_ready.txt") { $ready = Get-Content -LiteralPath "$out\directoryhost_ready.txt" -Raw; break }
      Start-Sleep -Milliseconds 250
    }
    if (-not $ready -and (Test-Path -LiteralPath "$out\directoryhost_ready.txt")) { $ready = Get-Content -LiteralPath "$out\directoryhost_ready.txt" -Raw }
    Start-Sleep -Milliseconds 300  # a harness that dies right after its readiness is seen dead here, not taken as ready
    $why = $null
    if ($pr.HasExited) { $why = "its harness ended (exit $($pr.ExitCode))$(if ($ready) { ' -- after its readiness: not ready' } else { ' before any readiness' })" }
    elseif (-not $ready) { $why = "no readiness within $ReadyTimeoutSec s" }
    elseif ($ready -notmatch "^ready run=$([regex]::Escape($hid)) harness_pid=$($pr.Id) host_pid=\d+ host_log=\S+") {
      $why = "a readiness that is not this run's ready one: $($ready.Trim())"
    }
    if ($why) { throw "host $($h.n): $why" }
    Say "  host $($h.name) ready: $($ready.Trim())"
  }

  # ---- every host ready AND every child still alive: said whole, then wait for the bundle's stop
  $dead = Dead-Child
  if ($dead) { throw "before the fixture's readiness: $dead" }
  "ready run=$RunId hosts=$Hosts" | Out-File "$Root\fixture_ready.tmp" -Encoding ascii
  Move-Item -LiteralPath "$Root\fixture_ready.tmp" -Destination "$Root\fixture_ready.txt"
  Say "  fixture ready"
  if ($env:KE_FIXTURE_TEST_CRASH_AFTER_READY -eq '1') { Say "  TEST: the fixture crashes now (no cleanup of its own)"; [Environment]::Exit(9) }
  $early = $null
  while (-not (Test-Path -LiteralPath "$Root\stop_fixture")) {
    if (-not $early) {
      $early = Dead-Child
      if ($early) {
        [void]$failed.Add("$early before the stop")
        Say "  LATE FAILURE: $early before the stop"
        "late failure: $early before the stop" | Out-File "$Root\fixture_failed.txt" -Encoding utf8
      }
    }
    Start-Sleep 1
  }
  Stop-All 'the bundle asked'
  $exitCode = if ($failed.Count -gt 0) { 1 } else { 0 }
} catch {
  [void]$failed.Add("$_")
  try { Say "  NOT ready / error: $_ -- nothing more is started" } catch {}
  $exitCode = 2
} finally {
  Stop-All 'the fixture ends'   # G2: whatever happened above -- once (a no-op after the normal stop)
}
if ($exitCode -eq 0) { "fixture: PASS`nRESULT: PASS" }
elseif ($exitCode -eq 1) { "fixture: FAIL ($($failed -join '; '))`nRESULT: FAIL" }
else { "FAIL  SETUP fixture: $($failed -join '; ')`nRESULT: INVALID" }
exit $exitCode
