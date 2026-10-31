$ErrorActionPreference = 'Continue'
$wt = 'C:\work\gnlink-wt\apk-prelogin-select'
$exe = "$wt\build\apps\native_poc\Release\remote60_viewer_prelogin_login_e2e_test.exe"
$out = 'D:\remote\remote\.claude\caplife\apk\r3'
New-Item -ItemType Directory -Force $out | Out-Null
cmake --build "$wt\build" --config Release --target remote60_viewer_prelogin_login_e2e_test -- /m /v:q /nologo 2>&1 | Select-String 'error C' | Select-Object -First 5
"build rc=$LASTEXITCODE exe $((Get-FileHash $exe).Hash)"
"qwinsta:"; qwinsta | Select-Object -First 3
$slot = 'C:\work\gnlink-wt\SLOT_REQUEST.txt'
try { $fs = [System.IO.File]::Open($slot, 'CreateNew'); $fs.Close() } catch { "slot busy"; exit 1 }
$env:REMOTE60_ALLOW_HOST_E2E = '1'; $env:REMOTE60_ALLOW_UI_TEST = '1'
try {
  & $exe --android-select > "$out\android_select.txt" 2>&1
  $rc = $LASTEXITCODE
  "android_select rc=$rc PASS=$((Select-String -Path "$out\android_select.txt" -Pattern '^PASS').Count) FAIL=$((Select-String -Path "$out\android_select.txt" -Pattern '^FAIL').Count) | $((Get-Content "$out\android_select.txt" | Select-String 'RESULT' | Select-Object -Last 1).Line)"
  Get-Content "$out\android_select.txt" | Where-Object { $_ -like 'FAIL*' } | ForEach-Object { "   " + $_.Substring(0, [Math]::Min(160, $_.Length)) }
} finally {
  Remove-Item $slot -Confirm:$false
  "slot removed=$(-not (Test-Path $slot))"
  "leftover: $((Get-Process | Where-Object { $_.ProcessName -match 'GNLinkStream|remote60_' } | ForEach-Object { "$($_.ProcessName):$($_.Id)" }) -join ',')"
}
