param([string[]]$Only = @(), [string]$Tag = 'r2')
$ErrorActionPreference = 'Continue'
$wt = 'C:\work\gnlink-wt\fix-capture-lifecycle'
$rel = "$wt\build\apps\native_poc\Release"
$out = "D:\remote\remote\.claude\caplife\$Tag"
New-Item -ItemType Directory -Force $out | Out-Null
$slot = 'C:\work\gnlink-wt\SLOT_REQUEST.txt'
try { $fs = [System.IO.File]::Open($slot, 'CreateNew'); $b = [Text.Encoding]::UTF8.GetBytes("capture-lifecycle r2 e2e (worker1) $(Get-Date -Format o)"); $fs.Write($b, 0, $b.Length); $fs.Close() } catch { "slot busy: $_"; exit 1 }
$env:REMOTE60_ALLOW_HOST_E2E = '1'; $env:REMOTE60_ALLOW_UI_TEST = '1'
Remove-Item Env:REMOTE60_E2E_OLD_BIN -ErrorAction SilentlyContinue
$runs = @(
  @('ps1', 'remote60_viewer_prelogin_login_e2e_test', @('--still-screen')),
  @('ps2', 'remote60_viewer_prelogin_login_e2e_test', @('--still-screen')),
  @('psnofirst', 'remote60_viewer_prelogin_login_e2e_test', @('--still-screen', '--still-nofirst')),
  @('psptr', 'remote60_viewer_prelogin_login_e2e_test', @('--still-screen', '--still-pointerfirst')),
  @('ua1', 'remote60_host_capture_lifecycle_ua_e2e_test', @()),
  @('ua2', 'remote60_host_capture_lifecycle_ua_e2e_test', @()),
  @('uaff', 'remote60_host_capture_lifecycle_ua_e2e_test', @('--fullframe-fails')),
  @('uafail', 'remote60_host_capture_lifecycle_ua_e2e_test', @('--rebuild-fails')),
  @('lk_lock', 'remote60_viewer_prelogin_login_e2e_test', @('--lock-screen')),
  @('lk_prelogin', 'remote60_viewer_prelogin_login_e2e_test', @()),
  @('lk_notprelogin', 'remote60_viewer_prelogin_login_e2e_test', @('--not-prelogin'))
)
try {
  foreach ($r in $runs) {
    $name = $r[0]; if ($Only.Count -gt 0 -and $Only -notcontains $name) { continue }
    $exe = "$rel\$($r[1]).exe"; $a = $r[2]
    if (-not (Test-Path $exe)) { "$name MISSING"; continue }
    if ($a.Count -gt 0) { & $exe @a > "$out\e2e_$name.txt" 2>&1 } else { & $exe > "$out\e2e_$name.txt" 2>&1 }
    $rc = $LASTEXITCODE
    $pass = (Select-String -Path "$out\e2e_$name.txt" -Pattern '^PASS' | Measure-Object).Count
    $fail = (Select-String -Path "$out\e2e_$name.txt" -Pattern '^FAIL' | Measure-Object).Count
    "$name rc=$rc pass=$pass fail=$fail | $((Get-Content "$out\e2e_$name.txt" | Where-Object { $_ -match 'RESULT' } | Select-Object -Last 1))"
  }
} finally {
  Remove-Item $slot -Confirm:$false
  "slot removed=$(-not (Test-Path $slot))"
}



