$ErrorActionPreference = 'Continue'
$rel = 'C:\work\gnlink-wt\rel-0163\build-test\apps\native_poc\Release'
$out = 'D:\remote\remote\.claude\caplife\e1_final'
New-Item -ItemType Directory -Force $out | Out-Null
"qwinsta:"; qwinsta | Select-Object -First 4
$slot = 'C:\work\gnlink-wt\SLOT_REQUEST.txt'
try { $fs = [System.IO.File]::Open($slot, 'CreateNew'); $fs.Close() } catch { "slot busy"; exit 1 }
$env:REMOTE60_ALLOW_HOST_E2E = '1'; $env:REMOTE60_ALLOW_UI_TEST = '1'
try {
  foreach ($n in 1, 2) {
    $shots = "$out\ua$n"; New-Item -ItemType Directory -Force $shots | Out-Null
    $env:REMOTE60_E2E_HOST_LOG_COPY = "$out\ua$n-host.log"
    & "$rel\remote60_host_capture_lifecycle_ua_e2e_test.exe" --render --shots $shots > "$out\ua$n.txt" 2>&1
    $rc = $LASTEXITCODE
    Remove-Item Env:REMOTE60_E2E_HOST_LOG_COPY
    "ua$n rc=$rc | $((Get-Content "$out\ua$n.txt" | Where-Object { $_ -match 'RESULT' } | Select-Object -Last 1))"
  }
  $shots = "$out\ps1"; New-Item -ItemType Directory -Force $shots | Out-Null
  & "$rel\remote60_viewer_prelogin_login_e2e_test.exe" --still-screen --render --shots $shots > "$out\ps1.txt" 2>&1
  "ps1 rc=$LASTEXITCODE | $((Get-Content "$out\ps1.txt" | Where-Object { $_ -match 'RESULT' } | Select-Object -Last 1))"
} finally {
  Remove-Item $slot -Confirm:$false
  "slot removed=$(-not (Test-Path $slot))"
  "leftover test processes: $((Get-Process | Where-Object { $_.ProcessName -match 'remote60_|GNLinkStreamMigrationTest' } | Measure-Object).Count)"
}
