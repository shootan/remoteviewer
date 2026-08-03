# Aggregates baseline/candidate run JSONs produced by run_perf_baseline.ps1.
#
# One directory  -> per-scene median / min / max / spread for each metric (is the baseline
#                   stable enough to compare against?).
# Two directories -> side-by-side medians with delta percent (did the optimization help?).
param(
  [Parameter(Mandatory = $true)]
  [string]$BaselineDir,
  [string]$CandidateDir = "",
  [string[]]$Metrics = @(
    "DEC_AVG",
    "LAT_P95_US",
    "MBPS_AVG",
    "PRESENT_GAP_OVER_1S",
    "FRAME_GAP_P95_US",
    "FRAME_GAP_MAX_US",
    "FRAME_GAP_OVER_1_5X",
    "FRAME_GAP_OVER_2X",
    "HOST_CPU_SINGLE_CORE_PCT",
    "CLIENT_CPU_SINGLE_CORE_PCT",
    "HOST_PEAK_WS_MB",
    "CLIENT_PEAK_WS_MB",
    "USER_FEEDBACK_UF_H_CAPTURECOPYMAPUS_AVG_US",
    "USER_FEEDBACK_UF_H_CAPTUREMEMCPYUS_AVG_US",
    "USER_FEEDBACK_UF_H_SCALEUS_AVG_US",
    "USER_FEEDBACK_UF_H_NV12US_AVG_US",
    "USER_FEEDBACK_UF_H_ENCUS_AVG_US",
    "USER_FEEDBACK_UF_H_QUEUETOSENDUS_AVG_US"
  )
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Load-Runs {
  param([string]$Dir)
  $files = @(Get-ChildItem -Path $Dir -Filter "run-*.json" -ErrorAction Stop)
  if ($files.Count -eq 0) { throw "no run-*.json in $Dir" }
  $runs = @()
  foreach ($f in $files) {
    $runs += ,(Get-Content $f.FullName -Raw | ConvertFrom-Json)
  }
  return $runs
}

function Metric-Values {
  param($Runs, [string]$Tag, [string]$Metric)
  $vals = @()
  foreach ($r in $Runs) {
    if ($r.tag -ne $Tag) { continue }
    $v = $r.metrics.PSObject.Properties[$Metric]
    if ($null -ne $v -and "$($v.Value)" -match '^-?[0-9]+(\.[0-9]+)?$') {
      $vals += [double]$v.Value
    }
  }
  return ,$vals
}

function Median {
  param([double[]]$Vals)
  if ($Vals.Count -eq 0) { return $null }
  $s = @($Vals | Sort-Object)
  $mid = [int][Math]::Floor($s.Count / 2)
  if ($s.Count % 2 -eq 1) { return $s[$mid] }
  return ($s[$mid - 1] + $s[$mid]) / 2.0
}

$baseRuns = Load-Runs -Dir $BaselineDir
$candRuns = if (-not [string]::IsNullOrWhiteSpace($CandidateDir)) { Load-Runs -Dir $CandidateDir } else { $null }
$tags = @($baseRuns | ForEach-Object { $_.tag } | Sort-Object -Unique)

$rows = @()
foreach ($tag in $tags) {
  foreach ($metric in $Metrics) {
    $bv = Metric-Values -Runs $baseRuns -Tag $tag -Metric $metric
    if ($bv.Count -eq 0) { continue }
    $bMed = Median $bv
    $row = [ordered]@{
      tag = $tag
      metric = $metric
      n = $bv.Count
      median = [Math]::Round($bMed, 2)
      min = [Math]::Round(($bv | Measure-Object -Minimum).Minimum, 2)
      max = [Math]::Round(($bv | Measure-Object -Maximum).Maximum, 2)
    }
    if ($null -ne $candRuns) {
      $cv = Metric-Values -Runs $candRuns -Tag $tag -Metric $metric
      if ($cv.Count -gt 0) {
        $cMed = Median $cv
        $row["candMedian"] = [Math]::Round($cMed, 2)
        $row["candMin"] = [Math]::Round(($cv | Measure-Object -Minimum).Minimum, 2)
        $row["candMax"] = [Math]::Round(($cv | Measure-Object -Maximum).Maximum, 2)
        $row["deltaPct"] = if ($bMed -ne 0) { [Math]::Round(100.0 * ($cMed - $bMed) / [Math]::Abs($bMed), 1) } else { $null }
      }
    }
    $rows += [PSCustomObject]$row
  }
}

$rows | Format-Table -AutoSize | Out-String -Width 300 | Write-Output

$outPath = Join-Path $BaselineDir "comparison.json"
if ($null -ne $candRuns) {
  $outPath = Join-Path $CandidateDir "comparison-vs-baseline.json"
}
$rows | ConvertTo-Json -Depth 3 | Out-File -FilePath $outPath -Encoding utf8
Write-Output "COMPARISON_JSON=$outPath"
