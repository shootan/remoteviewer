# Approved independent P once: published APK, manual TCP control + direct UDP video; S remains unresolved.
param(
  [ValidatePattern('^[0-9a-fA-F]{40}$')][string]$CandidateHead = '',
  [string]$TraceServerJs = ''
)
# Reviewed 98d71ef + exact public/internal IME distinction patch; three file hashes pinned.
# Based on reviewed r7 runner 5533a518; five exe gates, first-nonzero stop, and finally slot cleanup retained.
# Verifier: lane 7 (old APK 0.2.32 x 0.2.164 candidate host/server), arm S then arm P, under ONE measuring slot.
# Codex codex_old_apk_lane_src_r3.md: 494d05b, independent build verify-0164, OA_PM_CLEAR_TIMEOUT_SEC=60, no fake adb.
# The launcher is started with Start-Process -Wait (no output redirect: a redirected fixture launcher can hang on
# inherited handles). The slot is taken with CreateNew and only this script's own file is removed.
$ErrorActionPreference = 'Stop'
$wt = 'D:\remote\remote\.claude\worktrees\verify-0164'
$b = "$wt\build-verify\apps\native_poc\Release"
$slot = 'C:\work\gnlink-wt\SLOT_REQUEST.txt'
$log = 'D:\remote\remote\.claude\verify_0164\old_apk_lane_summary.txt'
$adbSha = '7035cf5ec7f99f7b5662a9f5395f9dccfc3b6fe42583d0b9550124a0c3496606'
$server = "$wt\apps\directory\server.js"
$traceSha = '52918c04b8fef8f4e66af5c6abe4a7332429416397004432accb79d8c231492a'
if ($TraceServerJs -notmatch '^[A-Za-z]:[\\/]') { "trace path must be drive-absolute -- nothing run" | Tee-Object -Append $log; exit 2 }
$traceFull = [IO.Path]::GetFullPath($TraceServerJs)
if (-not $traceFull.StartsWith("$wt\.claude\oa_relay_server\", [StringComparison]::OrdinalIgnoreCase) -or
    -not (Test-Path -LiteralPath $traceFull -PathType Leaf) -or
    (Get-FileHash -LiteralPath $traceFull).Hash.ToLower() -ne $traceSha) {
  "trace copy is outside the fixture anchor or not pinned -- nothing run" | Tee-Object -Append $log; exit 2
}
$plainRawSha = (Get-FileHash -LiteralPath $server).Hash.ToLower()
$plainText = [Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($server)).TrimStart([char]0xfeff).Replace("`r`n", "`n")
$plainAlg = [Security.Cryptography.SHA256]::Create()
try { $plainSourceSha = [BitConverter]::ToString($plainAlg.ComputeHash([Text.Encoding]::UTF8.GetBytes($plainText))).Replace('-', '').ToLowerInvariant() } finally { $plainAlg.Dispose() }
if ($plainSourceSha -ne '2742143d72bb9482429f3046109b0a833bbf8a5049a3b26a8e1eac2f586c4bb7') {
  "plain candidate server changed -- nothing run" | Tee-Object -Append $log; exit 2
}

$apkS = 'C:\work\gnlink-wt\old-apk-lane\apk_out\apk-0232-fixture-r2.apk'
$apkP = 'D:\remote\remote\dist\GNLink-0.2.32.apk'
$want = @{ $apkS = '50080B2BD586EE66CE446AB8F647CE4E5C11FAFC22593D15BEF7E948D2816962'; $apkP = '1A3EEE63A992371F7AD455DEEDFABBC0E0B627C1C7432FA9F9251B8183D815AB' }
foreach ($k in $want.Keys) { if ((Get-FileHash $k).Hash -ne $want[$k]) { "APK hash mismatch $k -- nothing run" | Tee-Object -Append $log; exit 2 } }
if ($CandidateHead -ne 'c45fb9b768accaf3ad54561a7766e2d81cf56c86') {
  "independent P is pinned to c45fb9b -- nothing run" | Tee-Object -Append $log; exit 2
}
$baseHead = 'ff1c72a19aa2e8656d207047c5e076c656c4f540'
$sourceBase = '98d71efca956f07d24009c8baa5416f2e5424a29'
if ((git -C $wt rev-parse HEAD) -ne $CandidateHead) { "worktree not at the pinned candidate -- nothing run" | Tee-Object -Append $log; exit 2 }
git -C $wt merge-base --is-ancestor $sourceBase $CandidateHead
if ($LASTEXITCODE -ne 0) { "candidate is not based on reviewed 98d71ef -- nothing run" | Tee-Object -Append $log; exit 2 }
$changed = @(git -C $wt diff --name-only $baseHead $CandidateHead)
$diffRc = $LASTEXITCODE
$allowed = @('automation/old_apk_lane/oa_avd.py', 'automation/old_apk_lane/oa_pick_test.py', 'automation/old_apk_lane/stubs/fake_ui_adb.py')
$unexpected = @($changed | Where-Object { $_ -notin $allowed })
if ($diffRc -ne 0 -or $changed.Count -ne 3 -or $unexpected.Count -ne 0) {
  "candidate contains changes outside the three approved files -- nothing run" | Tee-Object -Append $log; exit 2
}
$sourcePath = "$wt\automation\old_apk_lane\oa_avd.py"
$text = [Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($sourcePath)).TrimStart([char]0xfeff).Replace("`r`n", "`n")
$sha = [Security.Cryptography.SHA256]::Create()
try { $sourceSha = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($text))).Replace('-', '').ToLowerInvariant() } finally { $sha.Dispose() }
if ($sourceSha -ne '39a0d515a03341e4e6f8b88f38dd3a23b5d558fb053d30b060bc810b71a39cdc') {
  "candidate's oa_avd.py is not the approved UTF-8/LF source -- nothing run" | Tee-Object -Append $log; exit 2
}
$testPath = "$wt\automation\old_apk_lane\oa_pick_test.py"
$testText = [Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($testPath)).TrimStart([char]0xfeff).Replace("`r`n", "`n")
$testAlg = [Security.Cryptography.SHA256]::Create()
try { $testSourceSha = [BitConverter]::ToString($testAlg.ComputeHash([Text.Encoding]::UTF8.GetBytes($testText))).Replace('-', '').ToLowerInvariant() } finally { $testAlg.Dispose() }
if ($testSourceSha -ne '0decb28628764745a8a4a16af42c55a8bf0b38f4f2c58e829008caed2b1b1ced') {
  "candidate's stub split is not the approved UTF-8/LF source -- nothing run" | Tee-Object -Append $log; exit 2
}

$stubPath = "$wt\automation\old_apk_lane\stubs\fake_ui_adb.py"
$stubText = [Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($stubPath)).TrimStart([char]0xfeff).Replace("`r`n", "`n")
$stubAlg = [Security.Cryptography.SHA256]::Create()
try { $stubSourceSha = [BitConverter]::ToString($stubAlg.ComputeHash([Text.Encoding]::UTF8.GetBytes($stubText))).Replace('-', '').ToLowerInvariant() } finally { $stubAlg.Dispose() }
if ($stubSourceSha -ne '3dd6a073290274e7e65564ce551213d7f157e1f4d1025630a8eb28dd7def7465') {
  "candidate's fake-adb helper is not the approved UTF-8/LF source -- nothing run" | Tee-Object -Append $log; exit 2
}
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
  $env:OA_OBSERVE_SEC = '30'
  "pinned source $CandidateHead oa_avd.py=$sourceSha test=$testSourceSha helper=$stubSourceSha trace=$traceSha plainRaw=$plainRawSha plainLF=$plainSourceSha" | Tee-Object -Append $log
  Get-ChildItem "$b\remote60_ka_standby_holder_tool.exe","$b\remote60_mobile_abr_session_e2e_test.exe","$b\GNLinkStreamPinnedSelectTest.exe","$b\GNLinkViewerKaTest.exe","$b\e2e_job_run.exe" | Get-FileHash | ForEach-Object { "$($_.Hash)  $(Split-Path $_.Path -Leaf)" } | Tee-Object -Append $log
  # r6 (7db9c27): the relay address is per arm, so case 7 (the PC-side standby lease) runs with arm P only
  foreach ($arm in @(@{ tag = 'oa-p-direct-check'; arm = 'P'; apk = $apkP; sb = $true })) {
    $env:KE_RELAY_TRACE = if ($arm.arm -eq 'S') { '1' } else { '0' }
    $armServer = if ($arm.arm -eq 'S') { $traceFull } else { $server }
    $t0 = Get-Date
    $al = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "$wt\automation\old_apk_lane\oa_slot.ps1",
              '-Tag', $arm.tag, '-Arm', $arm.arm, '-Apk', $arm.apk, '-BuildDir', $b,
              '-JobRun', "$b\e2e_job_run.exe", '-ServerJs', $armServer, '-AdbSha256', $adbSha)
    if ($arm.arm -eq 'S') { $al += @('-ServerSha256', $traceSha) }
    if ($arm.sb) { $al += '-WithStandby' }
    $p = Start-Process -FilePath 'powershell.exe' -ArgumentList $al -Wait -NoNewWindow -PassThru
    "arm $($arm.arm) tag=$($arm.tag) rc=$($p.ExitCode) secs=$([int]((Get-Date)-$t0).TotalSeconds)" | Tee-Object -Append $log
    if ($null -eq $p -or $null -eq $p.ExitCode -or $p.ExitCode -ne 0) {
      "NOT RUN: remaining arm after a nonzero/unknown result; inspect cleanup before any independent retry" | Tee-Object -Append $log
      if ($null -eq $p -or $null -eq $p.ExitCode) { exit 2 }
      exit ([int]$p.ExitCode)
    }
  }
} finally {
  if ($mine) { Remove-Item -LiteralPath $slot -Force }
  "slot released $(Get-Date -Format 'HH:mm:ss')" | Tee-Object -Append $log
}
