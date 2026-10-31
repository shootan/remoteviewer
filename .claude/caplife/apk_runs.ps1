param([string]$Tag = 'r1')
$ErrorActionPreference = 'Continue'
$wt = 'C:\work\gnlink-wt\apk-prelogin-select'
$rel = "$wt\build\apps\native_poc\Release"
$out = "D:\remote\remote\.claude\caplife\apk\$Tag"
New-Item -ItemType Directory -Force $out | Out-Null
"qwinsta:"; qwinsta | Select-Object -First 3
cmake --build "$wt\build" --config Release --target remote60_viewer_prelogin_login_e2e_test remote60_client_session_prelogin_select_test -- /m /v:q /nologo 2>&1 | Select-String -Pattern 'error C' | Select-Object -First 5
if ($LASTEXITCODE -ne 0) { "BUILD FAILED"; exit 2 }
$pure = 'remote60_client_session_prelogin_select_test','remote60_client_session_role_park_test','remote60_native_video_client_shared_core_test',
        'remote60_monitor_select_wire_test','remote60_client_telemetry_window_test','remote60_client_udp_recv_loop_test',
        'remote60_pinned_seam_boundary_test','remote60_viewer_selection_gate_test','remote60_viewer_window_proc_isolated_test'
foreach ($t in $pure) {
  if (-not (Test-Path "$rel\$t.exe")) { "$t MISSING"; continue }
  & "$rel\$t.exe" > "$out\$t.txt" 2>&1
  "$t rc=$LASTEXITCODE | $((Get-Content "$out\$t.txt" | Where-Object { $_.Trim() } | Select-Object -Last 1)) | $((Get-FileHash "$rel\$t.exe").Hash.Substring(0,12))"
}
$slot = 'C:\work\gnlink-wt\SLOT_REQUEST.txt'
try { $fs = [System.IO.File]::Open($slot, 'CreateNew'); $fs.Close() } catch { "slot busy"; exit 1 }
$env:REMOTE60_ALLOW_HOST_E2E = '1'; $env:REMOTE60_ALLOW_UI_TEST = '1'
$runs = @(
  @('android_select', 'remote60_viewer_prelogin_login_e2e_test', @('--android-select')),
  @('android_session', 'remote60_viewer_prelogin_login_e2e_test', @('--android-session')),
  @('foreign_peer', 'remote60_viewer_prelogin_login_e2e_test', @('--foreign-peer')),
  @('lk_prelogin', 'remote60_viewer_prelogin_login_e2e_test', @()),
  @('monitor_fallback', 'remote60_host_monitor_fallback_e2e_test', @()),
  @('android_reboot', 'remote60_android_session_reboot_e2e_test', @()),
  @('mobile_abr', 'remote60_mobile_abr_session_e2e_test', @())
)
try {
  foreach ($r in $runs) {
    $name = $r[0]; $exe = "$rel\$($r[1]).exe"; $a = $r[2]
    if (-not (Test-Path $exe)) { "$name MISSING"; continue }
    if ($a.Count -gt 0) { & $exe @a > "$out\$name.txt" 2>&1 } else { & $exe > "$out\$name.txt" 2>&1 }
    $rc = $LASTEXITCODE
    $fails = (Get-Content "$out\$name.txt" | Where-Object { $_ -like 'FAIL*' } | ForEach-Object { $_.Substring(0, [Math]::Min(140, $_.Length)) }) -join ' || '
    "$name rc=$rc | $((Get-Content "$out\$name.txt" | Select-String 'RESULT' | Select-Object -Last 1).Line) | $fails"
  }
} finally {
  Remove-Item $slot -Confirm:$false
  "slot removed=$(-not (Test-Path $slot))"
  "leftover: $((Get-Process | Where-Object { $_.ProcessName -match 'remote60_|GNLinkStream' } | ForEach-Object { "$($_.ProcessName):$($_.Id)" }) -join ',')"
}
