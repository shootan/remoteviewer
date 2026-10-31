# apk-decoder-inbox A/B (Codex codex_apk_inbox_plan_src_r1/r2; plan caplife/plan_apk_inbox_ab_r2.md + r3 절): ONE slot (held by
# sh_run.ps1 -NativeExe), in it: a 30 s adb stability watch of emulator-5554, then arm A (9f9a91c ke-off.apk) and arm B
# (ac587dd plain-debug-r5.apk), each KE_PLAN=e2-shoff (Settings SH-off, host a only), KE_WATCH_SEC=120, the local
# relay trace on (79b5c25 server.js: rxBytes). Harness r12 (feat-apk-0233\.claude\e2e, snapshot avd_guard_r12_snapshot).
# ke_slot.ps1 called WITHOUT redirection. Unstable adb -> both NOT RUN (INVALID), nothing retried.
$ErrorActionPreference = 'Continue'
$e = 'C:\work\gnlink-wt\feat-apk-0233\.claude\e2e'
$pin = '7035cf5ec7f99f7b5662a9f5395f9dccfc3b6fe42583d0b9550124a0c3496606'
$adb = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
$server = 'C:\work\gnlink-wt\apk-decoder-liveness\apps\directory\server.js'
# apk-inbox C r1 (for the VERIFIER, after C SOURCE; not run by the worker): the B-only batch (ab_shoff_batch_b_only_r3.ps1,
# itself the reviewed r3 with arm A removed) with its one arm replaced by C -- plain-debug-c1.apk (53e3fdb+, no app trace,
# the same fixture directory as B's plain-debug-r5). Everything else unchanged: the adb pin and 30 s watch, r12, SH-off,
# KE_WATCH_SEC=120, the relay trace, server.js 372e6eb8, the stop at a nonzero rc.
$arms = @(
  @{ tag = 'c1-shoff'; off = 'C:\work\gnlink-wt\apk-inbox-c\apk_out\plain-debug-c1.apk';
     on = 'C:\work\gnlink-wt\apk-inbox-c\apk_out\on-debug-c1.apk';
     offSha = '9496645943955da504032f94fb898198abf5dfef12a8c4e6995ed8284c2de3db' })
function Now { (Get-Date).ToString('yyyy-MM-ddTHH:mm:ss.fff') }
function Invoke-Adb([string[]]$a) {
  $psi = New-Object Diagnostics.ProcessStartInfo $adb
  $psi.Arguments = ($a -join ' ')
  $psi.UseShellExecute = $false; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true; $psi.CreateNoWindow = $true
  $p = [Diagnostics.Process]::Start($psi)
  $o = $p.StandardOutput.ReadToEndAsync(); $er = $p.StandardError.ReadToEndAsync()
  if (-not $p.WaitForExit(15000)) { try { $p.Kill() } catch {}; return "timeout" }
  "rc=$($p.ExitCode) '$($o.Result.Trim())'$(if ($er.Result.Trim()) { " err='$(($er.Result -replace '\s+',' ').Trim())'" })"
}
function Listener {
  try {
    $c = Get-NetTCPConnection -LocalPort 5037 -State Listen -ErrorAction Stop | Select-Object -First 1
    $pr = Get-Process -Id $c.OwningProcess -ErrorAction Stop
    "pid=$($pr.Id) path=$($pr.Path) started=$($pr.StartTime.ToString('s'))"
  } catch { "none" }
}
"batch start $(Now)"
if ((Get-FileHash $adb).Hash.ToLower() -ne $pin) { "INVALID: the adb client is not the pinned one -- no run"; exit 2 }
"server.js $((Get-FileHash $server).Hash.ToLower())  ke_avd.py $((Get-FileHash "$e\ke_avd.py").Hash.ToLower())"
foreach ($a in $arms) {
  $h = (Get-FileHash $a.off).Hash.ToLower()
  "apk $($a.tag) $h"
  if ($h -ne $a.offSha) { "INVALID: $($a.tag) APK is not the fixed one -- no run"; exit 2 }
}
$listeners = @(); $states = @(); $boots = @()
for ($i = 0; $i -lt 7; ++$i) {
  $l = Listener
  $s = Invoke-Adb @('-s', 'emulator-5554', 'get-state')
  $id = Invoke-Adb @('-s', 'emulator-5554', 'shell', 'cat', '/proc/sys/kernel/random/boot_id')
  "watch $(Now) listener[$l] state[$s] boot_id[$id]"
  $listeners += $l; $states += $s; $boots += $id
  if ($i -lt 6) { Start-Sleep -Seconds 5 }
}
$stable = (@($listeners | Select-Object -Unique).Count -eq 1) -and ($listeners[0] -ne 'none') -and
          (@($states | Where-Object { $_ -ne "rc=0 'device'" }).Count -eq 0) -and (@($boots | Select-Object -Unique).Count -eq 1) -and
          (@($boots | Where-Object { $_ -notmatch "^rc=0 '[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}'$" }).Count -eq 0)
"adb watch: $(if ($stable) { 'STABLE' } else { 'NOT STABLE' })"
if (-not $stable) { "INVALID: adb not stable over 30 s -- A and B NOT RUN"; exit 2 }
$env:KE_PLAN = 'e2-shoff'; $env:KE_WATCH_SEC = '120'; $env:KE_RELAY_TRACE = '1'; $env:KE_SERVER_JS = $server
foreach ($a in $arms) {
  "=== $($a.tag) start $(Now)"
  & powershell -NoProfile -ExecutionPolicy Bypass -File "$e\ke_slot.ps1" -Tag $a.tag -ApkOff $a.off -ApkOn $a.on -AdbSha256 $pin
  $armRc = $LASTEXITCODE
  "=== $($a.tag) end $(Now) rc=$armRc"
  if ($null -eq $armRc -or $armRc -ne 0) {
    "NOT RUN: any remaining arm; stopped after the first nonzero/unknown arm rc"
    Remove-Item env:KE_PLAN, env:KE_WATCH_SEC, env:KE_RELAY_TRACE, env:KE_SERVER_JS -ErrorAction SilentlyContinue
    if ($null -eq $armRc) { exit 2 }
    exit ([int]$armRc)
  }
}
Remove-Item env:KE_PLAN, env:KE_WATCH_SEC, env:KE_RELAY_TRACE, env:KE_SERVER_JS -ErrorAction SilentlyContinue
"batch end $(Now)"
exit 0
