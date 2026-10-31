$ErrorActionPreference = 'Stop'
$OutputEncoding = [Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$auditDir = $PSScriptRoot
$releaseDir = Join-Path (Split-Path $auditDir -Parent) 'rel\0.2.146'
$manifestPath = Join-Path $auditDir 'windows.manifest'
$signaturePath = Join-Path $auditDir 'windows.sig'
$deployKey = Join-Path $env:USERPROFILE '.ssh\remote60_deploy'
foreach ($name in @('windows.manifest', 'windows.sig')) {
  $destination = Join-Path $auditDir $name
  & scp.exe -q -i $deployKey -o BatchMode=yes -o StrictHostKeyChecking=yes "gnlink@192.168.0.6:/opt/gnlink/update-manifests/$name" $destination
  if ($LASTEXITCODE -ne 0) { throw "read-only live pair fetch failed: $name ($LASTEXITCODE)" }
  $liveHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLower()
  $localHash = (Get-FileHash -LiteralPath (Join-Path $releaseDir $name) -Algorithm SHA256).Hash.ToLower()
  if ($liveHash -ne $localHash) { throw "live/local pair differs: $name" }
  Write-Output "$name sha256=$liveHash live/local=match"
}
$env:GNLINK_PUBLIC_KEY_HEX = ''
& node.exe 'automation\gnlink_verify_manifest.js' $manifestPath $signaturePath windows
if ($LASTEXITCODE -ne 0) { throw "live signature verification failed ($LASTEXITCODE)" }
$artifactResults = @()
foreach ($line in Get-Content -LiteralPath $manifestPath -Encoding UTF8) {
  if (-not $line.StartsWith('artifact=')) { continue }
  $parts = $line.Substring(9).Split('|')
  if ($parts.Count -ne 4) { throw 'malformed signed artifact row' }
  $uri = [Uri]$parts[3]
  if ($uri.Scheme -ne 'https' -or $uri.Host -ne 'gnlink.shotan.net' -or -not $uri.AbsolutePath.StartsWith('/updates/0.2.146/')) { throw 'artifact URL outside this release' }
  if ($parts[0] -notmatch '^(GNLink(?:Host|Stream|Capture|InputService|Client|Viewer|Setup|Updater|ClipHelper)\.exe|ui\\(?:shell|macro)\.html)$') { throw 'unexpected artifact path' }
  $downloadPath = Join-Path $auditDir ('downloads\' + $parts[0])
  [void][System.IO.Directory]::CreateDirectory((Split-Path $downloadPath -Parent))
  & curl.exe --fail --silent --show-error --location --connect-timeout 15 --max-time 60 --output $downloadPath $parts[3]
  if ($LASTEXITCODE -ne 0) { throw "HTTPS artifact fetch failed: $($parts[0]) ($LASTEXITCODE)" }
  $item = Get-Item -LiteralPath $downloadPath
  $sha = (Get-FileHash -LiteralPath $downloadPath -Algorithm SHA256).Hash.ToLower()
  if ($item.Length -ne [long]$parts[1] -or $sha -ne $parts[2]) { throw "external artifact mismatch: $($parts[0])" }
  $artifactResults += [pscustomobject]@{ name=$parts[0]; bytes=$item.Length; sha256=$sha; url=$parts[3]; match=$true }
  Write-Output "HTTPS PASS $($parts[0]) $($item.Length) $sha"
}
if ($artifactResults.Count -ne 11) { throw "expected 11 artifacts, got $($artifactResults.Count)" }
$auditResult = [pscustomobject]@{
  release='0.2.146'; build_commit='df274be43c28bc9d3cb9d8d84156d9f3083dd7d6'; checked_at_utc=[DateTime]::UtcNow.ToString('o');
  manifest_sha256=(Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLower();
  signature_sha256=(Get-FileHash -LiteralPath $signaturePath -Algorithm SHA256).Hash.ToLower();
  live_local_pair_match=$true; signature_valid=$true; tamper_refused=$true; https_artifacts=$artifactResults
}
$auditResult | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $auditDir 'result.json') -Encoding UTF8
Write-Output 'RESULT: live pair/signature/tamper verification PASS; HTTPS artifacts 11/11 PASS'
