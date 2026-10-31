$ErrorActionPreference = 'Continue'
$r = & D:\remote\remote\.claude\caplife\sh_run.ps1 -Scenario strength_ab -Tag r5_ab -Seam true
"strength_ab: $(($r | Select-String 'gradle rc|apk |RESULT') -join ' | ')"
$r | Select-String '^FAIL|panB133' | ForEach-Object { "   " + $_.Line.Substring(0, [Math]::Min(170, $_.Line.Length)) }
foreach ($sc in 'layout_b', 'latency_ab', 'touch_ab') {
  $r = & D:\remote\remote\.claude\caplife\sh_run.ps1 -Scenario $sc -Tag "r5_$sc" -Seam true -NoBuild
  "${sc}: $(($r | Select-String 'RESULT') -join ' | ')"
  $r | Select-String '^FAIL' | ForEach-Object { "   " + $_.Line.Substring(0, [Math]::Min(170, $_.Line.Length)) }
}
"AVD-DONE"
