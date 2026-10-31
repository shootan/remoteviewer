# Per-second traffic sampler for the GNLink vs OSLink APK comparison (2026-09-25).
# Records NIC totals and the I/O counters of the two host processes. Read-only: it opens nothing
# but counters, writes only its own CSV, and stops at -Minutes or when STOP exists beside the CSV.
param(
  [string]$Adapter = '이더넷',
  [int]$Minutes = 60,
  [string]$OutDir = 'D:\remote\remote\.claude\measure'
)
$ErrorActionPreference = 'Continue'
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$csv = Join-Path $OutDir "traffic_$stamp.csv"
$stop = Join-Path $OutDir 'STOP'
if (Test-Path $stop) { Remove-Item $stop -Force }
'ts,ms,nicTxB,nicRxB,gnPid,gnWriteB,gnOtherB,osPid,osWriteB,osOtherB' | Out-File -FilePath $csv -Encoding ascii

function Snap {
  $n = Get-NetAdapterStatistics -Name $Adapter
  $p = @{}
  foreach ($proc in (Get-CimInstance Win32_Process -Filter "Name='GNLinkStream.exe' OR Name='ldremote.exe'")) {
    $p[$proc.Name] = $proc
  }
  [pscustomobject]@{
    tx = [int64]$n.SentBytes; rx = [int64]$n.ReceivedBytes
    gn = $p['GNLinkStream.exe']; os = $p['ldremote.exe']
  }
}
function D($now, $prev, $field) {
  if ($null -eq $now -or $null -eq $prev -or $now.ProcessId -ne $prev.ProcessId) { return '' }
  return [int64]$now.$field - [int64]$prev.$field
}

"csv=$csv"
$prev = Snap
$sw = [Diagnostics.Stopwatch]::StartNew()
$end = (Get-Date).AddMinutes($Minutes)
while ((Get-Date) -lt $end -and -not (Test-Path $stop)) {
  Start-Sleep -Milliseconds 1000
  $now = Snap
  $ms = $sw.ElapsedMilliseconds; $sw.Restart()
  $line = '{0},{9},{1},{2},{3},{4},{5},{6},{7},{8}' -f (Get-Date -Format 'HH:mm:ss'),
    ($now.tx - $prev.tx), ($now.rx - $prev.rx),
    $(if ($now.gn) { $now.gn.ProcessId } else { '' }), (D $now.gn $prev.gn 'WriteTransferCount'), (D $now.gn $prev.gn 'OtherTransferCount'),
    $(if ($now.os) { $now.os.ProcessId } else { '' }), (D $now.os $prev.os 'WriteTransferCount'), (D $now.os $prev.os 'OtherTransferCount'), $ms
  Add-Content -Path $csv -Value $line -Encoding ascii
  $prev = $now
}
"stopped at $(Get-Date -Format 'HH:mm:ss')"
