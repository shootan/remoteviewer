# apk-inbox C: the existing regressions on the C build (no device, no real host). One run each, with a hard limit;
# PASS count, exit code and the last summary line are read together (a run that did not finish is not a result).
param([string]$Bin = 'C:\work\gnlink-wt\apk-inbox-c\build\apps\native_poc\Release', [string]$Out = 'C:\work\gnlink-wt\apk-inbox-c\build\regress',
      [string[]]$Tests = @('remote60_decoder_chain_inbox_test', 'remote60_android_decoder_inbox_test', 'remote60_android_decoder_inbox_c_test',
        'remote60_android_decoder_owed_key_test', 'remote60_android_decoder_geometry_test', 'remote60_android_decoder_binding_test',
        'remote60_android_decoder_decode_stats_test', 'remote60_android_decoder_liveness_test', 'remote60_android_decoder_session_liveness_test',
        'remote60_decoder_pending_slot_test', 'remote60_native_video_client_shared_core_test', 'remote60_client_session_prelogin_select_test',
        'remote60_udp_control_e2e_test'),
      [int]$LimitSec = 300)
New-Item -ItemType Directory -Force $Out | Out-Null
foreach ($t in $Tests) {
  $exe = Join-Path $Bin "$t.exe"
  if (-not (Test-Path $exe)) { "{0,-52} MISSING" -f $t; continue }
  $o = Join-Path $Out "$t.out.txt"
  $p = Start-Process -FilePath $exe -NoNewWindow -PassThru -RedirectStandardOutput $o -RedirectStandardError "$o.err"
  $h = $p.Handle  # keep the handle so ExitCode is readable
  if (-not $p.WaitForExit($LimitSec * 1000)) { $p.Kill(); "{0,-52} TIMEOUT after {1} s" -f $t, $LimitSec; continue }
  $p.WaitForExit()
  $pass = @(Select-String -Path $o -Pattern '^PASS').Count
  $fail = @(Select-String -Path $o -Pattern '^FAIL').Count
  $last = (Get-Content $o | Where-Object { $_ -match 'RESULT|checks|passed|failed' } | Select-Object -Last 1)
  "{0,-52} exit={1} PASS={2} FAIL={3} | {4}" -f $t, $p.ExitCode, $pass, $fail, $last
}
