param([string]$Tag = 'km2_r1', [string]$Avd = 'km2_avd.py', [string]$Apk = 'km2-fixture.apk')
# keepalive-multi r2 V1: one AVD bundle on this worktree's fixture APK and km2_fixture.ps1's six fail-closed pinned test
# hosts. SLOT_REQUEST.txt is made (CreateNew) and removed by the caller, after the measuring runner's current run ended.
$ErrorActionPreference = 'Continue'
$wt = 'C:\work\gnlink-wt\feat-keepalive-multi'
$e = "$wt\.claude\e2e"
$root = "$e\$Tag"
New-Item -ItemType Directory -Force $root | Out-Null
$sum = "$root\slot.txt"
"slot $Tag start $(Get-Date -Format s) (qwinsta: $((qwinsta | Select-String '^>').Line.Trim()))" | Out-File $sum -Append -Encoding utf8
$log = Get-ChildItem C:\work\gnlink-wt\fix-catchup-only\.claude\*runs.log | Sort-Object LastWriteTime -Descending | Select-Object -First 1
"  runner $($log.Name) last: $(Get-Content $log.FullName -Tail 1)" | Out-File $sum -Append -Encoding utf8
$p = Get-Process GNLinkStream* -ErrorAction SilentlyContinue | Where-Object { $_.Path -like 'C:\work\gnlink-wt\*' -and $_.Path -notlike "$wt\*" }
"  other workers' test hosts running: $(@($p).Count) $(($p | ForEach-Object Path) -join ',')" | Out-File $sum -Append -Encoding utf8
"  apk $Apk $((Get-FileHash "$e\$Apk").Hash.ToLower())" | Out-File $sum -Append -Encoding utf8
& powershell -NoProfile -ExecutionPolicy Bypass -File "$e\km2_fixture.ps1" -Tag $Tag -HostSec 2400
$env:KA_SERIAL = 'emulator-5614'
& python -I "$e\$Avd" $root "$e\$Apk" "$root\avd" > "$root\avd_stdout.txt" 2>&1
"  avd exit=$LASTEXITCODE $((Get-Content "$root\avd_stdout.txt" | Select-String 'run end').Line)" | Out-File $sum -Append -Encoding utf8
foreach ($h in 'a', 'b', 'c', 'd', 'x', 'old') { New-Item -ItemType File "$root\host_$h\stop" -Force | Out-Null }
foreach ($h in 'a', 'b', 'c', 'd', 'x', 'old') { try { Wait-Process -Id ([int](Get-Content "$root\host_$h.pid")) -Timeout 40 -ErrorAction Stop } catch {} }
foreach ($n in 'node', 'proxy', 'fwd') { if (Test-Path "$root\$n.pid") { Stop-Process -Id ([int](Get-Content "$root\$n.pid")) -Confirm:$false -ErrorAction SilentlyContinue } }
& python -I "$e\km2_hosts.py" $root > "$root\hosts.txt" 2>&1
& python -I "$e\km_host_park.py" "$root\host_a\directoryhost_host.log" "$root\host_b\directoryhost_host.log" "$root\host_c\directoryhost_host.log" "$root\host_d\directoryhost_host.log" > "$root\park.txt" 2>&1
"slot $Tag done $(Get-Date -Format s)" | Out-File $sum -Append -Encoding utf8
