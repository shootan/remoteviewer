# verifier copy of the worker runner (paths changed only): independent build verify-0164 @53fced4
# rel-0164 r1, run 2 (binaries built INSIDE the repository: .claude/worktrees/rel-0164-e2e at the same commit -- the admission harness refuses a build directory outside it): the Host-launching e2e of the integrated candidate (53fced4; test build rel-0164-testbuild), each inside
# the reviewed e2e_job_run (verified kill-on-close job, a wall bound) in a NEW test root, all under ONE GPU slot held by
# this PowerShell OUTSIDE the jobs (CreateNew; its finally deletes only its own file). Every harness here uses the
# shared guard (input none + power off + the two-line gate + job). old-host: not set (NOT RUN); real-input 4: not run;
# viewer_prelogin_login: never. An INVALID (rc 2) stops the chain.
$ErrorActionPreference = 'Stop'
$slot = 'C:\work\gnlink-wt\SLOT_REQUEST.txt'
$jobRun = 'C:\work\gnlink-wt\feat-apk-0233\build\apps\native_poc\Release\e2e_job_run.exe'
$bin = 'D:\remote\remote\.claude\worktrees\verify-0164\build-verify\apps\native_poc\Release'
$logs = 'D:\remote\remote\.claude\verify_0164\host_e2e'
if ((Get-FileHash $jobRun).Hash -ne 'DC4C5049E8E9D32A8C9096005A2702C95EF30CCE2D3F1998BD4005DA8C89462F') { 'job_run hash mismatch -- nothing run'; exit 2 }
if (Test-Path -LiteralPath $logs) { "$logs exists -- not reused"; exit 2 }
New-Item -ItemType Directory -Path $logs | Out-Null
Get-ChildItem "$bin\*.exe" | Get-FileHash | ForEach-Object { "$($_.Hash)  $(Split-Path $_.Path -Leaf)" } | Out-File "$logs\hashes_before.txt" -Encoding utf8
$runs = @(
  @{ name = 'host_standby_admission_e2e';    exe = 'remote60_host_standby_admission_e2e_test.exe'; args = '';                                   grace = 900 },
  @{ name = 'host_standby_admission_e2e_pc'; exe = 'remote60_host_standby_admission_e2e_test.exe'; args = ' --product-client --real-directory'; grace = 900 },
  @{ name = 'host_punch_reply_e2e';          exe = 'remote60_host_punch_reply_e2e_test.exe';       args = '';                                   grace = 900 },
  @{ name = 'ka_pc_managed_e2e';             exe = 'remote60_ka_pc_managed_e2e_test.exe';          args = '';                                   grace = 600 },
  @{ name = 'ka_pc_shell_e2e';               exe = 'remote60_ka_pc_shell_e2e_test.exe';            args = '';                                   grace = 900 }
)
$mine = $false
while (-not $mine) {
  if (-not (Test-Path $slot)) {
    try {
      $fs = [IO.File]::Open($slot, 'CreateNew', 'Write')
      $bb = [Text.Encoding]::UTF8.GetBytes('rel-0164 verifier independent host e2e (remote#7fsfy22z) ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
      $fs.Write($bb, 0, $bb.Length); $fs.Close()
      $mine = $true
    } catch { }
  }
  if (-not $mine) { Start-Sleep -Seconds 15 }
}
"slot taken " + (Get-Date -Format 'HH:mm:ss') | Tee-Object -Append "$logs\summary.txt"
try {
  $foreign = Get-Process GNLinkStream -ErrorAction SilentlyContinue | Where-Object { $_.Id -ne 27044 }
  if ($foreign) { "foreign GNLinkStream running: $($foreign.Id -join ',') -- nothing run" | Tee-Object -Append "$logs\summary.txt"; exit 5 }
  $env:REMOTE60_ALLOW_HOST_E2E = '1'
  $env:REMOTE60_KA_REPO = 'D:\remote\remote\.claude\worktrees\verify-0164'
  Remove-Item Env:REMOTE60_KA_OLD_STREAM -ErrorAction SilentlyContinue
  foreach ($r in $runs) {
    $root = "D:\remote\remote\.claude\test-tmp\verify-0164-host-$($r.name)"
    New-Item -ItemType Directory -Path $root | Out-Null   # NEW (fails if it exists)
    $stop = Join-Path $root 'bound.stop'
    New-Item -ItemType File -Path $stop | Out-Null
    $env:REMOTE60_TEST_TMP = $root
    $log = "$logs\$($r.name).log"
    $t0 = Get-Date
    $ErrorActionPreference = 'Continue'   # r3: a native stderr line under *> is output, not a terminating error
    & $jobRun --stop-file $stop --grace-sec $r.grace --cmd ("`"$bin\$($r.exe)`"" + $r.args) *> $log
    $rc = $LASTEXITCODE
    $l = Get-Content $log
    $last = ($l | Where-Object { $_.Trim() -and $_ -notmatch '^job_run:' } | Select-Object -Last 1)
    ("{0,-30} rc={1} secs={2} PASS={3} FAIL={4} | {5} | last: {6}" -f $r.name, $rc, [int]((Get-Date) - $t0).TotalSeconds,
      ($l | Select-String '^PASS').Count, ($l | Select-String '^FAIL').Count, ($l | Select-String '^job_run:').Line, $last) |
      Tee-Object -Append "$logs\summary.txt"
    if ($rc -eq 2 -or $rc -eq 97 -or $rc -eq 98) { "STOP: $($r.name) rc=$rc -- remaining runs skipped" | Tee-Object -Append "$logs\summary.txt"; break }
  }
  Get-ChildItem "$bin\*.exe" | Get-FileHash | ForEach-Object { "$($_.Hash)  $(Split-Path $_.Path -Leaf)" } | Out-File "$logs\hashes_after.txt" -Encoding utf8
} finally {
  if ($mine) { Remove-Item -LiteralPath $slot -Force }
  "slot released " + (Get-Date -Format 'HH:mm:ss') | Tee-Object -Append "$logs\summary.txt"
}


