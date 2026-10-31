param([switch]$FullHd)
$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$build = Join-Path $root 'build-incident'
$bin = Join-Path $build 'apps/native_poc/Release'
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class StabilityProbeWindow {
 public delegate bool Visitor(IntPtr h, IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumWindows(Visitor v, IntPtr p);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
 [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int w, int z, uint flags);
 public static IntPtr Find(uint wanted) {
  IntPtr result=IntPtr.Zero;
  EnumWindows((h,p)=> { uint pid; GetWindowThreadProcessId(h,out pid); var cls=new StringBuilder(128); GetClassName(h,cls,128);
   if(pid==wanted && cls.ToString()=="Remote60NativeVideoClient") { result=h; return false; } return true; },IntPtr.Zero);
  return result;
 }
}
'@
$fixtureMode = if($FullHd){'--serve-viewer-fhd'}else{'--serve-viewer'}
$fixture = Start-Process -FilePath (Join-Path $bin 'remote60_viewer_udp_recovery_test.exe') -ArgumentList $fixtureMode -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $build 'visible-fixture.log') -RedirectStandardError (Join-Path $build 'visible-fixture-error.log')
$fixtureHandle = $fixture.Handle
$port = $null
for($i=0; $i -lt 100 -and -not $port; ++$i) {
 Start-Sleep -Milliseconds 100
 foreach($line in (Get-Content (Join-Path $build 'visible-fixture.log'))) { if($line -match '^FIXTURE_PORT=(\d+)$') {$port=$matches[1]} }
}
if(-not $port){throw 'Fixture port missing'}
$previous = $env:REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE
$env:REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE='1'
try {
 $viewer = Start-Process -FilePath (Join-Path $bin 'GNLinkViewer.exe') -ArgumentList @('--host','127.0.0.1','--port',$port,'--transport','udp','--codec','h264','--fps-hint','60','--seconds','10','--initial-view','stream') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $build 'visible-viewer.log') -RedirectStandardError (Join-Path $build 'visible-viewer-error.log')
 $viewerHandle = $viewer.Handle
} finally {$env:REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=$previous}
$window = [IntPtr]::Zero
for($i=0; $i -lt 100 -and $window -eq [IntPtr]::Zero; ++$i) { Start-Sleep -Milliseconds 50; $window=[StabilityProbeWindow]::Find($viewer.Id) }
if($window -eq [IntPtr]::Zero){throw 'Owned viewer window missing'}
# Show only the test process window without taking keyboard focus from the user.
if(-not [StabilityProbeWindow]::SetWindowPos($window,[IntPtr](-1),40,40,1000,650,0x50)){throw 'Could not show test window'}
Start-Sleep -Seconds 2
Add-Type -AssemblyName System.Drawing
$shot = New-Object System.Drawing.Bitmap(1000,650)
$canvas = [System.Drawing.Graphics]::FromImage($shot)
$canvas.CopyFromScreen(40,40,0,0,$shot.Size)
$shot.Save((Join-Path $build 'visible-viewer.png'))
$canvas.Dispose(); $shot.Dispose()
$viewer.WaitForExit(); $fixture.WaitForExit()
Write-Output "VIEWER_EXIT=$($viewer.ExitCode) FIXTURE_EXIT=$($fixture.ExitCode)"
if($viewer.ExitCode -ne 0 -or $fixture.ExitCode -ne 0){exit 1}
Write-Output "SCREENSHOT=$(Join-Path $build 'visible-viewer.png')"
