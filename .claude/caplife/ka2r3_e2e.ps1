$ErrorActionPreference = 'Continue'
$wt = 'C:\work\gnlink-wt\ka-r3'
cmake --build "$wt\build" --config Release --target remote60_host_standby_admission_e2e_test -- /m /v:q /nologo 2>&1 | Select-String 'error C' | Select-Object -First 3
"e2e build rc=$LASTEXITCODE"
$exe = "$wt\build\apps\native_poc\Release\remote60_host_standby_admission_e2e_test.exe"
$old = 'C:\work\gnlink-wt\fix-capture-lifecycle-baseline\Release\GNLinkStream.exe'
$r = & D:\remote\remote\.claude\caplife\sh_run.ps1 -Tag ka2r3_e2e -NativeExe $exe -Extra "--product-client --real-directory --old-host $old"
$r | Select-String 'native|scenario rc|RESULT|slot|qwinsta' | ForEach-Object { $_.Line }
$r | Select-String '^FAIL' | ForEach-Object { '   ' + $_.Line.Substring(0, [Math]::Min(200, $_.Line.Length)) }
"ALL-DONE"
