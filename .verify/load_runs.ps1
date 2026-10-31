param([string]$Label, [string]$Exe, [int]$Count, [int]$BusySecs = 480)
$V = 'C:\work\gnlink-wt\verify-dirretry\.verify'
foreach ($i in 1..$Count) {
  $busyOut = "$V\load_${Label}_run$i.busy.txt"
  $bp = Start-Process -FilePath python -ArgumentList '-I', "$V\busy.py", "$BusySecs" -RedirectStandardOutput $busyOut -PassThru -WindowStyle Hidden
  Start-Sleep -Seconds 2
  $t0 = Get-Date
  $sw = [Diagnostics.Stopwatch]::StartNew()
  & $Exe > "$V\load_${Label}_run$i.out" 2>&1
  $rc = $LASTEXITCODE
  $sw.Stop()
  $t1 = Get-Date
  $busyAlive = -not $bp.HasExited
  $bp.WaitForExit()
  $f = (Select-String -Path "$V\load_${Label}_run$i.out" -Pattern '^FAIL').Count
  "load $Label run$i rc=$rc FAIL=$f secs=$([int]$sw.Elapsed.TotalSeconds) busyAliveAtTestEnd=$busyAlive start=$($t0.ToString('HH:mm:ss')) end=$($t1.ToString('HH:mm:ss')) last=$((Get-Content "$V\load_${Label}_run$i.out" -Tail 1))"
  Select-String -Path "$V\load_${Label}_run$i.out" -Pattern '^FAIL' | % { "   " + $_.Line.Substring(0, [Math]::Min(220, $_.Line.Length)) }
}
