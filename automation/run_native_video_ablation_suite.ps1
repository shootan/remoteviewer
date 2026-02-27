param(
  [string]$Root = "",
  [ValidateSet("M2", "M3")]
  [string]$Mode = "M2",
  [int]$Iterations = 20,
  [string]$RemoteHost = "127.0.0.1",
  [int]$Port = 43000,
  [int]$ControlPort = 0,
  [string]$Transport = "udp",
  [int]$Fps = 30,
  [string]$Codec = "h264",
  [int]$Bitrate = 2500000,
  [int]$Keyint = 30,
  [int]$EncodeWidth = 1280,
  [int]$EncodeHeight = 720,
  [int]$FpsHint = 30,
  [int]$UdpMtu = 1200,
  [int]$HostSeconds = 12,
  [int]$ClientSeconds = 8,
  [int]$TraceEvery = 15,
  [int]$TraceMax = 40
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Root)) {
  $Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

$verifyScript = Join-Path $Root "automation\verify_native_video_runtime.ps1"
if (-not (Test-Path $verifyScript)) {
  throw "verify script not found: $verifyScript"
}

$runTimestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$ablationRoot = Join-Path $Root ("automation\logs\native-video-ablation-" + $runTimestamp)
New-Item -ItemType Directory -Path $ablationRoot -Force | Out-Null

$variants = @()
if ($Mode -ieq "M2") {
  $variants += @{
    Name = "m2_gpu_scaler_on"
    Env = @{}
  }
  $variants += @{
    Name = "m2_gpu_scaler_off"
    Env = @{ "REMOTE60_NATIVE_DISABLE_GPU_SCALER" = "1" }
  }
} else {
  $variants += @{
    Name = "m3_quality_first_off"
    Env = @{ "REMOTE60_NATIVE_ADAPTIVE_QUALITY_FIRST" = "0" }
  }
  $variants += @{
    Name = "m3_quality_first_on"
    Env = @{ "REMOTE60_NATIVE_ADAPTIVE_QUALITY_FIRST" = "1" }
  }
}

function Parse-VerifyOutput {
  param([string[]]$Lines)
  $values = @{}
  foreach ($line in $Lines) {
    if ($null -eq $line) { continue }
    if ($line -match '^\s*([A-Za-z0-9_]+)=(.*)$') {
      $values[$Matches[1]] = $Matches[2].Trim()
    }
  }
  return $values
}

function ToDouble {
  param([hashtable]$Map, [string]$Key)
  if (-not $Map.ContainsKey($Key)) { return $null }
  $v = 0.0
  if ([double]::TryParse($Map[$Key], [ref]$v)) { return $v }
  return $null
}

function ToInt {
  param([hashtable]$Map, [string]$Key)
  if (-not $Map.ContainsKey($Key)) { return $null }
  $v = 0
  if ([int]::TryParse($Map[$Key], [ref]$v)) { return $v }
  return $null
}

function Stats {
  param([double[]]$Values)
  if ($Values.Count -eq 0) {
    return [ordered]@{ Count = 0; Avg = 0; P95 = 0; Max = 0 }
  }
  $sorted = $Values | Sort-Object
  $sum = 0.0
  foreach ($v in $sorted) { $sum += $v }
  $p95Index = [Math]::Floor(($sorted.Count - 1) * 0.95)
  if ($p95Index -lt 0) { $p95Index = 0 }
  if ($p95Index -ge $sorted.Count) { $p95Index = $sorted.Count - 1 }
  return [ordered]@{
    Count = $sorted.Count
    Avg = [Math]::Round($sum / $sorted.Count, 2)
    P95 = $sorted[$p95Index]
    Max = $sorted[-1]
  }
}

$runRows = New-Object System.Collections.Generic.List[object]

for ($v = 0; $v -lt $variants.Count; $v++) {
  $variant = $variants[$v]
  $variantName = $variant.Name
  Write-Output ("[ABLATION] variant={0}" -f $variantName)

  $variantDir = Join-Path $ablationRoot $variantName
  New-Item -ItemType Directory -Path $variantDir -Force | Out-Null

  for ($i = 1; $i -le $Iterations; $i++) {
    $runArgs = @{
      Root = $Root
      RemoteHost = $RemoteHost
      Port = $Port
      ControlPort = $ControlPort
      Transport = $Transport
      Fps = $Fps
      Codec = $Codec
      Bitrate = $Bitrate
      Keyint = $Keyint
      EncodeWidth = $EncodeWidth
      EncodeHeight = $EncodeHeight
      UdpMtu = $UdpMtu
      FpsHint = $FpsHint
      HostSeconds = $HostSeconds
      ClientSeconds = $ClientSeconds
      TraceEvery = $TraceEvery
      TraceMax = $TraceMax
      NoInputChannel = $true
    }

    $savedEnv = @{}
    $envKeys = @(
      "REMOTE60_NATIVE_DISABLE_GPU_SCALER",
      "REMOTE60_NATIVE_ADAPTIVE_QUALITY_FIRST"
    )
    foreach ($key in $envKeys) {
      $envValue = $null
      $envItem = Get-Item ("Env:" + $key) -ErrorAction SilentlyContinue
      if ($null -ne $envItem) { $envValue = $envItem.Value }
      $savedEnv[$key] = $envValue
    }
    try {
      foreach ($key in $envKeys) { Remove-Item ("Env:$key") -ErrorAction SilentlyContinue }
      foreach ($kv in $variant.Env.GetEnumerator()) {
        Set-Item -Path ("Env:" + $kv.Key) -Value $kv.Value
      }

      $runOutput = @()
      try {
        $runOutput = @(& $verifyScript @runArgs 2>&1 | ForEach-Object { $_.ToString() })
      } catch {
        $runOutput += ("VERIFY_EXCEPTION={0}" -f $_.Exception.Message)
      }

      $values = Parse-VerifyOutput -Lines $runOutput
      $logDir = if ($values.ContainsKey("LOG_DIR")) { $values["LOG_DIR"] } else { "" }
      $runOutputPath = Join-Path $variantDir ("run-{0:D2}.txt" -f $i)
      Set-Content -Path $runOutputPath -Value $runOutput -Encoding UTF8

      $runRows.Add([PSCustomObject]@{
        Mode = $Mode
        Variant = $variantName
        Run = $i
        OverallOk = if ($values.ContainsKey("OVERALL_OK")) { $values["OVERALL_OK"] } else { "False" }
        LogDir = $logDir
        VerifyOutput = $runOutputPath
        LatAvgUs = ToDouble $values "LAT_AVG_US"
        LatP95Us = ToDouble $values "LAT_P95_US"
        LatMaxUs = ToDouble $values "LAT_MAX_US"
        DecAvg = ToDouble $values "DEC_AVG"
        DecP95 = ToDouble $values "DEC_P95"
        DecodedFrames = ToDouble $values "DEC_COUNT"
        MbpsAvg = ToDouble $values "MBPS_AVG"
        EncRatioX100 = ToDouble $values "ENC_RATIO_X100_AVG"
        DecRatioX100 = ToDouble $values "DECODE_RATIO_X100_AVG"
        UploadYUs = ToDouble $values "UPLOADY_AVG"
        UploadUVUs = ToDouble $values "UPLOADUV_AVG"
        GpuScaleAttempts = ToDouble $values "GPU_SCALE_ATTEMPTS_AVG"
        GpuScaleSuccess = ToDouble $values "GPU_SCALE_SUCCESS_AVG"
        GpuScaleCpuFallback = ToDouble $values "GPU_SCALE_CPU_FALLBACK_AVG"
        AbrSwitchCount = ToInt $values "ABR_SWITCH_COUNT"
        AbrToHigh = ToInt $values "ABR_TO_HIGH_COUNT"
        AbrToMid = ToInt $values "ABR_TO_MID_COUNT"
        AbrToLow = ToInt $values "ABR_TO_LOW_COUNT"
        KeyReqClientSent = ToInt $values "KEYREQ_CLIENT_SENT"
        KeyReqHostRecv = ToInt $values "KEYREQ_HOST_RECV"
        KeyReqHostConsumed = ToInt $values "KEYREQ_HOST_CONSUMED"
        PresentGapOver1s = ToInt $values "PRESENT_GAP_OVER_1S"
        PresentGapOver3s = ToInt $values "PRESENT_GAP_OVER_3S"
        D3dPresentSuccess = ToInt $values "D3D_PRESENT_SUCCESS_TOTAL"
        GdiFallbackPresented = ToInt $values "GDI_FALLBACK_PRESENTED_TOTAL"
      })
    } finally {
      foreach ($key in $savedEnv.Keys) {
        if ($null -eq $savedEnv[$key]) {
          Remove-Item ("Env:$key") -ErrorAction SilentlyContinue
        } else {
          Set-Item -Path ("Env:" + $key) -Value $savedEnv[$key]
        }
      }
    }

    Start-Sleep -Milliseconds 250
  }
}

$runSummary = New-Object System.Collections.Generic.List[object]
$grouped = $runRows | Group-Object -Property Variant
foreach ($g in $grouped) {
  $rows = @($g.Group)
  $ok = @($rows | Where-Object { $_.OverallOk -ieq "True" }).Count
  $latAvg = Stats -Values @($rows | ForEach-Object { $_.LatAvgUs } | Where-Object { $_ -ne $null })
  $latP95 = Stats -Values @($rows | ForEach-Object { $_.LatP95Us } | Where-Object { $_ -ne $null })
  $latMax = Stats -Values @($rows | ForEach-Object { $_.LatMaxUs } | Where-Object { $_ -ne $null })
  $decAvg = Stats -Values @($rows | ForEach-Object { $_.DecAvg } | Where-Object { $_ -ne $null })
  $decP95 = Stats -Values @($rows | ForEach-Object { $_.DecP95 } | Where-Object { $_ -ne $null })
  $encRatio = Stats -Values @($rows | ForEach-Object { $_.EncRatioX100 } | Where-Object { $_ -ne $null })
  $uploadY = Stats -Values @($rows | ForEach-Object { $_.UploadYUs } | Where-Object { $_ -ne $null })
  $uploadUV = Stats -Values @($rows | ForEach-Object { $_.UploadUVUs } | Where-Object { $_ -ne $null })
  $gpuScaleAttempts = Stats -Values @($rows | ForEach-Object { $_.GpuScaleAttempts } | Where-Object { $_ -ne $null })
  $gpuScaleSuccess = Stats -Values @($rows | ForEach-Object { $_.GpuScaleSuccess } | Where-Object { $_ -ne $null })
  $abrSwitch = Stats -Values @($rows | ForEach-Object { $_.AbrSwitchCount } | Where-Object { $_ -ne $null })

  $runSummary.Add([PSCustomObject]@{
    Mode = $Mode
    Variant = $g.Name
    Iterations = $rows.Count
    OverallOk = $ok
    OverallOkRate = if ($rows.Count -gt 0) { [Math]::Round(($ok * 100.0 / $rows.Count), 2) } else { 0 }
    LatAvgUs_Avg = $latAvg.Avg
    LatAvgUs_P95 = $latAvg.P95
    LatAvgUs_Max = $latAvg.Max
    LatP95Us_Avg = $latP95.Avg
    LatP95Us_P95 = $latP95.P95
    LatP95Us_Max = $latP95.Max
    LatMaxUs_Avg = $latMax.Avg
    LatMaxUs_P95 = $latMax.P95
    LatMaxUs_Max = $latMax.Max
    DecAvg_Avg = $decAvg.Avg
    DecAvg_P95 = $decAvg.P95
    DecAvg_Max = $decAvg.Max
    DecP95_Avg = $decP95.Avg
    DecP95_P95 = $decP95.P95
    DecP95_Max = $decP95.Max
    EncRatioX100_Avg = $encRatio.Avg
    EncRatioX100_P95 = $encRatio.P95
    EncRatioX100_Max = $encRatio.Max
    UploadYUs_Avg = $uploadY.Avg
    UploadUVUs_Avg = $uploadUV.Avg
    GpuScaleAttempts_Avg = $gpuScaleAttempts.Avg
    GpuScaleSuccess_Avg = $gpuScaleSuccess.Avg
    AbrSwitchCount_Avg = $abrSwitch.Avg
  })
}

$runLogPath = Join-Path $ablationRoot "runs.csv"
$summaryPath = Join-Path $ablationRoot "summary.csv"
$runRows | Export-Csv -Path $runLogPath -NoTypeInformation -Encoding UTF8
$runSummary | Export-Csv -Path $summaryPath -NoTypeInformation -Encoding UTF8

Write-Output ("ABLATION_ROOT={0}" -f $ablationRoot)
Write-Output ("RUNS_CSV={0}" -f $runLogPath)
Write-Output ("SUMMARY_CSV={0}" -f $summaryPath)
foreach ($row in $runSummary) {
  Write-Output ("SUMMARY variant={0} ok={1}/{2} ({3}%) lat_p95_avg={4} lat_p95_p95={5} lat_p95_max={6}" -f $row.Variant, $row.OverallOk, $row.Iterations, $row.OverallOkRate, $row.LatP95Us_Avg, $row.LatP95Us_P95, $row.LatP95Us_Max)
}
