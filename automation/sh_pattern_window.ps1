# sh-filter r1 -- TEST TOOL: the picture the sharpening filter is judged on.
#
# A borderless 1920x1080 window titled GNLINK-SH-PATTERN (so the encoder pads to 1088 and the phone's
# decoder must crop -- contract 1's 1088 case). It sits ON screen, TOPMOST and never activated, for the short
# time a run needs: a window fully covered by others delivered no capture frames in 3 of 5 runs here
# (callbackFrames=0), and one off screen captured black (DWM does not render outside the monitor).
# -Bottom keeps the old behaviour (bottom of the z-order) for a run that must not cover the screen.
# -Layered (sh-strength r2): covers NOTHING -- the picture is drawn once into a bitmap and handed to DWM with
# UpdateLayeredWindow, and the window sits OUTSIDE every monitor (right of the virtual screen), not topmost. A
# layered window keeps its pixels there (an ordinary one off screen captured black); the host captures it as a
# WINDOW (WGC). Static only (no moving square). The user's screen is never covered.
#
# Content, each in its own region (sh_filter_avd.py measures the regions by these coordinates):
#   A  thin text   Korean + Latin, 9-14 px, black on white                      x  40..900  y  40..400
#   B  low contrast  the same text, #505050 on #646464                           x 960..1880 y  40..400
#   C  1-px lines  alternating black/white vertical and horizontal single lines  x  40..440  y 440..700
#   D  flat colour  red / green / blue / mid grey blocks                         x 480..1000 y 440..700
#   E  hard edges  8-px black/white checkerboard                                 x1040..1400 y 440..700
#   F  colour text  red on blue, yellow on black                                 x1440..1880 y 440..700
#   G  moving       a square crossing the strip at 30 fps (the "video" case)     x  40..1880 y 760..1000
#   border          1-px rows/columns: top red, bottom green, left blue, right yellow (crop/flip check)
#
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File sh_pattern_window.ps1 [-Seconds 1800] [-Static]
#   -Static: the square does not move (identical frames for the off/0.20/0.35 picture comparison).
param([int]$Seconds = 1800, [int]$Left = 0, [int]$Top = 0, [int]$Width = 1920, [int]$Height = 1080,
      [string]$Title = 'GNLINK-SH-PATTERN', [string]$InputLog = '', [switch]$Static, [switch]$Bottom, [switch]$Layered, [int]$PulseMs = 200)

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

# Never takes the foreground (another session's UI test may own it): shown without activation, no taskbar.
Add-Type -ReferencedAssemblies System.Windows.Forms -TypeDefinition @"
public class GnlinkNoActivateForm : System.Windows.Forms.Form {
  protected override bool ShowWithoutActivation { get { return true; } }
  protected override System.Windows.Forms.CreateParams CreateParams {
    get { var cp = base.CreateParams; cp.ExStyle |= 0x08000000; return cp; }  // NOACTIVATE (not TOOLWINDOW: the host skips tool windows)
  }
}
"@
if ($Layered) {
  Add-Type -ReferencedAssemblies System.Windows.Forms,System.Drawing -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public class GnlinkLayeredForm : System.Windows.Forms.Form {
  protected override bool ShowWithoutActivation { get { return true; } }
  protected override System.Windows.Forms.CreateParams CreateParams {
    get { var cp = base.CreateParams; cp.ExStyle |= 0x08000000 | 0x00080000; return cp; }  // NOACTIVATE | LAYERED
  }
}
public static class GnlinkLayered {
  [DllImport("user32.dll")] static extern IntPtr GetDC(IntPtr h);
  [DllImport("user32.dll")] static extern int ReleaseDC(IntPtr h, IntPtr dc);
  [DllImport("gdi32.dll")] static extern IntPtr CreateCompatibleDC(IntPtr dc);
  [DllImport("gdi32.dll")] static extern IntPtr SelectObject(IntPtr dc, IntPtr o);
  [DllImport("gdi32.dll")] static extern bool DeleteObject(IntPtr o);
  [DllImport("gdi32.dll")] static extern bool DeleteDC(IntPtr dc);
  [StructLayout(LayoutKind.Sequential)] struct PT { public int x, y; }
  [StructLayout(LayoutKind.Sequential)] struct SZ { public int cx, cy; }
  [StructLayout(LayoutKind.Sequential, Pack = 1)] struct BLEND { public byte op, flags, alpha, fmt; }
  [DllImport("user32.dll")] static extern bool UpdateLayeredWindow(IntPtr h, IntPtr dst, ref PT pt, ref SZ sz, IntPtr src, ref PT srcPt, int key, ref BLEND b, int flags);
  public static bool Push(IntPtr hwnd, System.Drawing.Bitmap bmp, int x, int y) {
    IntPtr screen = GetDC(IntPtr.Zero), mem = CreateCompatibleDC(screen);
    IntPtr hbmp = bmp.GetHbitmap(System.Drawing.Color.FromArgb(0)), old = SelectObject(mem, hbmp);
    PT pt = new PT { x = x, y = y }, src = new PT { x = 0, y = 0 };
    SZ sz = new SZ { cx = bmp.Width, cy = bmp.Height };
    BLEND bl = new BLEND { op = 0, flags = 0, alpha = 255, fmt = 1 };
    bool ok = UpdateLayeredWindow(hwnd, screen, ref pt, ref sz, mem, ref src, 0, ref bl, 2);
    SelectObject(mem, old); DeleteObject(hbmp); DeleteDC(mem); ReleaseDC(IntPtr.Zero, screen);
    return ok;
  }
}
"@
  $form = New-Object GnlinkLayeredForm
  $Static = $true
  $vs = [System.Windows.Forms.SystemInformation]::VirtualScreen
  $Left = $vs.Right + 200
  $Top = $vs.Top
} else {
  $form = New-Object GnlinkNoActivateForm
}
$form.Text = $Title
$form.FormBorderStyle = 'None'
$form.StartPosition = 'Manual'
# ShowInTaskbar stays true: with false, WinForms gives the window a hidden OWNER, and the host never
# captures an owned window (host_window_enum.cpp match_capture_window).
$form.ShowInTaskbar = $true
$form.Location = New-Object System.Drawing.Point($Left, $Top)
# r2: -Width/-Height/-Title make a second, smaller pattern window (a resolution change for the viewer:
# switching to it is a new picture size while the sharpening pass is live). Content is drawn at the same
# coordinates; what falls outside a smaller window is simply not there.
$form.ClientSize = New-Object System.Drawing.Size($Width, $Height)
$form.BackColor = [System.Drawing.Color]::White
$prop = $form.GetType().GetProperty('DoubleBuffered', [Reflection.BindingFlags]'Instance,NonPublic')
$prop.SetValue($form, $true, $null)

$script:x = 40
$korean = '가는 글씨 선명도 확인 — 원격 화면 GNLink 한글 테스트 0123456789'
$latin = 'The quick brown fox jumps over the lazy dog 0O1lI| il1 rn m'

# r3 (Codex R3.3): an OWNED input target. With -InputLog, every mouse press/release this window receives
# (the host's background-message injection posts them here) is appended as "down|up <clientX> <clientY>".
if ($InputLog) {
  $form.Add_MouseDown({ param($s, $e) Add-Content -Path $InputLog -Value ("down {0} {1}" -f $e.X, $e.Y) })
  $form.Add_MouseUp({ param($s, $e) Add-Content -Path $InputLog -Value ("up {0} {1}" -f $e.X, $e.Y) })
}

$draw = {
  param($g)
  $g.TextRenderingHint = if ($Layered) { [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit } else { [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit }
  $black = [System.Drawing.Brushes]::Black
  # A: thin text
  $y = 40
  foreach ($size in 9, 10, 11, 12, 14) {
    $font = New-Object System.Drawing.Font('Malgun Gothic', $size, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
    $g.DrawString($korean, $font, $black, 40, $y); $y += $size + 8
    $font2 = New-Object System.Drawing.Font('Segoe UI', $size, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
    $g.DrawString($latin, $font2, $black, 40, $y); $y += $size + 12
  }
  # B: low contrast
  $g.FillRectangle((New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(100,100,100))), 960, 40, 920, 360)
  $low = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(80,80,80))
  $y = 50
  foreach ($size in 9, 10, 11, 12, 14) {
    $font = New-Object System.Drawing.Font('Malgun Gothic', $size, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
    $g.DrawString($korean, $font, $low, 970, $y); $y += $size + 8
    $font2 = New-Object System.Drawing.Font('Segoe UI', $size, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
    $g.DrawString($latin, $font2, $low, 970, $y); $y += $size + 12
  }
  # C: 1-px lines
  for ($i = 0; $i -lt 200; $i += 4) { $g.FillRectangle($black, 40 + $i, 440, 1, 120) }
  for ($i = 0; $i -lt 120; $i += 4) { $g.FillRectangle($black, 240, 580 + $i, 200, 1) }
  # D: flat colour
  $g.FillRectangle([System.Drawing.Brushes]::Red, 480, 440, 120, 260)
  $g.FillRectangle([System.Drawing.Brushes]::Lime, 610, 440, 120, 260)
  $g.FillRectangle([System.Drawing.Brushes]::Blue, 740, 440, 120, 260)
  $g.FillRectangle((New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(128,128,128))), 870, 440, 130, 260)
  # E: checkerboard
  for ($cy = 0; $cy -lt 260; $cy += 8) { for ($cx = 0; $cx -lt 360; $cx += 8) {
    if ((($cx + $cy) / 8) % 2 -eq 0) { $g.FillRectangle($black, 1040 + $cx, 440 + $cy, 8, 8) } } }
  # F: colour text
  $g.FillRectangle([System.Drawing.Brushes]::Blue, 1440, 440, 440, 120)
  $g.FillRectangle($black, 1440, 580, 440, 120)
  $f12 = New-Object System.Drawing.Font('Malgun Gothic', 12, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
  $g.DrawString('빨강 글씨 red text 012', $f12, [System.Drawing.Brushes]::Red, 1450, 480)
  $g.DrawString('노랑 글씨 yellow text 012', $f12, [System.Drawing.Brushes]::Yellow, 1450, 620)
  # G: moving square
  $g.FillRectangle((New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(230,230,230))), 40, 760, 1840, 240)
  $g.FillRectangle([System.Drawing.Brushes]::DarkOrange, $script:x, 800, 160, 160)
  # border
  $g.FillRectangle([System.Drawing.Brushes]::Red, 0, 0, $Width, 1)
  $g.FillRectangle([System.Drawing.Brushes]::Lime, 0, $Height - 1, $Width, 1)
  $g.FillRectangle([System.Drawing.Brushes]::Blue, 0, 0, 1, $Height)
  $g.FillRectangle([System.Drawing.Brushes]::Yellow, $Width - 1, 0, 1, $Height)
}
$form.Add_Paint({ param($s, $e) & $draw $e.Graphics })

$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 33
$timer.Add_Tick({
  $script:x = $script:x + 6
  if ($script:x -gt 1700) { $script:x = 40 }
  $form.Invalidate((New-Object System.Drawing.Rectangle(40, 760, 1840, 240)))
})
$stop = New-Object System.Windows.Forms.Timer
$stop.Interval = [Math]::Max(1000, $Seconds * 1000)
$stop.Add_Tick({ $form.Close() })
Add-Type -Name Z -Namespace GnlinkSh -MemberDefinition '[System.Runtime.InteropServices.DllImport("user32.dll")] public static extern bool SetWindowPos(System.IntPtr h, System.IntPtr after, int x, int y, int cx, int cy, uint flags);'
$sink = New-Object System.Windows.Forms.Timer
$sink.Interval = 2000
$z = if ($Bottom) { [System.IntPtr]1 } else { [System.IntPtr](-1) }  # HWND_BOTTOM or HWND_TOPMOST
$sink.Add_Tick({ [GnlinkSh.Z]::SetWindowPos($form.Handle, $z, 0, 0, 0, 0, 0x0013) | Out-Null })  # NOSIZE|NOMOVE|NOACTIVATE
if ($Layered) {
  $form.Add_Shown({
    $bmp = New-Object System.Drawing.Bitmap($Width, $Height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $bg = [System.Drawing.Graphics]::FromImage($bmp)
    $bg.Clear([System.Drawing.Color]::White)
    & $draw $bg
    $bg.Dispose()
    $ok = [GnlinkLayered]::Push($form.Handle, $bmp, $Left, $Top)
    Write-Output ("layered pattern at {0},{1} {2}x{3} pushed={4} virtualScreen={5}" -f $Left, $Top, $Width, $Height, $ok, $vs)
    # A window capture (WGC) only delivers frames when the window changes: a 4x4 block in the G strip (outside
    # every measured region) flips every 200 ms and the bitmap is pushed again, so a new session gets frames.
    $script:lbmp = $bmp
    $script:flip = $false
    $pulse = New-Object System.Windows.Forms.Timer
    $pulse.Interval = [Math]::Max(16, $PulseMs)  # -PulseMs 33: ~30 changes/s (the cost runs)
    $pulse.Add_Tick({
      $script:flip = -not $script:flip
      $pg = [System.Drawing.Graphics]::FromImage($script:lbmp)
      $c = if ($script:flip) { 200 } else { 210 }
      $pg.FillRectangle((New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb($c, $c, $c))), 1300, 780, 4, 4)
      $pg.Dispose()
      [GnlinkLayered]::Push($form.Handle, $script:lbmp, $Left, $Top) | Out-Null
    })
    $pulse.Start()
    $stop.Start()
  })
} else {
  $form.Add_Shown({ [GnlinkSh.Z]::SetWindowPos($form.Handle, $z, 0, 0, 0, 0, 0x0013) | Out-Null; $sink.Start(); if (-not $Static) { $timer.Start() }; $stop.Start() })
}
[System.Windows.Forms.Application]::Run($form)
