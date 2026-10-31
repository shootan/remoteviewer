# Checks that a session reached through the tunnel -- no separate control port -- actually has a
# control channel: input enabled, window list arriving, input events leaving and being acked.
#
# The throwaway host runs WITHOUT --enable-input-injection on purpose. The client side is what
# changed, and injection would move the real cursor on whichever machine runs this.

param(
  [string]$Viewer = "D:\remote\remote\build-local\apps\native_poc\Release\GNLinkViewer.exe",
  [string]$Stream = "D:\remote\remote\build-local\apps\native_poc\Release\GNLinkStream.exe",
  [int]$Port = 43200
)

Add-Type @'
using System;using System.Runtime.InteropServices;using System.Text;
public class CtrlProbe {
  public delegate bool Cb(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(Cb c, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int m);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
  [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  public static IntPtr View = IntPtr.Zero;
  public static uint TargetPid = 0;
  public static bool Locate(IntPtr h, IntPtr l) {
    uint owner = 0; GetWindowThreadProcessId(h, out owner);
    if (owner != TargetPid) return true;
    StringBuilder sb = new StringBuilder(256); GetClassNameW(h, sb, 256);
    if (sb.ToString() == "Remote60NativeVideoClient") View = h;
    return true;
  }
  // Posted at the window, not injected globally, so this cannot land in another application.
  // A bare move is deliberately dropped by the viewer -- it only forwards motion while a button
  // is held -- so the probe has to press, drag and release to exercise the input path at all.
  // F13: no coordinate mapping involved, which separates "the click was mapped away" from
  // "nothing reaches the wire at all". Nothing on a normal desktop is bound to it.
  public static void Key(IntPtr h) {
    SendMessageW(h, 0x0100, (IntPtr)0x7C, IntPtr.Zero);  // WM_KEYDOWN  VK_F13
    SendMessageW(h, 0x0101, (IntPtr)0x7C, IntPtr.Zero);  // WM_KEYUP
  }
  public static void Drag(IntPtr h, int x, int y) {
    IntPtr at = (IntPtr)((y << 16) | (x & 0xFFFF));
    SendMessageW(h, 0x0201, (IntPtr)1, at);   // WM_LBUTTONDOWN
    for (int i = 1; i <= 4; i++) {
      IntPtr moved = (IntPtr)(((y + i * 3) << 16) | ((x + i * 4) & 0xFFFF));
      SendMessageW(h, 0x0200, (IntPtr)1, moved);  // WM_MOUSEMOVE, button still down
    }
    SendMessageW(h, 0x0202, IntPtr.Zero, at); // WM_LBUTTONUP
  }
}
'@

$env:REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE = "1"
$env:REMOTE60_NATIVE_START_STREAM_VIEW = "1"

$h = Start-Process $Stream -PassThru -NoNewWindow `
  -ArgumentList "--transport","udp","--codec","h264","--bind-port","$Port","--input-log-every","1","--seconds","45" `
  -RedirectStandardOutput "$env:TEMP\ctrl_host.txt" -RedirectStandardError "$env:TEMP\ctrl_host_err.txt"
Start-Sleep -Seconds 3

# No --control-port: that is what selects the tunnel, and it is the path the product uses.
$v = Start-Process $Viewer -PassThru -NoNewWindow `
  -ArgumentList "--transport","udp","--codec","h264","--enable-input-channel", `
                "--host","127.0.0.1","--port","$Port","--input-log-every","1","--seconds","16" `
  -RedirectStandardOutput "$env:TEMP\ctrl_view.txt" -RedirectStandardError "$env:TEMP\ctrl_view_err.txt"
Start-Sleep -Seconds 6

[CtrlProbe]::TargetPid = $v.Id
$cb = [CtrlProbe+Cb]{ param($a, $b) return [CtrlProbe]::Locate($a, $b) }
[void][CtrlProbe]::EnumWindows($cb, [IntPtr]::Zero)

if ([CtrlProbe]::View -eq [IntPtr]::Zero) {
  # Saying so rather than skipping quietly: a probe that silently sends nothing reports the
  # same "no input" as a client that drops everything.
  "WARNING: the viewer window was not found; no clicks were sent"
} else {
  foreach ($i in 1..6) { [CtrlProbe]::Drag([CtrlProbe]::View, (200 + $i * 20), (150 + $i * 15)); Start-Sleep -Milliseconds 200 }
  foreach ($i in 1..4) { [CtrlProbe]::Key([CtrlProbe]::View); Start-Sleep -Milliseconds 200 }
}
# Let the viewer reach its own --seconds deadline instead of killing it. Its stdout is a
# redirected file, so it is block buffered: killing the process throws away whatever has not
# reached 4KB, which is exactly the last few lines this probe cares about.
[void]$v.WaitForExit(20000)
if (-not $v.HasExited) { $v.Kill(); "WARNING: the viewer had to be killed; the tail of its log is lost" }
$log = Get-Content "$env:TEMP\ctrl_view.txt" -ErrorAction SilentlyContinue
if (-not $h.HasExited) { $h.Kill() }

$tunnel   = @($log | Where-Object { $_ -match 'control tunnelled' }).Count
$connected= @($log | Where-Object { $_ -match 'control connected transport=udp-tunnel' }).Count
$inputOn  = @($log | Where-Object { $_ -match 'inputChannel=1' }).Count
$winList  = @($log | Where-Object { $_ -match 'window-list seq' }).Count
$inputAck = @($log | Where-Object { $_ -match '\[input\] ackSeq' }).Count

"tunnel configured : $tunnel"
"control connected : $connected"
"input enabled     : $inputOn"
"window lists      : $winList"
"input acks        : $inputAck"
if ($inputAck -gt 0) { ($log | Where-Object { $_ -match '\[input\] ackSeq' } | Select-Object -Last 2) }
if ($tunnel -and $connected -and $inputOn -and $winList -and $inputAck) { "VERDICT: control and input work over the tunnel" }
else { "VERDICT: INCOMPLETE" }
