# Keeps the display on while a baseline runs.
#
# WGC only delivers frames the compositor produces; once the display powers down, capture
# drops to a few frames per second and every number collected is garbage. This holds
# ES_DISPLAY_REQUIRED (no synthetic input involved) for the requested duration.
param([int]$Seconds = 120)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class DisplayKeepAlive {
  [DllImport("kernel32.dll")] public static extern uint SetThreadExecutionState(uint flags);
  [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr hwnd, uint msg, IntPtr w, IntPtr l);
}
"@

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class WakeInput {
  // Exact x64 layout: INPUT is 4-byte type + 4 padding + the 32-byte mouse union = 40.
  // Anything else makes SendInput fail with ERROR_INVALID_PARAMETER and the display never
  // wakes -- silently, because nobody checks.
  [StructLayout(LayoutKind.Sequential)]
  public struct INPUT { public uint type; public MOUSEINPUT mi; }
  [StructLayout(LayoutKind.Sequential)]
  public struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr dwExtraInfo; }
  [DllImport("user32.dll", SetLastError=true)] public static extern uint SendInput(uint n, INPUT[] inputs, int size);
  public static void Jiggle() {
    // One pixel out and back: a zero-delta move is coalesced away and never counts as
    // input, so the display stays asleep. This touches no window and presses nothing.
    var i = new INPUT(); i.type = 0; i.mi.dwFlags = 0x0001; i.mi.dx = 1; i.mi.dy = 0;
    SendInput(1, new INPUT[]{ i }, Marshal.SizeOf(typeof(INPUT)));
    i.mi.dx = -1;
    SendInput(1, new INPUT[]{ i }, Marshal.SizeOf(typeof(INPUT)));
  }
}
"@

# ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED
[void][DisplayKeepAlive]::SetThreadExecutionState(0x80000000 -bor 0x1 -bor 0x2)
# ES_DISPLAY_REQUIRED prevents the display turning off but never turns it back on; an
# already-dark display (which starves both WGC and DXGI duplication) needs a wake.
[WakeInput]::Jiggle()
[void][DisplayKeepAlive]::SendMessageW([IntPtr]0xFFFF, 0x0112, [IntPtr]0xF170, [IntPtr](-1))

$deadline = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $deadline) {
  Start-Sleep -Seconds 240
  # Re-assert both: some display drivers drop ES holds across power transitions.
  [void][DisplayKeepAlive]::SetThreadExecutionState(0x80000000 -bor 0x1 -bor 0x2)
  [WakeInput]::Jiggle()
}
[void][DisplayKeepAlive]::SetThreadExecutionState(0x80000000)
