# Verifier: independent runs under ONE measurement slot.
#  - record lane: os_input_record_e2e (ka-r3 ba2eaf2 build, verify-inj)
#  - general 8 + fatal (ka-r3 b8dcc93 build, verify-inj-b8), Codex table codex_test_injection_finding.md:754-766
# Slot: CreateNew C:\work\gnlink-wt\SLOT_REQUEST.txt; this script deletes only the file it created.
# Not run here: --old-host, real-input 4, viewer_prelogin_login (banned), e2e_guard_runtime_test (%TEMP% fixture).
param([string]$Tag = 'r1')
$ErrorActionPreference = 'Continue'
$recBin = 'D:\remote\remote\.claude\worktrees\verify-inj\build-verify\apps\native_poc\Release'
$b8Bin  = 'D:\remote\remote\.claude\worktrees\verify-inj-b8\build-verify\apps\native_poc\Release'
$out = "D:\remote\remote\.claude\verify_inj\$Tag"
$slot = 'C:\work\gnlink-wt\SLOT_REQUEST.txt'
$tmp = 'D:\remote\remote\.claude\test-tmp\verify-inj'
New-Item -ItemType Directory -Force $out | Out-Null
New-Item -ItemType Directory -Force $tmp | Out-Null

try { $fs = [System.IO.File]::Open($slot, 'CreateNew', 'Write', 'None') } catch { "SLOT BUSY: $slot exists -- nothing run"; exit 4 }
$b = [System.Text.Encoding]::ASCII.GetBytes("verifier remote#7fsfy22z verify_inj $Tag $(Get-Date -Format o)"); $fs.Write($b, 0, $b.Length); $fs.Close()
try {
  # other workers' TEST hosts must be gone first (memory: check-other-hosts-before-e2e). The installed product
  # (GNLinkHost's child, elevated: no readable path) is the user's and is neither waited on nor touched.
  function Get-TestHosts { @(Get-Process GNLinkStream* -ErrorAction SilentlyContinue | Where-Object {
      $_.Path -and ($_.Path -like 'D:\remote\*' -or $_.Path -like 'C:\work\*') }) }
  for ($i = 0; $i -lt 60; $i++) {
    if ((Get-TestHosts).Count -eq 0) { break }
    Start-Sleep -Seconds 10
  }
  Get-TestHosts | Select-Object Id, Path, StartTime | Out-File "$out\test_hosts_before.txt" -Encoding utf8
  if ((Get-TestHosts).Count -gt 0) {
    "foreign GNLinkStream still running after 10 min -- nothing run" | Tee-Object "$out\ABORT.txt"; exit 5
  }
  qwinsta | Out-File "$out\qwinsta.txt" -Encoding utf8
  Get-ChildItem $recBin\GNLinkStreamInputRecordTest.exe, $recBin\remote60_os_input_record_e2e_test.exe,
    $b8Bin\*.exe | Get-FileHash | ForEach-Object { "$($_.Hash)  $($_.Path)" } | Out-File "$out\hashes.txt" -Encoding utf8

  $env:REMOTE60_ALLOW_HOST_E2E = '1'
  $env:REMOTE60_TEST_TMP = $tmp
  $runs = @(
    @{ name = 'os_input_record_e2e';            exe = "$recBin\remote60_os_input_record_e2e_test.exe";      args = @() },
    @{ name = 'host_standby_admission_e2e';     exe = "$b8Bin\remote60_host_standby_admission_e2e_test.exe"; args = @() },
    @{ name = 'host_standby_admission_e2e_pc';  exe = "$b8Bin\remote60_host_standby_admission_e2e_test.exe"; args = @('--product-client', '--real-directory') },
    @{ name = 'host_punch_reply_e2e';           exe = "$b8Bin\remote60_host_punch_reply_e2e_test.exe";      args = @() },
    @{ name = 'host_migration_e2e';             exe = "$b8Bin\remote60_host_migration_e2e_test.exe";        args = @() },
    @{ name = 'host_reboot_delegate_e2e';       exe = "$b8Bin\remote60_host_reboot_delegate_e2e_test.exe";  args = @() },
    @{ name = 'android_session_reboot_e2e';     exe = "$b8Bin\remote60_android_session_reboot_e2e_test.exe"; args = @() },
    @{ name = 'bwe_observe_e2e';                exe = "$b8Bin\remote60_bwe_observe_e2e_test.exe";           args = @() },
    @{ name = 'host_control_deadline_e2e';      exe = "$b8Bin\remote60_host_control_deadline_e2e_test.exe"; args = @() },
    @{ name = 'host_two_dispatcher_e2e';        exe = "$b8Bin\remote60_host_two_dispatcher_e2e_test.exe";   args = @() },
    @{ name = 'host_fatal_exception';           exe = "$b8Bin\remote60_host_fatal_exception_test.exe";      args = @() }
  )
  foreach ($r in $runs) {
    $t0 = Get-Date
    & $r.exe @($r.args) > "$out\$($r.name).log" 2>&1
    $rc = $LASTEXITCODE
    $last = (Get-Content "$out\$($r.name).log" -Tail 3) -join ' | '
    "$($r.name) rc=$rc sec=$([int]((Get-Date)-$t0).TotalSeconds) last=$last" | Tee-Object -Append "$out\summary.txt"
    if ($rc -eq 2) { "STOP: $($r.name) INVALID (safety gate) -- remaining runs skipped" | Tee-Object -Append "$out\summary.txt"; break }
  }
} finally {
  Remove-Item $slot -Confirm:$false
}
