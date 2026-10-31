# r7g draft (verifier, from Codex reviewed r7 5533a518): pinned b11fc0d; arm S uses the trace-only test copy of the
# candidate server (52918c04, built by oa_relay_trace_server.py) with KE_RELAY_TRACE=1; arm P as before (no trace env).
# Same five exe gates; first nonzero arm stops, finally retains slot cleanup.
# Verifier: lane 7 (old APK 0.2.32 x 0.2.164 candidate host/server), arm S then arm P, under ONE measuring slot.
# Codex codex_old_apk_lane_src_r3.md: 494d05b, independent build verify-0164, OA_PM_CLEAR_TIMEOUT_SEC=60, no fake adb.
# The launcher is started with Start-Process -Wait (no output redirect: a redirected fixture launcher can hang on
# inherited handles). The slot is taken with CreateNew and only this script's own file is removed.
$ErrorActionPreference = 'Continue'
$wt = 'D:\remote\remote\.claude\worktrees\verify-0164'
$b = "$wt\build-verify\apps\native_poc\Release"
$slot = 'C:\work\gnlink-wt\SLOT_REQUEST.txt'
$log = 'D:\remote\remote\.claude\verify_0164\old_apk_lane_summary.txt'
$adbSha = '7035cf5ec7f99f7b5662a9f5395f9dccfc3b6fe42583d0b9550124a0c3496606'
$server = "$wt\apps\directory\server.js"
$traced = "$wt\.claude\oa_relay_server\v-traced\apps\directory\server.js"
$tracedSha = '52918c04b8fef8f4e66af5c6abe4a7332429416397004432accb79d8c231492a'
if ((Get-FileHash $traced).Hash.ToLower() -ne $tracedSha) { "traced server copy is not the pinned one -- nothing run" | Tee-Object -Append $log; exit 2 }
$apkS = 'C:\work\gnlink-wt\old-apk-lane\apk_out\apk-0232-fixture-r2.apk'
$apkP = 'D:\remote\remote\dist\GNLink-0.2.32.apk'
$want = @{ $apkS = '50080B2BD586EE66CE446AB8F647CE4E5C11FAFC22593D15BEF7E948D2816962'; $apkP = '1A3EEE63A992371F7AD455DEEDFABBC0E0B627C1C7432FA9F9251B8183D815AB' }
foreach ($k in $want.Keys) { if ((Get-FileHash $k).Hash -ne $want[$k]) { "APK hash mismatch $k -- nothing run" | Tee-Object -Append $log; exit 2 } }
if ((git -C $wt rev-parse HEAD) -ne 'b11fc0d40b49e086ed3df9bcbfcaa467fd7318a2') { "worktree not at b11fc0d -- nothing run" | Tee-Object -Append $log; exit 2 }
$dirty = @(git -C $wt status --porcelain --untracked-files=no)
if ($dirty.Count -ne 0) { "tracked worktree changed -- nothing run" | Tee-Object -Append $log; exit 2 }
# Codex r6: the existing five targets, by their independent hashes (953aef2 -> 7db9c27 changes scripts only)
$exeWant = @{ 'remote60_ka_standby_holder_tool.exe' = '65B937CD16E46CFD24D9E172DFD3B2F19F51E1BA17867650BC2FFFD80907D890'; 'remote60_mobile_abr_session_e2e_test.exe' = 'BC78FEA02DBB740315892CA308E41217C2C39D539169E16E8301A391D95467A7'; 'GNLinkStreamPinnedSelectTest.exe' = '980AB2CCBA2B93DCB5C0AF0190CF7C7367C8B1C4F675E8500EDDDDC3BC478D87'; 'GNLinkViewerKaTest.exe' = '691889F91F5213037990195EA4E9C6E1524AFD34EB2A7885FB1CD25C6CF60EDB'; 'e2e_job_run.exe' = 'A0FD86C0586DF314E8BB1A6F2E01ED17415084462484FC9B4164B58861BA4D69' }
foreach ($k in $exeWant.Keys) { if ((Get-FileHash "$b\$k").Hash -ne $exeWant[$k]) { "exe hash mismatch $k -- nothing run" | Tee-Object -Append $log; exit 2 } }

$mine = $false
while (-not $mine) {
  if (-not (Test-Path $slot)) {
    try { $fs = [IO.File]::Open($slot, 'CreateNew', 'Write'); $bb = [Text.Encoding]::UTF8.GetBytes('verifier remote#7fsfy22z old-apk lane ' + (Get-Date -Format o)); $fs.Write($bb, 0, $bb.Length); $fs.Close(); $mine = $true } catch { }
  }
  if (-not $mine) { Start-Sleep -Seconds 20 }
}
"slot taken $(Get-Date -Format 'HH:mm:ss')" | Tee-Object -Append $log
try {
  Remove-Item env:OA_TEST_FAKE_ADB -ErrorAction SilentlyContinue
  $env:OA_PM_CLEAR_TIMEOUT_SEC = '60'
  Get-ChildItem "$b\remote60_ka_standby_holder_tool.exe","$b\remote60_mobile_abr_session_e2e_test.exe","$b\GNLinkStreamPinnedSelectTest.exe","$b\GNLinkViewerKaTest.exe","$b\e2e_job_run.exe" | Get-FileHash | ForEach-Object { "$($_.Hash)  $(Split-Path $_.Path -Leaf)" } | Tee-Object -Append $log
  # r6 (7db9c27): the relay address is per arm, so case 7 (the PC-side standby lease) runs with arm P only
  foreach ($arm in @(@{ tag = 'oa-s'; arm = 'S'; apk = $apkS; sb = $false }, @{ tag = 'oa-p'; arm = 'P'; apk = $apkP; sb = $true })) {
    $t0 = Get-Date
    $al = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "$wt\automation\old_apk_lane\oa_slot.ps1",
              '-Tag', $arm.tag, '-Arm', $arm.arm, '-Apk', $arm.apk, '-BuildDir', $b,
              '-JobRun', "$b\e2e_job_run.exe", '-ServerJs', $server, '-AdbSha256', $adbSha)
    if ($arm.sb) { $al += '-WithStandby' }
    if ($arm.arm -eq 'S') {
      $al[($al.IndexOf('-ServerJs') + 1)] = $traced
      $al += @('-ServerSha256', $tracedSha)
      $env:KE_RELAY_TRACE = '1'
    } else {
      Remove-Item env:KE_RELAY_TRACE -ErrorAction SilentlyContinue
    }
    $p = Start-Process -FilePath 'powershell.exe' -ArgumentList $al -Wait -NoNewWindow -PassThru
    "arm $($arm.arm) tag=$($arm.tag) rc=$($p.ExitCode) secs=$([int]((Get-Date)-$t0).TotalSeconds)" | Tee-Object -Append $log
    if ($null -eq $p -or $null -eq $p.ExitCode -or $p.ExitCode -ne 0) {
      "NOT RUN: remaining arm after a nonzero/unknown result; inspect cleanup before any independent retry" | Tee-Object -Append $log
      if ($null -eq $p -or $null -eq $p.ExitCode) { exit 2 }
      exit ([int]$p.ExitCode)
    }
  }
} finally {
  Remove-Item env:KE_RELAY_TRACE -ErrorAction SilentlyContinue
  if ($mine) { Remove-Item -LiteralPath $slot -Force }
  "slot released $(Get-Date -Format 'HH:mm:ss')" | Tee-Object -Append $log
}
