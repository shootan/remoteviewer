param([string]$Tag = 'ke_r1', [int]$HostSec = 2400)
# keepalive-r2 ② e (kept model A) AVD fixture, from feat-keepalive-multi km2_fixture.ps1: a REAL server.js on loopback
# (its relay on, the test account only) and four pinned TEST hosts, each with its own harness / layered (alpha 1,
# click-through) target window / staged copy / LOCALAPPDATA / machine id, bound to 127.0.0.1, input injection off:
#   ke-pc-a ff00ff, ke-pc-b 00ffff, ke-pc-c ffff00 -- this branch's host (standby entries)
#   ke-pc-old ff8000 -- the pinned test host of a build from BEFORE standby (fix-capture-lifecycle-baseline): it never
#                       answers a StandbyHello (the "older host" of model A)
# Ports 18410 (http) / 18411 (observe) / 18443 (relay); the AVD reaches them at 10.0.2.2.
$ErrorActionPreference = 'Continue'
$wt = 'C:\work\gnlink-wt\feat-apk-0233'
$root = "$wt\.claude\e2e\$Tag"
New-Item -ItemType Directory -Force "$root\dir" | Out-Null
$sum = "$root\fixture.txt"
"fixture $Tag start $(Get-Date -Format s) (qwinsta: $((qwinsta | Select-String '^>').Line.Trim()))" | Out-File $sum -Encoding utf8
$chars = 'abcdefghijkmnpqrstuvwxyz23456789'
$pw = -join (1..16 | ForEach-Object { $chars[(Get-Random -Maximum $chars.Length)] })
"ketest $pw" | Out-File "$root\fixture_account.txt" -Encoding ascii
$env:REMOTE60_DIR_DATA = "$root\dir\directory-data.json"
$env:REMOTE60_DIR_PORT = '18410'
$env:REMOTE60_DIR_UDP_PORT = '18411'
$env:REMOTE60_LOG_DIR = "$root\dir\logs"
$env:REMOTE60_RELAY_ENABLED = '1'
$env:REMOTE60_RELAY_IP = '10.0.2.2'
# the relay as the HOSTS dial it: they run on this PC, where 10.0.2.2 (the emulator's name for it) is not reachable --
# without it a standby reservation's host proof never arrives (ke_r2: "host-side datagram refused reason=no-host-proof")
$env:REMOTE60_RELAY_HOST_FACING_IP = '127.0.0.1'
$env:REMOTE60_RELAY_PORT = '18443'
$env:REMOTE60_RELAY_ALLOW_IPS = '*'
$env:REMOTE60_RELAY_ALLOW_ACCOUNTS = 'ketest'
$server = "$wt\apps\directory\server.js"
& node $server --add-account ketest $pw *> "$root\dir\add_account.txt"
$node = Start-Process -FilePath node -ArgumentList @($server) -PassThru -NoNewWindow -RedirectStandardOutput "$root\dir\server_stdout.txt" -RedirectStandardError "$root\dir\server_stderr.txt"
$node.Id | Out-File "$root\node.pid" -Encoding ascii
"  directory node pid $($node.Id) :18410 (server.js $((Get-FileHash $server).Hash.ToLower().Substring(0,16)))" | Out-File $sum -Append -Encoding utf8
Start-Sleep 2

$env:REMOTE60_ALLOW_HOST_E2E = '1'
$b = "$wt\build\apps\native_poc\Release"
# 2026-10-28 (test-injection-finding): no older host -- its build predates the power policy and cannot pass the shared
# guard (input none + power off, both whole OFF lines before any client). The old-host cases are NOT RUN.
"  old host: NOT RUN (no build of it passes the injection / power guard)" | Out-File $sum -Append -Encoding utf8
"  harness $((Get-FileHash "$b\remote60_mobile_abr_session_e2e_test.exe").Hash.ToLower()) test host $((Get-FileHash "$b\GNLinkStreamPinnedSelectTest.exe").Hash.ToLower())" | Out-File $sum -Append -Encoding utf8
$hostsDef = @(
  @{ n = 'a'; name = 'ke-pc-a'; mark = 'ff00ff'; exe = 'GNLinkStreamPinnedSelectTest.exe' },
  @{ n = 'b'; name = 'ke-pc-b'; mark = '00ffff'; exe = 'GNLinkStreamPinnedSelectTest.exe' },
  @{ n = 'c'; name = 'ke-pc-c'; mark = 'ffff00'; exe = 'GNLinkStreamPinnedSelectTest.exe' })
foreach ($h in $hostsDef) {
  $out = "$root\host_$($h.n)"
  New-Item -ItemType Directory -Force $out | Out-Null
  '' | Out-File "$out\fault.txt" -Encoding ascii -NoNewline
  $env:REMOTE60_PINNED_TEST_FAULT_FILE = "$out\fault.txt"
  $env:REMOTE60_PINNED_TEST_MACHINE_ID = "ke-fixture-machine-$($h.n)"
  $proc = Start-Process -FilePath "$b\remote60_mobile_abr_session_e2e_test.exe" -ArgumentList @('--scenario', 'directoryhost',
    '--dir-url', 'http://127.0.0.1:18410', '--dir-id', 'ketest', '--dir-pw', $pw, '--host-name', $h.name, '--target-mark', $h.mark,
    '--host-exe', $h.exe, '--external-sec', "$HostSec", '--watchdog-sec', "$($HostSec + 400)",
    '--out', $out) -PassThru -NoNewWindow -RedirectStandardOutput "$out\harness_stdout.txt" -RedirectStandardError "$out\harness_stderr.txt"
  $proc.Id | Out-File "$root\host_$($h.n).pid" -Encoding ascii
  $deadline = (Get-Date).AddSeconds(60)
  while (-not (Test-Path "$out\directoryhost_ready.txt") -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
  "  host $($h.name) ($($h.exe)) harness pid $($proc.Id) ready: $(Get-Content "$out\directoryhost_ready.txt" -ErrorAction SilentlyContinue)" | Out-File $sum -Append -Encoding utf8
}
$env:REMOTE60_PINNED_TEST_FAULT_FILE = ''
$env:REMOTE60_PINNED_TEST_MACHINE_ID = ''
Start-Sleep 4
try {
  $d = Get-Content "$root\dir\directory-data.json" -Raw | ConvertFrom-Json
  $hosts = @($d.hosts.PSObject.Properties | ForEach-Object { "$($_.Value.hostName)=$($_.Value.hostId)" }) -join ' '
  "  directory hosts: $hosts" | Out-File $sum -Append -Encoding utf8
} catch { "  directory read failed ($_)" | Out-File $sum -Append -Encoding utf8 }
Get-Content $sum
