$ErrorActionPreference = 'Continue'
$wt = 'C:\work\gnlink-wt\apk-prelogin-select'
$env:JAVA_HOME = "C:\Program Files\Android\Android Studio\jbr"; $env:ANDROID_HOME = "$env:LOCALAPPDATA\Android\Sdk"
$rel = 'D:\remote\remote\.claude\rel-apk-0232'; $a = "$rel\GNLink-0.2.32.apk"
"HEAD $(git -C $wt log --oneline -1)"
"tracked changes: $((git -C $wt status --short --untracked-files=no) -join ',')"
Push-Location "$wt\apps\android_direct_client"
.\gradlew.bat clean assembleDebug testDebugUnitTest "-Pgnlink.shTestSeam=false" --console=plain > D:\remote\remote\.claude\caplife\apk\gradle_0232r5.txt 2>&1
"gradle rc=$LASTEXITCODE"
$t = 0; $f = 0; Get-ChildItem app\build\test-results\testDebugUnitTest -Filter *.xml | ForEach-Object { [xml]$x = Get-Content $_.FullName; $t += [int]$x.testsuite.tests; $f += [int]$x.testsuite.failures + [int]$x.testsuite.errors }
"unit=$t fail=$f"
Copy-Item app\build\outputs\apk\debug\app-debug.apk $a -Force
Pop-Location
"candidate $((Get-Item $a).Length) B $((Get-FileHash $a).Hash)"
$bt = "$env:LOCALAPPDATA\Android\Sdk\build-tools\37.0.0"
& "$bt\aapt.exe" dump badging $a | Select-String "^package|native-code"
& "$bt\apksigner.bat" verify --print-certs $a | Select-String 'V2 Signer: certificate SHA-256'
Push-Location $wt; "gate candidate: $(python automation\gnlink_check_fixed_server.py --apk $a 2>&1 | Select-Object -Last 1)"; Pop-Location
Copy-Item $a D:\remote\remote\.claude\caplife\sh_r2\app-noseam.apk -Force
$r = & D:\remote\remote\.claude\caplife\sh_run.ps1 -Scenario prelogin_ui_control -Tag rel5_prelogin_ui_control -Seam false -NoBuild
"prelogin_ui_control (candidate, ordinary host -- EXPECTED to fail the no-window-list check): $(($r | Select-String 'RESULT') -join ' | ')"
$r | Select-String '^FAIL' | ForEach-Object { '   ' + $_.Line.Substring(0, [Math]::Min(170, $_.Line.Length)) }
foreach ($sc in 'method_ui', 'ua2', 'ua2_desktop', 'prelogin_ui', 'prelogin_tripwire') {
  $r = & D:\remote\remote\.claude\caplife\sh_run.ps1 -Scenario $sc -Tag "rel5_$sc" -Seam false -NoBuild
  "${sc} (candidate): $(($r | Select-String 'apk |host |RESULT') -join ' | ')"
  $r | Select-String '^FAIL' | ForEach-Object { '   ' + $_.Line.Substring(0, [Math]::Min(170, $_.Line.Length)) }
}
$r = & D:\remote\remote\.claude\caplife\sh_run.ps1 -Scenario strength_ab -Tag rel5_strength_ab -Seam true
"strength_ab (seam): $(($r | Select-String 'gradle rc|apk |RESULT') -join ' | ')"
Push-Location $wt; "gate seam (must FAIL): $(python automation\gnlink_check_fixed_server.py --apk D:\remote\remote\.claude\caplife\sh_r2\app-shseam.apk 2>&1 | Select-Object -Last 1)"; Pop-Location
foreach ($sc in 'compare_ab', 'touch_ab', 'latency_ab', 'layout_b') {
  $r = & D:\remote\remote\.claude\caplife\sh_run.ps1 -Scenario $sc -Tag "rel5_$sc" -Seam true -NoBuild
  "${sc} (seam): $(($r | Select-String 'RESULT') -join ' | ')"
  $r | Select-String '^FAIL' | ForEach-Object { "   " + $_.Line.Substring(0, [Math]::Min(170, $_.Line.Length)) }
}
"BATCH-DONE"
