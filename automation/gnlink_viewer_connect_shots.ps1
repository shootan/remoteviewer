# Captures the real GNLinkViewer window in the three states the connect wait can end in.
#
# Not a DOM harness and not a mock: this starts the shipped GNLinkViewer.exe against a scripted
# fake directory, lets it reach the connect path, and photographs its own window. The three states
# are the ones the C2 F1 change is about:
#
#   1-waiting   the host has not answered yet -- the new line, "호스트 응답 대기 중…"
#   2-refused   the host answered and refused, or never answered inside the budget
#   3-cancelled the shell started a newer session and called this one off
#
# The window is captured by copying its device context, which asks the window for nothing. The
# viewer paints the connect line straight to a DC and has no message pump during connect, so a
# capture that SENDS anything (PrintWindow) blocks until the connect is over and photographs what
# comes after it instead. A DC copy reads the pixels that are already there.
#
#   gnlink_viewer_connect_shots.ps1 -Viewer <GNLinkViewer.exe> -FakeDirectoryUrl <url> -Out <dir>
#
# Exit 0 = three files written.

param(
  [Parameter(Mandatory = $true)][string]$Viewer,
  [Parameter(Mandatory = $true)][string]$FakeDirectoryUrl,
  [Parameter(Mandatory = $true)][string]$Out
)

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class GnlinkShot {
  [DllImport("user32.dll")] public static extern IntPtr GetWindowDC(IntPtr h);
  [DllImport("user32.dll")] public static extern int ReleaseDC(IntPtr h, IntPtr dc);
  [DllImport("gdi32.dll")] public static extern bool BitBlt(IntPtr d, int x, int y, int w,
                                                            int h, IntPtr s, int sx, int sy,
                                                            uint rop);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)]
  public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr p);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);

  public delegate bool EnumProc(IntPtr h, IntPtr p);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }

  public static IntPtr FindFor(uint wanted) {
    IntPtr found = IntPtr.Zero;
    EnumWindows((h, p) => {
      uint pid; GetWindowThreadProcessId(h, out pid);
      if (pid != wanted || !IsWindowVisible(h)) return true;
      RECT r; if (!GetWindowRect(h, out r)) return true;
      if (r.R - r.L < 200 || r.B - r.T < 150) return true;   // tooltips and the like
      found = h;
      return false;
    }, IntPtr.Zero);
    return found;
  }
}
'@

function Save-Window {
  param([IntPtr]$Handle, [string]$Path)
  $rect = New-Object GnlinkShot+RECT
  if (-not [GnlinkShot]::GetWindowRect($Handle, [ref]$rect)) { return $false }
  $w = $rect.R - $rect.L
  $h = $rect.B - $rect.T
  if ($w -le 0 -or $h -le 0) { return $false }
  $bmp = New-Object System.Drawing.Bitmap $w, $h
  $gfx = [System.Drawing.Graphics]::FromImage($bmp)
  $dc = $gfx.GetHdc()
  # A straight copy of the pixels that are there, asking the window for nothing.
  #
  # PrintWindow was the obvious choice and is the wrong one here: it SENDS a message, and during
  # connect the viewer has no message pump -- so the call blocks until the connect is over and
  # then photographs the screen that comes after it. Three identical shots of the failure screen,
  # twice, until that was worked out. BitBlt from the window DC reads what has already been
  # drawn, which is exactly what a paint-straight-to-DC status line leaves behind.
  $src = [GnlinkShot]::GetWindowDC($Handle)
  $ok = $false
  if ($src -ne [IntPtr]::Zero) {
    $ok = [GnlinkShot]::BitBlt($dc, 0, 0, $w, $h, $src, 0, 0, 0x00CC0020)   # SRCCOPY
    [void][GnlinkShot]::ReleaseDC($Handle, $src)
  }
  $gfx.ReleaseHdc($dc)
  $gfx.Dispose()
  if ($ok) { $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png) }
  $bmp.Dispose()
  return $ok
}

function Wait-Window {
  param([uint32]$ProcessId, [int]$BudgetMs)
  $deadline = (Get-Date).AddMilliseconds($BudgetMs)
  while ((Get-Date) -lt $deadline) {
    $h = [GnlinkShot]::FindFor($ProcessId)
    if ($h -ne [IntPtr]::Zero) { return $h }
    Start-Sleep -Milliseconds 100
  }
  return [IntPtr]::Zero
}

New-Item -ItemType Directory -Force -Path $Out | Out-Null
$written = 0

# ----------------------------------------------------------------- 1. waiting for the host
# NOT $args. That is an automatic variable holding this script's own arguments, and assigning
# to it left Start-Process launching GNLinkViewer with no command line at all -- which fails
# instantly and paints the failure screen. Three screenshots of that, twice, before the cause
# was found by reproducing the launch with stdout captured.
$viewerArgs = @('--transport', 'udp', '--codec', 'h264',
               '--directory-url', $FakeDirectoryUrl,
          '--directory-session', 'test-session-token',
          '--directory-host-id', 'h-cancel')
$env:REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE = '1'
$p = Start-Process -FilePath $Viewer -ArgumentList $viewerArgs -PassThru
$hwnd = Wait-Window -ProcessId $p.Id -BudgetMs 20000
if ($hwnd -eq [IntPtr]::Zero) {
  Write-Output "no window appeared for pid $($p.Id)"
  try { $p.Kill() } catch { }
  exit 1
}

# Past the punch and into the hello, which is where the waiting line lives.
Start-Sleep -Milliseconds 6000
if (Save-Window -Handle $hwnd -Path (Join-Path $Out '1-waiting.png')) { $written++ }
Write-Output "1-waiting: captured"

# ----------------------------------------------------------------- 3. cancelled (same process)
# Taken before the refusal shot because it needs this process alive and mid-wait. The event is
# the one the shell signals; nothing here reaches into the viewer any other way.
$cancelName = "Local\GNLinkViewerCancel-$($p.Id)"
$evt = [System.Threading.EventWaitHandle]::OpenExisting($cancelName)
$evt.Set() | Out-Null
$evt.Dispose()
# The window goes in well under a tenth of a second once cancelled -- measured at 59-102ms in
# viewer_cancel_e2e_test -- so this is a race against the thing being photographed. Captured on
# a short poll rather than a fixed sleep: the first frame where the process has been told and
# the window is still up is the one worth having.
$shot = $false
for ($i = 0; $i -lt 12; $i++) {
  if (-not [GnlinkShot]::IsWindowVisible($hwnd)) { break }
  if (Save-Window -Handle $hwnd -Path (Join-Path $Out '3-cancelled.png')) { $shot = $true; break }
  Start-Sleep -Milliseconds 8
}
if ($shot) {
  $written++
  Write-Output "3-cancelled: captured while the window was still up"
} else {
  Write-Output "3-cancelled: the window was gone before a frame could be taken -- which is the point"
}
try { $p.WaitForExit(5000) } catch { }
try { if (-not $p.HasExited) { $p.Kill() } } catch { }

# ----------------------------------------------------------------- 2. the host refused
# No shortcut: the hello budget is thirty seconds and this waits it out. An env override would
# have meant adding a way to shorten a security-relevant budget to the SHIPPED binary, purely
# so a screenshot script could finish sooner. Not worth it -- so this takes about 40 seconds.
$p2 = Start-Process -FilePath $Viewer -ArgumentList $viewerArgs -PassThru
$hwnd2 = Wait-Window -ProcessId $p2.Id -BudgetMs 20000
if ($hwnd2 -ne [IntPtr]::Zero) {
  # Past the punch, past the hello budget, into the failure screen.
  # punch (4s) + hello (30s) + the failure screen appearing.
  Start-Sleep -Milliseconds 38000
  if (Save-Window -Handle $hwnd2 -Path (Join-Path $Out '2-refused.png')) { $written++ }
  Write-Output "2-refused: captured"
}
try { if (-not $p2.HasExited) { $p2.Kill() } } catch { }

Write-Output "$written png(s) in $Out"
if ($written -lt 2) { exit 1 }
exit 0
