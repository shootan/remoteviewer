$ErrorActionPreference = 'Continue'
$wt = 'C:\work\gnlink-wt\fix-capture-lifecycle'
$rel = "$wt\build\apps\native_poc\Release"
$out = 'D:\remote\remote\.claude\caplife\r3_mut'
New-Item -ItemType Directory -Force $out | Out-Null
$py = 'D:\remote\remote\.claude\caplife\r3_mutant.py'
function Build { $o = cmake --build "$wt\build" --config Release -- /m /v:q 2>&1; $rc = $LASTEXITCODE; $o | Select-String -Pattern "error C|error LNK|: error" | Select-Object -First 5; return $rc }
function Hash($f) { (Get-FileHash $f -Algorithm SHA256).Hash.Substring(0, 12) }
$host_exe = "$rel\GNLinkStreamMigrationTest.exe"
"baseline host=$(Hash $host_exe)"
$plan = @(
  @('M10', 'unit', 'remote60_host_backend_status_board_test', @()),
  @('M11', 'unit', 'remote60_host_backend_owner_test', @()),
  @('M14', 'unit', 'remote60_capture_attachment_test', @()),
  @('M15', 'unit', 'remote60_capture_attachment_test', @()),
  @('M9', 'e2e', 'remote60_viewer_prelogin_login_e2e_test', @('--still-screen', '--still-pointerfirst')),
  @('M12', 'e2e', 'remote60_host_capture_lifecycle_ua_e2e_test', @('--rebuild-fails')),
  @('M13', 'e2e', 'remote60_host_capture_lifecycle_ua_e2e_test', @('--rebuild-fails'))
)
$slot = 'C:\work\gnlink-wt\SLOT_REQUEST.txt'
$env:REMOTE60_ALLOW_HOST_E2E = '1'; $env:REMOTE60_ALLOW_UI_TEST = '1'
foreach ($m in $plan) {
  $name = $m[0]
  python -I $py $name apply
  $b = Build; if ($b -ne 0) { "$name build failed"; python -I $py $name revert; continue }
  "$name built host=$(Hash $host_exe)"
  if ($m[1] -eq 'e2e') {
    try { $fs = [System.IO.File]::Open($slot, 'CreateNew'); $fs.Close() } catch { "slot busy"; python -I $py $name revert; break }
  }
  $a = $m[3]
  if ($a.Count -gt 0) { & "$rel\$($m[2]).exe" @a > "$out\$name.txt" 2>&1 } else { & "$rel\$($m[2]).exe" > "$out\$name.txt" 2>&1 }
  $rc = $LASTEXITCODE
  if ($m[1] -eq 'e2e') { Remove-Item $slot -Confirm:$false }
  $fails = Select-String -Path "$out\$name.txt" -Pattern '^FAIL|FAIL ' | Select-Object -First 4 | ForEach-Object { $_.Line.Substring(0, [Math]::Min(170, $_.Line.Length)) }
  "$name rc=$rc"; $fails | ForEach-Object { "    $_" }
  python -I $py $name revert
}
$b = Build
"restored build exit=$b host=$(Hash $host_exe)"
"slot present=$(Test-Path $slot)"
