# Every keystroke must reach the host exactly once, through exactly one path.
#
# The failure this guards against: printables arriving twice -- once as committed text
# (Korean survives) and once as a raw virtual key (the host's English layout turns it into
# a trailing English letter). Typing 11 characters injected 22.
#
# The probe emulates what TranslateMessage produces for each class of key and counts what
# the host actually injected, by kind.

param(
  [string]$Viewer = "D:\remote\remote\build-local\apps\native_poc\Release\GNLinkViewer.exe",
  [string]$Stream = "D:\remote\remote\build-local\apps\native_poc\Release\GNLinkStream.exe",
  [int]$Port = 43260
)

Add-Type @'
using System;using System.Runtime.InteropServices;using System.Text;
public class KeyProbe {
  public delegate bool Cb(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(Cb c, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int m);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
  [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  public static IntPtr V = IntPtr.Zero; public static uint Pid = 0;
  public static bool Loc(IntPtr h, IntPtr l) {
    uint o=0; GetWindowThreadProcessId(h, out o); if (o != Pid) return true;
    StringBuilder sb=new StringBuilder(256); GetClassNameW(h,sb,256);
    if (sb.ToString()=="Remote60NativeVideoClient") V=h; return true; }
  // What TranslateMessage produces for a printable: down, char, up.
  public static void Printable(IntPtr h, int vk, char ch) {
    SendMessageW(h, 0x0100, (IntPtr)vk, IntPtr.Zero);
    SendMessageW(h, 0x0102, (IntPtr)ch, IntPtr.Zero);
    SendMessageW(h, 0x0101, (IntPtr)vk, IntPtr.Zero);
  }
  // Enter also yields a WM_CHAR ('\r'); the fix must deliver it once, as the key.
  public static void Enter(IntPtr h) { Printable(h, 0x0D, '\r'); }
  // Navigation keys yield no char at all.
  public static void Bare(IntPtr h, int vk) {
    SendMessageW(h, 0x0100, (IntPtr)vk, IntPtr.Zero);
    SendMessageW(h, 0x0101, (IntPtr)vk, IntPtr.Zero);
  }
}
'@

$env:REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE = "1"
$env:REMOTE60_NATIVE_START_STREAM_VIEW = "1"

# The injection target deliberately does not exist. Injecting for real on a single machine
# feeds the host's output back into the viewer's input -- each 'a' typed became an 'a'
# injected became an 'a' typed, 212 deep before the first run was stopped. A target that
# resolves to nothing still logs every arriving event as no-target, which is all the count
# needs, and nothing lands on the desktop of whoever runs this.
$h = Start-Process $Stream -PassThru -NoNewWindow `
  -ArgumentList "--transport","udp","--codec","h264","--bind-port","$Port", `
                "--enable-input-injection","--input-target-title","GNLinkKeyProbeSinkNone", `
                "--input-log-every","1","--seconds","40" `
  -RedirectStandardOutput "$env:TEMP\key_host.txt" -RedirectStandardError "$env:TEMP\key_host_err.txt"
Start-Sleep -Seconds 3

$v = Start-Process $Viewer -PassThru -NoNewWindow `
  -ArgumentList "--transport","udp","--codec","h264","--enable-input-channel", `
                "--host","127.0.0.1","--port","$Port","--input-log-every","1","--seconds","16" `
  -RedirectStandardOutput "$env:TEMP\key_view.txt" -RedirectStandardError "$env:TEMP\key_view_err.txt"
Start-Sleep -Seconds 6

[KeyProbe]::Pid = $v.Id
$cb = [KeyProbe+Cb]{ param($a,$b) return [KeyProbe]::Loc($a,$b) }
[void][KeyProbe]::EnumWindows($cb,[IntPtr]::Zero)
if ([KeyProbe]::V -eq [IntPtr]::Zero) { "viewer window NOT found"; if(-not $v.HasExited){$v.Kill()}; if(-not $h.HasExited){$h.Kill()}; exit 1 }

foreach ($i in 1..5) { [KeyProbe]::Printable([KeyProbe]::V, 0x41, 'a'); Start-Sleep -Milliseconds 120 }  # 'a' x5
foreach ($i in 1..3) { [KeyProbe]::Enter([KeyProbe]::V); Start-Sleep -Milliseconds 120 }                 # Enter x3
foreach ($i in 1..4) { [KeyProbe]::Bare([KeyProbe]::V, 0x25); Start-Sleep -Milliseconds 120 }            # Left x4

[void]$v.WaitForExit(20000); if (-not $v.HasExited) { $v.Kill() }
if (-not $h.HasExited) { $h.Kill() }

# Every physical key -- letters included -- now takes the key-event path; only IME composition
# results are text. So the a's, the Enters, and the arrows are all keydowns (12 total), there
# is no [input-text] at all, and a double-send would push keydowns past 12.
# The session-start reset emits 11 modifier ups first, so keyups run ahead of keydowns.
$hostLog = Get-Content "$env:TEMP\key_host.txt" -ErrorAction SilentlyContinue
$textArrived = @($hostLog | Where-Object { $_ -match '\[input-text\]' }).Count
$keyDowns    = @($hostLog | Where-Object { $_ -match '\[input\] .*kind=5' }).Count
$keyUps      = @($hostLog | Where-Object { $_ -match '\[input\] .*kind=6' }).Count

"text units arrived : $textArrived   (expect 0: nothing composes; letters go as keys now)"
"keydowns arrived   : $keyDowns   (expect 12: 5 a + 3 Enter + 4 Left; >12 means a double-send)"
"keyups arrived     : $keyUps   (expect >=12: 12 releases + 11 session-start modifier ups)"
if ($textArrived -eq 0 -and $keyDowns -eq 12 -and $keyUps -ge 12) {
  "VERDICT: each keystroke reaches the host exactly once, as a key event"
} else {
  "VERDICT: INCOMPLETE"
}
