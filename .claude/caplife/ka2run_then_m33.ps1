$ErrorActionPreference = 'Continue'
$wt = 'C:\work\gnlink-wt\ka-r3'
cmake --build "$wt\build" --config Release --target remote60_host_standby_admission_e2e_test -- /m /v:q /nologo 2>&1 | Select-String 'error C' | Select-Object -First 3
"e2e build rc=$LASTEXITCODE"
$exe = "$wt\build\apps\native_poc\Release\remote60_host_standby_admission_e2e_test.exe"
$old = 'C:\work\gnlink-wt\fix-capture-lifecycle-baseline\Release\GNLinkStream.exe'
$r = & D:\remote\remote\.claude\caplife\sh_run.ps1 -Tag ka2run_2 -NativeExe $exe -Extra "--product-client --real-directory --old-host $old"
"e2e: $(($r | Select-String 'native|scenario rc|RESULT') -join ' | ')"
$r | Select-String '^FAIL' | ForEach-Object { '   ' + $_.Line.Substring(0, [Math]::Min(170, $_.Line.Length)) }
$out = 'D:\remote\remote\.claude\caplife\sh_r2'
"noseam $((Get-FileHash "$out\app-noseam.apk").Hash)  shseam $((Get-FileHash "$out\app-shseam.apk").Hash)"
foreach ($sc in 'method_ui', 'ua2', 'ua2_desktop', 'prelogin_ui') {
  $r = & D:\remote\remote\.claude\caplife\sh_run.ps1 -Scenario $sc -Tag "m33b_$sc" -Seam false -NoBuild
  "${sc} (merged noseam): $(($r | Select-String 'RESULT|slot busy') -join ' | ')"
  $r | Select-String '^FAIL' | ForEach-Object { '   ' + $_.Line.Substring(0, [Math]::Min(170, $_.Line.Length)) }
}
foreach ($sc in 'layout_b', 'touch_ab') {
  $r = & D:\remote\remote\.claude\caplife\sh_run.ps1 -Scenario $sc -Tag "m33b_$sc" -Seam true -NoBuild
  "${sc} (merged shseam): $(($r | Select-String 'RESULT|slot busy') -join ' | ')"
  $r | Select-String '^FAIL' | ForEach-Object { '   ' + $_.Line.Substring(0, [Math]::Min(170, $_.Line.Length)) }
}
"ALL-DONE"
