param([string]$Tag = 'km2_r1', [int]$HostSec = 2400)
# keepalive-multi r2 V1 (from km_fixture.ps1): the fixture directory (+ delay proxy :18320) and SIX fail-closed pinned
# TEST hosts, each its own harness / painted window / staged copy / LOCALAPPDATA / machine id (hostIds never collide),
# input injection off, bound to 127.0.0.1:
#   km-pc-a ff00ff, km-pc-b 00ffff, km-pc-c ffff00, km-pc-d 00ff00 (also reachable DIRECT: km_direct_fwd.js forwards its
#   private candidate), km-pc-x 808080 (stopped on cue: its Hello never answers -- r2 M1), km-pc-old ff8000 (behaves
#   as a host from before the pause: REMOTE60_PINNED_TEST_OLD_HOST=1).
$ErrorActionPreference = 'Continue'
$wt = 'C:\work\gnlink-wt\feat-keepalive-multi'
$root = "$wt\.claude\e2e\$Tag"
New-Item -ItemType Directory -Force "$root\dir" | Out-Null
$sum = "$root\fixture.txt"
"fixture $Tag start $(Get-Date -Format s) (qwinsta: $((qwinsta | Select-String '^>').Line.Trim()))" | Out-File $sum -Encoding utf8
$chars = 'abcdefghijkmnpqrstuvwxyz23456789'
$pw = -join (1..16 | ForEach-Object { $chars[(Get-Random -Maximum $chars.Length)] })
"katest $pw" | Out-File "$root\fixture_account.txt" -Encoding ascii
$env:REMOTE60_DIR_DATA = "$root\dir\directory-data.json"
$env:REMOTE60_DIR_PORT = '18310'
$env:REMOTE60_DIR_UDP_PORT = '18311'
$env:REMOTE60_LOG_DIR = "$root\dir\logs"
$env:REMOTE60_RELAY_ENABLED = '1'
$env:REMOTE60_RELAY_IP = '10.0.2.2'
$env:REMOTE60_RELAY_PORT = '18343'
$env:REMOTE60_RELAY_ALLOW_IPS = '*'
$env:REMOTE60_RELAY_ALLOW_ACCOUNTS = 'katest'
$server = "$wt\apps\directory\server.js"
& node $server --add-account katest $pw *> "$root\dir\add_account.txt"
$node = Start-Process -FilePath node -ArgumentList @($server) -PassThru -NoNewWindow -RedirectStandardOutput "$root\dir\server_stdout.txt" -RedirectStandardError "$root\dir\server_stderr.txt"
$node.Id | Out-File "$root\node.pid" -Encoding ascii
"" | Out-File "$root\delay_connect" -Encoding ascii -NoNewline
$proxy = Start-Process -FilePath node -ArgumentList @("$wt\.claude\e2e\ka_delay_proxy.js", '18320', '18310', "$root\delay_connect", "$root\dir\proxy.log") -PassThru -NoNewWindow
$proxy.Id | Out-File "$root\proxy.pid" -Encoding ascii
"  directory node pid $($node.Id) :18310 / proxy pid $($proxy.Id) :18320" | Out-File $sum -Append -Encoding utf8
Start-Sleep 2

$env:REMOTE60_ALLOW_HOST_E2E = '1'
$b = "$wt\build-local\apps\native_poc\Release"
"  harness $((Get-FileHash "$b\remote60_mobile_abr_session_e2e_test.exe").Hash.ToLower()) test host $((Get-FileHash "$b\GNLinkStreamPinnedSelectTest.exe").Hash.ToLower())" | Out-File $sum -Append -Encoding utf8
$hostsDef = @(
  @{ n = 'a'; name = 'km-pc-a'; mark = 'ff00ff'; old = '0' }, @{ n = 'b'; name = 'km-pc-b'; mark = '00ffff'; old = '0' },
  @{ n = 'c'; name = 'km-pc-c'; mark = 'ffff00'; old = '0' }, @{ n = 'd'; name = 'km-pc-d'; mark = '00ff00'; old = '0' },
  @{ n = 'x'; name = 'km-pc-x'; mark = '808080'; old = '0' }, @{ n = 'old'; name = 'km-pc-old'; mark = 'ff8000'; old = '1' })
foreach ($h in $hostsDef) {
  $out = "$root\host_$($h.n)"
  New-Item -ItemType Directory -Force $out | Out-Null
  '' | Out-File "$out\fault.txt" -Encoding ascii -NoNewline
  $env:REMOTE60_PINNED_TEST_FAULT_FILE = "$out\fault.txt"
  $env:REMOTE60_PINNED_TEST_MACHINE_ID = "km2-fixture-machine-$($h.n)"
  $env:REMOTE60_PINNED_TEST_OLD_HOST = $h.old
  $proc = Start-Process -FilePath "$b\remote60_mobile_abr_session_e2e_test.exe" -ArgumentList @('--scenario', 'directoryhost',
    '--dir-url', 'http://127.0.0.1:18310', '--dir-id', 'katest', '--dir-pw', $pw, '--host-name', $h.name, '--target-mark', $h.mark,
    '--host-exe', 'GNLinkStreamPinnedSelectTest.exe', '--external-sec', "$HostSec", '--watchdog-sec', "$($HostSec + 400)",
    '--out', $out) -PassThru -NoNewWindow -RedirectStandardOutput "$out\harness_stdout.txt" -RedirectStandardError "$out\harness_stderr.txt"
  $proc.Id | Out-File "$root\host_$($h.n).pid" -Encoding ascii
  $deadline = (Get-Date).AddSeconds(60)
  while (-not (Test-Path "$out\directoryhost_ready.txt") -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
  "  host $($h.name) harness pid $($proc.Id) ready: $(Get-Content "$out\directoryhost_ready.txt" -ErrorAction SilentlyContinue) old=$($h.old)" | Out-File $sum -Append -Encoding utf8
}
$env:REMOTE60_PINNED_TEST_FAULT_FILE = ''
$env:REMOTE60_PINNED_TEST_MACHINE_ID = ''
$env:REMOTE60_PINNED_TEST_OLD_HOST = '0'
Start-Sleep 4
try {
  $d = Get-Content "$root\dir\directory-data.json" -Raw | ConvertFrom-Json
  $hosts = @($d.hosts.PSObject.Properties | ForEach-Object { "$($_.Value.hostName)=$($_.Value.hostId)/$($_.Value.machineId)" }) -join ' '
  "  directory hosts: $hosts" | Out-File $sum -Append -Encoding utf8
  $dh = @($d.hosts.PSObject.Properties | Where-Object { $_.Value.hostName -eq 'km-pc-d' })[0].Value
  "  km-pc-d record: localIps=$($dh.localIps -join ',') localUdpPort=$($dh.localUdpPort) publicUdpPort=$($dh.publicUdpPort)" | Out-File $sum -Append -Encoding utf8
  $lan = @($dh.localIps | Where-Object { $_ -like '192.168.*' })[0]
  $port = if ($dh.localUdpPort) { $dh.localUdpPort } else { $dh.publicUdpPort }
  $fwd = Start-Process -FilePath node -ArgumentList @("$wt\.claude\e2e\km_direct_fwd.js", $lan, "$port", "$lan,127.0.0.1", "$root\dir\direct_fwd.log") -PassThru -NoNewWindow
  $fwd.Id | Out-File "$root\fwd.pid" -Encoding ascii
  "  direct forwarder for km-pc-d: ${lan}:$port -> 127.0.0.1:$port (pid $($fwd.Id))" | Out-File $sum -Append -Encoding utf8
} catch { "  directory read / forwarder: failed ($_)" | Out-File $sum -Append -Encoding utf8 }
Get-Content $sum
