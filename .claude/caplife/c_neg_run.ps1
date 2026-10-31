# apk-inbox C: the negative runs -- each source in neg\<name> built in its own build directory (build-neg\<name>) and the
# C contrasts run against it once. A run is read by its exit code AND its summary line; a test that did not finish is
# TIMEOUT, not a result.
param([string]$Wt = 'C:\work\gnlink-wt\apk-inbox-c', [string[]]$Names = @(), [int]$LimitSec = 240, [string[]]$Only = @())
$tests = @('remote60_decoder_chain_inbox_test', 'remote60_decoder_chain_inbox_r2_contrast_test',
           'remote60_android_decoder_inbox_test', 'remote60_android_decoder_inbox_c_test',
           'remote60_android_decoder_inbox_session_test', 'remote60_android_decoder_liveness_test')
if ($Only.Count -gt 0) { $tests = $Only }
if ($Names.Count -eq 0) { $Names = (Get-ChildItem "$Wt\neg" -Directory | Sort-Object Name).Name }
foreach ($n in $Names) {
  $src = "$Wt\neg\$n"
  $b = "$Wt\build-neg\$n"
  $cfg = & cmake -S $Wt -B $b -G "Visual Studio 17 2022" "-DREMOTE60_LIVENESS_SINK_DIR=$src" "-DREMOTE60_CHAIN_INBOX_DIR=$src" `
    "-DREMOTE60_INBOX_SESSION_SRC=$src\native_video_client_session.cpp" 2>&1
  & cmake --build $b --config Release --target $tests -- /m /v:q 2>&1 | Out-File -Encoding utf8 "$b\neg_build.txt"
  foreach ($t in $tests) {
    $exe = "$b\apps\native_poc\Release\$t.exe"
    if (-not (Test-Path $exe)) { "{0,-20} {1,-46} BUILD FAILED" -f $n, $t; continue }
    $o = "$b\$t.out.txt"
    $p = Start-Process -FilePath $exe -NoNewWindow -PassThru -RedirectStandardOutput $o -RedirectStandardError "$o.err"
    $h = $p.Handle
    if (-not $p.WaitForExit($LimitSec * 1000)) { $p.Kill(); "{0,-20} {1,-46} TIMEOUT" -f $n, $t; continue }
    $p.WaitForExit()
    $fails = @(Select-String -Path $o -Pattern '^FAIL' | ForEach-Object { ($_.Line -replace '^FAIL\s+', '').Substring(0, [Math]::Min(70, ($_.Line -replace '^FAIL\s+', '').Length)) })
    $last = (Get-Content $o | Where-Object { $_ -match 'RESULT' } | Select-Object -Last 1)
    "{0,-20} {1,-46} exit={2} FAIL={3} {4} | {5}" -f $n, $t, $p.ExitCode, $fails.Count, $last, (($fails | Select-Object -First 2) -join ' ; ')
  }
}
