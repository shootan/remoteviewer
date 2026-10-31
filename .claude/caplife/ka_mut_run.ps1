$ErrorActionPreference = 'Continue'
$wt = 'C:\work\gnlink-wt\ka-r3'
$exe = "$wt\build\apps\native_poc\Release\remote60_host_standby_table_test.exe"
$out = 'D:\remote\remote\.claude\caplife\ka_mut'
New-Item -ItemType Directory -Force $out | Out-Null
$hdr = "$wt\apps\native_poc\src\host_standby_table.hpp"
$base = (Get-FileHash $hdr).Hash
function Build { cmake --build "$wt\build" --config Release --target remote60_host_standby_table_test -- /m /v:q /nologo 2>&1 | Select-String -Pattern 'error' | Select-Object -First 5 }
Build
& $exe > "$out\base.txt" 2>&1
"base rc=$LASTEXITCODE | $((Get-Content "$out\base.txt" | Select-String 'RESULT').Line) | exe $((Get-FileHash $exe).Hash.Substring(0,12))"
foreach ($m in 'K1','K2','K3','K4','K5','K6','K7') {
  python -I D:\remote\remote\.claude\caplife\ka_mut.py $m apply
  Build
  & $exe > "$out\$m.txt" 2>&1
  $rc = $LASTEXITCODE
  $fails = (Get-Content "$out\$m.txt" | Where-Object { $_ -like 'FAIL*' } | ForEach-Object { $_.Substring(0, [Math]::Min(110, $_.Length)) }) -join ' || '
  "$m rc=$rc exe $((Get-FileHash $exe).Hash.Substring(0,12)) | $fails"
  python -I D:\remote\remote\.claude\caplife\ka_mut.py $m revert
}
(Get-Item $hdr).LastWriteTime = Get-Date
"header restored: $((Get-FileHash $hdr).Hash -eq $base)"
Build
& $exe > "$out\after.txt" 2>&1
"after rc=$LASTEXITCODE | $((Get-Content "$out\after.txt" | Select-String 'RESULT').Line) | exe $((Get-FileHash $exe).Hash.Substring(0,12))"
