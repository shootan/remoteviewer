param([string]$Tag = 'km_r1')
# keepalive-multi r1: the 2-host AVD bundle (km_avd.py) on this worktree's fixture APK and two fail-closed pinned test
# hosts (km_fixture.ps1). SLOT_REQUEST.txt is made and removed by the caller.
$ErrorActionPreference = 'Continue'
$wt = 'C:\work\gnlink-wt\feat-keepalive-multi'
$e = "$wt\.claude\e2e"
$root = "$e\$Tag"
New-Item -ItemType Directory -Force $root | Out-Null
$sum = "$root\slot.txt"
"slot $Tag start $(Get-Date -Format s) (qwinsta: $((qwinsta | Select-String '^>').Line.Trim()))" | Out-File $sum -Append -Encoding utf8
$p = Get-Process GNLinkStream* -ErrorAction SilentlyContinue | Where-Object { $_.Path -like 'C:\work\gnlink-wt\*' -and $_.Path -notlike "$wt\*" }
"  other workers' test hosts running: $(@($p).Count) $(($p | ForEach-Object Path) -join ',')" | Out-File $sum -Append -Encoding utf8
"  fixture apk $((Get-FileHash "$e\km-fixture.apk").Hash.ToLower())" | Out-File $sum -Append -Encoding utf8
& powershell -NoProfile -ExecutionPolicy Bypass -File "$e\km_fixture.ps1" -Tag $Tag -HostSec 1500
$env:KA_SERIAL = 'emulator-5614'
& python -I "$e\km_avd.py" $root "$e\km-fixture.apk" "$root\avd" > "$root\avd_stdout.txt" 2>&1
"  avd exit=$LASTEXITCODE $((Get-Content "$root\avd_stdout.txt" | Select-String 'run end').Line)" | Out-File $sum -Append -Encoding utf8
foreach ($h in 'a', 'b') { New-Item -ItemType File "$root\host_$h\stop" -Force | Out-Null }
foreach ($h in 'a', 'b') { try { Wait-Process -Id ([int](Get-Content "$root\host_$h.pid")) -Timeout 40 -ErrorAction Stop } catch {} }
foreach ($n in 'node', 'proxy') { Stop-Process -Id ([int](Get-Content "$root\$n.pid")) -Confirm:$false -ErrorAction SilentlyContinue }
& python -I "$e\km_host_route.py" $root > "$root\route.txt" 2>&1
"  route exit=$LASTEXITCODE $((Get-Content "$root\route.txt" | Select-String 'ROUTE').Line)" | Out-File $sum -Append -Encoding utf8
& python -I "$e\km_host_park.py" "$root\host_a\directoryhost_host.log" "$root\host_b\directoryhost_host.log" > "$root\park.txt" 2>&1
"slot $Tag done $(Get-Date -Format s)" | Out-File $sum -Append -Encoding utf8
