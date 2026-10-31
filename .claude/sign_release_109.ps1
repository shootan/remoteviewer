# Signs the 0.2.109 Windows manifest with the operational key.
#
# Windows only: 0.2.16 is already published, its signature is unchanged, and nothing in this
# release touches an Android file. `.claude/rel/0.2.108` is not read or written here -- the
# existing target and baseline stay exactly as they are.
#
# Read and signed as BYTES. The signature covers exact bytes: anything that translated line
# endings on the way in or out would produce a signature that verifies here and nowhere else.
#
# The private key never leaves this machine. It is unprotected from its DPAPI blob into memory,
# used, and the plaintext array is cleared before this exits. Nothing about it is written, logged
# or printed -- what goes to the NAS is the public key, the signature and the document.

param(
  # The release directory to sign. Defaults to the original 0.2.109 cut. Passed in rather than
  # copied into a second script, so there is one signer rather than two that can drift.
  [string]$Rel = 'D:\remote\remote\.claude\rel\0.2.109'
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Security

$keyDir = Join-Path $env:LOCALAPPDATA 'GNLink\ReleaseSigning\p256-a0e184579c5d4c2d'
$rel = $Rel

if (-not (Test-Path $keyDir)) { Write-Output "no key at $keyDir"; exit 1 }
if (-not (Test-Path (Join-Path $rel 'windows.manifest'))) { Write-Output "no manifest in $rel"; exit 1 }

$priv = $null
try {
  $blob = [System.IO.File]::ReadAllBytes((Join-Path $keyDir 'private.ecc.dpapi'))
  $priv = [System.Security.Cryptography.ProtectedData]::Unprotect(
    $blob, $null, [System.Security.Cryptography.DataProtectionScope]::CurrentUser)
  $key = [System.Security.Cryptography.CngKey]::Import(
    $priv, [System.Security.Cryptography.CngKeyBlobFormat]::EccPrivateBlob)
  $signer = New-Object System.Security.Cryptography.ECDsaCng($key)
  $signer.HashAlgorithm = [System.Security.Cryptography.CngAlgorithm]::Sha256

  $docPath = Join-Path $rel 'windows.manifest'
  $doc = [System.IO.File]::ReadAllBytes($docPath)
  # CNG returns raw r||s, which is what all three verifiers expect.
  $sig = $signer.SignData($doc)
  if ($sig.Length -ne 64) { throw "signature is $($sig.Length) bytes, expected 64" }
  $sigHex = ([System.BitConverter]::ToString($sig) -replace '-', '').ToLowerInvariant()
  [System.IO.File]::WriteAllText((Join-Path $rel 'windows.sig'), $sigHex)

  # Verified here, immediately, with the public half -- so a signature that cannot be checked is
  # never handed on as if it could.
  $pubHex = ([System.IO.File]::ReadAllText((Join-Path $keyDir 'public_xy.hex'))).Trim()
  $xy = New-Object byte[] 64
  for ($i = 0; $i -lt 64; $i++) {
    $xy[$i] = [Convert]::ToByte($pubHex.Substring($i * 2, 2), 16)
  }
  $params = New-Object System.Security.Cryptography.ECParameters
  $params.Curve = [System.Security.Cryptography.ECCurve]::CreateFromFriendlyName('nistP256')
  $point = New-Object System.Security.Cryptography.ECPoint
  $point.X = $xy[0..31]
  $point.Y = $xy[32..63]
  $params.Q = $point
  $verifier = [System.Security.Cryptography.ECDsa]::Create($params)
  $ok = $verifier.VerifyData($doc, $sig, [System.Security.Cryptography.HashAlgorithmName]::SHA256)
  $verifier.Dispose()

  Write-Output ("windows.manifest  doc={0}B  sig={1} chars  selfVerify={2}" -f $doc.Length, $sigHex.Length, $ok)
  if (-not $ok) { throw "the signature just written does not verify" }

  $signer.Dispose(); $key.Dispose()
}
finally {
  if ($priv) { [Array]::Clear($priv, 0, $priv.Length) }
}
