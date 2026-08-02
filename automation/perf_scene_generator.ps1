# Synthetic capture scenes for performance baselines.
#
# A baseline is only comparable when the screen content is the same shape every run. Asking a
# person to "scroll something" produces a different scene every time, so this paints one:
#   scroll  - a document of numbered text lines moving up at a constant rate
#   video   - full-window frame cycle where every pixel changes every frame
#
# Everything is pre-rendered into bitmaps once; each paint is one or two DrawImage calls.
# Drawing per frame in PowerShell (40+ GDI+ calls) takes ~250ms and silently turns a "30fps"
# scene into 4fps, which poisoned the first baseline attempt.
param(
  [ValidateSet("scroll", "video")]
  [string]$Scene = "scroll",
  [int]$Seconds = 30,
  [ValidateRange(1, 240)]
  [int]$Fps = 30,
  [int]$Width = 1600,
  [int]$Height = 900
)

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

# ---- pre-render ----------------------------------------------------------
$frames = @()
$doc = $null
$docH = 0
if ($Scene -eq "scroll") {
  $lineH = 26
  $lines = 120
  $docH = $lines * $lineH
  $doc = New-Object System.Drawing.Bitmap($Width, $docH)
  $g = [System.Drawing.Graphics]::FromImage($doc)
  $g.Clear([System.Drawing.Color]::White)
  $font = New-Object System.Drawing.Font("Consolas", 14)
  for ($i = 0; $i -lt $lines; $i++) {
    $text = ("{0:d6}  the quick brown fox jumps over the lazy dog 0123456789 ~!@#`$%^&*()" -f $i)
    $g.DrawString($text, $font, [System.Drawing.Brushes]::Black, 8, ($i * $lineH))
  }
  $font.Dispose()
  $g.Dispose()
} else {
  # 16 pre-rendered frames cycled forever: every pixel differs between consecutive frames.
  $rand = New-Object System.Random(12345)
  for ($f = 0; $f -lt 16; $f++) {
    $bmp = New-Object System.Drawing.Bitmap($Width, $Height)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $bw = [int]($Width / 8)
    $bh = [int]($Height / 6)
    for ($ry = 0; $ry -lt 6; $ry++) {
      for ($rx = 0; $rx -lt 8; $rx++) {
        $c = [System.Drawing.Color]::FromArgb(255, (($f * 31) + $rx * 29) % 256, (($f * 53) + $ry * 41) % 256, $rand.Next(0, 256))
        $b = New-Object System.Drawing.SolidBrush($c)
        $g.FillRectangle($b, $rx * $bw, $ry * $bh, $bw, $bh)
        $b.Dispose()
      }
    }
    $bigFont = New-Object System.Drawing.Font("Segoe UI", 22, [System.Drawing.FontStyle]::Bold)
    $g.DrawString(("VIDEO SCENE frame {0}" -f $f), $bigFont, [System.Drawing.Brushes]::White, 16, 12)
    $bigFont.Dispose()
    $g.Dispose()
    $frames += ,$bmp
  }
}

# ---- window --------------------------------------------------------------
$form = New-Object System.Windows.Forms.Form
$form.Text = "GNLink perf scene: $Scene ${Fps}fps"
$form.StartPosition = "Manual"
$form.Location = New-Object System.Drawing.Point(60, 60)
$form.Size = New-Object System.Drawing.Size($Width, $Height)
$form.FormBorderStyle = "None"
$form.TopMost = $true
# DoubleBuffered is protected; set it through reflection so repaints do not flicker.
$prop = [System.Windows.Forms.Control].GetProperty("DoubleBuffered", [System.Reflection.BindingFlags]"Instance,NonPublic")
$prop.SetValue($form, $true, $null)

$script:tick = 0
$scrollStep = [Math]::Max(1, [int][Math]::Round(120.0 / [double]$Fps))

$form.Add_Paint({
  param($s, $e)
  $gfx = $e.Graphics
  if ($Scene -eq "scroll") {
    # Keep scroll velocity near 120px/s at any requested refresh rate. Two blits make the
    # document loop seamless.
    $offset = ($script:tick * $scrollStep) % $docH
    $gfx.DrawImage($doc, 0, (-$offset))
    $gfx.DrawImage($doc, 0, ($docH - $offset))
  } else {
    $gfx.DrawImage($frames[$script:tick % 16], 0, 0)
  }
})

$timer = New-Object System.Windows.Forms.Timer
# WinForms rounds a 17ms 60fps request down to roughly 55-58 delivered paints once message
# dispatch overhead is included. Use the floor (16ms at 60fps); DWM still caps presentation
# to the display refresh, while the source no longer starves a 60fps capture measurement.
$timer.Interval = [Math]::Max(1, [int][Math]::Floor(1000.0 / [double]$Fps))
$timer.Add_Tick({
  $script:tick += 1
  $form.Invalidate()
  if ($Seconds -gt 0 -and $script:tick -gt ($Seconds * $Fps)) { $form.Close() }
})

$form.Add_Shown({ $timer.Start() })
[void]$form.ShowDialog()
$timer.Dispose()
if ($null -ne $doc) { $doc.Dispose() }
foreach ($f in $frames) { $f.Dispose() }
