param([int]$Repeat = 3)
$b = 'C:\work\gnlink-wt\ka-r3\build\apps\native_poc\Release'
$out = 'D:\remote\remote\.claude\caplife\ka2n12_mut'
New-Item -ItemType Directory -Force $out | Out-Null
function RunOne($log) {
  $p = Start-Process -FilePath "$b\remote60_standby_lease_test.exe" -NoNewWindow -PassThru -RedirectStandardOutput $log
  $h = $p.Handle  # keeps the exit code readable
  if (-not $p.WaitForExit(90000)) { $p.Kill(); $p.WaitForExit(); return 'TIMEOUT' }
  return "$($p.ExitCode)"
}
cmake --build C:\work\gnlink-wt\ka-r3\build --config Release --target remote60_standby_lease_test -- /m /v:q | Out-Null
"build rc=$LASTEXITCODE"
# the unmutated test, repeated: an intermittent hang shows here, not in one pass
$codes = @()
for ($i = 1; $i -le $Repeat; $i++) {
  $c = RunOne "$out\base_$i.txt"
  $res = (Select-String -Path "$out\base_$i.txt" -Pattern '^RESULT|checks,' | ForEach-Object { $_.Line }) -join ' '
  $codes += "${i}:${c} [${res}]"
}
"base x${Repeat}: $($codes -join ' | ')"
$muts = 'syncNoUntie','strayUncounted','noReopenWhenTied','notShared','n2aUnchecked','n2bNoIntent'
foreach ($m in $muts) {
  python D:/remote/remote/.claude/caplife/ka2n12_mut.py $m apply | Out-Null
  cmake --build C:\work\gnlink-wt\ka-r3\build --config Release --target remote60_standby_lease_test -- /m /v:q | Out-Null
  $built = $LASTEXITCODE
  $log = "$out\$m.txt"
  $code = if ($built -eq 0) { RunOne $log } else { 'build-fail' }
  python D:/remote/remote/.claude/caplife/ka2n12_mut.py $m restore | Out-Null
  $fails = @(Select-String -Path $log -Pattern '^FAIL' -ErrorAction SilentlyContinue).Count
  $first = (Select-String -Path $log -Pattern '^FAIL' -ErrorAction SilentlyContinue | Select-Object -First 1 | ForEach-Object { $_.Line })
  "$m exit=$code FAILlines=$fails first: $first"
}
cmake --build C:\work\gnlink-wt\ka-r3\build --config Release --target remote60_standby_lease_test -- /m /v:q | Out-Null
$c = RunOne "$out\restored.txt"
"restored exit=$c $((Select-String -Path "$out\restored.txt" -Pattern '^RESULT|checks,' | ForEach-Object { $_.Line }) -join ' ')"
