$ErrorActionPreference = 'Continue'
$wt = 'C:\work\gnlink-wt\apk-prelogin-select'
$rel = "$wt\build\apps\native_poc\Release"
$exe = "$rel\remote60_viewer_prelogin_login_e2e_test.exe"
$out = 'D:\remote\remote\.claude\caplife\apk\r1'
$src = "$wt\apps\native_poc\src\native_video_client_session.cpp"
$base = (Get-FileHash $src).Hash
$goodExe = (Get-FileHash $exe).Hash
function Build { cmake --build "$wt\build" --config Release --target remote60_viewer_prelogin_login_e2e_test -- /m /v:q /nologo 2>&1 | Select-String -Pattern 'error C' | Select-Object -First 5; $LASTEXITCODE }
$slot = 'C:\work\gnlink-wt\SLOT_REQUEST.txt'
try { $fs = [System.IO.File]::Open($slot, 'CreateNew'); $fs.Close() } catch { "slot busy"; exit 1 }
$env:REMOTE60_ALLOW_HOST_E2E = '1'; $env:REMOTE60_ALLOW_UI_TEST = '1'
try {
  python -I D:\remote\remote\.claude\caplife\apk_mut.py PRE apply
  $b = Build; "PRE build rc=$b exe $((Get-FileHash $exe).Hash.Substring(0,12))"
  if ($b -eq 0) {
    & $exe --android-select > "$out\android_select_PRE.txt" 2>&1
    $rc = $LASTEXITCODE
    $fails = (Get-Content "$out\android_select_PRE.txt" | Where-Object { $_ -like 'FAIL*' } | ForEach-Object { $_.Substring(0, [Math]::Min(150, $_.Length)) }) -join ' || '
    "PRE android_select rc=$rc | $((Get-Content "$out\android_select_PRE.txt" | Select-String 'RESULT' | Select-Object -Last 1).Line) | $fails"
  }
} finally {
  python -I D:\remote\remote\.claude\caplife\apk_mut.py PRE revert
  (Get-Item $src).LastWriteTime = Get-Date
  "source restored: $((Get-FileHash $src).Hash -eq $base)"
  $b = Build; "rebuild rc=$b exe $((Get-FileHash $exe).Hash.Substring(0,12)) (before $($goodExe.Substring(0,12)))"
  Remove-Item $slot -Confirm:$false
  "slot removed=$(-not (Test-Path $slot))"
}
