p = 'C:/work/gnlink-wt/apk-prelogin-select/automation/sh_pattern_window.ps1'
s = open(p, 'rb').read().decode('utf-8-sig')
bom = open(p, 'rb').read().startswith(b'\xef\xbb\xbf')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:80])
    s = s.replace(old, new)

rep('''# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File sh_pattern_window.ps1 [-Seconds 1800] [-Static]''',
    '''# -ScrollFile <path> (apk-0232 E1 shimmer, -Layered only): while that file exists the WHOLE content scrolls up at
# -ScrollPx px per 33 ms tick (wrapping), and every pushed picture carries its number -- digits and a 16-bit stripe code
# (white = 1, black = 0, least significant on the left) in the bottom-right box -- and is logged to stdout as
# "scroll frame=<n> offset=<px> ms=<tick ms>", so a recording of the phone's output can be matched to the source frames.
# Without the file: the static picture and the pulse, as before.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File sh_pattern_window.ps1 [-Seconds 1800] [-Static]''')
rep('''      [string]$Title = 'GNLINK-SH-PATTERN', [string]$InputLog = '', [switch]$Static, [switch]$Bottom, [switch]$Layered, [int]$PulseMs = 200)''',
    '''      [string]$Title = 'GNLINK-SH-PATTERN', [string]$InputLog = '', [switch]$Static, [switch]$Bottom, [switch]$Layered, [int]$PulseMs = 200,
      [string]$ScrollFile = '', [int]$ScrollPx = 4)''')
rep('''    $pulse.Start()
    $stop.Start()
  })''', '''    $pulse.Start()
    if ($ScrollFile) {
      # the source of the scrolling picture: the content twice, one above the other (a seamless wrap)
      $script:tall = New-Object System.Drawing.Bitmap($Width, ($Height * 2), [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
      $tg = [System.Drawing.Graphics]::FromImage($script:tall)
      $tg.Clear([System.Drawing.Color]::White)
      & $draw $tg
      $tg.TranslateTransform(0, $Height)
      & $draw $tg
      $tg.Dispose()
      $script:offset = 0
      $script:frame = 0
      $script:scrolling = $false
      $script:numFont = New-Object System.Drawing.Font('Consolas', 28, [System.Drawing.FontStyle]::Bold, [System.Drawing.GraphicsUnit]::Pixel)
      $sw = [System.Diagnostics.Stopwatch]::StartNew()
      $scroll = New-Object System.Windows.Forms.Timer
      $scroll.Interval = 33
      $scroll.Add_Tick({
        $on = Test-Path $ScrollFile
        if (-not $on) {
          if ($script:scrolling) {
            # stopped: back to the static picture (the pulse keeps a window capture alive)
            $script:scrolling = $false
            [GnlinkLayered]::Push($form.Handle, $script:lbmp, $Left, $Top) | Out-Null
          }
          return
        }
        $script:scrolling = $true
        $script:frame++
        $script:offset = ($script:offset + $ScrollPx) % $Height
        $fb = New-Object System.Drawing.Bitmap($Width, $Height, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $fg = [System.Drawing.Graphics]::FromImage($fb)
        $fg.DrawImage($script:tall, (New-Object System.Drawing.Rectangle(0, 0, $Width, $Height)),
                      (New-Object System.Drawing.Rectangle(0, $script:offset, $Width, $Height)), [System.Drawing.GraphicsUnit]::Pixel)
        # the frame number: a box with digits and 16 stripes (8 px each)
        $bx = $Width - 300; $by = $Height - 70
        $fg.FillRectangle([System.Drawing.Brushes]::Black, $bx, $by, 290, 64)
        $fg.DrawString(("{0:D6}" -f $script:frame), $script:numFont, [System.Drawing.Brushes]::White, ($bx + 6), ($by + 2))
        for ($b = 0; $b -lt 16; $b++) {
          $brush = if ((($script:frame -shr $b) -band 1) -eq 1) { [System.Drawing.Brushes]::White } else { [System.Drawing.Brushes]::Gray }
          $fg.FillRectangle($brush, ($bx + 6 + $b * 17), ($by + 40), 12, 18)
        }
        $fg.Dispose()
        [GnlinkLayered]::Push($form.Handle, $fb, $Left, $Top) | Out-Null
        $fb.Dispose()
        Write-Output ("scroll frame={0} offset={1} ms={2}" -f $script:frame, $script:offset, $sw.ElapsedMilliseconds)
      })
      $scroll.Start()
    }
    $stop.Start()
  })''')
if crlf:
    s = s.replace('\n', '\r\n')
data = s.encode('utf-8')
if bom:
    data = b'\xef\xbb\xbf' + data
open(p, 'wb').write(data)
print('ok')
