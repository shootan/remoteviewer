# rel-0164 r1: the pure tests of the merged units, on the integrated test build. No host, no slot. Each in a NEW test root
# under D:/remote/remote/.claude/test-tmp (rel0164-pure-<name>), output to its own log, bounded at 600 s.
$bin = 'C:\work\gnlink-wt\rel-0164-testbuild\apps\native_poc\Release'
$logs = 'D:\remote\remote\.claude\np_impl_logs\rel0164_pure_r3'
New-Item -ItemType Directory -Force -Path $logs | Out-Null
$env:REMOTE60_KA_REPO = 'C:\work\gnlink-wt\rel-0164'
$tests = 'remote60_directory_retry_test','remote60_directory_session_client_test','remote60_e2e_guard_runtime_test',
         'remote60_e2e_injection_guard_test','remote60_host_agent_punch_test','remote60_host_power_policy_record_test',
         'remote60_host_standby_table_test','remote60_ka_pc_e2e_child_test','remote60_ka_pc_fence_test',
         'remote60_ka_pc_ipc_close_race_test','remote60_ka_pc_shell_logic_test','remote60_ka_pc_units_test',
         'remote60_os_input_record_match_test','remote60_punch_any_test','remote60_standby_entry_client_test',
         'remote60_standby_lease_test','remote60_standby_legacy_host_test'
foreach ($t in $tests) {
  $short = $t -replace '^remote60_', ''
  $env:REMOTE60_TEST_TMP = "D:\remote\remote\.claude\test-tmp\rel0164-pure-r3-$short"
  New-Item -ItemType Directory -Path $env:REMOTE60_TEST_TMP | Out-Null   # a NEW root (refused if it exists)
  $out = Join-Path $logs "$short.log"
  $err = Join-Path $logs "$short.err.log"
  $p = Start-Process -FilePath (Join-Path $bin "$t.exe") -RedirectStandardOutput $out -RedirectStandardError $err -NoNewWindow -PassThru
  $null = $p.Handle   # held, so ExitCode is kept after the exit
  $done = $p.WaitForExit(600000)
  if (-not $done) { $p.Kill(); $p.WaitForExit(); $rc = 'TIMEOUT' } else { $rc = $p.ExitCode }
  $l = @(Get-Content $out) + @(Get-Content $err)
  $last = ($l | Where-Object { $_.Trim() } | Select-Object -Last 1)
  "{0,-34} rc={1,-7} PASS={2,-4} FAIL={3,-3} last: {4}" -f $short, $rc, ($l | Select-String '^PASS').Count, ($l | Select-String '^FAIL').Count, $last
}
