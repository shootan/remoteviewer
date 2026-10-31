# Signs a release manifest with the operational key, verifies the signature, then writes it.
#
# Generalised from .claude/sign_release_134.ps1, which had the release directory hard-coded. The
# key never leaves memory: it is unwrapped from its DPAPI blob, used, and cleared in a finally
# block. Nothing here prints the key, the blob, or any part of either -- the only thing written is
# the detached signature, and the only thing logged is its length and whether it verifies.
#
# Order matters and is deliberate: sign, verify with the PUBLIC key, and only then write --
# through a temporary and a move. A run that fails leaves no .sig at all, because the caller
# decides whether to publish by asking whether that file exists.
#
#   gnlink_release_sign.ps1 -ReleaseDir <dir> -TrustedKeyHex <128 hex> [-Platform windows] [-KeyDir <dir>]
#
# Exit 0 = signed and self-verified. Anything else = do not publish.

param(
  [Parameter(Mandatory = $true)][string]$ReleaseDir,
  # The key the PRODUCT trusts, 128 hex characters of X||Y. Required, and required to match
  # the public half of the key about to sign: a signing key that the shipped client does not
  # recognise produces a release that verifies here and fails on every machine it reaches.
  # The caller reads it out of the candidate rather than this file carrying a second copy.
  [Parameter(Mandatory = $true)][string]$TrustedKeyHex,
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

$priv     = $null
$key      = $null
$signer   = $null
$verifier = $null
$tmpPath  = $null
try {
  $blob = [System.IO.File]::ReadAllBytes((Join-Path $KeyDir 'private.ecc.dpapi'))
  $priv = [System.Security.Cryptography.ProtectedData]::Unprotect(
    $blob, $null, [System.Security.Cryptography.DataProtectionScope]::CurrentUser)
  $key = [System.Security.Cryptography.CngKey]::Import(
    $priv, [System.Security.Cryptography.CngKeyBlobFormat]::EccPrivateBlob)
  $signer = New-Object System.Security.Cryptography.ECDsaCng($key)
  $signer.HashAlgorithm = [System.Security.Cryptography.CngAlgorithm]::Sha256

  $pubHex = ([System.IO.File]::ReadAllText((Join-Path $KeyDir 'public_xy.hex'))).Trim().ToLowerInvariant()

  # Before the key is used for anything. A mismatch here means the release would be signed by
  # a key the product does not trust, and the only place that would show up is on a user
  # machine refusing the update.
  $trusted = $TrustedKeyHex.Trim().ToLowerInvariant()
  if ($trusted.Length -ne 128) { throw "the trusted key is $($trusted.Length) characters, expected 128" }
  if ($pubHex.Length -ne 128) { throw "the signing key's public half is $($pubHex.Length) characters, expected 128" }
  if ($pubHex -ne $trusted) {
    # Fingerprints, not the keys: a public key is not a secret, but there is no reason to put
    # 256 characters in a log to say two things differ.
    $h = [System.Security.Cryptography.SHA256]::Create()
    $mine = ([System.BitConverter]::ToString($h.ComputeHash([System.Text.Encoding]::ASCII.GetBytes($pubHex))) -replace '-', '').ToLowerInvariant().Substring(0, 16)
    $theirs = ([System.BitConverter]::ToString($h.ComputeHash([System.Text.Encoding]::ASCII.GetBytes($trusted))) -replace '-', '').ToLowerInvariant().Substring(0, 16)
    $h.Dispose()
    throw "the signing key is not the one the product trusts (signing $mine, product $theirs); nothing signed"
  }

  $xy = New-Object byte[] 64
  for ($i = 0; $i -lt 64; $i++) { $xy[$i] = [Convert]::ToByte($pubHex.Substring($i * 2, 2), 16) }

  # Read and signed as BYTES: line endings are part of what is signed, so the manifest must not be
  # rewritten by anything between here and publication.
  $doc = [System.IO.File]::ReadAllBytes($docPath)
  $sig = $signer.SignData($doc)
  if ($sig.Length -ne 64) { throw "signature is $($sig.Length) bytes, expected 64" }
  $sigHex = ([System.BitConverter]::ToString($sig) -replace '-', '').ToLowerInvariant()

  # Verified with the PUBLIC key, from the file, the way a client would -- not with the signer
  # object, which would agree with itself whatever went wrong.
  #
  # BEFORE anything is written. The earlier order wrote the .sig and verified afterwards, so a
  # signature that did not verify was already on disk beside a valid manifest -- and the caller
  # only tests that the file EXISTS. A failed signing run must leave nothing behind that a
  # later --deploy could pick up.
  $params = New-Object System.Security.Cryptography.ECParameters
  $params.Curve = [System.Security.Cryptography.ECCurve]::CreateFromFriendlyName('nistP256')
  $point = New-Object System.Security.Cryptography.ECPoint
  $point.X = $xy[0..31]; $point.Y = $xy[32..63]
  $params.Q = $point
  $verifier = [System.Security.Cryptography.ECDsa]::Create($params)
  $ok = $verifier.VerifyData($doc, $sig, [System.Security.Cryptography.HashAlgorithmName]::SHA256)
  if (-not $ok) { throw "the signature does not verify against the published public key; nothing written" }

  # Written through a temporary in the same directory and moved into place, so a reader never
  # sees a half-written signature, and a crash mid-write cannot leave one either.
  $sigPath = Join-Path $ReleaseDir ("{0}.sig" -f $Platform)
  $tmpPath = "$sigPath.tmp"
  [System.IO.File]::WriteAllText($tmpPath, $sigHex)
  # Windows PowerShell 5.1 is .NET Framework, where File.Move has no overwrite overload and
  # File.Replace is the atomic one. Checked against the runtime rather than assumed: the
  # three-argument Move written here first would have thrown on the machine that signs.
  if (Test-Path -LiteralPath $sigPath) {
    [System.IO.File]::Replace($tmpPath, $sigPath, $null)
  } else {
    [System.IO.File]::Move($tmpPath, $sigPath)
  }

  Write-Output ("{0}.manifest  dir={1}  doc={2}B  sig={3}chars  selfVerify={4}" -f
                $Platform, $ReleaseDir, $doc.Length, $sigHex.Length, $ok)
}
finally {
  # All of it here, not on the success path. A throw above -- a short signature, a verification
  # that failed -- used to skip every Dispose and leave the CNG handles to the finalizer.
  if ($verifier) { $verifier.Dispose() }
  if ($signer)   { $signer.Dispose() }
  if ($key)      { $key.Dispose() }
  if ($tmpPath -and (Test-Path -LiteralPath $tmpPath)) {
    Remove-Item -LiteralPath $tmpPath -Force -ErrorAction SilentlyContinue
  }
  if ($priv) { [Array]::Clear($priv, 0, $priv.Length) }
}
exit 0
