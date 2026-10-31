param([int]$Repeat = 1, [string]$Tag = "r3", [string[]]$ExtraArgs = @())
$ErrorActionPreference = 'Continue'
$wt = 'C:\work\gnlink-wt\ka-r3'
$rel = "$wt\build\apps\native_poc\Release"
$old = 'C:\work\gnlink-wt\fix-capture-lifecycle-baseline\Release\GNLinkStream.exe'
$out = "D:\remote\remote\.claude\caplife\ka_e2e_$Tag"
New-Item -ItemType Directory -Force $out | Out-Null
"qwinsta:"; qwinsta | Select-Object -First 3
cmake --build "$wt\build" --config Release --target remote60_host_standby_table_test remote60_host_standby_admission_e2e_test -- /m /v:q /nologo 2>&1 | Select-String -Pattern ' error |error C' | Select-Object -First 8
if ($LASTEXITCODE -ne 0) { "BUILD FAILED rc=$LASTEXITCODE"; exit 2 }
foreach ($f in "$rel\remote60_host_standby_table_test.exe", "$rel\remote60_host_standby_admission_e2e_test.exe", "$rel\GNLinkStream.exe", $old) {
  "$((Get-FileHash $f).Hash) $f"
}
& "$rel\remote60_host_standby_table_test.exe" > "$out\table.txt" 2>&1
"table rc=$LASTEXITCODE | $((Get-Content "$out\table.txt" | Select-String 'RESULT').Line)"
$slot = 'C:\work\gnlink-wt\SLOT_REQUEST.txt'
try { $fs = [System.IO.File]::Open($slot, 'CreateNew'); $fs.Close() } catch { "slot busy"; exit 1 }
$env:REMOTE60_ALLOW_HOST_E2E = '1'
try {
  for ($i = 1; $i -le $Repeat; $i++) {
    & "$rel\remote60_host_standby_admission_e2e_test.exe" --old-host $old @ExtraArgs > "$out\e2e_$i.txt" 2>&1
    $rc = $LASTEXITCODE
    $fails = (Get-Content "$out\e2e_$i.txt" | Where-Object { $_ -like 'FAIL*' } | ForEach-Object { $_.Substring(0, [Math]::Min(150, $_.Length)) }) -join ' || '
    "e2e#$i rc=$rc | $((Get-Content "$out\e2e_$i.txt" | Select-String 'RESULT').Line) | $fails"
  }
} finally {
  Remove-Item $slot -Confirm:$false
  "slot removed=$(-not (Test-Path $slot))"
  "leftover: $((Get-Process | Where-Object { $_.ProcessName -match 'remote60_host_standby|GNLinkStream|^node$' } | ForEach-Object { "$($_.ProcessName):$($_.Id)" }) -join ',')"
}
