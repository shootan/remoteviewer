param(
  [string]$Root = ".",
  [string]$ExeDir = "build-vcpkg-local/apps/native_poc/Debug",
  [string]$BaseConfig = "automation/native_video_profile_1080p_lowlat.json",
  [int]$HostClientSeconds = 18,
  [string]$TargetProcess = "notepad.exe",
  [string]$RemoteHost = "127.0.0.1",
  [int]$TargetWindowX = 640,
  [int]$TargetWindowY = 520,
  [int]$TargetWindowW = 700,
  [int]$TargetWindowH = 460,
  [int]$ClientWindowX = 980,
  [int]$ClientWindowY = 80,
  [int]$ClientWindowW = 900,
  [int]$ClientWindowH = 640,
  [int]$FocusClickX = 420,
  [int]$FocusClickY = 430,
  [string]$ClientProcessName = "remote60_native_video_client_poc"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public static class Win32M13 {
  [StructLayout(LayoutKind.Sequential)]
  public struct RECT {
    public int Left;
    public int Top;
    public int Right;
    public int Bottom;
  }

  [DllImport("user32.dll")]
  public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);

  [DllImport("user32.dll")]
  public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

  [DllImport("user32.dll")]
  public static extern bool SetForegroundWindow(IntPtr hWnd);

  [DllImport("user32.dll")]
  public static extern IntPtr PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
}
"@

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
    [int]$TimeoutMs = 6000
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
    Start-Sleep -Milliseconds 120
  }
  return [IntPtr]::Zero
}

function Wait-ForMainWindowByProcessName {
  param(
    [string]$ProcessName,
    [DateTime]$StartedAfterUtc,
    [int]$TimeoutMs = 12000
  )
  $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMs)
  while ([DateTime]::UtcNow -lt $deadline) {
    $candidates = @(Get-Process -Name $ProcessName -ErrorAction SilentlyContinue)
    foreach ($p in ($candidates | Sort-Object StartTime -Descending)) {
      try {
        $startUtc = $p.StartTime.ToUniversalTime()
      } catch {
        continue
      }
      if ($startUtc -lt $StartedAfterUtc.AddSeconds(-1)) { continue }
      if ($p.MainWindowHandle -ne 0) {
        return [IntPtr]$p.MainWindowHandle
      }
    }
    Start-Sleep -Milliseconds 120
  }
  return [IntPtr]::Zero
}

function Move-Window {
  param(
    [IntPtr]$Hwnd,
    [int]$X,
    [int]$Y,
    [int]$W,
    [int]$H
  )
  if ($Hwnd -eq [IntPtr]::Zero) { return $false }
  $SW_RESTORE = 9
  $SWP_NOZORDER = 0x0004
  $SWP_NOACTIVATE = 0x0010
  [void][Win32M13]::ShowWindow($Hwnd, $SW_RESTORE)
  return [Win32M13]::SetWindowPos($Hwnd, [IntPtr]::Zero, $X, $Y, $W, $H, $SWP_NOZORDER -bor $SWP_NOACTIVATE)
}

function Send-F9 {
  param([IntPtr]$Hwnd)
  if ($Hwnd -eq [IntPtr]::Zero) { return }
  $WM_KEYDOWN = 0x0100
  $WM_KEYUP = 0x0101
  $VK_F9 = 0x78
  [void][Win32M13]::SetForegroundWindow($Hwnd)
  [void][Win32M13]::PostMessage($Hwnd, $WM_KEYDOWN, [IntPtr]$VK_F9, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 50
  [void][Win32M13]::PostMessage($Hwnd, $WM_KEYUP, [IntPtr]$VK_F9, [IntPtr]::Zero)
}

function Send-LeftClick {
  param(
    [IntPtr]$Hwnd,
    [int]$X,
    [int]$Y
  )
  if ($Hwnd -eq [IntPtr]::Zero) { return }
  $WM_LBUTTONDOWN = 0x0201
  $WM_LBUTTONUP = 0x0202
  $MK_LBUTTON = 0x0001
  $lParam = (($Y -band 0xFFFF) -shl 16) -bor ($X -band 0xFFFF)
  [void][Win32M13]::PostMessage($Hwnd, $WM_LBUTTONDOWN, [IntPtr]$MK_LBUTTON, [IntPtr]$lParam)
  Start-Sleep -Milliseconds 35
  [void][Win32M13]::PostMessage($Hwnd, $WM_LBUTTONUP, [IntPtr]::Zero, [IntPtr]$lParam)
}

$resolvedRoot = (Resolve-Path -LiteralPath $Root).Path
$resolvedExeDir = Resolve-FromRoot -Base $resolvedRoot -Value $ExeDir
$resolvedBaseConfig = Resolve-FromRoot -Base $resolvedRoot -Value $BaseConfig
$runScript = Resolve-FromRoot -Base $resolvedRoot -Value "automation/run_native_video_with_config.ps1"

$ts = Get-Date -Format "yyyyMMdd-HHmmss"
$logDir = Join-Path $resolvedRoot ("automation/logs/m13-mode-switch-" + $ts)
New-Item -ItemType Directory -Path $logDir -Force | Out-Null

$cfgObj = Get-Content -LiteralPath $resolvedBaseConfig -Raw | ConvertFrom-Json
$cfgObj.remoteHost = $RemoteHost
$cfgObj.seconds = $HostClientSeconds
$cfgObj.captureWindowProcess = $TargetProcess
$cfgObj.captureWindowTitle = ""
$cfgObj.captureWindowClientOnly = $true
$cfgObj.captureWindowRebindIntervalMs = 500
$cfgPath = Join-Path $logDir "m13.mode.profile.json"
$cfgObj | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $cfgPath -Encoding UTF8

$hostOut = Join-Path $logDir "host.out.log"
$hostErr = Join-Path $logDir "host.err.log"
$clientOut = Join-Path $logDir "client.out.log"
$clientErr = Join-Path $logDir "client.err.log"

$startedTargets = New-Object System.Collections.Generic.List[int]
$hostProc = $null
$clientProc = $null

try {
  $targetProc = Start-Process -FilePath $TargetProcess -PassThru
  $startedTargets.Add($targetProc.Id)
  $targetHwnd = Wait-ForMainWindow -ProcessId $targetProc.Id -TimeoutMs 6000
  [void](Move-Window -Hwnd $targetHwnd -X $TargetWindowX -Y $TargetWindowY -W $TargetWindowW -H $TargetWindowH)

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
  $clientLaunchUtc = [DateTime]::UtcNow
  $clientProc = Start-Process -FilePath "powershell" -ArgumentList $clientArgs `
    -WorkingDirectory $resolvedRoot `
    -RedirectStandardOutput $clientOut `
    -RedirectStandardError $clientErr `
    -PassThru

  $clientHwnd = Wait-ForMainWindowByProcessName `
    -ProcessName $ClientProcessName `
    -StartedAfterUtc $clientLaunchUtc `
    -TimeoutMs 15000
  [void](Move-Window -Hwnd $clientHwnd -X $ClientWindowX -Y $ClientWindowY -W $ClientWindowW -H $ClientWindowH)

  Start-Sleep -Seconds 3
  Send-F9 -Hwnd $clientHwnd
  Start-Sleep -Milliseconds 800
  Send-LeftClick -Hwnd $clientHwnd -X $FocusClickX -Y $FocusClickY
  Start-Sleep -Milliseconds 600
  Send-LeftClick -Hwnd $clientHwnd -X $FocusClickX -Y $FocusClickY

  Wait-Process -Id $clientProc.Id -Timeout ([Math]::Max(20, $HostClientSeconds + 18))
  Wait-Process -Id $hostProc.Id -Timeout ([Math]::Max(20, $HostClientSeconds + 18))

  $hostRc = if ($hostProc.HasExited) { [int]$hostProc.ExitCode } else { -999 }
  $clientRc = if ($clientProc.HasExited) { [int]$clientProc.ExitCode } else { -999 }

  $hostText = if (Test-Path -LiteralPath $hostOut) { Get-Content -LiteralPath $hostOut -Raw } else { "" }
  $clientText = if (Test-Path -LiteralPath $clientOut) { Get-Content -LiteralPath $clientOut -Raw } else { "" }

  $overviewApply = [regex]::Matches($hostText, "capture-mode applied seq=.* mode=overview").Count
  $focusApply = [regex]::Matches($hostText, "capture-mode applied seq=.* mode=focus-window").Count
  $clientReqOverview = [regex]::Matches($clientText, "capture-mode-request seq=.* mode=1").Count
  $clientReqFocus = [regex]::Matches($clientText, "capture-mode-request seq=.* mode=2").Count
  $modeSwitchEvents = $overviewApply + $focusApply

  $hostCapMetaEvents = 0
  foreach ($m in [regex]::Matches($clientText, "hostCapProc=([^\\s]+)")) {
    $procName = $m.Groups[1].Value.Trim().ToLowerInvariant()
    if ($procName -ne "" -and $procName -ne "monitor" -and $procName -ne "unknown") {
      $hostCapMetaEvents++
    }
  }
  $targetProcEvents = [regex]::Matches($clientText, "hostCapProc=" + [regex]::Escape($TargetProcess)).Count

  Write-Output ("LOG_DIR=" + $logDir)
  Write-Output ("CONFIG_PATH=" + $cfgPath)
  Write-Output ("HOST_LOG=" + $hostOut)
  Write-Output ("CLIENT_LOG=" + $clientOut)
  Write-Output ("HOST_RC=" + $hostRc)
  Write-Output ("CLIENT_RC=" + $clientRc)
  Write-Output ("CLIENT_MODE_REQ_OVERVIEW_COUNT=" + $clientReqOverview)
  Write-Output ("CLIENT_MODE_REQ_FOCUS_COUNT=" + $clientReqFocus)
  Write-Output ("MODE_SWITCH_EVENT_COUNT=" + $modeSwitchEvents)
  Write-Output ("FOCUS_APPLY_COUNT=" + $focusApply)
  Write-Output ("HOSTCAP_METADATA_EVENT_COUNT=" + $hostCapMetaEvents)
  Write-Output ("HOSTCAP_TARGET_PROC_EVENT_COUNT=" + $targetProcEvents)

  $pass = ($hostRc -eq 0 -and $clientRc -eq 0 -and $overviewApply -ge 1 -and $focusApply -ge 1 -and `
          $clientReqOverview -ge 1 -and $clientReqFocus -ge 1 -and $hostCapMetaEvents -ge 1)
  Write-Output ("M13_MODE_SWITCH_PASS=" + $pass)
  exit ($(if ($pass) { 0 } else { 2 }))
}
finally {
  if ($clientProc -and -not $clientProc.HasExited) {
    try { Stop-Process -Id $clientProc.Id -Force -ErrorAction SilentlyContinue } catch {}
  }
  if ($hostProc -and -not $hostProc.HasExited) {
    try { Stop-Process -Id $hostProc.Id -Force -ErrorAction SilentlyContinue } catch {}
  }
  foreach ($targetId in $startedTargets) {
    try { Stop-Process -Id $targetId -Force -ErrorAction SilentlyContinue } catch {}
  }
}
