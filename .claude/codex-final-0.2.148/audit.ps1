$ErrorActionPreference = 'Stop'
$OutputEncoding = [Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$auditDir = $PSScriptRoot
$claudeDir = Split-Path $auditDir -Parent
$deployKey = Join-Path $env:USERPROFILE '.ssh\remote60_deploy'
$env:GNLINK_PUBLIC_KEY_HEX = ''
$releaseCommit = 'aeebe33ae605eb435940563042b59f835dfb1172'
$specs = @(
  @{ Platform='windows'; Version='0.2.148'; Rel='rel\0.2.148'; Count=11;
     ManifestHash='19cb7a271b2b0e311e61b4e1722e05afb4acbd9388d093ce0f4295c62a03a511';
     SigHash='1bee1ef02af01deafb8d956a3d6c71d71e282afc4c74c20a7fc553ef11f3fd14' },
  @{ Platform='android'; Version='0.2.25'; Rel='rel\android-0.2.25'; Count=1;
     ManifestHash='73dde974115ffd31d6a26f823cf9cfb1f919c4a033703cd8dd8814eeea3c4516';
     SigHash='2e55dabc9286c2e3bbfbdab2cb4a09607d865bba550c18cbefe96c97db5f79eb' }
)
$allResults = @()
foreach ($spec in $specs) {
  $platform = $spec.Platform
  $version = $spec.Version
  $platformDir = Join-Path $auditDir $platform
  [void][System.IO.Directory]::CreateDirectory($platformDir)
  $releaseDir = Join-Path $claudeDir $spec.Rel
  foreach ($extension in @('manifest','sig')) {
    $name = "$platform.$extension"
    $destination = Join-Path $platformDir $name
    & scp.exe -q -i $deployKey -o BatchMode=yes -o StrictHostKeyChecking=yes "gnlink@192.168.0.6:/opt/gnlink/update-manifests/$name" $destination
    if ($LASTEXITCODE -ne 0) { throw "live pair fetch failed: $name ($LASTEXITCODE)" }
    $liveHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLower()
    $localHash = (Get-FileHash -LiteralPath (Join-Path $releaseDir $name) -Algorithm SHA256).Hash.ToLower()
    $expectedHash = if ($extension -eq 'manifest') { $spec.ManifestHash } else { $spec.SigHash }
    if ($liveHash -ne $localHash -or $liveHash -ne $expectedHash) { throw "live/local/pinned pair differs: $name" }
    Write-Output "$name sha256=$liveHash live/local/pinned=match"
  }
  $manifestPath = Join-Path $platformDir "$platform.manifest"
  $signaturePath = Join-Path $platformDir "$platform.sig"
  & node.exe 'automation\gnlink_verify_manifest.js' $manifestPath $signaturePath $platform
  if ($LASTEXITCODE -ne 0) { throw "live signature/tamper verification failed: $platform" }
  $lines = @(Get-Content -LiteralPath $manifestPath -Encoding UTF8)
  if ($lines -notcontains "version=$version") { throw "wrong live version: $platform" }
  if ($platform -eq 'android' -and $lines -notcontains 'versionCode=24') { throw 'wrong android versionCode' }
  $artifactResults = @()
  foreach ($line in $lines) {
    if (-not $line.StartsWith('artifact=')) { continue }
    $parts = $line.Substring(9).Split('|')
    if ($parts.Count -ne 4 -or $parts[1] -notmatch '^\d+$' -or $parts[2] -notmatch '^[0-9a-f]{64}$') { throw 'malformed artifact row' }
    $uri = [Uri]$parts[3]
    if ($uri.Scheme -ne 'https' -or $uri.Host -ne 'gnlink.shotan.net' -or -not $uri.AbsolutePath.StartsWith("/updates/$version/")) { throw 'unexpected artifact URL' }
    if ($platform -eq 'windows') {
      if ($parts[0] -notmatch '^(GNLink(?:Host|Stream|Capture|InputService|Client|Viewer|Setup|Updater|ClipHelper)\.exe|ui\\(?:shell|macro)\.html)$') { throw 'unexpected Windows artifact path' }
    } elseif ($parts[0] -ne 'GNLink-0.2.25.apk') { throw 'unexpected Android artifact path' }
    $downloadPath = Join-Path $platformDir ('downloads\' + $parts[0])
    [void][System.IO.Directory]::CreateDirectory((Split-Path $downloadPath -Parent))
    & curl.exe --fail --silent --show-error --location --proto '=https' --proto-redir '=https' --connect-timeout 15 --max-time 60 --output $downloadPath $parts[3]
    if ($LASTEXITCODE -ne 0) { throw "HTTPS fetch failed: $($parts[0]) ($LASTEXITCODE)" }
    $item = Get-Item -LiteralPath $downloadPath
    $sha = (Get-FileHash -LiteralPath $downloadPath -Algorithm SHA256).Hash.ToLower()
    if ($item.Length -ne [long]$parts[1] -or $sha -ne $parts[2]) { throw "external artifact mismatch: $($parts[0])" }
    $artifactResults += [pscustomobject]@{ name=$parts[0]; bytes=$item.Length; sha256=$sha; url=$parts[3]; match=$true }
    Write-Output "HTTPS PASS $platform $($parts[0]) $($item.Length) $sha"
  }
  if ($artifactResults.Count -ne $spec.Count) { throw "artifact count mismatch: $platform" }
  $allResults += [pscustomobject]@{
    platform=$platform; version=$version; manifest_sha256=$spec.ManifestHash; signature_sha256=$spec.SigHash;
    live_local_pinned_pair_match=$true; signature_valid=$true; tamper_refused=$true; https_artifacts=$artifactResults
  }
}
$remoteMain = & git ls-remote origin refs/heads/main
if ($LASTEXITCODE -ne 0) { throw 'ls-remote failed' }
$remoteCommit = ($remoteMain -split '\s+')[0]
if ($remoteCommit -ne $releaseCommit) { throw "remote main differs from release commit: $remoteCommit" }
$result = [pscustomobject]@{
  build_commit=$releaseCommit; checked_at_utc=[DateTime]::UtcNow.ToString('o');
  origin_main_at_check=$remoteCommit; release_results=$allResults
}
$result | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $auditDir 'result.json') -Encoding UTF8
Write-Output 'RESULT: live pair/signature/tamper PASS both platforms; HTTPS 11/11 + 1/1 PASS; origin/main matches release'
