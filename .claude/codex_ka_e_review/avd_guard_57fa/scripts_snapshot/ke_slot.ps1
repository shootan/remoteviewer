param([string]$Tag = 'ke_r1')
# keepalive-r2 ② e: one AVD bundle (run under sh_run.ps1 -NativeExe, which holds the measuring slot): ke_fixture.ps1's
# directory + four pinned test hosts, ke_avd.py on the shared AVD (emulator-5554), then every process this bundle
# started is stopped (by its own pid -- never by name) so nothing keeps the runner's output handle open.
$ErrorActionPreference = 'Continue'
$wt = 'C:\work\gnlink-wt\feat-apk-0233'
$e = "$wt\.claude\e2e"
$root = "$e\$Tag"
New-Item -ItemType Directory -Force $root | Out-Null
$sum = "$root\slot.txt"
"slot $Tag start $(Get-Date -Format s) (qwinsta: $((qwinsta | Select-String '^>').Line.Trim()))" | Out-File $sum -Append -Encoding utf8
"  apk off $((Get-FileHash "$e\ke-off.apk").Hash.ToLower()) on $((Get-FileHash "$e\ke-on.apk").Hash.ToLower())" | Out-File $sum -Append -Encoding utf8
# NOT "& powershell ... | Out-Null": the fixture's node / harness processes inherit that pipe and keep it open until they
# end, so the pipe never closes and this script never gets past it (2026-10-28 ke_r1, as on 2026-10-15). Its own process,
# its output to a file, waited for alone.
$fx = Start-Process -FilePath powershell -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "$e\ke_fixture.ps1",
  '-Tag', $Tag, '-HostSec', '2400') -PassThru -NoNewWindow -RedirectStandardOutput "$root\fixture_stdout.txt" -RedirectStandardError "$root\fixture_stderr.txt"
$fx.WaitForExit()
$env:KA_SERIAL = 'emulator-5554'
& python -I "$e\ke_avd.py" $root "$e\ke-off.apk" "$e\ke-on.apk" "$root\avd" > "$root\avd_stdout.txt" 2>&1
$avdExit = $LASTEXITCODE
"  avd exit=$avdExit $((Get-Content "$root\avd_stdout.txt" | Select-String 'run end').Line)" | Out-File $sum -Append -Encoding utf8
foreach ($h in 'a', 'b', 'c', 'old') { New-Item -ItemType File "$root\host_$h\stop" -Force | Out-Null }
foreach ($h in 'a', 'b', 'c', 'old') { try { Wait-Process -Id ([int](Get-Content "$root\host_$h.pid")) -Timeout 60 -ErrorAction Stop } catch {} }
foreach ($h in 'a', 'b', 'c', 'old') { try { Stop-Process -Id ([int](Get-Content "$root\host_$h.pid")) -Confirm:$false -ErrorAction Stop } catch {} }
if (Test-Path "$root\node.pid") { Stop-Process -Id ([int](Get-Content "$root\node.pid")) -Confirm:$false -ErrorAction SilentlyContinue }
"slot $Tag done $(Get-Date -Format s)" | Out-File $sum -Append -Encoding utf8
Get-Content "$root\avd_stdout.txt" | Where-Object { $_ -match '(PASS|FAIL) |run end' }
exit $avdExit
