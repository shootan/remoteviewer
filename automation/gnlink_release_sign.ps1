# Signs a release manifest with the operational key, and verifies the signature it just wrote.
#
# Generalised from .claude/sign_release_134.ps1, which had the release directory hard-coded. The
# key never leaves memory: it is unwrapped from its DPAPI blob, used, and cleared in a finally
# block. Nothing here prints the key, the blob, or any part of either -- the only thing written is
# the detached signature, and the only thing logged is its length and whether it verifies.
#
#   gnlink_release_sign.ps1 -ReleaseDir <dir> [-Platform windows] [-KeyDir <dir>]
#
# Exit 0 = signed and self-verified. Anything else = do not publish.

param(
  [Parameter(Mandatory = $true)][string]$ReleaseDir,
  [string]$Platform = 'windows',
  [string]$KeyDir = (Join-Path $env:LOCALAPPDATA 'GNLink\ReleaseSigning\p256-a0e184579c5d4c2d')
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Security

if (-not (Test-Path $KeyDir)) {
  Write-Output "no signing key at $KeyDir"
  exit 1
}
$docPath = Join-Path $ReleaseDir ("{0}.manifest" -f $Platform)
if (-not (Test-Path $docPath)) {
  Write-Output "no manifest at $docPath"
  exit 1
}

$priv = $null
try {
  $blob = [System.IO.File]::ReadAllBytes((Join-Path $KeyDir 'private.ecc.dpapi'))
  $priv = [System.Security.Cryptography.ProtectedData]::Unprotect(
    $blob, $null, [System.Security.Cryptography.DataProtectionScope]::CurrentUser)
  $key = [System.Security.Cryptography.CngKey]::Import(
    $priv, [System.Security.Cryptography.CngKeyBlobFormat]::EccPrivateBlob)
  $signer = New-Object System.Security.Cryptography.ECDsaCng($key)
  $signer.HashAlgorithm = [System.Security.Cryptography.CngAlgorithm]::Sha256

  $pubHex = ([System.IO.File]::ReadAllText((Join-Path $KeyDir 'public_xy.hex'))).Trim()
  $xy = New-Object byte[] 64
  for ($i = 0; $i -lt 64; $i++) { $xy[$i] = [Convert]::ToByte($pubHex.Substring($i * 2, 2), 16) }

  # Read and signed as BYTES: line endings are part of what is signed, so the manifest must not be
  # rewritten by anything between here and publication.
  $doc = [System.IO.File]::ReadAllBytes($docPath)
  $sig = $signer.SignData($doc)
  if ($sig.Length -ne 64) { throw "signature is $($sig.Length) bytes, expected 64" }
  $sigHex = ([System.BitConverter]::ToString($sig) -replace '-', '').ToLowerInvariant()
  [System.IO.File]::WriteAllText((Join-Path $ReleaseDir ("{0}.sig" -f $Platform)), $sigHex)

  # Verified with the PUBLIC key, from the file, the way a client would -- not with the signer
  # object, which would agree with itself whatever went wrong.
  $params = New-Object System.Security.Cryptography.ECParameters
  $params.Curve = [System.Security.Cryptography.ECCurve]::CreateFromFriendlyName('nistP256')
  $point = New-Object System.Security.Cryptography.ECPoint
  $point.X = $xy[0..31]; $point.Y = $xy[32..63]
  $params.Q = $point
  $verifier = [System.Security.Cryptography.ECDsa]::Create($params)
  $ok = $verifier.VerifyData($doc, $sig, [System.Security.Cryptography.HashAlgorithmName]::SHA256)
  $verifier.Dispose()

  Write-Output ("{0}.manifest  dir={1}  doc={2}B  sig={3}chars  selfVerify={4}" -f
                $Platform, $ReleaseDir, $doc.Length, $sigHex.Length, $ok)
  $signer.Dispose(); $key.Dispose()
  if (-not $ok) { throw "the signature just written does not verify" }
}
finally {
  if ($priv) { [Array]::Clear($priv, 0, $priv.Length) }
}
exit 0
