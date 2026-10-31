# ka-pc V1 (Codex codex_ka_pc_v1_src.md): the managed e2e ONCE, inside e2e_job_run's verified kill-on-close job with a
# 600 s wall bound (its stop-file made before the start, --grace-sec 600). This PowerShell owns the GPU slot OUTSIDE that
# job, so its finally always runs: the slot is taken with CreateNew and only its own file is deleted. No old host.
$ErrorActionPreference = 'Stop'
$slot = 'C:\work\gnlink-wt\SLOT_REQUEST.txt'
$jobRun = 'C:\work\gnlink-wt\feat-apk-0233\build\apps\native_poc\Release\e2e_job_run.exe'
$exe = 'C:\work\gnlink-wt\ka-pc-build\apps\native_poc\Release\remote60_ka_pc_managed_e2e_test.exe'
$root = 'D:\remote\remote\.claude\test-tmp\ka-managed-v1-1'
$log = 'D:\remote\remote\.claude\np_impl_logs\ka_managed_v1-1.log'
if ((Get-FileHash $jobRun).Hash -ne 'DC4C5049E8E9D32A8C9096005A2702C95EF30CCE2D3F1998BD4005DA8C89462F') { 'job_run hash mismatch -- not run'; exit 2 }
if ((Get-FileHash $exe).Hash.Substring(0, 16) -ne '163EE8F6CF6E7274') { 'managed exe hash mismatch -- not run'; exit 2 }
if (Test-Path -LiteralPath $root) { "root $root exists -- not reused"; exit 2 }
if (Test-Path -LiteralPath $log) { "log $log exists -- not overwritten"; exit 2 }
$mine = $false
while (-not $mine) {
  if (-not (Test-Path $slot)) {
    try {
      $fs = [IO.File]::Open($slot, 'CreateNew', 'Write')
      $bb = [Text.Encoding]::UTF8.GetBytes('ka-pc V1 managed e2e (worker 2) ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
      $fs.Write($bb, 0, $bb.Length); $fs.Close()
      $mine = $true
    } catch { }
  }
  if (-not $mine) { Start-Sleep -Seconds 15 }
}
"slot taken " + (Get-Date -Format 'HH:mm:ss')
try {
  New-Item -ItemType Directory -Path $root | Out-Null
  $stop = Join-Path $root 'v1.stop'
  New-Item -ItemType File -Path $stop | Out-Null   # made before the start: the run has 600 s, then it is forced
  $env:REMOTE60_KA_REPO = 'C:\work\gnlink-wt\ka-pc'
  Remove-Item Env:REMOTE60_KA_OLD_STREAM -ErrorAction SilentlyContinue
  $env:REMOTE60_TEST_TMP = $root
  $t0 = Get-Date
  & $jobRun --stop-file $stop --grace-sec 600 --cmd "`"$exe`"" *> $log
  $rc = $LASTEXITCODE
  $secs = [int]((Get-Date) - $t0).TotalSeconds
  $l = Get-Content $log
  "managed:v1-1 rc=$rc secs=$secs PASS=" + ($l | Select-String '^PASS').Count + " FAIL=" + ($l | Select-String '^FAIL').Count
  ($l | Select-String '^job_run:').Line
} finally {
  if ($mine) { Remove-Item -LiteralPath $slot -Force }
  "slot released " + (Get-Date -Format 'HH:mm:ss')
}
