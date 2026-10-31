# Start-GnlinkCapture.ps1 -- GNLink host UDP capture, 2nd-run helper (task t-2occguzn, Codex-approved conditions).
# Run in an elevated PowerShell. Fail-closed: refuses to start unless every check below passes.
#   admin -> pktmon status readable and "not running" -> existing filters narrow (kept) -> add GNLinkUDP-<ts> (UDP port 43000 only)
#   -> pktmon start exit 0 -> status shows active AND its log file == our etl -> "START ..." -> loop (stop.request | DurationSec)
#   -> stop only if status log file == our etl -> stats -> etl2pcap -> manifest json.
# Watchdog is the in-script loop (no Start-Job). Filters: pktmon has no per-name remove ("filter remove" = all), so
# our filter is removed via "filter remove" ONLY when no pre-existing filter was present; otherwise all filters are kept
# and reported (manual cleanup).
[CmdletBinding()]
param(
  [string]$Dir = 'D:\remote\remote\.claude\packet-capture-20260908',
  [int]$DurationSec = 600,
  [int]$FileSizeMB = 512,
  [int]$Port = 43000,
  [int]$PktSize = 192
)
$ErrorActionPreference = 'Continue'
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch {}
if (-not (Test-Path $Dir)) { New-Item -ItemType Directory -Force $Dir | Out-Null }
$ts = Get-Date -Format yyyyMMdd_HHmmss
$log = Join-Path $Dir "run2_$ts.log"
$manifestPath = Join-Path $Dir "run2_$ts.manifest.json"
$etl = Join-Path $Dir "gnlink_host2_$ts.etl"
$pcap = Join-Path $Dir "gnlink_host2_$ts.pcapng"
$filterName = "GNLinkUDP-$ts"
$stopReq = Join-Path $Dir 'stop.request'
$m = [ordered]@{
  run = "run2_$ts"; startedAt = $null; endedAt = $null; exitCode = $null; endReason = $null
  filterName = $filterName; filterCommand = "pktmon filter add $filterName -t UDP -p $Port"
  startCommand = "pktmon start --capture --comp nics --pkt-size $PktSize --file-name $etl --file-size $FileSizeMB --log-mode circular"
  params = [ordered]@{ comp = 'nics'; pktSize = $PktSize; fileSizeMB = $FileSizeMB; logMode = 'circular'; port = $Port; ipFilter = 'none'; durationSec = $DurationSec }
  etl = $etl; pcapng = $null; preExistingFilters = $null; filtersRemovedAtEnd = $null
  statusBefore = $null; statusAfterStart = $null; statusBeforeStop = $null; stopOutput = $null; etl2pcapOutput = $null; notes = @()
}
function W($s) { $line = "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff') $s"; Add-Content -Path $log -Value $line -Encoding utf8; Write-Host $line }
function Save() { $m.endedAt = (Get-Date).ToString('o'); ($m | ConvertTo-Json -Depth 6) | Out-File -FilePath $manifestPath -Encoding utf8 }
function Fail($code, $why) { $m.exitCode = $code; $m.endReason = $why; W "REFUSED ($code): $why"; Save; Start-Sleep 10; exit $code }
function Run($argLine) {  # returns @{ out=<string>; code=<int> }. chcp 65001 in the same cmd so pktmon emits UTF-8; console decodes UTF-8.
  [Console]::OutputEncoding = [System.Text.Encoding]::UTF8
  $out = (& cmd.exe /c "chcp 65001>nul & pktmon $argLine 2>&1" | Out-String); $code = $LASTEXITCODE
  return @{ out = $out; code = $code }
}
function StatusLogFile($text) {  # the "Log file:" path pktmon status prints while capturing (ko/en), else $null
  foreach ($ln in ($text -split "`r?`n")) { if ($ln -match '\.etl\s*$') { return ($ln -replace '^.*?:\s*(?=[A-Za-z]:\\)', '').Trim() } }
  return $null
}
function SamePath($a, $b) { if (-not $a -or -not $b) { return $false }; return ([IO.Path]::GetFullPath($a).TrimEnd('\') -ieq [IO.Path]::GetFullPath($b).TrimEnd('\')) }

$m.startedAt = (Get-Date).ToString('o')
# (1) admin
$isHigh = ((whoami /groups) -join "`n") -match 'S-1-16-12288'
W "elevated=$isHigh user=$(whoami) pid=$PID dir=$Dir"
if (-not $isHigh) { Fail 3 'not elevated' }

# (2) pktmon status must be readable and must say "not running" (fail-closed on anything else)
$st = Run 'status'; $m.statusBefore = $st.out; W "status-before (exit $($st.code)):`n$($st.out)"
# Language-independent: active := a status line ending in .etl (the capture's log file); not-running := exit 0,
# no denial phrase, no .etl line and some output. Korean/English phrases are only auxiliary (logged), never required.
if ($st.code -ne 0) { Fail 2 "pktmon status exit $($st.code)" }
if ($st.out -match '액세스|denied|Access is denied|통신하지 못|Failed to communicate') { Fail 2 'pktmon status: access denied / driver unreachable' }
$activeEtl = StatusLogFile $st.out
if ($null -ne $activeEtl) { Fail 2 "a capture is already active (log file $activeEtl) - not touching it" }
if ($st.out.Trim().Length -eq 0) { Fail 2 'pktmon status printed nothing - fail closed' }
$phraseNotRunning = $st.out -match 'is not running|실행되고 있지 않'
W "state: no .etl line, exit 0 -> not running (phrase match: $phraseNotRunning)"
if (-not $phraseNotRunning) { $m.notes += 'not-running phrase not matched (locale/encoding); decided by exit 0 + no .etl line + no denial' }

# (3) existing filters: keep; refuse if broad
$fl = Run 'filter list'; W "filters-before (exit $($fl.code)):`n$($fl.out)"
if ($fl.code -ne 0 -or $fl.out -match '액세스|denied|통신하지 못|Failed to communicate') { Fail 2 'pktmon filter list unreadable' }
$rows = @(($fl.out -split "`r?`n") | Where-Object { $_ -match '^\s*\d+\s+\S' })
$m.preExistingFilters = $rows
if ($rows.Count -gt 0) {
  W "pre-existing filters kept ($($rows.Count)): $($rows -join ' | ')"
  foreach ($r in $rows) {
    $hasProto = $r -match '\b(UDP|TCP|ICMP|ICMPv6)\b'; $hasPort = $r -match '\b\d{2,5}\b(?!.*\b(UDP|TCP)\b)'
    if (-not ($hasProto -or $hasPort)) { Fail 2 "pre-existing broad filter would widen the capture: '$r'" }
  }
  $m.notes += 'pre-existing filters present: pktmon cannot remove one filter by name, so no "filter remove" will run at the end; GNLinkUDP-<ts> stays until manual cleanup'
}

# (4) our filter: UDP port only, no IP restriction
$fa = Run "filter add $filterName -t UDP -p $Port"; W "filter-add (exit $($fa.code)): $($fa.out.Trim())"
if ($fa.code -ne 0) { Fail 2 "filter add failed: $($fa.out.Trim())" }
$fl2 = Run 'filter list'; W "filters-after-add:`n$($fl2.out)"
if ($fl2.out -notmatch [regex]::Escape($filterName)) { Fail 2 'our filter is not listed after add' }

# (5) start, then prove it is ours
$sc = Run "start --capture --comp nics --pkt-size $PktSize --file-name `"$etl`" --file-size $FileSizeMB --log-mode circular"
W "start (exit $($sc.code)):`n$($sc.out)"
if ($sc.code -ne 0) { $m.notes += 'start failed'; if ($rows.Count -eq 0) { Run 'filter remove' | Out-Null; $m.filtersRemovedAtEnd = $true }; Fail 4 "pktmon start exit $($sc.code)" }
$st2 = Run 'status'; $m.statusAfterStart = $st2.out; W "status-after-start (exit $($st2.code)):`n$($st2.out)"
$lf = StatusLogFile $st2.out
if ($st2.code -ne 0 -or -not (SamePath $lf $etl)) {
  $m.notes += "status after start does not show our etl (got '$lf') - stopping nothing"; Fail 4 'capture not confirmed as ours'
}
$stopBy = (Get-Date).AddSeconds($DurationSec)
W "START $ts stopBy=$($stopBy.ToString('HH:mm:ss')) filter=$filterName(UDP port $Port, no IP limit) comp=nics pktsize=$PktSize filesize=${FileSizeMB}MB circular file=$etl"
W "note: circular ${FileSizeMB}MB keeps the most recent packets only; it is NOT a guarantee that all $DurationSec s are retained"
Save

# (6) in-script watchdog: stop.request or DurationSec; stop only if status still points at our etl
$reason = 'timeout'
try {
  while ((Get-Date) -lt $stopBy) { if (Test-Path $stopReq) { $reason = 'stop.request'; break }; Start-Sleep 2 }
  $m.endReason = $reason; W "stopping reason=$reason"
  $st3 = Run 'status'; $m.statusBeforeStop = $st3.out
  $lf3 = StatusLogFile $st3.out
  if ($st3.code -eq 0 -and (SamePath $lf3 $etl)) {
    $so = Run 'stop'; $m.stopOutput = $so.out; W "stop (exit $($so.code)):`n$($so.out)"
    $cv = Run "etl2pcap `"$etl`" -o `"$pcap`""; $m.etl2pcapOutput = $cv.out; W "etl2pcap (exit $($cv.code)):`n$($cv.out)"
    if (Test-Path $pcap) { $m.pcapng = $pcap }
    $m.exitCode = 0
  } else {
    $m.notes += "status before stop does not show our etl (got '$lf3', exit $($st3.code)) - NOT stopping"; $m.exitCode = 5
    W "NOT STOPPING: active log file is '$lf3', not ours"
  }
} finally {
  if ($rows.Count -eq 0) { $fr = Run 'filter remove'; $m.filtersRemovedAtEnd = $true; W "filter remove (only ours existed): $($fr.out.Trim())" }
  else { $m.filtersRemovedAtEnd = $false; W "filters left in place (pre-existing filters present): $filterName still listed" }
  Save
  W "DONE exit=$($m.exitCode) reason=$($m.endReason) etl=$etl pcapng=$($m.pcapng) manifest=$manifestPath"
  Start-Sleep 5
}
exit $m.exitCode
