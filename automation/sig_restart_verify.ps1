param(
  [string]$Root = "",
  [int]$ClientSeconds = 8,
  [int]$InitialConnectTimeoutSec = 15,
  [int]$ReconnectTimeoutSec = 20
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($Root)) {
  $Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

$ts = Get-Date -Format "yyyyMMdd-HHmmss"
$logDir = Join-Path $Root ("automation/logs/sig-restart-safe-" + $ts)
New-Item -ItemType Directory -Path $logDir -Force | Out-Null

$sig1Out = Join-Path $logDir "sig1.out.log"
$sig1Err = Join-Path $logDir "sig1.err.log"
$sig2Out = Join-Path $logDir "sig2.out.log"
$sig2Err = Join-Path $logDir "sig2.err.log"
$hostOut = Join-Path $logDir "host.out.log"
$hostErr = Join-Path $logDir "host.err.log"
$clientOut = Join-Path $logDir "client.out.log"
$clientErr = Join-Path $logDir "client.err.log"
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
  param(
    [string]$Path,
    [string]$Pattern
  )
  if (-not (Test-Path $Path)) { return 0 }
  return (Select-String -Path $Path -Pattern $Pattern | Measure-Object).Count
}

function Wait-PatternCount {
  param(
    [string]$Path,
    [string]$Pattern,
    [int]$Target,
    [int]$TimeoutSec
  )

  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  while ((Get-Date) -lt $deadline) {
    $count = Count-Pattern -Path $Path -Pattern $Pattern
    if ($count -ge $Target) {
      return $true
    }
    Start-Sleep -Milliseconds 300
  }
  return $false
}

$sigProc1 = $null
$sigProc2 = $null
$hostProc = $null
$clientRc = 999
$hostAliveAtStart = $false
$hostAliveAfterClient = $false
$firstConnected = $false
$reconnected = $false

try {
  $sigProc1 = Start-Process -FilePath "node" -ArgumentList @("apps/signaling/server.js") -WorkingDirectory $Root -RedirectStandardOutput $sig1Out -RedirectStandardError $sig1Err -PassThru
  Start-Sleep -Seconds 2

  $hostProc = Start-Process -FilePath (Join-Path $Root "build-vcpkg-local/apps/host/Debug/remote60_host.exe") -ArgumentList @("--runtime-server") -WorkingDirectory $Root -RedirectStandardOutput $hostOut -RedirectStandardError $hostErr -PassThru
  Start-Sleep -Seconds 1

  $hostProc.Refresh()
  $hostAliveAtStart = -not $hostProc.HasExited

  $firstConnected = Wait-PatternCount -Path $sig1Out -Pattern "registered .*role: 'host'" -Target 1 -TimeoutSec $InitialConnectTimeoutSec

  Stop-ChildProcess -Proc $sigProc1
  $sigProc1 = $null
  Start-Sleep -Seconds 1

  $sigProc2 = Start-Process -FilePath "node" -ArgumentList @("apps/signaling/server.js") -WorkingDirectory $Root -RedirectStandardOutput $sig2Out -RedirectStandardError $sig2Err -PassThru
  Start-Sleep -Seconds 1

  $reconnected = Wait-PatternCount -Path $sig2Out -Pattern "registered .*role: 'host'" -Target 1 -TimeoutSec $ReconnectTimeoutSec

  & (Join-Path $Root "build-vcpkg-local/apps/client/Debug/remote60_client.exe") --runtime-view $ClientSeconds 1> $clientOut 2> $clientErr
  $clientRc = $LASTEXITCODE

  $hostProc.Refresh()
  $hostAliveAfterClient = -not $hostProc.HasExited
}
finally {
  Stop-ChildProcess -Proc $hostProc
  Stop-ChildProcess -Proc $sigProc1
  Stop-ChildProcess -Proc $sigProc2

  $connectSuccessCount = Count-Pattern -Path $hostOut -Pattern "phase=signaling connect_end success=1"
  $sigRegisteredCount = (Count-Pattern -Path $sig1Out -Pattern "registered") + (Count-Pattern -Path $sig2Out -Pattern "registered")
  $overallOk = ($clientRc -eq 0 -and $hostAliveAtStart -and $hostAliveAfterClient -and $firstConnected -and $reconnected)

  @(
    "client_rc=$clientRc",
    "host_alive_at_start=$hostAliveAtStart",
    "host_alive_after_client=$hostAliveAfterClient",
    "first_connected=$firstConnected",
    "reconnected_after_sig_restart=$reconnected",
    "host_connect_success_count=$connectSuccessCount",
    "signaling_registered_count=$sigRegisteredCount",
    "overall_ok=$overallOk"
  ) | Set-Content -Path $resultPath -Encoding ascii

  Write-Output "LOG_DIR=$logDir"
  Get-Content $resultPath
}
