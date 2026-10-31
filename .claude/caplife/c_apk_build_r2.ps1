# apk-inbox C r2: the test APKs from one commit (the same five as r1, c_apk_build.ps1), tagged $Tag, into apk_out_$Tag --
# one gradle run each, each output copied out at once (the next run overwrites it) with its BuildConfig.
param([string]$Wt = 'C:\work\gnlink-wt\apk-inbox-c', [string]$Tag = 'c2')
$env:JAVA_HOME = 'C:\Program Files\Android\Android Studio\jbr'
$env:ANDROID_HOME = "$env:LOCALAPPDATA\Android\Sdk"
$proj = "$Wt\apps\android_direct_client"
$out = "$Wt\apk_out_$Tag"
New-Item -ItemType Directory -Force $out | Out-Null
"HEAD " + (git -C $Wt rev-parse HEAD) + " tracked changes: " + @(git -C $Wt status --short --untracked-files=no).Count
$bc = "$proj\app\build\generated\source\buildConfig\debug\com\remote60\androiddirect\BuildConfig.java"
$dir = '-Pgnlink.testDirectoryUrl=http://10.0.2.2:18410'
$runs = @(
  @{ name = "plain-debug-$Tag"; task = 'assembleDebug'; args = @($dir) },
  @{ name = "on-debug-$Tag"; task = 'assembleDebug'; args = @($dir, '-Pgnlink.testStandbyAutoPrepare=true') },
  @{ name = "trace-debug-$Tag"; task = 'assembleDebug'; args = @($dir, '-Pgnlink.livenessTrace=true') },
  @{ name = "release-$Tag-unsigned"; task = 'assembleRelease'; args = @() },
  @{ name = "seam-debug-$Tag"; task = 'assembleDebug'; args = @('-Pgnlink.shTestSeam=true') }
)
Push-Location $proj
try {
  foreach ($r in $runs) {
    $log = "$Wt\build\gradle_$($r.name).txt"
    & .\gradlew.bat --no-daemon $r.task @($r.args) *> $log
    $ok = (Select-String -Path $log -Pattern 'BUILD SUCCESSFUL' -Quiet)
    if (-not $ok) { "$($r.name): BUILD FAILED (log $log)"; continue }
    if ($r.task -eq 'assembleRelease') {
      $apk = Get-ChildItem "$proj\app\build\outputs\apk\release" -Filter *.apk | Select-Object -First 1
    } else {
      $apk = Get-Item "$proj\app\build\outputs\apk\debug\app-debug.apk"
      Copy-Item $bc "$out\$($r.name).BuildConfig.java"
    }
    Copy-Item $apk.FullName "$out\$($r.name).apk"
    "$($r.name): $((Get-FileHash "$out\$($r.name).apk").Hash.ToLower())  ($($r.task) $($r.args -join ' '))"
  }
} finally { Pop-Location }
"HEAD after " + (git -C $Wt rev-parse HEAD) + " tracked changes: " + @(git -C $Wt status --short --untracked-files=no).Count
