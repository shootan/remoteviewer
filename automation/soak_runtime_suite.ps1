param(
  [int]$Iterations = 10,
  [int]$PauseSeconds = 3
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ts = Get-Date -Format "yyyyMMdd-HHmmss"
$outDir = Join-Path $root ("automation/logs/soak-" + $ts)
New-Item -ItemType Directory -Path $outDir -Force | Out-Null
$summaryPath = Join-Path $outDir "summary.txt"

$pass = 0
$fail = 0
$rows = @()

for ($i = 1; $i -le $Iterations; $i++) {
  Write-Output "[soak] iteration=$i/$Iterations begin"
  $runOutput = & powershell -ExecutionPolicy Bypass -File (Join-Path $root "automation/verify_runtime_suite.ps1")
  $logDirLine = $runOutput | Where-Object { $_ -like "LOG_DIR=*" } | Select-Object -First 1
  $logDir = if ($logDirLine) { $logDirLine.Substring("LOG_DIR=".Length) } else { "" }
  $resultPath = if ($logDir) { Join-Path $logDir "result.txt" } else { "" }

  $overall = "False"
  $continuous = "999"
  $r1 = "999"
  $r2 = "999"
  $r3 = "999"
  $alive = "False"
  if ($resultPath -and (Test-Path $resultPath)) {
    $lines = Get-Content $resultPath
    foreach ($line in $lines) {
      if ($line.StartsWith("overall_ok=")) { $overall = $line.Substring("overall_ok=".Length) }
      if ($line.StartsWith("continuous12s=")) { $continuous = $line.Substring("continuous12s=".Length) }
      if ($line.StartsWith("reconnect1=")) { $r1 = $line.Substring("reconnect1=".Length) }
      if ($line.StartsWith("reconnect2=")) { $r2 = $line.Substring("reconnect2=".Length) }
      if ($line.StartsWith("reconnect3=")) { $r3 = $line.Substring("reconnect3=".Length) }
      if ($line.StartsWith("host_alive_before_stop=")) { $alive = $line.Substring("host_alive_before_stop=".Length) }
    }
  }

  if ($overall -eq "True") { $pass++ } else { $fail++ }
  $row = "iter=$i overall_ok=$overall continuous12s=$continuous reconnect1=$r1 reconnect2=$r2 reconnect3=$r3 host_alive_before_stop=$alive log_dir=$logDir"
  $rows += $row
  Write-Output "[soak] $row"

  if ($i -lt $Iterations) { Start-Sleep -Seconds $PauseSeconds }
}

@(
  "iterations=$Iterations",
  "pass=$pass",
  "fail=$fail"
) + $rows | Set-Content -Path $summaryPath -Encoding ascii

Write-Output "SUMMARY_PATH=$summaryPath"
Get-Content $summaryPath
