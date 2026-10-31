param([string]$Run)
$r = $Run
"=== $r"
Get-Content "$r\slot.txt" | Select-String 'job_run|done' | ForEach-Object { $_.Line.Trim() }
(Get-Content "$r\fixture_result.txt") -join ' / '
$steps = Get-Content "$r\avd\steps.txt"
$seg = @{}
foreach ($l in $steps) { if ($l -match 'SEGMENT (\w+) (begin|end) epoch_ms=(\d+)') { $seg["$($matches[1]).$($matches[2])"] = [int64]$matches[3] } }
$steps | Select-String 'SEGMENT|watch begins|FIRST END|watched=' | ForEach-Object { $_.Line.Substring(0, [Math]::Min(220, $_.Line.Length)) }
$h = Get-Content "$r\a\directoryhost_host.log"
function ToEpoch([string]$line) {
  # host lines: "10-30 HH:mm:ss.fff" local time, year 2026
  if ($line -match '^(\d\d)-(\d\d) (\d\d:\d\d:\d\d\.\d{3})') {
    $dt = [datetime]::ParseExact("2026-$($matches[1])-$($matches[2]) $($matches[3])", 'yyyy-MM-dd HH:mm:ss.fff', $null)
    return [int64]([DateTimeOffset]$dt).ToUnixTimeMilliseconds()
  }
  return $null
}
foreach ($s in 'startup', 'watch', 'list') {
  if (-not $seg.ContainsKey("$s.begin") -or -not $seg.ContainsKey("$s.end")) { "$s : no segment"; continue }
  $b = $seg["$s.begin"]; $e = $seg["$s.end"]
  $k = @($h | Where-Object { $_ -match '\[keyframe\]' } | Where-Object { $t = ToEpoch $_; $t -ge $b -and $t -lt $e })
  $q = @($h | Where-Object { $_ -match 'keyframe-request seq=' } | Where-Object { $t = ToEpoch $_; $t -ge $b -and $t -lt $e })
  $bytes = ($k | ForEach-Object { if ($_ -match 'bytes=(\d+)') { [int64]$matches[1] } } | Measure-Object -Sum).Sum
  $len = ($e - $b) / 1000.0
  "{0}: len={1:N3}s keyframe={2} bytes={3} kreq={4} kreq/s={5:N3}" -f $s, $len, $k.Count, $bytes, $q.Count, ($q.Count / $len)
}
"peer-lost: " + (@($h | Select-String 'peer-lost' | ForEach-Object { $_.Line.Substring(0, 120) }) -join ' | ')
# relay snaps of the main connect
$snaps = Get-Content "$r\dir\server.stdout.txt" | Select-String 'relay-trace\] connect=(\S+) snap wall=(\S+) mono=(\d+).*rxBytes c2h=(\d+)/(\d+) h2c=(\d+)/(\d+)' | ForEach-Object {
  [pscustomobject]@{ c = $_.Matches[0].Groups[1].Value; wall = [DateTimeOffset]::Parse($_.Matches[0].Groups[2].Value).ToUnixTimeMilliseconds(); mono = [int64]$_.Matches[0].Groups[3].Value; c2h = [int64]$_.Matches[0].Groups[4].Value; h2c = [int64]$_.Matches[0].Groups[6].Value } }
if ($seg.ContainsKey('watch.begin') -and $snaps) {
  $main = ($snaps | Group-Object c | Sort-Object Count -Descending | Select-Object -First 1).Name
  $ms = @($snaps | Where-Object c -eq $main)
  $b = $seg['watch.begin']; $e = $seg['watch.end']
  $s0 = $ms | Sort-Object { [math]::Abs($_.wall - $b) } | Select-Object -First 1
  $s1 = $ms | Sort-Object { [math]::Abs($_.wall - $e) } | Select-Object -First 1
  $dt = ($s1.mono - $s0.mono) / 1000.0
  if ($dt -gt 0) {
    "relay {0} watch: snap0 errMs={1} snap1 errMs={2} dt={3:N3}s h2c {4:N0} B/s c2h {5:N0} B/s" -f $main, ($s0.wall - $b), ($s1.wall - $e), $dt, (($s1.h2c - $s0.h2c) / $dt), (($s1.c2h - $s0.c2h) / $dt)
  } else { "relay $main watch: snaps too close (dt=$dt)" }
}
# last video_debug inside the watch segment (app log lines are UTC "yyyy-MM-dd HH:mm:ss.fff")
if ($seg.ContainsKey('watch.begin')) {
  $b = $seg['watch.begin']; $e = $seg['watch.end']
  $vd = Get-Content "$r\avd\app_final.log" | Where-Object { $_ -match '^(\S+ \S+) \[video_debug\]' } | Where-Object {
    $t = [DateTimeOffset]::new([datetime]::ParseExact(($_.Substring(0, 23)), 'yyyy-MM-dd HH:mm:ss.fff', $null), [TimeSpan]::Zero).ToUnixTimeMilliseconds(); $t -ge $b -and $t -le $e }
  "video_debug in watch: n=$(@($vd).Count) last=" + (@($vd)[-1] -replace '^(.{23}).*?( in=\d+ out=\d+).*?( gateQueueDrops=\d+)?.*$', '$1$2$3')
}
"sample lines: " + @($steps | Select-String 'SAMPLE').Count + " miss-nonempty: " + @($steps | Select-String 'SAMPLE' | Where-Object { $_.Line -notmatch 'miss=\[\]' }).Count
