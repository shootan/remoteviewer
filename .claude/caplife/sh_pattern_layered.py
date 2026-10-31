p = 'C:/work/gnlink-wt/apk-prelogin-select/automation/sh_pattern_window.ps1'
raw = open(p, 'rb').read()
bom = raw.startswith(b'\xef\xbb\xbf')
s = raw.decode('utf-8-sig')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:90])
    s = s.replace(old, new)

rep('''# -Bottom keeps the old behaviour (bottom of the z-order) for a run that must not cover the screen.''',
    '''# -Bottom keeps the old behaviour (bottom of the z-order) for a run that must not cover the screen.
# -Layered (sh-strength r2): covers NOTHING -- the picture is drawn once into a bitmap and handed to DWM with
# UpdateLayeredWindow, and the window sits OUTSIDE every monitor (right of the virtual screen), not topmost. A
# layered window keeps its pixels there (an ordinary one off screen captured black); the host captures it as a
# WINDOW (WGC). Static only (no moving square). The user's screen is never covered.''')

rep('''      [string]$Title = 'GNLINK-SH-PATTERN', [string]$InputLog = '', [switch]$Static, [switch]$Bottom)''',
    '''      [string]$Title = 'GNLINK-SH-PATTERN', [string]$InputLog = '', [switch]$Static, [switch]$Bottom, [switch]$Layered)''')

rep('''$form = New-Object GnlinkNoActivateForm''', '''if ($Layered) {
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
}''')

rep('''$form.Add_Paint({
  param($s, $e)
  $g = $e.Graphics
  $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit''', '''$draw = {
  param($g)
  $g.TextRenderingHint = if ($Layered) { [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit } else { [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit }''')

rep('''  $g.FillRectangle([System.Drawing.Brushes]::Yellow, $Width - 1, 0, 1, $Height)
})''', '''  $g.FillRectangle([System.Drawing.Brushes]::Yellow, $Width - 1, 0, 1, $Height)
}
$form.Add_Paint({ param($s, $e) & $draw $e.Graphics })''')

rep('''$form.Add_Shown({ [GnlinkSh.Z]::SetWindowPos($form.Handle, $z, 0, 0, 0, 0, 0x0013) | Out-Null; $sink.Start(); if (-not $Static) { $timer.Start() }; $stop.Start() })''',
    '''if ($Layered) {
  $form.Add_Shown({
    $bmp = New-Object System.Drawing.Bitmap($Width, $Height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $bg = [System.Drawing.Graphics]::FromImage($bmp)
    $bg.Clear([System.Drawing.Color]::White)
    & $draw $bg
    $bg.Dispose()
    $ok = [GnlinkLayered]::Push($form.Handle, $bmp, $Left, $Top)
    Write-Output ("layered pattern at {0},{1} {2}x{3} pushed={4} virtualScreen={5}" -f $Left, $Top, $Width, $Height, $ok, $vs)
    $stop.Start()
  })
} else {
  $form.Add_Shown({ [GnlinkSh.Z]::SetWindowPos($form.Handle, $z, 0, 0, 0, 0, 0x0013) | Out-Null; $sink.Start(); if (-not $Static) { $timer.Start() }; $stop.Start() })
}''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write((b'\xef\xbb\xbf' if bom else b'') + s.encode('utf-8'))
print('ok')
