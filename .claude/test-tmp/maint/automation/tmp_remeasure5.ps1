Set-Location C:\work\remote\remote60
$rows = @()
$buildDir = 'C:\Users\CHO\AppData\Local\Temp\build-native2-test'
for ($i = 1; $i -le 20; $i++) {
  Write-Output ("RUN=" + $i)
  $lines = @(powershell -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 -BuildDir $buildDir -Codec h264 -Transport udp -Fps 30 -FpsHint 30 -HostSeconds 12 -ClientSeconds 10 -Bitrate 1100000 -Keyint 15 -TraceEvery 0 -NoInputChannel)
  $pick = {
    param($name)
    $line = $lines | Where-Object { $_ -like ($name + '=*') } | Select-Object -First 1
    if ($null -eq $line) { return "" }
    return ($line -replace '^[^=]+=', '')
  }
  $r = [pscustomobject]@{
    run = $i
    pipe = & $pick 'USER_FEEDBACK_UF_H_PIPEUS_AVG_US'
    c2e = & $pick 'USER_FEEDBACK_UF_H_C2EUS_AVG_US'
    encQueue = & $pick 'USER_FEEDBACK_UF_H_ENCQUEUEUS_AVG_US'
    q2e = & $pick 'USER_FEEDBACK_UF_H_QUEUETOENCODEUS_AVG_US'
    q2s = & $pick 'USER_FEEDBACK_UF_H_QUEUETOSENDUS_AVG_US'
    selectWait = & $pick 'USER_FEEDBACK_UF_H_SELECTWAITUS_AVG_US'
    queueSelectWait = & $pick 'USER_FEEDBACK_UF_H_QUEUESELECTWAITUS_AVG_US'
    captureToAuCount = & $pick 'USER_FEEDBACK_UF_H_CAPTURETOAUUS_COUNT'
    captureToAuSkewCount = & $pick 'USER_FEEDBACK_UF_H_CAPTURETOAUSKEWUS_COUNT'
    captureToAuSkewAvg = & $pick 'USER_FEEDBACK_UF_H_CAPTURETOAUSKEWUS_AVG_US'
    hostBottleneck = (($lines | Where-Object { $_ -like 'USER_FEEDBACK_HOST_BOTTLENECK_STAGE=*' } | Select-Object -First 1) -replace '^[^=]+=', '')
    skip = (($lines | Where-Object { $_ -like 'HOST_QUEUE_SKIPPED_BY_OVERWRITE=*' } | Select-Object -First 1) -replace '^[^=]+=', '')
    timeoutCnt = (($lines | Where-Object { $_ -like 'HOST_QUEUE_WAIT_TIMEOUT_COUNT=*' } | Select-Object -First 1) -replace '^[^=]+=', '')
    noWorkCnt = (($lines | Where-Object { $_ -like 'HOST_QUEUE_WAIT_NOWORK_COUNT=*' } | Select-Object -First 1) -replace '^[^=]+=', '')
  }
  $rows += $r
  Write-Output ("PIPE={0} C2E={1} EQ={2} Q2E={3} Q2S={4} SW={5} QSW={6} SkewCnt={7}/{8} Avg={9} Bn={10} Skip={11} Timeout={12} NoWork={13}" -f $r.pipe, $r.c2e, $r.encQueue, $r.q2e, $r.q2s, $r.selectWait, $r.queueSelectWait, $r.captureToAuCount, $r.captureToAuSkewCount, $r.captureToAuSkewAvg, $r.hostBottleneck, $r.skip, $r.timeoutCnt, $r.noWorkCnt)
}

$nums = $rows | Where-Object { $_.pipe -as [double] } | ForEach-Object { [double]$_.pipe }
Write-Output ("SUMMARY_PIPE_AVG=" + [math]::Round(($nums | Measure-Object -Average).Average,1))
$nums = $rows | Where-Object { $_.c2e -as [double] } | ForEach-Object { [double]$_.c2e }
Write-Output ("SUMMARY_C2E_AVG=" + [math]::Round(($nums | Measure-Object -Average).Average,1))
$nums = $rows | Where-Object { $_.encQueue -as [double] } | ForEach-Object { [double]$_.encQueue }
Write-Output ("SUMMARY_ENCQUEUE_AVG=" + [math]::Round(($nums | Measure-Object -Average).Average,1))
$nums = $rows | Where-Object { $_.q2e -as [double] } | ForEach-Object { [double]$_.q2e }
Write-Output ("SUMMARY_Q2E_AVG=" + [math]::Round(($nums | Measure-Object -Average).Average,1))
$nums = $rows | Where-Object { $_.q2s -as [double] } | ForEach-Object { [double]$_.q2s }
Write-Output ("SUMMARY_Q2S_AVG=" + [math]::Round(($nums | Measure-Object -Average).Average,1))
$skewCountList = $rows | ForEach-Object { if ($_.captureToAuSkewCount) { [double]$_.captureToAuSkewCount } else { 0 } }
Write-Output ("SUMMARY_CAPTURETOAUSKEW_COUNT_SUM=" + ($skewCountList | Measure-Object -Sum).Sum)
$skewAvgList = $rows | Where-Object { $_.captureToAuSkewAvg -as [double] } | ForEach-Object { [double]$_.captureToAuSkewAvg }
if ($skewAvgList.Count -gt 0) { Write-Output ("SUMMARY_CAPTURETOAUSKEW_AVG=" + [math]::Round(($skewAvgList | Measure-Object -Average).Average,1)) } else { Write-Output "SUMMARY_CAPTURETOAUSKEW_AVG=0" }
$queueTimeout = $rows | Where-Object { $_.timeoutCnt -as [double] } | ForEach-Object { [double]$_.timeoutCnt } | Measure-Object -Sum
$queueNoWork = $rows | Where-Object { $_.noWorkCnt -as [double] } | ForEach-Object { [double]$_.noWorkCnt } | Measure-Object -Sum
$queueSkip = $rows | Where-Object { $_.skip -as [double] } | ForEach-Object { [double]$_.skip } | Measure-Object -Sum
Write-Output ("SUMMARY_TIMEOUT_TOTAL=" + $queueTimeout.Sum)
Write-Output ("SUMMARY_NOWORK_TOTAL=" + $queueNoWork.Sum)
Write-Output ("SUMMARY_SKIP_TOTAL=" + $queueSkip.Sum)
