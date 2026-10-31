# 검증용 독립 배치 — APK 0.2.32 최종 후보 1a3eee63 (SH r6, 2026-10-27)
$ErrorActionPreference = 'Continue'
$O = 'D:\remote\remote\.claude\verify_0232\r6'
$R = 'C:\work\gnlink-wt\apk-prelogin-select\build\apps\native_poc\Release'
$run = 'D:\remote\remote\.claude\caplife\sh_run.ps1'
$cand = 'D:\remote\remote\.claude\rel-apk-0232\GNLink-0.2.32.apk'
$sum = Join-Path $O 'summary.txt'
"start $(Get-Date -Format s)" | Out-File $sum -Encoding utf8

$expect = @{
  $cand = '1A3EEE63A992371F7AD455DEEDFABBC0E0B627C1C7432FA9F9251B8183D815AB'
  'D:\remote\remote\.claude\caplife\sh_r2\app-shseam.apk' = 'B105A845D9ACD89A1280AAC8D8B45F5AD213AD641CEF7F4F24D6AA5379AB2E81'
  "$R\GNLinkStreamMigrationTest.exe" = '881B3E05E0A789A94E76034B7946EDE1AB1B84F0D903210CC203A46C25ECE8CD'
  "$R\GNLinkStream.exe" = '1C0BC4F05A970729B1A9305B5884EAC0FB386B6E5A0B53A91DE01A1D17E2D657'
  "$R\remote60_viewer_prelogin_login_e2e_test.exe" = 'ED69EA1F8E59A7CEB907B2047E588311F1565579587E3CDB4CB6B2415FEC82B9'
  "$R\remote60_client_session_prelogin_select_test.exe" = '453BE9CF7F58989BD09F6EDE164DFD5D64EB1D28D0C81425517C7C29E28E1683'
}
function CheckHashes($label) {
  foreach ($k in $expect.Keys) {
    $h = (Get-FileHash $k -Algorithm SHA256).Hash
    "$label $([IO.Path]::GetFileName($k)) $(if ($h -eq $expect[$k]) {'OK'} else {"MISMATCH $h"})" | Out-File $sum -Append -Encoding utf8
  }
}
CheckHashes 'hash-before'
Copy-Item $cand 'D:\remote\remote\.claude\caplife\sh_r2\app-noseam.apk' -Force
"noseam-copy $((Get-FileHash 'D:\remote\remote\.claude\caplife\sh_r2\app-noseam.apk').Hash)" | Out-File $sum -Append -Encoding utf8

function S($scen, $tag, $seam, $extra) {
  $args2 = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$run,'-Scenario',$scen,'-Tag',$tag,'-Seam',$seam,'-NoBuild')
  if ($extra) { $args2 += $extra }
  & powershell @args2
  "$tag rc=$LASTEXITCODE" | Out-File $sum -Append -Encoding utf8
}
S 'prelogin_ui' 'v6_A' 'false' $null
S 'prelogin_tripwire' 'v6_A_trip' 'false' $null
S 'prelogin_ui_control' 'v6_A_ctl' 'false' $null
S 'ua2_desktop' 'v6_ua2d' 'false' $null
S 'method_ui' 'v6_method' 'false' $null
S 'ua2' 'v6_ua2' 'false' $null
S 'layout_b' 'v6_layout' 'true' $null
S 'strength_ab' 'v6_ab' 'true' $null
S 'compare_ab' 'v6_cmp' 'true' $null
S 'touch_ab' 'v6_touch' 'true' $null
S 'latency_ab' 'v6_lat' 'true' $null
& powershell -NoProfile -ExecutionPolicy Bypass -File $run -Tag 'v6_sel' -NativeExe "$R\remote60_viewer_prelogin_login_e2e_test.exe" -Extra '--android-select'
"v6_sel rc=$LASTEXITCODE" | Out-File $sum -Append -Encoding utf8
& "$R\remote60_client_session_prelogin_select_test.exe" *> (Join-Path $O 'picker_unit.txt')
"picker_unit rc=$LASTEXITCODE" | Out-File $sum -Append -Encoding utf8
& python 'C:\work\gnlink-wt\apk-prelogin-select\automation\gnlink_check_fixed_server.py' --apk $cand *> (Join-Path $O 'gate_apk.txt')
"gate_apk rc=$LASTEXITCODE" | Out-File $sum -Append -Encoding utf8
& python 'C:\work\gnlink-wt\apk-prelogin-select\automation\gnlink_check_fixed_server.py' --self-test *> (Join-Path $O 'gate_self.txt')
"gate_self rc=$LASTEXITCODE" | Out-File $sum -Append -Encoding utf8
CheckHashes 'hash-after'
"end $(Get-Date -Format s)" | Out-File $sum -Append -Encoding utf8
