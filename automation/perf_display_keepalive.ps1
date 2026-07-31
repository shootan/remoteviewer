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

# ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED
[void][DisplayKeepAlive]::SetThreadExecutionState(0x80000000 -bor 0x1 -bor 0x2)
# Wake a display that is already off: WM_SYSCOMMAND / SC_MONITORPOWER -1 to HWND_BROADCAST.
[void][DisplayKeepAlive]::SendMessageW([IntPtr]0xFFFF, 0x0112, [IntPtr]0xF170, [IntPtr](-1))

Start-Sleep -Seconds $Seconds
[void][DisplayKeepAlive]::SetThreadExecutionState(0x80000000)
