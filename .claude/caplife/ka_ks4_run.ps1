$ErrorActionPreference = 'Continue'
$wt = 'C:\work\gnlink-wt\ka-r3'
$rel = "$wt\build\apps\native_poc\Release"
$exe = "$rel\remote60_host_standby_admission_e2e_test.exe"
$old = 'C:\work\gnlink-wt\fix-capture-lifecycle-baseline\Release\GNLinkStream.exe'
$out = 'D:\remote\remote\.claude\caplife\ka_mut'
$src = "$wt\apps\native_poc\src\host_standby_admission_e2e_test.cpp"
$base = (Get-FileHash $src).Hash
$goodExe = (Get-FileHash $exe).Hash
function Build { cmake --build "$wt\build" --config Release --target remote60_host_standby_admission_e2e_test -- /m /v:q /nologo 2>&1 | Select-String -Pattern 'error C' | Select-Object -First 5; $LASTEXITCODE }
$slot = 'C:\work\gnlink-wt\SLOT_REQUEST.txt'
try { $fs = [System.IO.File]::Open($slot, 'CreateNew'); $fs.Close() } catch { "slot busy"; exit 1 }
$env:REMOTE60_ALLOW_HOST_E2E = '1'
try {
  python -I D:\remote\remote\.claude\caplife\ka_mut.py KS4 apply
  $b = Build
  "build rc=$b exe $((Get-FileHash $exe).Hash.Substring(0,12))"
  if ($b -eq 0) {
    & $exe --old-host $old > "$out\KS4.txt" 2>&1
    $rc = $LASTEXITCODE
    $fails = (Get-Content "$out\KS4.txt" | Where-Object { $_ -like 'FAIL*' } | ForEach-Object { $_.Substring(0, [Math]::Min(150, $_.Length)) }) -join ' || '
    "KS4 rc=$rc | $((Get-Content "$out\KS4.txt" | Select-String 'RESULT').Line) | $fails"
  }
} finally {
  python -I D:\remote\remote\.claude\caplife\ka_mut.py KS4 revert
  (Get-Item $src).LastWriteTime = Get-Date
  "source restored: $((Get-FileHash $src).Hash -eq $base)"
  $b = Build
  "rebuild rc=$b exe $((Get-FileHash $exe).Hash.Substring(0,12)) (was $($goodExe.Substring(0,12)))"
  Remove-Item $slot -Confirm:$false
  "slot removed=$(-not (Test-Path $slot))"
  "leftover: $((Get-Process | Where-Object { $_.ProcessName -match 'remote60_host_standby|GNLinkStream' } | ForEach-Object { "$($_.ProcessName):$($_.Id)" }) -join ',')"
}
