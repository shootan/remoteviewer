# Read and signed as BYTES (line endings are part of what is signed). The private key is unwrapped
# from its DPAPI blob into memory, used, and cleared before exit -- never written, logged or printed.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Security

$keyDir = Join-Path $env:LOCALAPPDATA 'GNLink\ReleaseSigning\p256-a0e184579c5d4c2d'
if (-not (Test-Path $keyDir)) { Write-Output "no key at $keyDir"; exit 1 }

# (release dir, platform-name) pairs -- the two manifests live in different version dirs.
$targets = @(
  @{ dir = 'D:\remote\remote-worktrees\host-pc-recovery\.claude\rel\0.2.131'; name = 'windows' }
)

$priv = $null
try {
  $blob = [System.IO.File]::ReadAllBytes((Join-Path $keyDir 'private.ecc.dpapi'))
  $priv = [System.Security.Cryptography.ProtectedData]::Unprotect(
    $blob, $null, [System.Security.Cryptography.DataProtectionScope]::CurrentUser)
  $key = [System.Security.Cryptography.CngKey]::Import(
    $priv, [System.Security.Cryptography.CngKeyBlobFormat]::EccPrivateBlob)
  $signer = New-Object System.Security.Cryptography.ECDsaCng($key)
  $signer.HashAlgorithm = [System.Security.Cryptography.CngAlgorithm]::Sha256

  $pubHex = ([System.IO.File]::ReadAllText((Join-Path $keyDir 'public_xy.hex'))).Trim()
  $xy = New-Object byte[] 64
  for ($i = 0; $i -lt 64; $i++) { $xy[$i] = [Convert]::ToByte($pubHex.Substring($i * 2, 2), 16) }

  foreach ($t in $targets) {
    $docPath = Join-Path $t.dir ("{0}.manifest" -f $t.name)
    if (-not (Test-Path $docPath)) { throw "no manifest at $docPath" }
    $doc = [System.IO.File]::ReadAllBytes($docPath)
    $sig = $signer.SignData($doc)
    if ($sig.Length -ne 64) { throw "signature is $($sig.Length) bytes, expected 64" }
    $sigHex = ([System.BitConverter]::ToString($sig) -replace '-', '').ToLowerInvariant()
    [System.IO.File]::WriteAllText((Join-Path $t.dir ("{0}.sig" -f $t.name)), $sigHex)

    $params = New-Object System.Security.Cryptography.ECParameters
    $params.Curve = [System.Security.Cryptography.ECCurve]::CreateFromFriendlyName('nistP256')
    $point = New-Object System.Security.Cryptography.ECPoint
    $point.X = $xy[0..31]; $point.Y = $xy[32..63]
    $params.Q = $point
    $verifier = [System.Security.Cryptography.ECDsa]::Create($params)
    $ok = $verifier.VerifyData($doc, $sig, [System.Security.Cryptography.HashAlgorithmName]::SHA256)
    $verifier.Dispose()
    Write-Output ("{0}.manifest  dir={1}  doc={2}B  sig={3}chars  selfVerify={4}" -f $t.name, $t.dir, $doc.Length, $sigHex.Length, $ok)
    if (-not $ok) { throw "the signature just written does not verify" }
  }
  $signer.Dispose(); $key.Dispose()
}
finally {
  if ($priv) { [Array]::Clear($priv, 0, $priv.Length) }
}
