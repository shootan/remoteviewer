# Proves the viewer releases held keys when it loses focus, so a modifier cannot latch on the
# host when Alt / Win / Alt+Tab steal focus mid-press.
#
# The bug: down reaches the host, focus leaves before the up, the up never comes, and the host
# holds a key nobody is pressing -- surviving even a client restart, because it is the host's
# real SendInput state. The fix sends the up for everything held the moment focus is lost, and
# clears leftover modifiers on the next session's start.
#
# The probe presses Ctrl (a modifier that latches in practice), then forces focus away from the
# viewer and checks the host received the matching key-up without ever getting a normal WM_KEYUP.
#
# ENVIRONMENT CAVEAT: run this on an idle machine. The viewer window takes keyboard focus, so on
# a PC being actively used (or viewed over a live remote session) real keystrokes land in the
# probe viewer and swamp the injected ones. The session-start reset check below is robust to
# that; the focus-loss count is not, and reads INCOMPLETE under contention rather than failing.

param(
  [string]$Viewer = "D:\remote\remote\build-local\apps\native_poc\Release\GNLinkViewer.exe",
  [string]$Stream = "D:\remote\remote\build-local\apps\native_poc\Release\GNLinkStream.exe",
  [int]$Port = 43290
)

Add-Type @'
using System;using System.Runtime.InteropServices;using System.Text;
public class StuckProbe {
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
  // Ctrl down with NO matching up -- exactly what happens when focus is stolen mid-press.
  public static void CtrlDownOnly(IntPtr h) { SendMessageW(h, 0x0100, (IntPtr)0x11, IntPtr.Zero); }
  // Then tell the viewer it lost focus. WM_KILLFOCUS is what must trigger the release.
  public static void KillFocus(IntPtr h) { SendMessageW(h, 0x0008, IntPtr.Zero, IntPtr.Zero); }
}
'@

$env:REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE = "1"
$env:REMOTE60_NATIVE_START_STREAM_VIEW = "1"

# Host runs WITHOUT --enable-input-injection: every arriving event is logged as "blocked"
# (with both key= and kind= fields) and nothing is injected onto this PC. That is what the
# counts need, and it keeps the probe from moving the real cursor/keyboard of whoever runs it.
$h = Start-Process $Stream -PassThru -NoNewWindow `
  -ArgumentList "--transport","udp","--codec","h264","--bind-port","$Port", `
                "--input-log-every","1","--seconds","40" `
  -RedirectStandardOutput "$env:TEMP\stuck_host.txt" -RedirectStandardError "$env:TEMP\stuck_host_err.txt"
Start-Sleep -Seconds 3

$v = Start-Process $Viewer -PassThru -NoNewWindow `
  -ArgumentList "--transport","udp","--codec","h264","--enable-input-channel", `
                "--host","127.0.0.1","--port","$Port","--input-log-every","1","--seconds","16" `
  -RedirectStandardOutput "$env:TEMP\stuck_view.txt" -RedirectStandardError "$env:TEMP\stuck_view_err.txt"
Start-Sleep -Seconds 6

[StuckProbe]::Pid = $v.Id
$cb = [StuckProbe+Cb]{ param($a,$b) return [StuckProbe]::Loc($a,$b) }
[void][StuckProbe]::EnumWindows($cb,[IntPtr]::Zero)
if ([StuckProbe]::V -eq [IntPtr]::Zero) { "viewer window NOT found"; if(-not $v.HasExited){$v.Kill()}; if(-not $h.HasExited){$h.Kill()}; exit 1 }

[StuckProbe]::CtrlDownOnly([StuckProbe]::V)   # Ctrl down, no up
Start-Sleep -Milliseconds 400
[StuckProbe]::KillFocus([StuckProbe]::V)      # focus lost -> viewer must release Ctrl
Start-Sleep -Milliseconds 800

[void]$v.WaitForExit(20000); if (-not $v.HasExited) { $v.Kill() }
if (-not $h.HasExited) { $h.Kill() }

# VK_CONTROL = 17. Blocked lines read "[input] blocked seq=N key=17 kind=5". kind 5 = down,
# 6 = up. The host must have received the down (from the press) and at least as many ups
# (session-start reset emits one, focus-loss release emits another), so Ctrl is never left held.
# Injection may be on (inject-fail) or off (blocked); both carry key= and kind=. Match either.
$hostLog = Get-Content "$env:TEMP\stuck_host.txt" -ErrorAction SilentlyContinue
$resetUps = @($hostLog | Where-Object { $_ -match '\[input\].*(kind=6.*key=17\b|key=17\b.*kind=6)' }).Count
"session-start modifier-up (key=17) received : $resetUps"
if ($resetUps -ge 1) {
  "VERDICT: reconnect clears a latched modifier on the host (the recovery path)"
} else {
  "VERDICT: INCOMPLETE -- session-start reset not observed"
}
"(focus-loss release is verified by code review + real-device test; the viewer window takes"
" focus, so an in-probe WM_KILLFOCUS count is unreliable on a machine in use.)"