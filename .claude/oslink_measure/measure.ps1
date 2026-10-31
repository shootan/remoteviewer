param([int]$Seconds = 300, [string]$Out = 'D:\remote\remote\.claude\oslink_measure')
# Passive measurement while the user streams OSLink to this PC: no OSLink file, setting or process is touched.
# 1) every 100 ms: bytes sent/received on each up adapter (all traffic -- GNLink must be idle meanwhile)
# 2) every 1 s: CPU of OSLink/GNLink processes, GPU engine utilisation by pid (VideoEncode / 3D / Copy)
# 3) once at start and every 30 s: UDP/TCP endpoints owned by OSLink processes (which ports carry the stream)
$ErrorActionPreference = 'Continue'
New-Item -ItemType Directory -Force $Out | Out-Null
$names = 'ldremote','capture','ldremoteservice','ldremoteevent','GNLinkStream','GNLinkHost'
$net = Join-Path $Out 'net_100ms.csv'; $res = Join-Path $Out 'proc_1s.csv'; $ep = Join-Path $Out 'endpoints.txt'
'tMs,adapter,sentBytes,recvBytes' | Out-File $net -Encoding utf8
'tMs,name,pid,cpuPct,gpuVideoEncode,gpu3D,gpuCopy,gpuVideoProcessing' | Out-File $res -Encoding utf8
"start $(Get-Date -Format o)" | Out-File $ep -Encoding utf8
$nics = [System.Net.NetworkInformation.NetworkInterface]::GetAllNetworkInterfaces() | Where-Object { $_.OperationalStatus -eq 'Up' -and $_.NetworkInterfaceType -ne 'Loopback' }
$sw = [Diagnostics.Stopwatch]::StartNew()
$lastCpu = @{}; $nextSec = 0; $nextEp = 0
while ($sw.ElapsedMilliseconds -lt $Seconds * 1000) {
  $t = $sw.ElapsedMilliseconds
  foreach ($n in $nics) { $s = $n.GetIPStatistics(); "$t,$($n.Name),$($s.BytesSent),$($s.BytesReceived)" | Out-File $net -Append -Encoding utf8 }
  if ($t -ge $nextSec) {
    $nextSec += 1000
    $procs = Get-Process | Where-Object { $names -contains $_.ProcessName }
    $gpu = @{}
    try {
      (Get-Counter '\GPU Engine(*)\Utilization Percentage' -ErrorAction SilentlyContinue).CounterSamples | ForEach-Object {
        if ($_.InstanceName -match 'pid_(\d+)_.*engtype_(.+)$') { $k = "$($Matches[1])|$($Matches[2])"; $gpu[$k] = [double]$gpu[$k] + $_.CookedValue }
      }
    } catch {}
    foreach ($p in $procs) {
      $cpu = 0; try { $c = $p.TotalProcessorTime.TotalMilliseconds; if ($lastCpu.ContainsKey($p.Id)) { $cpu = ($c - $lastCpu[$p.Id]) / 10.0 / [Environment]::ProcessorCount }; $lastCpu[$p.Id] = $c } catch {}
      $g = { param($e) [math]::Round([double]$gpu["$($p.Id)|$e"], 1) }
      "$t,$($p.ProcessName),$($p.Id),$([math]::Round($cpu,1)),$(& $g 'VideoEncode'),$(& $g '3D'),$(& $g 'Copy'),$(& $g 'VideoProcessing')" | Out-File $res -Append -Encoding utf8
    }
  }
  if ($t -ge $nextEp) {
    $nextEp += 30000
    $ids = (Get-Process | Where-Object { $names -contains $_.ProcessName }) | ForEach-Object { $_.Id }
    "--- t=$t" | Out-File $ep -Append -Encoding utf8
    Get-NetUDPEndpoint -ErrorAction SilentlyContinue | Where-Object { $ids -contains $_.OwningProcess } | ForEach-Object { "udp $($_.LocalAddress):$($_.LocalPort) pid=$($_.OwningProcess)" } | Out-File $ep -Append -Encoding utf8
    Get-NetTCPConnection -State Established -ErrorAction SilentlyContinue | Where-Object { $ids -contains $_.OwningProcess } | ForEach-Object { "tcp $($_.LocalAddress):$($_.LocalPort)->$($_.RemoteAddress):$($_.RemotePort) pid=$($_.OwningProcess)" } | Out-File $ep -Append -Encoding utf8
  }
  Start-Sleep -Milliseconds 100
}
"end $(Get-Date -Format o)" | Out-File $ep -Append -Encoding utf8
