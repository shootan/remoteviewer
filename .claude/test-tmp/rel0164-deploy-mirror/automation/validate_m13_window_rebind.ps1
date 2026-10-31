param(
  [string]$Root = ".",
  [string]$ExeDir = "build-vcpkg-local/apps/native_poc/Debug",
  [string]$BaseConfig = "automation/native_video_profile_1080p_lowlat.json",
  [int]$HostClientSeconds = 18,
  [string]$TargetProcess = "notepad.exe",
  [string]$RemoteHost = "127.0.0.1"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-FromRoot {
  param(
    [string]$Base,
    [string]$Value
  )
  if ([System.IO.Path]::IsPathRooted($Value)) {
    return (Resolve-Path -LiteralPath $Value).Path
  }
  return (Resolve-Path -LiteralPath (Join-Path $Base $Value)).Path
}

function Wait-ForMainWindow {
  param(
    [int]$ProcessId,
    [int]$TimeoutMs = 5000
  )
  $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
  while ([DateTime]::UtcNow -lt $deadline) {
    try {
      $p = Get-Process -Id $ProcessId -ErrorAction Stop
      if ($p.MainWindowHandle -ne 0) {
        return [IntPtr]$p.MainWindowHandle
      }
    } catch {
      return [IntPtr]::Zero
    }
    Start-Sleep -Milliseconds 150
  }
  return [IntPtr]::Zero
}

$resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
$resolvedExeDir = Resolve-FromRoot -Base $resolvedRoot -Value $ExeDir
$resolvedBaseConfig = Resolve-FromRoot -Base $resolvedRoot -Value $BaseConfig
$runScript = Resolve-FromRoot -Base $resolvedRoot -Value "automation/run_native_video_with_config.ps1"

$ts = Get-Date -Format "yyyyMMdd-HHmmss"
$logDir = Join-Path $resolvedRoot ("automation/logs/m13-window-rebind-" + $ts)
New-Item -ItemType Directory -Path $logDir -Force | Out-Null

$cfgObj = Get-Content -LiteralPath $resolvedBaseConfig -Raw | ConvertFrom-Json
$cfgObj.remoteHost = $RemoteHost
$cfgObj.seconds = $HostClientSeconds
$cfgObj.captureWindowProcess = $TargetProcess
$cfgObj.captureWindowTitle = ""
$cfgObj.captureWindowClientOnly = $true
$cfgObj.captureWindowRebindIntervalMs = 500
$cfgPath = Join-Path $logDir "m13.profile.json"
$cfgObj | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $cfgPath -Encoding UTF8

$hostOut = Join-Path $logDir "host.out.log"
$hostErr = Join-Path $logDir "host.err.log"
$clientOut = Join-Path $logDir "client.out.log"
$clientErr = Join-Path $logDir "client.err.log"

$startedNotepads = New-Object System.Collections.Generic.List[int]

try {
  $np1 = Start-Process -FilePath "notepad.exe" -PassThru
  $startedNotepads.Add($np1.Id)
  [void](Wait-ForMainWindow -ProcessId $np1.Id -TimeoutMs 5000)

  $hostArgs = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", $runScript,
    "-Role", "host",
    "-ConfigPath", $cfgPath,
    "-ExeDir", $resolvedExeDir
  )
  $hostProc = Start-Process -FilePath "powershell" -ArgumentList $hostArgs `
    -WorkingDirectory $resolvedRoot `
    -RedirectStandardOutput $hostOut `
    -RedirectStandardError $hostErr `
    -PassThru

  Start-Sleep -Seconds 2

  $clientArgs = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", $runScript,
    "-Role", "client",
    "-ConfigPath", $cfgPath,
    "-ExeDir", $resolvedExeDir,
    "-RemoteHost", $RemoteHost
  )
  $clientProc = Start-Process -FilePath "powershell" -ArgumentList $clientArgs `
    -WorkingDirectory $resolvedRoot `
    -RedirectStandardOutput $clientOut `
    -RedirectStandardError $clientErr `
    -PassThru

  Start-Sleep -Seconds 4
  try { Stop-Process -Id $np1.Id -Force -ErrorAction Stop } catch {}
  Start-Sleep -Milliseconds 700
  $np2 = Start-Process -FilePath "notepad.exe" -PassThru
  $startedNotepads.Add($np2.Id)
  [void](Wait-ForMainWindow -ProcessId $np2.Id -TimeoutMs 5000)

  Wait-Process -Id $clientProc.Id -Timeout ([Math]::Max(20, $HostClientSeconds + 15))
  Wait-Process -Id $hostProc.Id -Timeout ([Math]::Max(20, $HostClientSeconds + 15))

  $hostRc = if ($hostProc.HasExited) { [int]$hostProc.ExitCode } else { -999 }
  $clientRc = if ($clientProc.HasExited) { [int]$clientProc.ExitCode } else { -999 }

  $hostText = if (Test-Path -LiteralPath $hostOut) { Get-Content -LiteralPath $hostOut -Raw } else { "" }
  $clientText = if (Test-Path -LiteralPath $clientOut) { Get-Content -LiteralPath $clientOut -Raw } else { "" }

  $targetFound = [regex]::Matches($hostText, "capture-window target hwnd=").Count
  $rebindEvents = [regex]::Matches($hostText, "capture-window rebound ").Count
  $clientHostCapEvents = [regex]::Matches($clientText, "hostCapProc=").Count
  $maxRebindFromStats = 0
  foreach ($m in [regex]::Matches($hostText, "captureWindowRebindCount=(\\d+)")) {
    $v = [int]$m.Groups[1].Value
    if ($v -gt $maxRebindFromStats) { $maxRebindFromStats = $v }
  }

  Write-Output ("LOG_DIR=" + $logDir)
  Write-Output ("CONFIG_PATH=" + $cfgPath)
  Write-Output ("HOST_LOG=" + $hostOut)
  Write-Output ("CLIENT_LOG=" + $clientOut)
  Write-Output ("HOST_RC=" + $hostRc)
  Write-Output ("CLIENT_RC=" + $clientRc)
  Write-Output ("TARGET_FOUND_COUNT=" + $targetFound)
  Write-Output ("REBIND_EVENT_COUNT=" + $rebindEvents)
  Write-Output ("REBIND_STATS_MAX=" + $maxRebindFromStats)
  Write-Output ("CLIENT_HOSTCAP_EVENT_COUNT=" + $clientHostCapEvents)
  $pass = ($hostRc -eq 0 -and $clientRc -eq 0 -and ($rebindEvents -ge 1 -or $maxRebindFromStats -ge 1))
  Write-Output ("M13_REBIND_PASS=" + $pass)
  exit ($(if ($pass) { 0 } else { 2 }))
}
finally {
  foreach ($npId in $startedNotepads) {
    try { Stop-Process -Id $npId -Force -ErrorAction SilentlyContinue } catch {}
  }
}
