param(
  [string]$Root = "",
  [ValidateSet("static", "scroll", "video")]
  [string[]]$Scenes = @("static", "scroll", "video"),
  [int]$ScenePrepareSeconds = 5,
  [switch]$NonInteractive,
  [string]$RemoteHost = "127.0.0.1",
  [int]$Port = 43000,
  [int]$ControlPort = 43001,
  [string]$Transport = "",
  [int]$UdpMtu = 1200,
  [int]$Fps = 30,
  [string]$Codec = "raw",
  [int]$Bitrate = 1100000,
  [int]$Keyint = 15,
  [int]$EncodeWidth = 0,
  [int]$EncodeHeight = 0,
  [string]$EncoderBackend = "",
  [string]$DecoderBackend = "",
  [int]$FpsHint = 30,
  [string]$BuildDir = "build-native2",
  [switch]$NoInputChannel,
  [int]$HostSeconds = 12,
  [int]$ClientSeconds = 8,
  [int]$TraceEvery = 0,
  [int]$TraceMax = 0
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Root)) {
  $Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

$verifyScript = Join-Path $PSScriptRoot "verify_native_video_runtime.ps1"
if (-not (Test-Path $verifyScript)) {
  throw "verify script not found: $verifyScript"
}

$sceneGuide = @{
  static = "Static scene: keep the screen mostly unchanged"
  scroll = "Scroll scene: continuously scroll document or web page"
  video  = "Video scene: play motion-heavy video content"
}

function Get-KeyValue {
  param(
    [string[]]$Lines,
    [string]$Key
  )
  foreach ($line in $Lines) {
    if ($line -match ("^" + [Regex]::Escape($Key) + "=(.*)$")) {
      return $Matches[1].Trim()
    }
  }
  return ""
}

$suiteTs = Get-Date -Format "yyyyMMdd-HHmmss"
$suiteDir = Join-Path $Root ("automation/logs/verify-native-video-scenes-" + $suiteTs)
New-Item -ItemType Directory -Path $suiteDir -Force | Out-Null

$results = New-Object System.Collections.Generic.List[object]

for ($i = 0; $i -lt $Scenes.Count; $i++) {
  $scene = $Scenes[$i]
  $sceneSlug = ("{0:d2}-{1}" -f ($i + 1), $scene)
  $sceneDir = Join-Path $suiteDir ("scene-" + $sceneSlug)
  $sceneVerifyOutPath = Join-Path $suiteDir ($sceneSlug + ".verify.out.log")

  Write-Output ""
  Write-Output ("[scene-suite] scene {0}/{1}: {2}" -f ($i + 1), $Scenes.Count, $scene)
  Write-Output ("[scene-suite] guide: {0}" -f $sceneGuide[$scene])

  if ($NonInteractive) {
    if ($ScenePrepareSeconds -gt 0) {
      Write-Output ("[scene-suite] non-interactive wait: {0}s" -f $ScenePrepareSeconds)
      Start-Sleep -Seconds $ScenePrepareSeconds
    }
  } else {
    [void](Read-Host ("[{0}] Prepare scene and press Enter" -f $scene))
  }

  $verifyArgs = @{
    Root = $Root
    RemoteHost = $RemoteHost
    Port = $Port
    ControlPort = $ControlPort
    Transport = $Transport
    UdpMtu = $UdpMtu
    Fps = $Fps
    Codec = $Codec
    Bitrate = $Bitrate
    Keyint = $Keyint
    EncodeWidth = $EncodeWidth
    EncodeHeight = $EncodeHeight
    EncoderBackend = $EncoderBackend
    DecoderBackend = $DecoderBackend
    FpsHint = $FpsHint
    BuildDir = $BuildDir
    HostSeconds = $HostSeconds
    ClientSeconds = $ClientSeconds
    TraceEvery = $TraceEvery
    TraceMax = $TraceMax
  }
  if ($NoInputChannel) { $verifyArgs["NoInputChannel"] = $true }

  $verifyOutput = @()
  try {
    $verifyOutput = @(& $verifyScript @verifyArgs 2>&1 | ForEach-Object { $_.ToString() })
  } catch {
    $verifyOutput += ("VERIFY_EXCEPTION={0}" -f $_.Exception.Message)
  }

  Set-Content -Path $sceneVerifyOutPath -Value $verifyOutput -Encoding UTF8

  $logDir = Get-KeyValue -Lines $verifyOutput -Key "LOG_DIR"
  $verifyOverallOk = Get-KeyValue -Lines $verifyOutput -Key "OVERALL_OK"
  $latP95 = Get-KeyValue -Lines $verifyOutput -Key "LAT_P95_US"
  $decAvg = Get-KeyValue -Lines $verifyOutput -Key "DEC_AVG"
  $mbpsAvg = Get-KeyValue -Lines $verifyOutput -Key "MBPS_AVG"
  $presentGapOver1s = Get-KeyValue -Lines $verifyOutput -Key "PRESENT_GAP_OVER_1S"

  $status = "fail"
  $resolvedSceneLogDir = $sceneDir
  if (-not [string]::IsNullOrWhiteSpace($logDir) -and (Test-Path $logDir)) {
    New-Item -ItemType Directory -Path $sceneDir -Force | Out-Null
    Set-Content -Path (Join-Path $sceneDir "source-log-dir.txt") -Value @("LOG_DIR=$logDir") -Encoding UTF8
    $resolvedSceneLogDir = $logDir
    $status = "ok"
  } else {
    New-Item -ItemType Directory -Path $sceneDir -Force | Out-Null
  }

  $summaryLines = @(
    ("SCENE={0}" -f $scene),
    ("STATUS={0}" -f $status),
    ("VERIFY_OVERALL_OK={0}" -f $verifyOverallOk),
    ("LAT_P95_US={0}" -f $latP95),
    ("DEC_AVG={0}" -f $decAvg),
    ("MBPS_AVG={0}" -f $mbpsAvg),
    ("PRESENT_GAP_OVER_1S={0}" -f $presentGapOver1s),
    ("SCENE_LOG_DIR={0}" -f $resolvedSceneLogDir),
    ("VERIFY_OUTPUT_LOG={0}" -f $sceneVerifyOutPath)
  )
  Set-Content -Path (Join-Path $sceneDir "scene-summary.txt") -Value $summaryLines -Encoding UTF8

  $result = [PSCustomObject]@{
    scene = $scene
    status = $status
    verifyOverallOk = $verifyOverallOk
    latP95Us = $latP95
    decAvg = $decAvg
    mbpsAvg = $mbpsAvg
    presentGapOver1s = $presentGapOver1s
    logDir = $resolvedSceneLogDir
    verifyOutput = $sceneVerifyOutPath
  }
  [void]$results.Add($result)

  Write-Output ("[scene-suite] done scene={0} status={1} lat_p95={2} dec_avg={3} log={4}" -f `
    $scene, $status, $latP95, $decAvg, $resolvedSceneLogDir)
}

$successCount = @($results | Where-Object { $_.status -eq "ok" }).Count
$verifyOkCount = @($results | Where-Object { $_.verifyOverallOk -ieq "True" }).Count
$overallOk = ($successCount -eq $Scenes.Count -and $verifyOkCount -eq $Scenes.Count)

$suiteSummary = New-Object System.Collections.Generic.List[string]
$suiteSummary.Add(("SCENE_SUITE_DIR={0}" -f $suiteDir))
$suiteSummary.Add(("SCENE_COUNT={0}" -f $Scenes.Count))
$suiteSummary.Add(("SCENE_SUCCESS_COUNT={0}" -f $successCount))
$suiteSummary.Add(("SCENE_VERIFY_OK_COUNT={0}" -f $verifyOkCount))
$suiteSummary.Add(("SUITE_OVERALL_OK={0}" -f $overallOk))
foreach ($result in $results) {
  $sceneUpper = $result.scene.ToUpperInvariant()
  $suiteSummary.Add(("SCENE_{0}_STATUS={1}" -f $sceneUpper, $result.status))
  $suiteSummary.Add(("SCENE_{0}_VERIFY_OK={1}" -f $sceneUpper, $result.verifyOverallOk))
  $suiteSummary.Add(("SCENE_{0}_LAT_P95_US={1}" -f $sceneUpper, $result.latP95Us))
  $suiteSummary.Add(("SCENE_{0}_DEC_AVG={1}" -f $sceneUpper, $result.decAvg))
  $suiteSummary.Add(("SCENE_{0}_MBPS_AVG={1}" -f $sceneUpper, $result.mbpsAvg))
  $suiteSummary.Add(("SCENE_{0}_PRESENT_GAP_OVER_1S={1}" -f $sceneUpper, $result.presentGapOver1s))
  $suiteSummary.Add(("SCENE_{0}_LOG_DIR={1}" -f $sceneUpper, $result.logDir))
  $suiteSummary.Add(("SCENE_{0}_VERIFY_OUTPUT={1}" -f $sceneUpper, $result.verifyOutput))
}

$suiteSummaryPath = Join-Path $suiteDir "scene-suite-summary.txt"
Set-Content -Path $suiteSummaryPath -Value $suiteSummary -Encoding UTF8

foreach ($line in $suiteSummary) {
  Write-Output $line
}
Write-Output ("SCENE_SUITE_SUMMARY={0}" -f $suiteSummaryPath)
