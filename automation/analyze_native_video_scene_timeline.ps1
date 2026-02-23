param(
  [string]$LogDir = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Normalize-LogDir {
  param(
    [string]$RawPath,
    [string]$RootPath
  )
  if ([string]::IsNullOrWhiteSpace($RawPath)) { return "" }
  $p = $RawPath -replace '[\x00-\x1F]', ''
  $p = $p.Trim().Trim('"').Trim("'")
  # Remove invisible/control-ish Unicode chars copied from terminal output.
  $p = [regex]::Replace($p, '[^\x20-\x7E]', '')
  $p = $p -replace '/', '\'
  if ([string]::IsNullOrWhiteSpace($p)) { return "" }
  if (-not [System.IO.Path]::IsPathRooted($p)) {
    $p = Join-Path $RootPath $p
  }
  return $p
}

function Parse-Fields {
  param([string]$Line)
  $m = [regex]::Matches($Line, '([A-Za-z][A-Za-z0-9_]*)=([0-9]+(?:\.[0-9]+)?)')
  $h = @{}
  foreach ($x in $m) {
    $h[$x.Groups[1].Value] = $x.Groups[2].Value
  }
  return $h
}

function Stats-Summary {
  param([System.Collections.Generic.List[double]]$vals)
  if ($vals.Count -eq 0) {
    return [ordered]@{ count = 0; avg = 0; p95 = 0; max = 0 }
  }
  $arr = @($vals.ToArray() | Sort-Object)
  $sum = 0.0
  foreach ($v in $arr) { $sum += $v }
  $avg = $sum / $arr.Count
  $p95Index = [Math]::Min($arr.Count - 1, [Math]::Floor(($arr.Count - 1) * 0.95))
  $p95 = $arr[$p95Index]
  $max = $arr[$arr.Count - 1]
  return [ordered]@{
    count = $arr.Count
    avg = [Math]::Round($avg, 2)
    p95 = $p95
    max = $max
  }
}

function Resolve-LatestLogDir {
  param([string]$RootPath)
  $logsRoot = Join-Path $RootPath "automation/logs"
  if (-not (Test-Path -LiteralPath $logsRoot)) { throw "logs dir not found: $logsRoot" }
  $latest = Get-ChildItem -LiteralPath $logsRoot -Directory |
    Where-Object { $_.Name -like "verify-native-video-*" -or $_.Name -like "native-video-live-*" } |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
  if (-not $latest) { throw "no log directory found under $logsRoot" }
  return $latest.FullName
}

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$LogDir = Normalize-LogDir -RawPath $LogDir -RootPath $root
if ([string]::IsNullOrWhiteSpace($LogDir)) {
  $LogDir = Resolve-LatestLogDir -RootPath $root
}
try {
  if (-not (Test-Path -LiteralPath $LogDir)) {
    $LogDir = Resolve-LatestLogDir -RootPath $root
  }
} catch {
  $LogDir = Resolve-LatestLogDir -RootPath $root
}

$hostLog = Join-Path $LogDir "host.out.log"
$clientLog = Join-Path $LogDir "client.out.log"
if (-not (Test-Path -LiteralPath $hostLog)) { throw "host log not found: $hostLog" }
if (-not (Test-Path -LiteralPath $clientLog)) { throw "client log not found: $clientLog" }

$hostBySeq = @{}
foreach ($line in (Get-Content $hostLog)) {
  if ($line -notmatch '\[native-video-host\]\[trace\]') { continue }
  $f = Parse-Fields -Line $line
  if (-not $f.ContainsKey("seq")) { continue }
  $seq = [int]$f["seq"]
  $hostBySeq[$seq] = $f
}

$clientBySeq = @{}
foreach ($line in (Get-Content $clientLog)) {
  if ($line -notmatch '\[native-video-client\]\[trace_present\]') { continue }
  $f = Parse-Fields -Line $line
  if (-not $f.ContainsKey("seq")) { continue }
  $seq = [int]$f["seq"]
  $clientBySeq[$seq] = $f
}

$rows = New-Object System.Collections.Generic.List[object]
$e2eVals = New-Object System.Collections.Generic.List[double]
$over1s = 0
$over3s = 0

foreach ($seq in ($clientBySeq.Keys | Sort-Object)) {
  if (-not $hostBySeq.ContainsKey($seq)) { continue }
  $h = $hostBySeq[$seq]
  $c = $clientBySeq[$seq]

  $captureUs = if ($c.ContainsKey("captureUs")) { [int64]$c["captureUs"] } else { 0 }
  $sendUs = if ($h.ContainsKey("sendUs")) { [int64]$h["sendUs"] } elseif ($c.ContainsKey("sendUs")) { [int64]$c["sendUs"] } else { 0 }
  $presentUs = if ($c.ContainsKey("presentUs")) { [int64]$c["presentUs"] } else { 0 }
  $totalUs = if ($c.ContainsKey("totalUs")) { [int64]$c["totalUs"] } elseif ($presentUs -ge $captureUs) { $presentUs - $captureUs } else { 0 }

  [void]$e2eVals.Add([double]$totalUs)
  if ($totalUs -ge 1000000) { $over1s += 1 }
  if ($totalUs -ge 3000000) { $over3s += 1 }

  $rows.Add([pscustomobject]@{
      seq = $seq
      captureUs = $captureUs
      sendUs = $sendUs
      presentUs = $presentUs
      totalUs = $totalUs
      c2eUs = if ($c.ContainsKey("c2eUs")) { [int64]$c["c2eUs"] } else { 0 }
      encUs = if ($c.ContainsKey("encUs")) { [int64]$c["encUs"] } else { 0 }
      e2sUs = if ($c.ContainsKey("e2sUs")) { [int64]$c["e2sUs"] } else { 0 }
      netUs = if ($c.ContainsKey("netUs")) { [int64]$c["netUs"] } else { 0 }
      r2dUs = if ($c.ContainsKey("r2dUs")) { [int64]$c["r2dUs"] } else { 0 }
      decUs = if ($c.ContainsKey("decUs")) { [int64]$c["decUs"] } else { 0 }
      d2pUs = if ($c.ContainsKey("d2pUs")) { [int64]$c["d2pUs"] } else { 0 }
      renderUs = if ($c.ContainsKey("renderUs")) { [int64]$c["renderUs"] } else { 0 }
    })
}

$stats = Stats-Summary -vals $e2eVals
$csvPath = Join-Path $LogDir "scene_timeline.csv"
$rows | Sort-Object seq | Export-Csv -Path $csvPath -NoTypeInformation -Encoding ascii

Write-Output "LOG_DIR=$LogDir"
Write-Output "HOST_TRACE_COUNT=$($hostBySeq.Count)"
Write-Output "CLIENT_PRESENT_TRACE_COUNT=$($clientBySeq.Count)"
Write-Output "SCENE_JOINED_COUNT=$($rows.Count)"
Write-Output "E2E_AVG_US=$($stats.avg)"
Write-Output "E2E_P95_US=$($stats.p95)"
Write-Output "E2E_MAX_US=$($stats.max)"
Write-Output "E2E_OVER_1S=$over1s"
Write-Output "E2E_OVER_3S=$over3s"
Write-Output "CSV=$csvPath"

$top = $rows | Sort-Object totalUs -Descending | Select-Object -First 12
foreach ($r in $top) {
  Write-Output ("TOP seq={0} totalUs={1} c2e={2} enc={3} e2s={4} net={5} r2d={6} dec={7} d2p={8}" -f `
      $r.seq, $r.totalUs, $r.c2eUs, $r.encUs, $r.e2sUs, $r.netUs, $r.r2dUs, $r.decUs, $r.d2pUs)
}
