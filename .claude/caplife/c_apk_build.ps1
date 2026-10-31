# apk-inbox C r1: the test APKs from one commit, one gradle run each, each output copied out at once (the next run
# overwrites it) with its BuildConfig. Plain = no app trace (the verifier's 120 s arm, as B's plain-debug-r5);
# on = the ke harness's -ApkOn (hash only; the e2 plans install ApkOff alone); trace = the diagnostics build, apart.
param([string]$Wt = 'C:\work\gnlink-wt\apk-inbox-c')
$env:JAVA_HOME = 'C:\Program Files\Android\Android Studio\jbr'
$env:ANDROID_HOME = "$env:LOCALAPPDATA\Android\Sdk"
$proj = "$Wt\apps\android_direct_client"
$out = "$Wt\apk_out"
New-Item -ItemType Directory -Force $out | Out-Null
$bc = "$proj\app\build\generated\source\buildConfig\debug\com\remote60\androiddirect\BuildConfig.java"
$dir = '-Pgnlink.testDirectoryUrl=http://10.0.2.2:18410'
$runs = @(
  @{ name = 'plain-debug-c1'; task = 'assembleDebug'; args = @($dir) },
  @{ name = 'on-debug-c1'; task = 'assembleDebug'; args = @($dir, '-Pgnlink.testStandbyAutoPrepare=true') },
  @{ name = 'trace-debug-c1'; task = 'assembleDebug'; args = @($dir, '-Pgnlink.livenessTrace=true') },
  @{ name = 'release-c1-unsigned'; task = 'assembleRelease'; args = @() },
  @{ name = 'seam-debug-c1'; task = 'assembleDebug'; args = @('-Pgnlink.shTestSeam=true') }
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
