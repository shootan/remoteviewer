param(
  [Parameter(Mandatory = $true)][string]$Root,
  [Parameter(Mandatory = $true)][string]$RunId,
  [int]$HostSec = 2400,
  [int]$ReadyTimeoutSec = 60,
  [int]$StopWaitSec = 180,  # how long a harness gets to end by itself after its stop
  # what starts a host's harness and the directory -- the product files by default; the orchestration test
  # (ke_orchestration_test.ps1) passes stand-ins here (no host, no node, no AVD)
  [string[]]$HarnessCmd = @(),
  [string[]]$NodeCmd = @()
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
function Start-Owned([string]$name, [string]$exe, [string[]]$argv, [string]$out, [hashtable]$envVars) {
  foreach ($k in $envVars.Keys) { Set-Item "env:$k" $envVars[$k] }
  try {
    $pr = Start-Process -FilePath $exe -ArgumentList $argv -PassThru -NoNewWindow -RedirectStandardOutput "$out.stdout.txt" -RedirectStandardError "$out.stderr.txt"
  } finally {
    foreach ($k in $envVars.Keys) { Remove-Item "env:$k" -ErrorAction SilentlyContinue }
  }
  $null = $pr.Handle  # the handle, taken now: its exit code stays readable after it ends
  $script:children[$name] = $pr
  Say "  started $name pid $($pr.Id) at $($pr.StartTime.ToString('s'))"
  return $pr
}
function Stop-All([string]$why) {
  Say "  stopping ($why)"
  $result = [ordered]@{}
  foreach ($name in @($script:children.Keys)) {
    if ($name -eq 'node') { continue }
    $pr = $script:children[$name]
    New-Item -ItemType File "$Root\$name\stop" -Force | Out-Null  # the harness's own stop: its loop ends, the guard's end runs
    if (-not $pr.WaitForExit($StopWaitSec * 1000)) {
      $pr.Kill()
      $confirmed = $pr.WaitForExit(15000)
      $result[$name] = "killed by its handle (it did not end by itself)$(if (-not $confirmed) { '; its end NOT confirmed' })"
      [void]$script:failed.Add("$name did not end by itself")
    } else {
      $result[$name] = "exit $($pr.ExitCode)"
      if ($pr.ExitCode -ne 0) { [void]$script:failed.Add("$name exit $($pr.ExitCode)") }
    }
  }
  if ($script:children.Contains('node')) {
    $nd = $script:children['node']
    if (-not $nd.HasExited) { $nd.Kill() }
    $result['node'] = if ($nd.WaitForExit(15000)) { 'ended' } else { 'end NOT confirmed' }
    if (-not $nd.HasExited) { [void]$script:failed.Add('node end not confirmed') }
  }
  $lines = @("fixture run=$RunId result") + @($result.Keys | ForEach-Object { "  $_ $($result[$_])" }) +
           @("  failures: $(if ($script:failed.Count) { $script:failed -join '; ' } else { 'none' })")
  $lines | Out-File "$Root\fixture_result.txt" -Encoding utf8
  $lines | ForEach-Object { Say $_ }
}

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
if ($NodeCmd.Count -eq 0) {
  $server = "$wt\apps\directory\server.js"
  foreach ($k in $dirEnv.Keys) { Set-Item "env:$k" $dirEnv[$k] }
  & node $server --add-account ketest $pw *> "$Root\dir\add_account.txt"
  foreach ($k in $dirEnv.Keys) { Remove-Item "env:$k" -ErrorAction SilentlyContinue }
  $NodeCmd = @('node', $server)
  Say "  directory server.js $((Get-FileHash $server).Hash.ToLower().Substring(0,16))"
}
$node = Start-Owned 'node' $NodeCmd[0] @($NodeCmd | Select-Object -Skip 1) "$Root\dir\server" $dirEnv
Start-Sleep 2
if ($node.HasExited) {
  [void]$failed.Add("node ended at once (exit $($node.ExitCode))")
  Stop-All 'the directory did not start'
  "FAIL  SETUP fixture: the directory did not start`nRESULT: INVALID"; exit 2
}

# ---- the hosts, one after another; any failure stops the rest from starting
$b = "$wt\build\apps\native_poc\Release"
if ($HarnessCmd.Count -eq 0) {
  $HarnessCmd = @("$b\remote60_mobile_abr_session_e2e_test.exe")
  Say "  harness $((Get-FileHash $HarnessCmd[0]).Hash.ToLower()) test host $((Get-FileHash "$b\GNLinkStreamPinnedSelectTest.exe").Hash.ToLower())"
}
$hostsDef = @(
  @{ n = 'a'; name = 'ke-pc-a'; mark = 'ff00ff' },
  @{ n = 'b'; name = 'ke-pc-b'; mark = '00ffff' },
  @{ n = 'c'; name = 'ke-pc-c'; mark = 'ffff00' })
foreach ($h in $hostsDef) {
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
  $why = $null
  if (-not $ready -and $pr.HasExited) { $why = "its harness ended before any readiness (exit $($pr.ExitCode))" }
  elseif (-not $ready) { $why = "no readiness within $ReadyTimeoutSec s" }
  elseif ($ready -notmatch "^ready run=$([regex]::Escape($hid)) harness_pid=$($pr.Id) host_pid=\d+ host_log=\S+") {
    $why = "a readiness that is not this run's ready one: $($ready.Trim())"
  }
  if ($why) {
    [void]$failed.Add("$($h.n): $why")
    Say "  host $($h.name): NOT ready -- $why; no further host is started"
    Stop-All "host $($h.n) not ready"
    "FAIL  SETUP fixture: host $($h.n) $why`nRESULT: INVALID"
    exit 2
  }
  Say "  host $($h.name) ready: $($ready.Trim())"
}

# ---- every host ready: said whole, then wait for the bundle's stop, noting a child that ends first
"ready run=$RunId hosts=a,b,c" | Out-File "$Root\fixture_ready.tmp" -Encoding ascii
Move-Item -LiteralPath "$Root\fixture_ready.tmp" -Destination "$Root\fixture_ready.txt"
Say "  fixture ready"
$early = $null
while (-not (Test-Path -LiteralPath "$Root\stop_fixture")) {
  foreach ($name in @($children.Keys)) {
    if (-not $early -and $children[$name].HasExited) {
      $early = "$name ended before the stop (exit $($children[$name].ExitCode))"
      [void]$failed.Add($early)
      Say "  LATE FAILURE: $early"
      "late failure: $early" | Out-File "$Root\fixture_failed.txt" -Encoding utf8
    }
  }
  Start-Sleep 1
}
Stop-All 'the bundle asked'
if ($failed.Count -gt 0) { "fixture: FAIL ($($failed -join '; '))`nRESULT: FAIL"; exit 1 }
"fixture: PASS`nRESULT: PASS"
exit 0
