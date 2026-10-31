# Photographs the real viewer window in the states it can reach without a session.
#
# Runs at user privilege -- no elevation, and nothing the user is using is touched. The viewer is
# pointed at a port nobody is listening on, deliberately: a GNLink host IS running on this machine
# and connecting to it would put a second viewer into the session the user has open.
#
# What this can show: the states before a host answers. Anything with a window list in it needs a
# live session and is out of reach here; that limit is printed rather than implied.

param(
  [string]$Exe = "build-local\apps\native_poc\Release\GNLinkViewer.exe",
  [string]$OutDir = ".claude\ui-preview",
  [int]$DeadPort = 43999
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

Add-Type -Namespace Win32 -Name Shot -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
public struct RECT { public int Left, Top, Right, Bottom; }
'@

function Save-Window([IntPtr]$hwnd, [string]$path) {
  $r = New-Object Win32.Shot+RECT
  if (-not [Win32.Shot]::GetWindowRect($hwnd, [ref]$r)) { return $false }
  $w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top
  if ($w -le 0 -or $h -le 0) { return $false }
  $bmp = New-Object System.Drawing.Bitmap($w, $h)
  $gfx = [System.Drawing.Graphics]::FromImage($bmp)
  $hdc = $gfx.GetHdc()
  # 2 = PW_RENDERFULLCONTENT first; some windows answer only the plain form, and this one draws
  # its picker with GDI while a swapchain composites on top, so both are worth asking.
  $ok = [Win32.Shot]::PrintWindow($hwnd, $hdc, 2)
  if (-not $ok) { $ok = [Win32.Shot]::PrintWindow($hwnd, $hdc, 0) }
  if (-not $ok) { $ok = [Win32.Shot]::PrintWindow($hwnd, $hdc, 1) }
  $gfx.ReleaseHdc($hdc); $gfx.Dispose()
  if ($ok) { $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png) }
  $bmp.Dispose()
  return $ok
}

if (-not (Test-Path $Exe)) { Write-Output "missing $Exe"; exit 1 }
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory $OutDir | Out-Null }

$live = Get-CimInstance Win32_Process -Filter "Name LIKE 'GNLink%'" | Select-Object -Expand ProcessId
Write-Output ("live GNLink pids before: " + ($live -join ",") + "  (none of these are touched)")

$p = Start-Process $Exe -ArgumentList "--host","127.0.0.1","--port","$DeadPort" -PassThru
Write-Output "started viewer pid $($p.Id) -> 127.0.0.1:$DeadPort (nobody listening, on purpose)"

$hwnd = [IntPtr]::Zero
for ($i = 0; $i -lt 100; $i++) {
  Start-Sleep -Milliseconds 200
  $p.Refresh()
  if ($p.HasExited) { Write-Output "viewer exited early, code $($p.ExitCode)"; break }
  if ($p.MainWindowHandle -ne [IntPtr]::Zero -and [Win32.Shot]::IsWindowVisible($p.MainWindowHandle)) {
    $hwnd = $p.MainWindowHandle
    break
  }
}

if ($hwnd -ne [IntPtr]::Zero) {
  Write-Output "window title: '$($p.MainWindowTitle)'"
  # Shot straight away and then a few more times. Without a host answering the window does not
  # stay long, so a settle-then-capture ordering can end up photographing nothing at all.
  # A sequence, not one frame. The first capture can land before anything has been painted, and
  # "it was black once" is not the same claim as "it stays black".
  $wrote = 0
  for ($k = 0; $k -lt 30; $k++) {
    $p.Refresh()
    if ($p.HasExited) { Write-Output "viewer exited after $([int]($k*120))ms, code $($p.ExitCode)"; break }
    $out = Join-Path $OutDir ("product-viewer-connecting-{0:d2}.png" -f $k)
    if (Save-Window $hwnd $out) { $wrote++ } 
    Start-Sleep -Milliseconds 120
  }
  Write-Output "wrote $wrote frames over $([int]($wrote*120))ms"
} else {
  Write-Output "no visible viewer window appeared"
}

if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; Write-Output "stopped the viewer I started (pid $($p.Id))" }

$after = Get-CimInstance Win32_Process -Filter "Name LIKE 'GNLink%'" | Select-Object -Expand ProcessId
Write-Output ("live GNLink pids after : " + ($after -join ","))
Write-Output "LIMIT: only states reachable without a host answering. A populated window list,"
Write-Output "       selection progress and the toolbar's path badge all need a live session."
