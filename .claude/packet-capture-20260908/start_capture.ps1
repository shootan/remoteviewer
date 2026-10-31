# GNLink host UDP capture (task t-2occguzn). Runs elevated. Guarded start, bounded stop, pcapng conversion.
$ErrorActionPreference = 'Continue'
$dir = 'D:\remote\remote\.claude\packet-capture-20260908'
$log = Join-Path $dir 'start_output.txt'
function W($m) { $line = "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff') $m"; Add-Content -Path $log -Value $line -Encoding utf8; Write-Host $line }

$isHigh = ((whoami /groups) -join "`n") -match 'S-1-16-12288'
W "elevated=$isHigh user=$(whoami) pid=$PID"
if (-not $isHigh) { W 'NOT ELEVATED - NOT STARTED'; Start-Sleep 15; exit 3 }

$s = (pktmon status | Out-String)
W "status-before:`n$s"
if ($s -match '(?<!In)Active|(?<!비)활성') { W 'EXISTING CAPTURE ACTIVE - NOT STARTED'; Start-Sleep 20; exit 2 }

$ts = Get-Date -Format yyyyMMdd_HHmmss
$f = Join-Path $dir "gnlink_host_$ts.etl"
W "filters-before:`n$(pktmon filter list | Out-String)"
pktmon filter remove | Out-Null
W "filter-add: $((pktmon filter add GNLinkUDP -t UDP -p 43000 -i 211.218.222.1) -join ' ')"
W "start: $((pktmon start --capture --comp nics --pkt-size 192 --file-name $f --file-size 128 --log-mode circular) -join ' ')"
$st = (pktmon status | Out-String)
W "status-after:`n$st"
$stopBy = (Get-Date).AddSeconds(600)
W "START $ts stopBy=$($stopBy.ToString('HH:mm:ss')) filter=UDP/port43000/ip211.218.222.1 comp=nics pktsize=192 filesize=128MB circular file=$f"

$req = Join-Path $dir 'stop.request'
while ((Get-Date) -lt $stopBy -and -not (Test-Path $req)) { Start-Sleep 2 }
$reason = if (Test-Path $req) { 'request' } else { 'timeout' }
W "stopping reason=$reason"
W "stop:`n$(pktmon stop | Out-String)"
pktmon filter remove | Out-Null
$p = $f -replace '\.etl$', '.pcapng'
W "etl2pcap:`n$(pktmon etl2pcap $f -o $p | Out-String)"
W "DONE etl=$f pcapng=$p"
Start-Sleep 5
