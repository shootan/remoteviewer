$ErrorActionPreference = 'Continue'
$wt = 'C:\work\gnlink-wt\ka-r3'
$exe = "$wt\build\apps\native_poc\Release\remote60_host_standby_table_test.exe"
$out = 'D:\remote\remote\.claude\caplife\ka_r4_mut'
$files = "$wt\apps\native_poc\src\host_standby_table.hpp", "$wt\apps\native_poc\src\host_standby_wire.hpp"
$base = $files | ForEach-Object { (Get-FileHash $_).Hash }
function Build { cmake --build "$wt\build" --config Release --target remote60_host_standby_table_test -- /m /v:q /nologo 2>&1 | Select-String -Pattern 'error C' | Select-Object -First 5; $LASTEXITCODE }
$b = Build; & $exe > "$out\table_base.txt" 2>&1; "base build=$b rc=$LASTEXITCODE | $((Get-Content "$out\table_base.txt" | Select-String 'RESULT').Line)"
foreach ($m in 'N2a','N2b','N2c') {
  python -I D:\remote\remote\.claude\caplife\ka_r4_mut.py $m apply
  $b = Build
  & $exe > "$out\$m.txt" 2>&1
  $rc = $LASTEXITCODE
  $fails = (Get-Content "$out\$m.txt" | Where-Object { $_ -like 'FAIL*' } | ForEach-Object { $_.Substring(0, [Math]::Min(110, $_.Length)) }) -join ' || '
  "$m build=$b rc=$rc | $fails"
  python -I D:\remote\remote\.claude\caplife\ka_r4_mut.py $m revert
  $files | ForEach-Object { (Get-Item $_).LastWriteTime = Get-Date }
}
"restored: $((Compare-Object $base ($files | ForEach-Object { (Get-FileHash $_).Hash })) -eq $null)"
$b = Build; & $exe > "$out\table_after.txt" 2>&1; "after build=$b rc=$LASTEXITCODE | $((Get-Content "$out\table_after.txt" | Select-String 'RESULT').Line) | exe $((Get-FileHash $exe).Hash.Substring(0,12))"
