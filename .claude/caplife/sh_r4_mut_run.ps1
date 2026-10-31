$ErrorActionPreference = 'Continue'
$wt = 'C:\work\gnlink-wt\apk-prelogin-select\apps\android_direct_client\app\src\main\java\com\remote60\androiddirect'
$files = "$wt\SharpenRenderer.kt", "$wt\MainActivity.kt"
$base = $files | ForEach-Object { (Get-FileHash $_).Hash }
foreach ($pair in @(@('Ma', 'strength_ab'), @('Mb', 'strength_ab'), @('Mc', 'layout_b'))) {
  $m = $pair[0]; $sc = $pair[1]
  python -I D:\remote\remote\.claude\caplife\sh_r4_mut.py $m apply
  $r = & D:\remote\remote\.claude\caplife\sh_run.ps1 -Scenario $sc -Tag "r4mut_$m" -Seam true
  "$m ($sc): $(($r | Select-String 'gradle rc|RESULT') -join ' | ')"
  $r | Select-String '^FAIL' | ForEach-Object { "   " + $_.Line.Substring(0, [Math]::Min(170, $_.Line.Length)) }
  python -I D:\remote\remote\.claude\caplife\sh_r4_mut.py $m revert
  $files | ForEach-Object { (Get-Item $_).LastWriteTime = Get-Date }
}
"restored: $((Compare-Object $base ($files | ForEach-Object { (Get-FileHash $_).Hash })) -eq $null)"
