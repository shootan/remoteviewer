$ErrorActionPreference = 'Continue'
$rel = 'C:\work\gnlink-wt\ka-r3\build\apps\native_poc\Release'
$old = 'C:\work\gnlink-wt\fix-capture-lifecycle-baseline\Release\GNLinkStream.exe'
$runs = @(
  @('host_standby_admission_e2e_test', "--product-client --real-directory --old-host $old"),
  @('host_punch_reply_e2e_test', ''),
  @('host_migration_e2e_test', ''),
  @('host_control_deadline_e2e_test', ''),
  @('host_two_dispatcher_e2e_test', ''),
  @('host_reboot_delegate_e2e_test', ''),
  @('android_session_reboot_e2e_test', ''),
  @('host_fatal_exception_test', ''),
  @('bwe_observe_e2e_test', '')
)
foreach ($r in $runs) {
  while (Test-Path C:\work\gnlink-wt\SLOT_REQUEST.txt) { Start-Sleep -Seconds 20 }
  $exe = "$rel\remote60_$($r[0]).exe"
  $tag = "inj2_$($r[0])"
  if ($r[1]) { $o = & D:\remote\remote\.claude\caplife\sh_run.ps1 -Tag $tag -NativeExe $exe -Extra $r[1] } else { $o = & D:\remote\remote\.claude\caplife\sh_run.ps1 -Tag $tag -NativeExe $exe }
  "== $($r[0]) $(Get-Date -Format s)"
  $log = "D:\remote\remote\.claude\caplife\sh_r2\$tag.log"
  Get-Content $log -ErrorAction SilentlyContinue | Where-Object { $_ -match 'injection|^RESULT|ALL PASS|checks, ' } | Select-Object -First 6
  $o | Select-String 'scenario rc' | ForEach-Object { $_.Line }
}
"all done $(Get-Date -Format s)"
