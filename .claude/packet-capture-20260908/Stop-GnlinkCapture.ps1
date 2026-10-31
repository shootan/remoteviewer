# Stop-GnlinkCapture.ps1 -- stop ONLY the capture whose log file is the given etl, then convert to pcapng.
# Elevated PowerShell. Usage: .\Stop-GnlinkCapture.ps1 -Etl <path\to\gnlink_host2_<ts>.etl>
# Normally the Start script stops itself (stop.request or timeout); use this if that window is gone.
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$Etl)
$ErrorActionPreference = 'Continue'
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch {}
$dir = Split-Path -Parent $Etl
$ts = Get-Date -Format yyyyMMdd_HHmmss
$log = Join-Path $dir "stop_$ts.log"
$manifestPath = Join-Path $dir ("stop_$ts.manifest.json")
$m = [ordered]@{ etl = $Etl; pcapng = $null; stoppedAt = $null; exitCode = $null; reason = $null; statusBefore = $null; stopOutput = $null; etl2pcapOutput = $null; filterListAfter = $null; notes = @() }
function W($s) { $line = "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff') $s"; Add-Content -Path $log -Value $line -Encoding utf8; Write-Host $line }
function Save() { ($m | ConvertTo-Json -Depth 6) | Out-File -FilePath $manifestPath -Encoding utf8 }
function Fail($code, $why) { $m.exitCode = $code; $m.reason = $why; W "REFUSED ($code): $why"; Save; Start-Sleep 8; exit $code }
function Run($argLine) { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8; $out = (& cmd.exe /c "chcp 65001>nul & pktmon $argLine 2>&1" | Out-String); return @{ out = $out; code = $LASTEXITCODE } }
function StatusLogFile($text) { foreach ($ln in ($text -split "`r?`n")) { if ($ln -match '\.etl\s*$') { return ($ln -replace '^.*?:\s*(?=[A-Za-z]:\\)', '').Trim() } }; return $null }
function SamePath($a, $b) { if (-not $a -or -not $b) { return $false }; return ([IO.Path]::GetFullPath($a).TrimEnd('\') -ieq [IO.Path]::GetFullPath($b).TrimEnd('\')) }

$isHigh = ((whoami /groups) -join "`n") -match 'S-1-16-12288'
W "elevated=$isHigh user=$(whoami) etl=$Etl"
if (-not $isHigh) { Fail 3 'not elevated' }
$st = Run 'status'; $m.statusBefore = $st.out; W "status (exit $($st.code)):`n$($st.out)"
if ($st.code -ne 0 -or $st.out -match '액세스|denied|통신하지 못|Failed to communicate') { Fail 2 'pktmon status unreadable' }
$lf = StatusLogFile $st.out
if (-not (SamePath $lf $Etl)) { Fail 5 "active capture log file is '$lf', not the given etl - NOT stopping" }
$so = Run 'stop'; $m.stopOutput = $so.out; $m.stoppedAt = (Get-Date).ToString('o'); W "stop (exit $($so.code)):`n$($so.out)"
$pcap = $Etl -replace '\.etl$', '.pcapng'
$cv = Run "etl2pcap `"$Etl`" -o `"$pcap`""; $m.etl2pcapOutput = $cv.out; W "etl2pcap (exit $($cv.code)):`n$($cv.out)"
if (Test-Path $pcap) { $m.pcapng = $pcap }
$fl = Run 'filter list'; $m.filterListAfter = $fl.out; W "filters now (not removed by this script; pktmon can only remove all):`n$($fl.out)"
$m.notes += 'filters intentionally untouched (pktmon filter remove would drop every filter); remove manually if only GNLinkUDP-<ts> is listed'
$m.exitCode = 0; $m.reason = 'stopped'; Save
W "DONE etl=$Etl pcapng=$($m.pcapng) manifest=$manifestPath"
Start-Sleep 5
exit 0
