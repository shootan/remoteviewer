# 검증용 독립 배치 — APK 0.2.32 후보 1d4a05f4 (2026-10-27)
$ErrorActionPreference = 'Continue'
$O = 'D:\remote\remote\.claude\verify_0232'
$R = 'C:\work\gnlink-wt\apk-prelogin-select\build\apps\native_poc\Release'
$run = 'D:\remote\remote\.claude\caplife\sh_run.ps1'
$cand = 'D:\remote\remote\.claude\rel-apk-0232\GNLink-0.2.32.apk'
$sum = Join-Path $O 'summary.txt'
"start $(Get-Date -Format s)" | Out-File $sum -Encoding utf8

$expect = @{
  $cand = '1D4A05F4F318496A895AC748BE5705365598C3DE677C6F4652988034BD3DEEBD'
  'D:\remote\remote\.claude\caplife\sh_r2\app-shseam.apk' = '9F1A0529D4C4A38DF5F283F380F7C6A6DC2A0733DBCB7D20C044EFE98EB5DF02'
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
S 'prelogin_ui' 'vv_A' 'false' $null
S 'prelogin_tripwire' 'vv_A_trip' 'false' $null
S 'prelogin_ui_control' 'vv_A_ctl' 'false' $null
S 'ua2_desktop' 'vv_ua2d' 'false' $null
S 'method_ui' 'vv_method' 'false' $null
S 'ua2' 'vv_ua2' 'false' $null
S 'ua2_old' 'vv_ua2old' 'false' @('-HostExe','C:\work\gnlink-wt\fix-capture-lifecycle-baseline\Release\GNLinkStreamMigrationTest.exe','-Extra',"--new-host-exe $R\GNLinkStreamMigrationTest.exe")
S 'layout_b' 'vv_layout' 'true' $null
S 'strength_ab' 'vv_ab' 'true' $null
S 'compare_ab' 'vv_cmp' 'true' $null
S 'touch_ab' 'vv_touch' 'true' $null
S 'latency_ab' 'vv_lat' 'true' $null
& powershell -NoProfile -ExecutionPolicy Bypass -File $run -Tag 'vv_sel' -NativeExe "$R\remote60_viewer_prelogin_login_e2e_test.exe" -Extra '--android-select'
"vv_sel rc=$LASTEXITCODE" | Out-File $sum -Append -Encoding utf8
& "$R\remote60_client_session_prelogin_select_test.exe" *> (Join-Path $O 'picker_unit.txt')
"picker_unit rc=$LASTEXITCODE" | Out-File $sum -Append -Encoding utf8
& python 'C:\work\gnlink-wt\apk-prelogin-select\automation\gnlink_check_fixed_server.py' --apk $cand *> (Join-Path $O 'gate_apk.txt')
"gate_apk rc=$LASTEXITCODE" | Out-File $sum -Append -Encoding utf8
& python 'C:\work\gnlink-wt\apk-prelogin-select\automation\gnlink_check_fixed_server.py' --self-test *> (Join-Path $O 'gate_self.txt')
"gate_self rc=$LASTEXITCODE" | Out-File $sum -Append -Encoding utf8
CheckHashes 'hash-after'
"end $(Get-Date -Format s)" | Out-File $sum -Append -Encoding utf8
