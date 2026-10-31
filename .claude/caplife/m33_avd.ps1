$ErrorActionPreference = 'Continue'
# The merged feat/apk-0.2.33 APK (before the standby wiring): the AVD scenarios touched by the merge.
$quietSince = $null
$deadline = (Get-Date).AddHours(8)
while ((Get-Date) -lt $deadline) {
  $busy = (Test-Path 'C:\work\gnlink-wt\SLOT_REQUEST.txt') -or
          @(Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match 'sh_run\.ps1|sh_filter_avd\.py|_e2e_test\.exe' }).Count -gt 0
  if ($busy) { $quietSince = $null } elseif (-not $quietSince) { $quietSince = Get-Date }
  if ($quietSince -and ((Get-Date) - $quietSince).TotalMinutes -ge 3) { break }
  Start-Sleep -Seconds 30
}
"quiet from $quietSince, starting at $(Get-Date)"
$wt = 'C:\work\gnlink-wt\feat-apk-0233'
$out = 'D:\remote\remote\.claude\caplife\sh_r2'
$env:JAVA_HOME = "C:\Program Files\Android\Android Studio\jbr"; $env:ANDROID_HOME = "$env:LOCALAPPDATA\Android\Sdk"
"HEAD $(git -C $wt log --oneline -1)"
Push-Location "$wt\apps\android_direct_client"
foreach ($seam in 'false', 'true') {
  .\gradlew.bat assembleDebug "-Pgnlink.shTestSeam=$seam" --console=plain > "D:\remote\remote\.claude\caplife\apk\gradle_m33_$seam.txt" 2>&1
  "gradle seam=$seam rc=$LASTEXITCODE"
  $name = if ($seam -eq 'true') { 'app-shseam.apk' } else { 'app-noseam.apk' }
  Copy-Item app\build\outputs\apk\debug\app-debug.apk "$out\$name" -Force
  "$name $((Get-FileHash "$out\$name").Hash)"
}
Pop-Location
foreach ($sc in 'method_ui', 'ua2', 'ua2_desktop', 'prelogin_ui') {
  $r = & D:\remote\remote\.claude\caplife\sh_run.ps1 -Scenario $sc -Tag "m33_$sc" -Seam false -NoBuild
  "${sc} (merged noseam): $(($r | Select-String 'RESULT') -join ' | ')"
  $r | Select-String '^FAIL' | ForEach-Object { '   ' + $_.Line.Substring(0, [Math]::Min(170, $_.Line.Length)) }
}
foreach ($sc in 'layout_b', 'touch_ab') {
  $r = & D:\remote\remote\.claude\caplife\sh_run.ps1 -Scenario $sc -Tag "m33_$sc" -Seam true -NoBuild
  "${sc} (merged shseam): $(($r | Select-String 'RESULT') -join ' | ')"
  $r | Select-String '^FAIL' | ForEach-Object { '   ' + $_.Line.Substring(0, [Math]::Min(170, $_.Line.Length)) }
}
"M33-AVD-DONE"
