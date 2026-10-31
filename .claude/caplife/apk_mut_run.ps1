$ErrorActionPreference = 'Continue'
$wt = 'C:\work\gnlink-wt\apk-prelogin-select'
$exe = "$wt\build\apps\native_poc\Release\remote60_client_session_prelogin_select_test.exe"
$out = 'D:\remote\remote\.claude\caplife\apk'
$files = "$wt\apps\native_poc\src\selection_ack_gate.hpp", "$wt\apps\native_poc\src\native_video_client_session.cpp"
$base = $files | ForEach-Object { (Get-FileHash $_).Hash }
function Build { cmake --build "$wt\build" --config Release --target remote60_client_session_prelogin_select_test -- /m /v:q /nologo 2>&1 | Select-String -Pattern 'error C' | Select-Object -First 5; $LASTEXITCODE }
function Run($name) {
  & $exe > "$out\$name.txt" 2> "$out\$name.err"
  $rc = $LASTEXITCODE
  $fails = (Get-Content "$out\$name.txt" | Where-Object { $_ -like 'FAIL*' } | ForEach-Object { $_.Substring(0, [Math]::Min(120, $_.Length)) }) -join ' || '
  "$name rc=$rc | $((Get-Content "$out\$name.txt" | Select-String 'RESULT').Line) | exe $((Get-FileHash $exe).Hash.Substring(0,12)) | $fails"
}
$b = Build; "base build rc=$b"; Run 'base'
foreach ($m in $(if ($args.Count) { $args } else { 'PRE','G1','G2','S2' })) {
  python -I D:\remote\remote\.claude\caplife\apk_mut.py $m apply
  $b = Build
  if ($b -eq 0) { Run $m } else { "$m BUILD FAILED" }
  python -I D:\remote\remote\.claude\caplife\apk_mut.py $m revert
  $files | ForEach-Object { (Get-Item $_).LastWriteTime = Get-Date }
}
$now = $files | ForEach-Object { (Get-FileHash $_).Hash }
"sources restored: $((Compare-Object $base $now) -eq $null)"
$b = Build; "final build rc=$b"; Run 'after'
