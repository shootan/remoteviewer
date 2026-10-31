# Collects the Release performance baseline the optimization work is judged against.
#
# Runs the verify script over the baseline matrix (1080p30 static/scroll/video plus 720p30
# scroll, 5 repeats each by default) on isolated ports so a live GNLink host is never touched,
# drives synthetic scenes so every run sees the same screen content, and writes one JSON per
# run that compare_optimization_runs.ps1 can aggregate.
param(
  [string]$Root = "",
  [string]$BuildDir = "build-local",
  [ValidateSet("Debug", "Release")]
  [string]$Configuration = "Release",
  [int]$Repeats = 5,
  [int]$Port = 44100,
  [int]$ControlPort = 44101,
  [int]$HostSeconds = 14,
  [int]$ClientSeconds = 10,
  [int]$Fps = 60,
  [string]$OutDir = "",
  [string[]]$Only = @(),   # e.g. "1080p-scroll" to run a single matrix entry
  [ValidateSet("", "low_latency", "stable_text")]
  [string]$TuneMode = "",   # empty inherits the host default
  # dxgi by default: on this machine WGC frame delivery degraded system-wide mid-session
  # (2-5 callbacks/s while DXGI does 20+ on the same animated scene) and only a reboot is
  # likely to restore it. DXGI is a supported product backend and the path H1 optimizes.
  [ValidateSet("dxgi", "wgc")]
  [string]$CaptureBackend = "dxgi"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Root)) {
  $Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}
$verifyScript = Join-Path $PSScriptRoot "verify_native_video_runtime.ps1"
$sceneScript = Join-Path $PSScriptRoot "perf_scene_generator.ps1"

if ([string]::IsNullOrWhiteSpace($OutDir)) {
  $ts = Get-Date -Format "yyyyMMdd-HHmmss"
  $OutDir = Join-Path $Root ("automation/logs/baseline-" + $ts)
}
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

if (-not [string]::IsNullOrWhiteSpace($TuneMode)) {
  $env:REMOTE60_NATIVE_ENCODER_TUNE_MODE = $TuneMode
} else {
  Remove-Item Env:REMOTE60_NATIVE_ENCODER_TUNE_MODE -ErrorAction SilentlyContinue
}
$env:REMOTE60_DESKTOP_CAPTURE_BACKEND = $CaptureBackend

# The primary comparison scene for optimization work is 1080p30 scroll.
$matrix = @(
  @{ tag = "1080p-static"; scene = "static"; width = 1920; height = 1080; bitrate = 8000000; keyint = $Fps },
  @{ tag = "1080p-scroll"; scene = "scroll"; width = 1920; height = 1080; bitrate = 8000000; keyint = $Fps },
  @{ tag = "1080p-video";  scene = "video";  width = 1920; height = 1080; bitrate = 8000000; keyint = $Fps },
  @{ tag = "720p-scroll";  scene = "scroll"; width = 1280; height = 720;  bitrate = 5000000; keyint = $Fps }
)
if ($Only.Count -gt 0) {
  $matrix = @($matrix | Where-Object { $Only -contains $_.tag })
  if ($matrix.Count -eq 0) { throw "no matrix entry matches: $($Only -join ',')" }
}

function Parse-KeyValues {
  param([string[]]$Lines)
  $map = [ordered]@{}
  foreach ($line in $Lines) {
    if ($line -match '^([A-Z][A-Z0-9_]*)=(.*)$') {
      $map[$Matches[1]] = $Matches[2].Trim()
    }
  }
  return $map
}

$runCount = 0
$failCount = 0
foreach ($entry in $matrix) {
  for ($i = 1; $i -le $Repeats; $i++) {
    $runCount += 1
    $label = "{0}-run{1:d2}" -f $entry.tag, $i
    Write-Output ("[baseline] {0} ({1}/{2})" -f $label, $runCount, ($matrix.Count * $Repeats))

    $sceneProc = $null
    if ($entry.scene -ne "static") {
      $sceneProc = Start-Process -FilePath "powershell.exe" -ArgumentList @(
        "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $sceneScript,
        "-Scene", $entry.scene, "-Seconds", ($HostSeconds + 20), "-Fps", $Fps
      ) -PassThru -WindowStyle Normal
      Start-Sleep -Seconds 2
    }

    $lines = @()
    try {
      $lines = @(& $verifyScript `
        -Root $Root `
        -BuildDir $BuildDir `
        -Configuration $Configuration `
        -Port $Port `
        -ControlPort $ControlPort `
        -Codec h264 `
        -Fps $Fps `
        -Bitrate $entry.bitrate `
        -Keyint $entry.keyint `
        -EncodeWidth $entry.width `
        -EncodeHeight $entry.height `
        -HostSeconds $HostSeconds `
        -ClientSeconds $ClientSeconds `
        -TraceEvery 30 `
        -TraceMax 400 `
        -NoInputChannel 2>&1 | ForEach-Object { $_.ToString() })
    } catch {
      $lines += ("VERIFY_EXCEPTION={0}" -f $_.Exception.Message)
    } finally {
      if ($sceneProc -and -not $sceneProc.HasExited) {
        try { Stop-Process -Id $sceneProc.Id -Force -ErrorAction SilentlyContinue } catch {}
      }
    }

    $rawPath = Join-Path $OutDir ($label + ".out.log")
    Set-Content -Path $rawPath -Value $lines -Encoding UTF8

    $metrics = Parse-KeyValues -Lines $lines
    $record = [ordered]@{
      tag = $entry.tag
      scene = $entry.scene
      iteration = $i
      configuration = $Configuration
      tuneMode = $TuneMode
      captureBackend = $CaptureBackend
      encodeWidth = $entry.width
      encodeHeight = $entry.height
      bitrate = $entry.bitrate
      keyint = $entry.keyint
      rawLog = $rawPath
      metrics = $metrics
    }
    $jsonPath = Join-Path $OutDir ("run-" + $label + ".json")
    $record | ConvertTo-Json -Depth 4 | Out-File -FilePath $jsonPath -Encoding utf8

    $ok = ($metrics.Contains("OVERALL_OK") -and $metrics["OVERALL_OK"] -ieq "True")
    if (-not $ok) { $failCount += 1 }
    $decAvg = if ($metrics.Contains("DEC_AVG")) { $metrics["DEC_AVG"] } else { "?" }
    $latP95 = if ($metrics.Contains("LAT_P95_US")) { $metrics["LAT_P95_US"] } else { "?" }
    $hostCpu = if ($metrics.Contains("HOST_CPU_SINGLE_CORE_PCT")) { $metrics["HOST_CPU_SINGLE_CORE_PCT"] } else { "?" }
    Write-Output ("[baseline]   ok={0} decAvg={1} latP95Us={2} hostCpu1core={3}%" -f $ok, $decAvg, $latP95, $hostCpu)
  }
}

Write-Output "BASELINE_DIR=$OutDir"
Write-Output "BASELINE_RUNS=$runCount"
Write-Output "BASELINE_FAILED_RUNS=$failCount"
