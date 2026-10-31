param([string]$Scenario = "compare_ab", [string]$Tag = "cmp1", [switch]$NoBuild, [string]$Seam = "true", [string]$HostExe = "", [string]$Extra = "",
      [string]$NativeExe = "")
# -NativeExe <exe>: run that native test (its arguments in -Extra) under the same slot handling instead of an AVD
# scenario -- e.g. the real logon-screen host e2e (remote60_viewer_prelogin_login_e2e_test.exe, -Extra "--android-select").
$ErrorActionPreference = 'Continue'
$wt = 'C:\work\gnlink-wt\apk-prelogin-select'
$out = "D:\remote\remote\.claude\caplife\sh_r2"
$env:JAVA_HOME = "C:\Program Files\Android\Android Studio\jbr"; $env:ANDROID_HOME = "$env:LOCALAPPDATA\Android\Sdk"
$apkName = if ($Seam -eq 'true') { 'app-shseam.apk' } else { 'app-noseam.apk' }
if ($NativeExe) {
  "native $NativeExe $((Get-FileHash $NativeExe).Hash)"
} else {
  if (-not $NoBuild) {
    Push-Location "$wt\apps\android_direct_client"
    .\gradlew.bat assembleDebug "-Pgnlink.shTestSeam=$Seam" --console=plain > "$out\gradle_$Tag.txt" 2>&1
    "gradle rc=$LASTEXITCODE"
    Pop-Location
    Copy-Item "$wt\apps\android_direct_client\app\build\outputs\apk\debug\app-debug.apk" "$out\$apkName" -Force
  }
  "apk $apkName $((Get-FileHash "$out\$apkName").Hash)"
}
$hostOverride = $HostExe  # PowerShell names are case-insensitive: read the parameter before $hostExe is set
$hostExe = "$wt\build\apps\native_poc\Release\GNLinkStreamMigrationTest.exe"
if ($hostOverride) { $hostExe = $hostOverride }
if (-not $NativeExe) { "host $((Get-FileHash $hostExe).Hash)" }
"qwinsta:"; qwinsta | Select-Object -First 3
$slot = 'C:\work\gnlink-wt\SLOT_REQUEST.txt'
try { $fs = [System.IO.File]::Open($slot, 'CreateNew'); $fs.Close() } catch { "slot busy"; exit 1 }
try {
  if ($NativeExe) {
    $env:REMOTE60_ALLOW_HOST_E2E = '1'; $env:REMOTE60_ALLOW_UI_TEST = '1'
    & $NativeExe @(if ($Extra) { $Extra -split ' ' } else { @() }) > "$out\$Tag.log" 2>&1
  } else {
    python "$wt\automation\sh_filter_avd.py" --host-exe $hostExe --apk "$out\$apkName" --out "$out\$Tag" --scenario $Scenario @(if ($Extra) { $Extra -split ' ' } else { @() }) > "$out\$Tag.log" 2>&1
  }
  "scenario rc=$LASTEXITCODE"
} finally {
  Remove-Item $slot -Confirm:$false
  "slot removed=$(-not (Test-Path $slot))"
  "leftover: $((Get-Process | Where-Object { $_.ProcessName -match 'GNLinkStreamMigrationTest' } | ForEach-Object { "$($_.ProcessName):$($_.Id)" }) -join ',')"
}
Get-Content "$out\$Tag.log" | Where-Object { $_ -match '^(PASS|FAIL|RESULT)' } | ForEach-Object { $_.Substring(0, [Math]::Min(200, $_.Length)) }
