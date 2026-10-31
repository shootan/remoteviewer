# apk-inbox C r1: what each APK carries -- per native library and the dex, counted strings (not a claim from the build
# flags): C's code, the trace, the test seam, the test-only sink hook, the fixture directory and the product one.
param([string]$Dir = 'C:\work\gnlink-wt\apk-inbox-c\apk_out')
Add-Type -AssemblyName System.IO.Compression.FileSystem
$probes = [ordered]@{ 'C-recovery' = 'decoder recovery exhausted'; 'C-chain' = 'decoder inbox: chain loss'; 'liveness-trace' = 'liveness-trace';
  'sh-seam' = 'sh-test-seam'; 'WaitIdleForTest' = 'WaitIdleForTest'; 'fixture-dir' = '10.0.2.2:18410'; 'product-dir' = 'gnlink.shotan.net' }
foreach ($apk in Get-ChildItem $Dir -Filter *.apk | Sort-Object Name) {
  "== $($apk.Name) sha256=$((Get-FileHash $apk.FullName).Hash.ToLower())"
  $z = [IO.Compression.ZipFile]::OpenRead($apk.FullName)
  try {
    $so = @{}; $dex = @{}
    foreach ($e in $z.Entries) {
      $isSo = $e.FullName -like 'lib/*/libremote60_android_direct.so'
      $isDex = $e.FullName -like 'classes*.dex'
      if (-not ($isSo -or $isDex)) { continue }
      $m = New-Object IO.MemoryStream; $s = $e.Open(); $s.CopyTo($m); $s.Close()
      $txt = [Text.Encoding]::ASCII.GetString($m.ToArray())
      foreach ($k in $probes.Keys) {
        $c = [regex]::Matches($txt, [regex]::Escape($probes[$k])).Count
        if ($isSo) { if ($c -gt 0) { $so[$k] = 1 + [int]$so[$k] } } else { $dex[$k] = [int]$dex[$k] + $c }
      }
      if ($isSo) { $so['abis'] = 1 + [int]$so['abis'] }
    }
    "  native: abis=$([int]$so['abis']) " + (($probes.Keys | ForEach-Object { "$_=$([int]$so[$_])/$([int]$so['abis'])" }) -join ' ')
    "  dex:    " + (($probes.Keys | ForEach-Object { "$_=$([int]$dex[$_])" }) -join ' ')
  } finally { $z.Dispose() }
  $bc = Join-Path $Dir ($apk.BaseName + '.BuildConfig.java')
  if (Test-Path $bc) { "  BuildConfig: " + ((Get-Content $bc | Select-String 'VERSION_NAME|DIRECTORY_URL =|LIVENESS_TRACE|STANDBY_AUTO_PREPARE' | ForEach-Object { $_.Line.Trim() -replace 'public static final ', '' }) -join ' ') }
}
