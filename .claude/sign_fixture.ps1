# Signs one fixture document with the release key, for verifying that the key works.
#
# The document is the shared TEST manifest, copied verbatim: the three suites assert its field
# values, so anything else would fail them for reasons that have nothing to do with the key. It is
# marked TEST DATA in its first line, its artifact URLs are on updates.example, and its artifacts
# are 24, 18 and 48 bytes -- it could not drive a real update even if it escaped. The fixture lives
# in .claude/, untracked, and is not a release manifest.
#
# Read and signed as BYTES. The signature covers exact bytes, and anything that translated line
# endings on the way in would produce a signature that verifies here and nowhere else.

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Security

$keyDir = Join-Path $env:LOCALAPPDATA 'GNLink\ReleaseSigning\p256-a0e184579c5d4c2d'
$src = 'D:\remote\remote\apps\shared\update_manifest\test_manifest.txt'
$out = 'D:\remote\remote\.claude\keyfixture'

if (-not (Test-Path $keyDir)) { Write-Output "no key at $keyDir"; exit 1 }
New-Item -ItemType Directory -Path $out -Force | Out-Null

$doc = [System.IO.File]::ReadAllBytes($src)
[System.IO.File]::WriteAllBytes((Join-Path $out 'test_manifest.txt'), $doc)

$xyHex = ([System.IO.File]::ReadAllText((Join-Path $keyDir 'public_xy.hex'))).Trim()
[System.IO.File]::WriteAllText((Join-Path $out 'test_public_key.txt'), $xyHex)

$protectedBlob = $null
$priv = $null
try {
  $protectedBlob = [System.IO.File]::ReadAllBytes((Join-Path $keyDir 'private.ecc.dpapi'))
  $priv = [System.Security.Cryptography.ProtectedData]::Unprotect(
    $protectedBlob, $null, [System.Security.Cryptography.DataProtectionScope]::CurrentUser)
  $key = [System.Security.Cryptography.CngKey]::Import(
    $priv, [System.Security.Cryptography.CngKeyBlobFormat]::EccPrivateBlob)
  $signer = New-Object System.Security.Cryptography.ECDsaCng($key)
  $signer.HashAlgorithm = [System.Security.Cryptography.CngAlgorithm]::Sha256

  # CNG hands back raw r||s, which is what all three verifiers expect: BCryptVerifySignature on
  # Windows, ieee-p1363 in Node, and a DER wrapper built from raw halves in Kotlin.
  $sig = $signer.SignData($doc)
  if ($sig.Length -ne 64) { throw "signature is $($sig.Length) bytes, expected 64" }
  $sigHex = ([System.BitConverter]::ToString($sig) -replace '-', '').ToLowerInvariant()
  [System.IO.File]::WriteAllText((Join-Path $out 'test_manifest.sig'), $sigHex)

  $signer.Dispose(); $key.Dispose()
  Write-Output "FIXTURE $out"
  Write-Output "SIGLEN  $($sigHex.Length)"
  Write-Output "DOCLEN  $($doc.Length)"
}
finally {
  if ($priv) { [Array]::Clear($priv, 0, $priv.Length) }
}
