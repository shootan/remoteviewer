$ErrorActionPreference = 'Continue'
$cand = 'D:\remote\remote\.claude\rel-apk-0232\GNLink-0.2.32.apk'
$noseam = 'D:\remote\remote\.claude\caplife\sh_r2\app-noseam.apk'
"candidate $((Get-FileHash $cand).Hash)  noseam copy $((Get-FileHash $noseam).Hash)"
foreach ($sc in 'prelogin_ui_control', 'method_ui', 'ua2', 'ua2_desktop', 'prelogin_ui', 'prelogin_tripwire') {
  $r = & D:\remote\remote\.claude\caplife\sh_run.ps1 -Scenario $sc -Tag "rel6b_$sc" -Seam false -NoBuild
  "${sc} (candidate): $(($r | Select-String 'apk |RESULT') -join ' | ')"
  $r | Select-String '^FAIL' | ForEach-Object { '   ' + $_.Line.Substring(0, [Math]::Min(170, $_.Line.Length)) }
}
foreach ($sc in 'strength_ab', 'layout_b', 'touch_ab', 'latency_ab') {
  $r = & D:\remote\remote\.claude\caplife\sh_run.ps1 -Scenario $sc -Tag "rel6b_$sc" -Seam true -NoBuild
  "${sc} (seam): $(($r | Select-String 'apk |RESULT') -join ' | ')"
  $r | Select-String '^FAIL' | ForEach-Object { '   ' + $_.Line.Substring(0, [Math]::Min(170, $_.Line.Length)) }
}
$r = & D:\remote\remote\.claude\caplife\sh_run.ps1 -Tag rel6b_android_select -NativeExe C:\work\gnlink-wt\apk-prelogin-select\build\apps\native_poc\Release\remote60_viewer_prelogin_login_e2e_test.exe -Extra '--android-select'
"android_select: $(($r | Select-String 'native|RESULT') -join ' | ')"
"RERUN-DONE"
