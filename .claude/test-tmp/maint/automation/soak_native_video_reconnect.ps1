param(
  [Parameter(Mandatory = $true)]
  [string]$ConfigPath,
  [string]$RemoteHost = "127.0.0.1",
  [string]$ExeDir = "",
  [int]$Cycles = 20,
  [int]$ClientRunSec = 3,
  [int]$ClientCooldownMs = 500,
  [int]$HostStartDelaySec = 2,
  [string]$Tag = "m10"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ($Cycles -lt 1) { throw "Cycles must be >= 1" }
if ($ClientRunSec -lt 1) { throw "ClientRunSec must be >= 1" }
if ($ClientCooldownMs -lt 0) { throw "ClientCooldownMs must be >= 0" }

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$runner = Join-Path $PSScriptRoot "run_native_video_with_config.ps1"
if (-not (Test-Path -LiteralPath $runner)) { throw "runner script not found: $runner" }

$resolvedConfig = (Resolve-Path -LiteralPath $ConfigPath).Path
$cfg = Get-Content -LiteralPath $resolvedConfig -Raw | ConvertFrom-Json

$ts = Get-Date -Format "yyyyMMdd-HHmmss"
$logDir = Join-Path $PSScriptRoot ("logs/reconnect-soak-" + $ts + "-" + $Tag)
New-Item -ItemType Directory -Path $logDir -Force | Out-Null

if ([string]::IsNullOrWhiteSpace($ExeDir)) {
  $ExeDir = Join-Path $root "build-vcpkg-local/apps/native_poc/Debug"
}
$resolvedExeDir = (Resolve-Path -LiteralPath $ExeDir).Path

# Force host to stay alive, and each client run to exit quickly.
$hostCfg = $cfg.PSObject.Copy()
$hostCfg.seconds = 0
$hostCfgPath = Join-Path $logDir "host.config.json"
($hostCfg | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $hostCfgPath -Encoding UTF8

$clientCfg = $cfg.PSObject.Copy()
$clientCfg.seconds = $ClientRunSec
$clientCfgPath = Join-Path $logDir "client.config.json"
($clientCfg | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $clientCfgPath -Encoding UTF8

$hostOut = Join-Path $logDir "host.out.log"
$hostErr = Join-Path $logDir "host.err.log"

Write-Output ("LOG_DIR=" + $logDir)
Write-Output ("RUNNER=" + $runner)
Write-Output ("CONFIG_HOST=" + $hostCfgPath)
Write-Output ("CONFIG_CLIENT=" + $clientCfgPath)
Write-Output ("EXE_DIR=" + $resolvedExeDir)
Write-Output ("CYCLES=" + $Cycles)

$hostArgs = @(
  "-NoProfile",
  "-ExecutionPolicy", "Bypass",
  "-File", $runner,
  "-Role", "host",
  "-ConfigPath", $hostCfgPath,
  "-RemoteHost", $RemoteHost,
  "-ExeDir", $resolvedExeDir
)

$hostProc = Start-Process -FilePath "powershell" -ArgumentList $hostArgs `
  -WorkingDirectory $root `
  -RedirectStandardOutput $hostOut `
  -RedirectStandardError $hostErr `
  -PassThru

Start-Sleep -Seconds $HostStartDelaySec

$results = New-Object System.Collections.Generic.List[object]
$failed = $false

for ($i = 1; $i -le $Cycles; $i++) {
  if ($hostProc.HasExited) {
    $failed = $true
    $results.Add([pscustomobject]@{
      cycle = $i
      rc = -999
      status = "host_exited"
      note = "host exited before client cycle start"
    }) | Out-Null
    break
  }

  $clientOut = Join-Path $logDir ("client-" + $i + ".out.log")
  $clientErr = Join-Path $logDir ("client-" + $i + ".err.log")
  $clientArgs = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", $runner,
    "-Role", "client",
    "-ConfigPath", $clientCfgPath,
    "-RemoteHost", $RemoteHost,
    "-ExeDir", $resolvedExeDir
  )
  $clientProc = Start-Process -FilePath "powershell" -ArgumentList $clientArgs `
    -WorkingDirectory $root `
    -RedirectStandardOutput $clientOut `
    -RedirectStandardError $clientErr `
    -PassThru

  $timeoutMs = ($ClientRunSec + 15) * 1000
  $finished = $clientProc.WaitForExit($timeoutMs)
  if (-not $finished) {
    try { Stop-Process -Id $clientProc.Id -Force -ErrorAction SilentlyContinue } catch {}
    $failed = $true
    $results.Add([pscustomobject]@{
      cycle = $i
      rc = -998
      status = "timeout"
      note = "client did not exit in timeout window"
    }) | Out-Null
    break
  }

  $rc = [int]$clientProc.ExitCode
  $status = if ($rc -eq 0) { "ok" } else { "client_fail" }
  if ($rc -ne 0) { $failed = $true }
  $results.Add([pscustomobject]@{
    cycle = $i
    rc = $rc
    status = $status
    note = ""
  }) | Out-Null

  Write-Output ("CYCLE=" + $i + " RC=" + $rc + " STATUS=" + $status)
  if ($failed) { break }
  if ($ClientCooldownMs -gt 0) { Start-Sleep -Milliseconds $ClientCooldownMs }
}

if (-not $hostProc.HasExited) {
  try { Stop-Process -Id $hostProc.Id -Force -ErrorAction SilentlyContinue } catch {}
  Start-Sleep -Milliseconds 200
}
try { $hostProc.Refresh() } catch {}
$hostRc = if ($hostProc.HasExited) { [int]$hostProc.ExitCode } else { -997 }

$summaryPath = Join-Path $logDir "summary.txt"
$okCount = ($results | Where-Object { $_.status -eq "ok" }).Count
$lastCycle = if ($results.Count -gt 0) { $results[$results.Count - 1].cycle } else { 0 }
$lines = @()
$lines += ("SOAK_TAG=" + $Tag)
$lines += ("SOAK_CYCLES_REQUESTED=" + $Cycles)
$lines += ("SOAK_CYCLES_EXECUTED=" + $lastCycle)
$lines += ("SOAK_OK_COUNT=" + $okCount)
$lines += ("HOST_RC=" + $hostRc)
$lines += ("RESULT=" + ($(if ($failed) { "FAIL" } else { "PASS" })))
$lines += ""
$lines += "cycle,rc,status,note"
foreach ($r in $results) {
  $lines += ($r.cycle.ToString() + "," + $r.rc.ToString() + "," + $r.status + "," + $r.note)
}
$lines | Set-Content -LiteralPath $summaryPath -Encoding UTF8

Write-Output ("SUMMARY=" + $summaryPath)
Get-Content -LiteralPath $summaryPath

if ($failed) {
  exit 1
}
exit 0
