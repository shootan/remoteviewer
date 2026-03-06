param(
  [string]$Root = "",
  [string]$ExeDir = "",
  [string]$BaseConfig = "automation/native_video_profile_720p.json",
  [string]$RemoteHost = "127.0.0.1",
  [int]$DurationSec = 20,
  [switch]$NoOccluder,
  [switch]$KeepWindows
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

function Wait-MainWindowHandle {
  param(
    [System.Diagnostics.Process]$Process,
    [int]$TimeoutSec = 10
  )
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  while ((Get-Date) -lt $deadline) {
    if ($null -eq $Process -or $Process.HasExited) { break }
    $Process.Refresh()
    if ($Process.MainWindowHandle -ne 0) {
      return [IntPtr]$Process.MainWindowHandle
    }
    Start-Sleep -Milliseconds 100
  }
  return [IntPtr]::Zero
}

function Stop-IfAlive {
  param(
    [System.Diagnostics.Process]$Process
  )
  if ($null -eq $Process) { return }
  try {
    if (-not $Process.HasExited) {
      Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
    }
  } catch {}
}

function Get-MetricValue {
  param(
    [string]$Line,
    [string]$Name
  )
  $m = [regex]::Match($Line, ("(?:^|\s){0}=(-?\d+)" -f [regex]::Escape($Name)))
  if (-not $m.Success) { return $null }
  return [int64]$m.Groups[1].Value
}

function Set-JsonProperty {
  param(
    [Parameter(Mandatory = $true)]$Object,
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $false)]$Value
  )
  if ($Object.PSObject.Properties.Name -contains $Name) {
    $Object.$Name = $Value
  } else {
    $Object | Add-Member -NotePropertyName $Name -NotePropertyValue $Value
  }
}

if ([string]::IsNullOrWhiteSpace($Root)) {
  $Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
}

if ([string]::IsNullOrWhiteSpace($ExeDir)) {
  if (Test-Path -LiteralPath (Join-Path $Root "bin")) {
    $ExeDir = "bin"
  } else {
    $ExeDir = "build-vcpkg-local/apps/native_poc/Debug"
  }
}

$resolvedExeDir = Resolve-FromRoot -Base $Root -Value $ExeDir
$hostExe = Join-Path $resolvedExeDir "remote60_native_video_host_poc.exe"
$clientExe = Join-Path $resolvedExeDir "remote60_native_video_client_poc.exe"
if (-not (Test-Path -LiteralPath $hostExe)) { throw "host exe not found: $hostExe" }
if (-not (Test-Path -LiteralPath $clientExe)) { throw "client exe not found: $clientExe" }

$resolvedBaseConfig = Resolve-FromRoot -Base $Root -Value $BaseConfig
$cfg = Get-Content -LiteralPath $resolvedBaseConfig -Raw | ConvertFrom-Json

$ts = Get-Date -Format "yyyyMMdd-HHmmss"
$logDir = Join-Path $Root ("automation/logs/m35-input-validate-" + $ts)
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$summaryPath = Join-Path $logDir "summary.txt"

$targetFile = Join-Path $logDir "m35_target_window.txt"
$targetSeed = @(
  "M3.5 input validation target"
  "Expect: click/drag/keyboard while target is occluded."
)
$targetSeed | Set-Content -LiteralPath $targetFile -Encoding UTF8
$targetTitleNeedle = [System.IO.Path]::GetFileName($targetFile)

$occluderFile = Join-Path $logDir "m35_occluder_window.txt"
"occluder" | Set-Content -LiteralPath $occluderFile -Encoding UTF8

$controlPort = if ($cfg.PSObject.Properties.Name -contains "controlPort" -and [int]$cfg.controlPort -gt 0) { [int]$cfg.controlPort } else { 43001 }
Set-JsonProperty -Object $cfg -Name "controlPort" -Value $controlPort
Set-JsonProperty -Object $cfg -Name "noInputChannel" -Value $false
Set-JsonProperty -Object $cfg -Name "enableInputInjection" -Value $true
Set-JsonProperty -Object $cfg -Name "inputInjectionMode" -Value "background_message"
Set-JsonProperty -Object $cfg -Name "inputTargetProcess" -Value "notepad.exe"
Set-JsonProperty -Object $cfg -Name "inputTargetTitle" -Value $targetTitleNeedle
Set-JsonProperty -Object $cfg -Name "captureWindowProcess" -Value "notepad.exe"
Set-JsonProperty -Object $cfg -Name "captureWindowTitle" -Value $targetTitleNeedle
Set-JsonProperty -Object $cfg -Name "captureWindowClientOnly" -Value $true
Set-JsonProperty -Object $cfg -Name "captureWindowRebindIntervalMs" -Value 500
Set-JsonProperty -Object $cfg -Name "seconds" -Value ([Math]::Max(5, $DurationSec))
Set-JsonProperty -Object $cfg -Name "remoteHost" -Value $RemoteHost

$profilePath = Join-Path $logDir "m35_profile.json"
$cfg | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $profilePath -Encoding UTF8

$hostOut = Join-Path $logDir "host.out.log"
$hostErr = Join-Path $logDir "host.err.log"
$clientOut = Join-Path $logDir "client.out.log"
$clientErr = Join-Path $logDir "client.err.log"

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class NativeInputWin32 {
  [StructLayout(LayoutKind.Sequential)]
  public struct RECT {
    public int Left;
    public int Top;
    public int Right;
    public int Bottom;
  }
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string className, string windowName);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr hWnd, int x, int y, int w, int h, bool repaint);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
}
"@

function New-LParam {
  param([int]$X, [int]$Y)
  $packed = (($Y -band 0xFFFF) -shl 16) -bor ($X -band 0xFFFF)
  return [IntPtr]$packed
}

function Wait-NativeWindowHandle {
  param(
    [string]$ClassName,
    [string]$WindowTitle = "",
    [int]$TimeoutSec = 10
  )
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  while ((Get-Date) -lt $deadline) {
    $hwnd = [NativeInputWin32]::FindWindow($ClassName, $(if ([string]::IsNullOrWhiteSpace($WindowTitle)) { $null } else { $WindowTitle }))
    if ($hwnd -ne [IntPtr]::Zero) { return $hwnd }
    Start-Sleep -Milliseconds 100
  }
  return [IntPtr]::Zero
}

function Send-ClientInputBurst {
  param([IntPtr]$Hwnd)
  if ($Hwnd -eq [IntPtr]::Zero) { return $false }
  $WM_MOUSEMOVE = 0x0200
  $WM_LBUTTONDOWN = 0x0201
  $WM_LBUTTONUP = 0x0202
  $WM_KEYDOWN = 0x0100
  $WM_KEYUP = 0x0101
  $MK_LBUTTON = 0x0001

  $clickLp = New-LParam -X 140 -Y 120
  [NativeInputWin32]::PostMessage($Hwnd, $WM_LBUTTONDOWN, [IntPtr]$MK_LBUTTON, $clickLp) | Out-Null
  Start-Sleep -Milliseconds 30
  [NativeInputWin32]::PostMessage($Hwnd, $WM_LBUTTONUP, [IntPtr]0, $clickLp) | Out-Null

  $dragStartX = 180
  $dragEndX = 420
  $dragY = 220
  [NativeInputWin32]::PostMessage($Hwnd, $WM_LBUTTONDOWN, [IntPtr]$MK_LBUTTON, (New-LParam -X $dragStartX -Y $dragY)) | Out-Null
  for ($x = $dragStartX + 30; $x -le $dragEndX; $x += 30) {
    [NativeInputWin32]::PostMessage($Hwnd, $WM_MOUSEMOVE, [IntPtr]$MK_LBUTTON, (New-LParam -X $x -Y $dragY)) | Out-Null
    Start-Sleep -Milliseconds 20
  }
  [NativeInputWin32]::PostMessage($Hwnd, $WM_LBUTTONUP, [IntPtr]0, (New-LParam -X $dragEndX -Y $dragY)) | Out-Null

  foreach ($vk in @(0x41, 0x42, 0x43, 0x31, 0x32, 0x33)) {
    [NativeInputWin32]::PostMessage($Hwnd, $WM_KEYDOWN, [IntPtr]$vk, [IntPtr]1) | Out-Null
    Start-Sleep -Milliseconds 15
    [NativeInputWin32]::PostMessage($Hwnd, $WM_KEYUP, [IntPtr]$vk, [IntPtr]0xC0000001) | Out-Null
    Start-Sleep -Milliseconds 15
  }
  return $true
}

$targetProc = $null
$occluderProc = $null
$hostProc = $null
$clientProc = $null

try {
  $targetProc = Start-Process -FilePath "notepad.exe" -ArgumentList @($targetFile) -PassThru
  $targetHwnd = Wait-MainWindowHandle -Process $targetProc -TimeoutSec 10

  if (-not $NoOccluder) {
    $occluderProc = Start-Process -FilePath "notepad.exe" -ArgumentList @($occluderFile) -PassThru
    $occluderHwnd = Wait-MainWindowHandle -Process $occluderProc -TimeoutSec 10
    if ($targetHwnd -ne [IntPtr]::Zero -and $occluderHwnd -ne [IntPtr]::Zero) {
      $rect = New-Object NativeInputWin32+RECT
      if ([NativeInputWin32]::GetWindowRect($targetHwnd, [ref]$rect)) {
        $w = [Math]::Max(600, $rect.Right - $rect.Left)
        $h = [Math]::Max(400, $rect.Bottom - $rect.Top)
        [NativeInputWin32]::MoveWindow($occluderHwnd, $rect.Left + 20, $rect.Top + 20, $w, $h, $true) | Out-Null
        [NativeInputWin32]::SetForegroundWindow($occluderHwnd) | Out-Null
      }
    }
  }

  $hostProc = Start-Process -FilePath $hostExe `
    -ArgumentList @("--config", $profilePath, "--seconds", "$DurationSec") `
    -WorkingDirectory $Root `
    -RedirectStandardOutput $hostOut `
    -RedirectStandardError $hostErr `
    -PassThru

  Start-Sleep -Milliseconds 800

  $clientProc = Start-Process -FilePath $clientExe `
    -ArgumentList @("--config", $profilePath, "--host", $RemoteHost, "--seconds", "$DurationSec") `
    -WorkingDirectory $Root `
    -RedirectStandardOutput $clientOut `
    -RedirectStandardError $clientErr `
    -PassThru

  $clientHwnd = Wait-NativeWindowHandle -ClassName "Remote60NativeVideoClientPoc" -WindowTitle "remote60 native video client poc" -TimeoutSec 10
  if ($clientHwnd -eq [IntPtr]::Zero) {
    $clientHwnd = Wait-MainWindowHandle -Process $clientProc -TimeoutSec 5
  }
  Start-Sleep -Milliseconds 700
  $inputBurstSent = Send-ClientInputBurst -Hwnd $clientHwnd
  Start-Sleep -Milliseconds 400
  $inputBurstSent2 = Send-ClientInputBurst -Hwnd $clientHwnd

  $waitMs = ([Math]::Max(10, $DurationSec + 8)) * 1000
  if (-not $clientProc.WaitForExit($waitMs)) {
    Stop-IfAlive -Process $clientProc
  }
  if (-not $hostProc.WaitForExit($waitMs)) {
    Stop-IfAlive -Process $hostProc
  }

  $hostLines = if (Test-Path -LiteralPath $hostOut) { Get-Content -LiteralPath $hostOut } else { @() }
  $hostStatMatches = @($hostLines | Where-Object { $_ -like "*native-video-host*" -and $_ -match "inputEvents=" })
  $hostStatLine = if ($hostStatMatches.Count -gt 0) { [string]$hostStatMatches[$hostStatMatches.Count - 1] } else { "" }

  $inputEvents = if ([string]::IsNullOrWhiteSpace($hostStatLine)) { $null } else { Get-MetricValue -Line $hostStatLine -Name "inputEvents" }
  $inputNoTarget = if ([string]::IsNullOrWhiteSpace($hostStatLine)) { $null } else { Get-MetricValue -Line $hostStatLine -Name "inputNoTarget" }
  $inputInjectFail = if ([string]::IsNullOrWhiteSpace($hostStatLine)) { $null } else { Get-MetricValue -Line $hostStatLine -Name "inputInjectFail" }
  $inputUnsupported = if ([string]::IsNullOrWhiteSpace($hostStatLine)) { $null } else { Get-MetricValue -Line $hostStatLine -Name "inputUnsupported" }
  $inputIgnoredMove = if ([string]::IsNullOrWhiteSpace($hostStatLine)) { $null } else { Get-MetricValue -Line $hostStatLine -Name "inputIgnoredMove" }

  $autoChecks = @()
  $autoChecks += [pscustomobject]@{ Name = "inputEvents>0"; Pass = ($null -ne $inputEvents -and $inputEvents -gt 0) }
  $autoChecks += [pscustomobject]@{ Name = "inputNoTarget==0"; Pass = ($null -ne $inputNoTarget -and $inputNoTarget -eq 0) }
  $autoChecks += [pscustomobject]@{ Name = "inputInjectFail==0"; Pass = ($null -ne $inputInjectFail -and $inputInjectFail -eq 0) }
  $autoChecks += [pscustomobject]@{ Name = "clientInputBurstSent"; Pass = ($inputBurstSent -or $inputBurstSent2) }
  $autoPass = ($autoChecks | Where-Object { -not $_.Pass } | Measure-Object).Count -eq 0

  $summary = @()
  $summary += ("AUTO_PASS={0}" -f ($(if ($autoPass) { 1 } else { 0 })))
  $summary += ("LOG_DIR={0}" -f $logDir)
  $summary += ("PROFILE={0}" -f $profilePath)
  $summary += ("HOST_OUT={0}" -f $hostOut)
  $summary += ("CLIENT_OUT={0}" -f $clientOut)
  $summary += ("CLIENT_HWND=0x{0}" -f ($clientHwnd.ToInt64().ToString("X")))
  $summary += ("HOST_STAT_COUNT={0}" -f $hostStatMatches.Count)
  $summary += ("INPUT_EVENTS={0}" -f $(if ($null -ne $inputEvents) { $inputEvents } else { "NA" }))
  $summary += ("INPUT_NO_TARGET={0}" -f $(if ($null -ne $inputNoTarget) { $inputNoTarget } else { "NA" }))
  $summary += ("INPUT_INJECT_FAIL={0}" -f $(if ($null -ne $inputInjectFail) { $inputInjectFail } else { "NA" }))
  $summary += ("INPUT_UNSUPPORTED={0}" -f $(if ($null -ne $inputUnsupported) { $inputUnsupported } else { "NA" }))
  $summary += ("INPUT_IGNORED_MOVE={0}" -f $(if ($null -ne $inputIgnoredMove) { $inputIgnoredMove } else { "NA" }))
  $summary += ("AUTO_CHECKS={0}" -f (($autoChecks | ForEach-Object { "{0}:{1}" -f $_.Name, ($(if ($_.Pass) { "PASS" } else { "FAIL" })) }) -join ","))
  $summary += "MANUAL_CHECK_1=OS cursor did not visibly move while click/drag/keyboard were injected"
  $summary += "MANUAL_CHECK_2=Occluded target notepad reflected click/drag/keyboard input"
  $summary += ("TARGET_TITLE_NEEDLE={0}" -f $targetTitleNeedle)
  $summary += ("RUN_AT={0}" -f (Get-Date -Format o))
  $summary | Set-Content -LiteralPath $summaryPath -Encoding UTF8

  $summary | ForEach-Object { Write-Output $_ }
} finally {
  if (-not $KeepWindows) {
    Stop-IfAlive -Process $targetProc
    Stop-IfAlive -Process $occluderProc
  }
  Stop-IfAlive -Process $clientProc
  Stop-IfAlive -Process $hostProc
}
