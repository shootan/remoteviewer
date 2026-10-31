# ke-r26 (Codex codex_apk_decoder_liveness_src_r5.md, verifier go): ONE slot (held by sh_run.ps1 -NativeExe), in it:
#   1. 30 s adb stability watch of emulator-5554 (the SDK client by absolute path; listener on 5037 read only)
#   2. ke-r26-sh-lt    KE_PLAN=e2-sh     (default sharpening: E1 + E2 + 60 s watch)
#   3. ke-r26-shoff-lt KE_PLAN=e2-shoff  (Settings SH-off:   E1 + E2 + 60 s watch)
# The r9 harness (C:\work\gnlink-wt\feat-apk-0233\.claude\e2e, snapshot avd_guard_r9_snapshot) unchanged; each run its own
# run folder / id. ke_slot.ps1 is called WITHOUT redirection (it writes its own slot.txt; a redirected fixture launcher
# hangs on inherited handles). Unstable adb -> both runs NOT RUN (INVALID), nothing retried.
$ErrorActionPreference = 'Continue'
$e = 'C:\work\gnlink-wt\feat-apk-0233\.claude\e2e'
$apkDir = 'C:\work\gnlink-wt\apk-decoder-liveness\apk_out'
$pin = '7035cf5ec7f99f7b5662a9f5395f9dccfc3b6fe42583d0b9550124a0c3496606'
$adb = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
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
"adb $adb sha256=$((Get-FileHash $adb).Hash.ToLower())"
if ((Get-FileHash $adb).Hash.ToLower() -ne $pin) { "INVALID: the adb client is not the pinned one -- no run"; exit 2 }
foreach ($f in 'ke-off-lt-r5.apk', 'ke-on-lt-r5.apk') { "apk $f $((Get-FileHash "$apkDir\$f").Hash.ToLower())" }
"ke_avd.py $((Get-FileHash "$e\ke_avd.py").Hash.ToLower())  ke_slot.ps1 $((Get-FileHash "$e\ke_slot.ps1").Hash.ToLower())"

# ---- 1. 30 s stability watch
$listeners = @(); $states = @(); $boots = @()
for ($i = 0; $i -lt 7; ++$i) {
  $l = Listener
  $s = Invoke-Adb @('-s', 'emulator-5554', 'get-state')
  $b = Invoke-Adb @('-s', 'emulator-5554', 'shell', 'getprop', 'sys.boot_completed')
  $id = Invoke-Adb @('-s', 'emulator-5554', 'shell', 'cat', '/proc/sys/kernel/random/boot_id')
  "watch $(Now) listener[$l] state[$s] boot[$b] boot_id[$id]"
  $listeners += $l; $states += $s; $boots += $id
  if ($i -lt 6) { Start-Sleep -Seconds 5 }
}
$stable = (@($listeners | Select-Object -Unique).Count -eq 1) -and ($listeners[0] -ne 'none') -and
          (@($states | Where-Object { $_ -ne "rc=0 'device'" }).Count -eq 0) -and (@($boots | Select-Object -Unique).Count -eq 1)
"adb watch: $(if ($stable) { 'STABLE' } else { 'NOT STABLE' }) (listener same=$(@($listeners | Select-Object -Unique).Count -eq 1), all device=$(@($states | Where-Object { $_ -ne "rc=0 'device'" }).Count -eq 0), boot same=$(@($boots | Select-Object -Unique).Count -eq 1))"
if (-not $stable) { "INVALID: adb not stable over 30 s -- ke-r26-sh-lt and ke-r26-shoff-lt NOT RUN"; exit 2 }

# ---- 2 + 3. the two runs, back to back
$env:KE_RELAY_TRACE = '1'
$env:KE_SERVER_JS = 'C:\work\gnlink-wt\apk-decoder-liveness\apps\directory\server.js'
$rcs = @{}
foreach ($run in @(@{ tag = 'ke-r26-sh-lt'; plan = 'e2-sh' }, @{ tag = 'ke-r26-shoff-lt'; plan = 'e2-shoff' })) {
  $env:KE_PLAN = $run.plan
  "=== $($run.tag) KE_PLAN=$($run.plan) start $(Now)"
  & powershell -NoProfile -ExecutionPolicy Bypass -File "$e\ke_slot.ps1" -Tag $run.tag -ApkOff "$apkDir\ke-off-lt-r5.apk" -ApkOn "$apkDir\ke-on-lt-r5.apk" -AdbSha256 $pin
  $rcs[$run.tag] = $LASTEXITCODE
  "=== $($run.tag) end $(Now) rc=$LASTEXITCODE"
}
Remove-Item env:KE_PLAN, env:KE_RELAY_TRACE, env:KE_SERVER_JS -ErrorAction SilentlyContinue
"batch end $(Now): sh rc=$($rcs['ke-r26-sh-lt']) shoff rc=$($rcs['ke-r26-shoff-lt'])"
exit 0
