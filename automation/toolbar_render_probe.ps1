# Renders the session toolbar to a PNG so its painting can be checked without a live host.
#
# Screen capture is not usable here: the toolbar is a small window over the viewer, and whatever
# happens to be in front on this desktop is what CopyFromScreen returns. PrintWindow asks the
# window itself to draw, which is exactly the code under test and nothing else.

param(
  [string]$Viewer = "D:\remote\remote\build-local\apps\native_poc\Release\GNLinkViewer.exe",
  [string]$Stream = "D:\remote\remote\build-local\apps\native_poc\Release\GNLinkStream.exe",
  [string]$Out = "D:\remote\remote\log\toolbar-check.png",
  [int]$Port = 43100,
  [uint32]$PrintFlags = 2   # PW_RENDERFULLCONTENT; layered windows come back blank without it
)

Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing @'
using System;using System.Runtime.InteropServices;using System.Text;using System.Drawing;
public class ToolbarProbe {
  public delegate bool Cb(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(Cb c, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int m);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  public struct RECT { public int L, T, R, B; }

  public static IntPtr Found = IntPtr.Zero;
  public static IntPtr Macro = IntPtr.Zero;
  public static RECT FoundRect;
  public static uint TargetPid = 0;

  public static bool Locate(IntPtr h, IntPtr l) {
    uint owner = 0;
    GetWindowThreadProcessId(h, out owner);
    if (owner != TargetPid) return true;
    StringBuilder sb = new StringBuilder(256);
    GetClassNameW(h, sb, 256);
    string name = sb.ToString();
    if (name == "Remote60SessionToolbar") {
      RECT r; GetWindowRect(h, out r);
      Found = h; FoundRect = r;
    }
    if (name == "GNLinkMacroWindow" && IsWindowVisible(h)) Macro = h;
    return true;
  }

  /** Posts a click straight at the toolbar, so nothing depends on which window has focus. */
  public static void Click(IntPtr h, int x, int y) {
    IntPtr pos = (IntPtr)((y << 16) | (x & 0xFFFF));
    SendMessageW(h, 0x0201, (IntPtr)1, pos);  // WM_LBUTTONDOWN
    SendMessageW(h, 0x0202, IntPtr.Zero, pos);  // WM_LBUTTONUP
  }

  public static Bitmap Grab(IntPtr h, int w, int ht, uint flags) {
    Bitmap b = new Bitmap(w, ht);
    using (Graphics g = Graphics.FromImage(b)) {
      IntPtr dc = g.GetHdc();
      PrintWindow(h, dc, flags);
      g.ReleaseHdc(dc);
    }
    return b;
  }
}
'@

$env:REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE = "1"
$env:REMOTE60_NATIVE_START_STREAM_VIEW = "1"

# The handshake has to actually succeed. The viewer creates its window early but does not pump
# messages until the session is up, so against a dead address PrintWindow only ever catches a
# blocked thread and returns an empty bitmap -- which looks exactly like a toolbar that does not
# paint. A throwaway host on its own port, with no directory, gives the pump something to run.
$host_ = Start-Process $Stream -PassThru -NoNewWindow `
  -ArgumentList "--transport","udp","--codec","h264", `
                "--bind-port","$Port","--control-port","$($Port + 1)","--seconds","40" `
  -RedirectStandardOutput "$env:TEMP\toolbar_probe_host_out.txt" `
  -RedirectStandardError  "$env:TEMP\toolbar_probe_host_err.txt"
Start-Sleep -Milliseconds 1500

$proc = Start-Process $Viewer -PassThru -NoNewWindow `
  -ArgumentList "--transport","udp","--codec","h264","--enable-input-channel", `
                "--host","127.0.0.1","--port","$Port","--control-port","$($Port + 1)", `
                "--seconds","30" `
  -RedirectStandardOutput "$env:TEMP\toolbar_probe_out.txt" `
  -RedirectStandardError  "$env:TEMP\toolbar_probe_err.txt"
Start-Sleep -Seconds 5

[ToolbarProbe]::TargetPid = $proc.Id
$cb = [ToolbarProbe+Cb]{ param($h, $l) return [ToolbarProbe]::Locate($h, $l) }
[void][ToolbarProbe]::EnumWindows($cb, [IntPtr]::Zero)

$handle = [ToolbarProbe]::Found
if ($handle -eq [IntPtr]::Zero) {
  if (-not $proc.HasExited) { $proc.Kill() }
if (-not $host_.HasExited) { $host_.Kill() }
  throw "the toolbar window was never created"
}

$r = [ToolbarProbe]::FoundRect
$w = $r.R - $r.L
$h = $r.B - $r.T
$bmp = [ToolbarProbe]::Grab($handle, $w, $h, $PrintFlags)

# A bar that paints nothing still prints as a correctly sized black rectangle, so the check that
# matters is whether any pixel differs from the darkest one.
$distinct = New-Object System.Collections.Generic.HashSet[int]
for ($y = 0; $y -lt $h; $y += 2) {
  for ($x = 0; $x -lt $w; $x += 2) { [void]$distinct.Add($bmp.GetPixel($x, $y).ToArgb()) }
}

$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()

# The button the user could not find. Clicking it has to actually produce the macro window,
# so the check goes all the way through rather than stopping at "the bar is drawn".
# 매크로 is the first (leftmost) button since the target picker left the toolbar.
[ToolbarProbe]::Click($handle, 30, 20)
Start-Sleep -Milliseconds 2500
[ToolbarProbe]::Macro = [IntPtr]::Zero
[void][ToolbarProbe]::EnumWindows($cb, [IntPtr]::Zero)
$macroOpened = [ToolbarProbe]::Macro -ne [IntPtr]::Zero

if (-not $proc.HasExited) { $proc.Kill() }
if (-not $host_.HasExited) { $host_.Kill() }

"rect      : $($r.L),$($r.T)  ${w}x${h}"
"colours   : $($distinct.Count) distinct"
"saved     : $Out"
if ($distinct.Count -le 1) { "VERDICT   : blank -- the toolbar is not painting" }
else { "VERDICT   : painted" }
if ($macroOpened) { "macro     : opened on click" } else { "macro     : NOT opened on click" }
