param(
  [string]$Root = "",
  [int]$ContinuousSeconds = 12,
  [int]$ReconnectSeconds = 6,
  [int]$ReconnectCount = 3,
  [int]$HostReadyTimeoutSec = 25
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Root)) {
  $Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

$ts = Get-Date -Format "yyyyMMdd-HHmmss"
$logDir = Join-Path $Root ("automation/logs/verify-" + $ts)
New-Item -ItemType Directory -Path $logDir -Force | Out-Null

$sigOut = Join-Path $logDir "sig.out.log"
$sigErr = Join-Path $logDir "sig.err.log"
$hostOut = Join-Path $logDir "host.out.log"
$hostErr = Join-Path $logDir "host.err.log"
$resultPath = Join-Path $logDir "result.txt"

function Stop-ChildProcess {
  param([object]$Proc)
  if ($null -eq $Proc) { return }
  try { $Proc.Refresh() } catch {}
  if (-not $Proc.HasExited) {
    Stop-Process -Id $Proc.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 200
  }
}

function Count-Pattern {
  param([string]$Path, [string]$Pattern)
  if (-not (Test-Path $Path)) { return 0 }
  return (Select-String -Path $Path -Pattern $Pattern | Measure-Object).Count
}

function Wait-ForPattern {
  param([string]$Path, [string]$Pattern, [int]$TimeoutSec)
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  while ((Get-Date) -lt $deadline) {
    if ((Count-Pattern -Path $Path -Pattern $Pattern) -gt 0) {
      return $true
    }
    Start-Sleep -Milliseconds 300
  }
  return $false
}

function Run-ClientCase {
  param(
    [string]$CaseName,
    [int]$Seconds,
    [string]$RootDir,
    [string]$OutDir
  )

  $clientExe = Join-Path $RootDir "build-vcpkg-local/apps/client/Debug/remote60_client.exe"
  $outFile = Join-Path $OutDir ($CaseName + ".client.out.log")
  $errFile = Join-Path $OutDir ($CaseName + ".client.err.log")
  $timeoutSec = [Math]::Max(30, $Seconds + 35)

  $clientProc = $null
  try {
    $clientProc = Start-Process -FilePath $clientExe -ArgumentList @("--runtime-view", "$Seconds") `
      -WorkingDirectory $RootDir -RedirectStandardOutput $outFile -RedirectStandardError $errFile -PassThru
    $deadline = (Get-Date).AddSeconds($timeoutSec)
    while ((Get-Date) -lt $deadline) {
      $clientProc.Refresh()
      if ($clientProc.HasExited) {
        return [int]$clientProc.ExitCode
      }
      Start-Sleep -Milliseconds 200
    }
    Stop-Process -Id $clientProc.Id -Force -ErrorAction SilentlyContinue
    return 997
  } catch {
    return 998
  } finally {
    Stop-ChildProcess -Proc $clientProc
  }
}

$sigProc = $null
$hostProc = $null
$continuousRc = 999
$reconnectResults = @()
$hostReady = $false
$hostAliveBeforeStop = $false

try {
  $sigProc = Start-Process -FilePath "node" -ArgumentList @("apps/signaling/server.js") `
    -WorkingDirectory $Root -RedirectStandardOutput $sigOut -RedirectStandardError $sigErr -PassThru
  Start-Sleep -Seconds 2
  $sigProc.Refresh()
  if ($sigProc.HasExited) {
    throw "signaling process exited early"
  }

  $hostProc = Start-Process -FilePath (Join-Path $Root "build-vcpkg-local/apps/host/Debug/remote60_host.exe") `
    -ArgumentList @("--runtime-server") -WorkingDirectory $Root `
    -RedirectStandardOutput $hostOut -RedirectStandardError $hostErr -PassThru

  $hostReady = Wait-ForPattern -Path $sigOut -Pattern "registered .*role: 'host'" -TimeoutSec $HostReadyTimeoutSec
  if (-not $hostReady) {
    $continuousRc = 996
    for ($i = 1; $i -le $ReconnectCount; $i++) { $reconnectResults += 996 }
  } else {
    $continuousRc = Run-ClientCase -CaseName "continuous12s" -Seconds $ContinuousSeconds -RootDir $Root -OutDir $logDir
    Start-Sleep -Seconds 1
    for ($i = 1; $i -le $ReconnectCount; $i++) {
      $rc = Run-ClientCase -CaseName ("reconnect" + $i) -Seconds $ReconnectSeconds -RootDir $Root -OutDir $logDir
      $reconnectResults += $rc
      Start-Sleep -Seconds 1
    }
  }

  $hostProc.Refresh()
  $hostAliveBeforeStop = -not $hostProc.HasExited
}
finally {
  Stop-ChildProcess -Proc $hostProc
  Stop-ChildProcess -Proc $sigProc

  while ($reconnectResults.Count -lt [Math]::Max($ReconnectCount, 3)) {
    $reconnectResults += 999
  }

  $overallOk = ($continuousRc -eq 0)
  for ($i = 0; $i -lt $ReconnectCount; $i++) {
    if ($reconnectResults[$i] -ne 0) { $overallOk = $false }
  }
  if (-not $hostAliveBeforeStop) { $overallOk = $false }

  $r1 = $reconnectResults[0]
  $r2 = $reconnectResults[1]
  $r3 = $reconnectResults[2]

  @(
    "continuous12s=$continuousRc",
    "reconnect1=$r1",
    "reconnect2=$r2",
    "reconnect3=$r3",
    "host_alive_before_stop=$hostAliveBeforeStop",
    "overall_ok=$overallOk"
  ) | Set-Content -Path $resultPath -Encoding ascii

  Write-Output "LOG_DIR=$logDir"
  Get-Content $resultPath
}
