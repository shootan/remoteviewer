# Works the GNLink Host window from outside, through UI Automation.
#
# Started by apps/directory/test/host_login_ui_runner.js against GNLinkHostUiTest.exe -- the
# product's window in an asInvoker build (see host_app_ui_test.cpp for what that build replaces).
# It is never pointed at the installed GNLinkHost: that one is elevated, so UI Automation from an
# ordinary session cannot reach it, and this script takes a process id rather than looking for a
# window by name.
#
#   -Expect signin     the sign-in form is up. Lists what it shows, photographs it, and -- when
#                      -Account and -Password are given -- types them and presses Sign in.
#   -Expect signedin   the status card is up without anything having been typed.
#
# UI Automation supplies the tree, what is on screen and where. On this window it reports every
# control as a Pane with no patterns, so a control is told apart by its window class (Edit,
# Button, Static) and worked with the messages a keyboard and mouse produce for it: WM_SETTEXT
# into a field, WM_LBUTTONDOWN/UP on the button. That is input at the message level. It is not
# a physical mouse or keyboard, and nothing is sent to whatever window has the foreground.
#
# The password is the fixture server's test account. Nothing here reads or types a real one.
#
# Prints PASS/FAIL lines and a last line "host_login_uia: ALL PASS" or "... FAIL"; exit 0 or 1.
# Exit 3 (INVALID) when a window of ANOTHER program still lies over the Sign in button after the
# test window was raised: the test could not look, which is not the product failing. Its identity
# is printed. Nothing is done to that window -- it is the user's, and it is left where it is.

param(
  [Parameter(Mandatory = $true)][int]$ProcessId,
  [Parameter(Mandatory = $true)][ValidateSet('signin', 'signedin')][string]$Expect,
  [string]$Account = '',
  [string]$Password = '',
  [Parameter(Mandatory = $true)][string]$Shot,
  [string]$ShotAfter = '',
  [int]$TimeoutSec = 30
)

$ErrorActionPreference = 'Stop'

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class GnlinkHostUia {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", EntryPoint = "GetWindowLongW")] public static extern int GetWindowLong(IntPtr h, int index);
  [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "SendMessageW")]
  public static extern IntPtr SendText(IntPtr h, uint msg, IntPtr w, string text);
  [DllImport("user32.dll", EntryPoint = "PostMessageW")]
  public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(POINT p);
  [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr h, uint flags);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)]
  public static extern int GetClassNameW(IntPtr h, System.Text.StringBuilder text, int max);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)]
  public static extern int GetWindowTextW(IntPtr h, System.Text.StringBuilder text, int max);
  [DllImport("user32.dll")]
  public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
}
'@
# Before anything asks for a rectangle: an unaware process is handed scaled coordinates, and the
# picture and the hit test would then describe a window of a different size.
[void][GnlinkHostUia]::SetProcessDPIAware()
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes, System.Drawing

$script:failed = 0
function Check([bool]$ok, [string]$label) {
  if ($ok) { Write-Output "PASS $label" } else { Write-Output "FAIL $label"; $script:failed++ }
}

$AE = [System.Windows.Automation.AutomationElement]
$CT = [System.Windows.Automation.ControlType]
$Scope = [System.Windows.Automation.TreeScope]

function Find-HostWindow {
  $byProcess = New-Object System.Windows.Automation.PropertyCondition($AE::ProcessIdProperty, $ProcessId)
  $byClass = New-Object System.Windows.Automation.PropertyCondition($AE::ClassNameProperty, 'Remote60HostApp')
  $both = New-Object System.Windows.Automation.AndCondition($byProcess, $byClass)
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  while ((Get-Date) -lt $deadline) {
    $w = $AE::RootElement.FindFirst($Scope::Children, $both)
    if ($w -and -not $w.Current.IsOffscreen) { return $w }
    Start-Sleep -Milliseconds 200
  }
  return $null
}

# Everything in the window that has a box on screen. A control the window has hidden (ShowWindow
# SW_HIDE) is either absent from the tree or reported off screen; both are left out.
function Get-Shown($window) {
  $all = $window.FindAll($Scope::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
  $shown = @()
  foreach ($e in $all) {
    $c = $e.Current
    if ($c.IsOffscreen) { continue }
    $r = $c.BoundingRectangle
    if ($r.IsEmpty -or $r.Width -le 0 -or $r.Height -le 0) { continue }
    $h = [IntPtr]$c.NativeWindowHandle
    $style = [GnlinkHostUia]::GetWindowLong($h, -16)
    $type = $c.ClassName
    if ($type -eq 'Static') { $type = 'Text' }
    # BS_AUTOCHECKBOX is 3 in the low nibble of a button's style.
    if ($type -eq 'Button' -and ($style -band 0xF) -eq 3) { $type = 'CheckBox' }
    $shown += [pscustomobject]@{
      Element  = $e
      Hwnd     = $h
      Type     = $type
      Name     = $c.Name
      Id       = $c.AutomationId
      Enabled  = $c.IsEnabled
      Password = ($type -eq 'Edit' -and ($style -band 0x20) -ne 0)  # ES_PASSWORD
      Rect     = $r
    }
  }
  # A field's name is the caption in front of it -- which is how a person knows what to type.
  for ($i = 0; $i -lt $shown.Count; $i++) {
    if ($shown[$i].Type -eq 'Edit') {
      $shown[$i] | Add-Member -NotePropertyName Value -NotePropertyValue $shown[$i].Name
      $shown[$i].Name = if ($i -gt 0 -and $shown[$i - 1].Type -eq 'Text') { $shown[$i - 1].Name } else { '' }
    }
  }
  return $shown
}

function Save-Shot($window, [string]$path) {
  $h = [IntPtr]$window.Current.NativeWindowHandle
  $r = New-Object GnlinkHostUia+RECT
  if (-not [GnlinkHostUia]::GetWindowRect($h, [ref]$r)) { return $false }
  $w = $r.R - $r.L; $ht = $r.B - $r.T
  if ($w -le 0 -or $ht -le 0) { return $false }
  $bmp = New-Object System.Drawing.Bitmap($w, $ht)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $dc = $g.GetHdc()
  # 2 = PW_RENDERFULLCONTENT: what the window draws, whether or not something is in front of it.
  $ok = [GnlinkHostUia]::PrintWindow($h, $dc, 2)
  $g.ReleaseHdc($dc); $g.Dispose()
  if ($ok) { $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png) }
  $bmp.Dispose()
  return $ok
}

function Write-Controls($shown) {
  foreach ($s in $shown) {
    if ($s.Type -in 'Edit', 'Button', 'CheckBox', 'Text') {
      Write-Output ("      ({0,-8} id={1,-5} enabled={2,-5} name='{3}')" -f $s.Type, $s.Id, $s.Enabled, ($s.Name -replace "\r?\n", ' '))
    }
  }
}

$window = Find-HostWindow
Check ($null -ne $window) "the host window of process $ProcessId is on screen"
if ($null -eq $window) { Write-Output 'host_login_uia: FAIL'; exit 1 }

if ($Expect -eq 'signin') {
  $shown = Get-Shown $window
  Write-Controls $shown
  $edits = @($shown | Where-Object { $_.Type -eq 'Edit' })
  $editNames = ($edits | ForEach-Object { $_.Name }) -join '|'
  Write-Output "      (input fields shown: $editNames)"
  # The PC's name is not a credential and has a default; it was on this form before and stays.
  Check ($editNames -eq "ID|Password|This PC's name") 'the sign-in form shows ID, Password and the PC name, and no other input field'
  $serverish = @($shown | Where-Object { $_.Name -match 'server|advanced|address|https?:' })
  Check ($serverish.Count -eq 0) 'nothing shown on the sign-in form names a server or an advanced setting'
  Check ([bool](Save-Shot $window $Shot)) "the sign-in form is photographed: $Shot"

  if ($Account -ne '') {
    $id = $edits | Where-Object { $_.Name -eq 'ID' } | Select-Object -First 1
    $pw = $edits | Where-Object { $_.Name -eq 'Password' } | Select-Object -First 1
    $button = $shown | Where-Object { $_.Type -eq 'Button' -and $_.Name -eq 'Sign in' } | Select-Object -First 1
    Check ($null -ne $id -and $null -ne $pw -and $null -ne $button) 'ID, Password and the Sign in button are all present'
    if ($null -eq $id -or $null -eq $pw -or $null -eq $button) { Write-Output 'host_login_uia: FAIL'; exit 1 }

    Check ($id.Enabled -and $pw.Enabled -and $pw.Password -and -not $id.Password) 'both fields accept input, and the password field hides what is typed'
    [void][GnlinkHostUia]::SendText($id.Hwnd, 0x000C, [IntPtr]::Zero, $Account)   # WM_SETTEXT
    [void][GnlinkHostUia]::SendText($pw.Hwnd, 0x000C, [IntPtr]::Zero, $Password)
    Check ($id.Element.Current.Name -eq $Account) 'the ID field holds what was typed'

    # "Can be pressed": enabled, inside the window, and the thing found at its centre is itself.
    $wr = $window.Current.BoundingRectangle
    $br = $button.Rect
    $cx = $br.X + $br.Width / 2; $cy = $br.Y + $br.Height / 2
    $insideWindow = $cx -ge $wr.X -and $cx -le ($wr.X + $wr.Width) -and $cy -ge $wr.Y -and $cy -le ($wr.Y + $wr.Height)
    Check ($button.Enabled -and $insideWindow) 'the Sign in button is enabled and lies inside the window'
    # The test's own window is raised first, without taking the focus: the desktop this runs on
    # is somebody's, and their windows (2026-09-29: GMux, the program these sessions run in) can
    # be in front of it. Topmost for as long as it is looked at and pressed, then back.
    $hostHwnd = [IntPtr]$window.Current.NativeWindowHandle
    $noMoveNoSizeNoActivate = 0x0002 -bor 0x0001 -bor 0x0010
    [void][GnlinkHostUia]::SetWindowPos($hostHwnd, [IntPtr](-1), 0, 0, 0, 0, $noMoveNoSizeNoActivate)   # HWND_TOPMOST
    Start-Sleep -Milliseconds 150
    $pt = New-Object GnlinkHostUia+POINT
    $pt.X = [int]$cx; $pt.Y = [int]$cy
    $atPoint = [GnlinkHostUia]::WindowFromPoint($pt)
    $coveredByOther = $false
    if ($atPoint -ne $button.Hwnd) {
      # Said at the moment it is seen: by the time anyone looks, the window in the way is gone.
      Write-Output "      (at the button's centre: window $atPoint, not the button $($button.Hwnd))"
      foreach ($h in @($atPoint, [GnlinkHostUia]::GetAncestor($atPoint, 2))) {   # 2 = GA_ROOT
        if ($h -eq [IntPtr]::Zero) { continue }
        $class = New-Object System.Text.StringBuilder 256
        $title = New-Object System.Text.StringBuilder 256
        [void][GnlinkHostUia]::GetClassNameW($h, $class, 256)
        [void][GnlinkHostUia]::GetWindowTextW($h, $title, 256)
        [uint32]$ownerPid = 0
        [void][GnlinkHostUia]::GetWindowThreadProcessId($h, [ref]$ownerPid)
        $exe = ''
        try { $exe = (Get-Process -Id $ownerPid -ErrorAction Stop).Path } catch { $exe = '(could not be read)' }
        Write-Output ("      (in the way: hwnd={0} class='{1}' title='{2}' pid={3} exe={4})" -f $h, $class, $title, $ownerPid, $exe)
        if ($ownerPid -ne 0 -and $ownerPid -ne $ProcessId) { $coveredByOther = $true }
      }
    }
    if ($coveredByOther) {
      # Another program's window, still in front after ours was raised. The button was not
      # looked at, so nothing is said about it either way.
      [void][GnlinkHostUia]::SetWindowPos($hostHwnd, [IntPtr](-2), 0, 0, 0, 0, $noMoveNoSizeNoActivate)   # HWND_NOTOPMOST
      Write-Output 'host_login_uia: INVALID (another program''s window covers the Sign in button; see "in the way" above)'
      exit 3
    }
    # A window or control of the product's own process in the way is the product's doing: FAIL.
    Check ($atPoint -eq $button.Hwnd) 'nothing lies over the Sign in button'

    # Left button down and up in the middle of the button, in its own coordinates.
    $l = [IntPtr]((([int]($br.Height / 2)) -shl 16) -bor ([int]($br.Width / 2)))
    [void][GnlinkHostUia]::PostMessage($button.Hwnd, 0x0201, [IntPtr]1, $l)   # WM_LBUTTONDOWN
    [void][GnlinkHostUia]::PostMessage($button.Hwnd, 0x0202, [IntPtr]::Zero, $l)   # WM_LBUTTONUP
    [void][GnlinkHostUia]::SetWindowPos($hostHwnd, [IntPtr](-2), 0, 0, 0, 0, $noMoveNoSizeNoActivate)   # HWND_NOTOPMOST
    $Expect = 'signedin'
    $Shot = $ShotAfter
  }
}

if ($Expect -eq 'signedin') {
  $deadline = (Get-Date).AddSeconds($TimeoutSec)
  $card = $null
  do {
    $shown = Get-Shown $window
    $card = $shown | Where-Object { $_.Type -eq 'Text' -and $_.Name -like 'Account:*' } | Select-Object -First 1
    if ($card) { break }
    Start-Sleep -Milliseconds 300
  } while ((Get-Date) -lt $deadline)
  Write-Controls $shown
  Check ($null -ne $card) 'the status card is shown'
  if ($card) { Write-Output ("      (status card: {0})" -f $card.Name) }
  if ($Account -ne '') {
    Check ($card -and ($card.Name -replace '^Account:\s*', '') -eq $Account) "the card names the account that signed in: $Account"
  }
  $edits = @($shown | Where-Object { $_.Type -eq 'Edit' })
  Check ($edits.Count -eq 0) 'no input field is shown once signed in'
  $signOut = $shown | Where-Object { $_.Type -eq 'Button' -and $_.Name -eq 'Sign out' } | Select-Object -First 1
  Check ($null -ne $signOut -and $signOut.Enabled) 'Sign out is offered'
  if ($Shot -ne '') { Check ([bool](Save-Shot $window $Shot)) "the status card is photographed: $Shot" }
}

if ($script:failed -eq 0) { Write-Output 'host_login_uia: ALL PASS'; exit 0 }
Write-Output "host_login_uia: FAIL ($($script:failed) failed)"
exit 1
