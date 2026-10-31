$slot = 'C:\work\gnlink-wt\SLOT_REQUEST.txt'
while ($true) {
  if (-not (Test-Path $slot)) {
    try {
      $fs = [IO.File]::Open($slot, 'CreateNew', 'Write')
      $bb = [Text.Encoding]::UTF8.GetBytes('remote_작업용2 ka-pc-impl r3 e2e ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
      $fs.Write($bb, 0, $bb.Length); $fs.Close()
      break
    } catch { }
  }
  Start-Sleep -Seconds 15
}
"slot taken " + (Get-Date -Format 'HH:mm:ss')
$bin = 'C:\work\gnlink-wt\ka-pc-build\apps\native_poc\Release'
$page = "$bin\ui\shell.html"
$cur = 'D:\remote\remote\.claude\np_impl_logs\shell_html_705ed23.html'
$old = 'D:\remote\remote\.claude\np_impl_logs\shell_html_a864105.html'
try {
  $env:REMOTE60_KA_REPO = 'C:\work\gnlink-wt\ka-pc'
  foreach ($r in @('shell:r3-3', 'shell:r3neg-page')) {
    $kind, $tag = $r.Split(':')
    if ($tag -eq 'r3neg-page') { Copy-Item $old $page -Force; "page swapped to a864105: " + (Get-FileHash $page).Hash.Substring(0,16) }
    $env:REMOTE60_TEST_TMP = "D:\remote\remote\.claude\test-tmp\ka-$kind-$tag"
    $log = "D:\remote\remote\.claude\np_impl_logs\ka_${kind}_$tag.log"
    & "$bin\remote60_ka_pc_shell_e2e_test.exe" *> $log
    $rc = $LASTEXITCODE
    $l = Get-Content $log
    "$r rc=$rc PASS=" + ($l | Select-String '^PASS').Count + " FAIL=" + ($l | Select-String '^FAIL').Count
  }
} finally {
  Copy-Item $cur $page -Force
  "page restored: " + (Get-FileHash $page).Hash.Substring(0,16)
  Remove-Item -LiteralPath $slot -Force
  "slot released " + (Get-Date -Format 'HH:mm:ss')
}
