$ErrorActionPreference = 'Continue'
$r = 'D:\remote\remote\.claude\caplife\sh_r2'
& D:\remote\remote\.claude\caplife\sh_run.ps1 -Scenario method_ui -Tag b3_method -Seam false | Select-String 'apk |RESULT'
& D:\remote\remote\.claude\caplife\sh_run.ps1 -Scenario strength_ab -Tag b3_ab -Seam true | Select-String 'apk |RESULT'
foreach ($sc in 'compare_ab', 'touch_ab', 'latency_ab') {
  & D:\remote\remote\.claude\caplife\sh_run.ps1 -Scenario $sc -Tag "b3_$sc" -Seam true -NoBuild | Select-String 'RESULT'
}
"BATCH-DONE"
