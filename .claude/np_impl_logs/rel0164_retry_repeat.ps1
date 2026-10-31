# rel-0164: remote60_directory_retry_test repeated N times -- the "single use" case's intermittent FAIL counted.
# Pure (fake directory, loopback); each run in a NEW test root; bounded at 600 s each.
param([string]$Exe, [string]$Tag, [int]$N = 20)
$logs = "D:\remote\remote\.claude\np_impl_logs\rel0164_retry_$Tag"
if (Test-Path -LiteralPath $logs) { "$logs exists -- not reused"; exit 2 }
New-Item -ItemType Directory -Path $logs | Out-Null
"exe: $Exe  sha256: $((Get-FileHash $Exe).Hash)" | Tee-Object "$logs\summary.txt"
$single = 0; $other = 0
for ($i = 1; $i -le $N; ++$i) {
  $env:REMOTE60_TEST_TMP = "D:\remote\remote\.claude\test-tmp\rel0164-retry-$Tag-$i"
  New-Item -ItemType Directory -Path $env:REMOTE60_TEST_TMP | Out-Null
  $out = "$logs\run$i.log"
  $p = Start-Process -FilePath $Exe -RedirectStandardOutput $out -RedirectStandardError "$logs\run$i.err.log" -NoNewWindow -PassThru
  $null = $p.Handle
  if (-not $p.WaitForExit(600000)) { $p.Kill(); $p.WaitForExit(); $rc = 'TIMEOUT' } else { $rc = $p.ExitCode }
  $l = Get-Content $out
  $fails = @($l | Select-String '^FAIL')
  $s = @($fails | Where-Object { $_.Line -match 'single use' }).Count
  $single += $s; $other += ($fails.Count - $s)
  ("run {0,2} rc={1} PASS={2} FAIL={3} single-use-FAIL={4} | {5}" -f $i, $rc, ($l | Select-String '^PASS').Count, $fails.Count, $s,
    ($l | Select-String 'single use|reissue|heartbeats between' | ForEach-Object { $_.Line.Trim() }) -join ' ; ') | Tee-Object -Append "$logs\summary.txt"
}
"TOTAL single-use FAIL in $single of $N runs; other FAIL lines: $other" | Tee-Object -Append "$logs\summary.txt"
