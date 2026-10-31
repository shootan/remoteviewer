# Stops the key folder inheriting, and names the only three principals that may reach it.
#
# Scope is one folder and the files in it. Nothing above it is touched: %LOCALAPPDATA% and the
# GNLink and ReleaseSigning folders keep the permissions they already had, because changing ACLs
# on directories other people's things live in is not what was asked for and not something to do
# on the way past.
#
# The original ACL is saved first. Getting this wrong locks the current user out of the folder
# holding a DPAPI-protected private key -- recoverable, since an owner can always rewrite
# permissions, but not something to discover without a way back.

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Security

$dir = Join-Path $env:LOCALAPPDATA 'GNLink\ReleaseSigning\p256-a0e184579c5d4c2d'
$backup = 'D:\remote\remote\.claude\key_acl_before.clixml'

if (-not (Test-Path $dir)) { Write-Output "no key folder at $dir"; exit 1 }

$me = [System.Security.Principal.WindowsIdentity]::GetCurrent()
$mySid = $me.User
Write-Output "USER   $($me.Name)  $($mySid.Value)"

# ---------------------------------------------------------------- before
$before = Get-Acl $dir
$before | Export-Clixml -Path $backup
Write-Output "SAVED  $backup"
Write-Output "BEFORE owner=$($before.Owner)"
foreach ($ace in $before.Access) {
  Write-Output ("BEFORE ACE {0} | {1} | inherited={2}" -f $ace.IdentityReference.Value, $ace.FileSystemRights, $ace.IsInherited)
}

# The owner has to be the account that will still need in. If it were not, breaking inheritance
# could leave nobody able to read the folder without taking ownership first.
if ($before.Owner -ne $me.Name) {
  Write-Output "REFUSED: owner is '$($before.Owner)', not the current user '$($me.Name)'"
  exit 2
}

try {
  # ---------------------------------------------------------------- apply
  $acl = Get-Acl $dir
  # $true = protect from inheritance, $false = do NOT copy the inherited entries down. Copying
  # them would keep exactly the entry this is meant to remove.
  $acl.SetAccessRuleProtection($true, $false)
  foreach ($existing in @($acl.Access)) { [void]$acl.RemoveAccessRule($existing) }

  $inherit = [System.Security.AccessControl.InheritanceFlags]'ContainerInherit, ObjectInherit'
  $none = [System.Security.AccessControl.PropagationFlags]::None
  $allow = [System.Security.AccessControl.AccessControlType]::Allow
  $full = [System.Security.AccessControl.FileSystemRights]::FullControl

  $principals = @(
    $mySid,
    (New-Object System.Security.Principal.SecurityIdentifier('S-1-5-18')),   # NT AUTHORITY\SYSTEM
    (New-Object System.Security.Principal.SecurityIdentifier('S-1-5-32-544')) # BUILTIN\Administrators
  )
  foreach ($p in $principals) {
    $acl.AddAccessRule((New-Object System.Security.AccessControl.FileSystemAccessRule(
      $p, $full, $inherit, $none, $allow)))
  }
  Set-Acl -Path $dir -AclObject $acl

  # The files were inheriting, so replacing the parent's inheritable entries propagates to them --
  # but "should propagate" is not a check. Each one is looked at below.

  # ---------------------------------------------------------------- (a) can this user still read
  $names = @('private.ecc.dpapi', 'public_xy.hex', 'spki_sha256.txt', 'README.txt')
  foreach ($n in $names) {
    $p = Join-Path $dir $n
    $len = ([System.IO.File]::ReadAllBytes($p)).Length
    Write-Output ("READ   {0} {1} bytes" -f $n, $len)
  }

  # ---------------------------------------------------------------- (b) the key still signs
  $priv = $null
  try {
    $protectedBlob = [System.IO.File]::ReadAllBytes((Join-Path $dir 'private.ecc.dpapi'))
    $priv = [System.Security.Cryptography.ProtectedData]::Unprotect(
      $protectedBlob, $null, [System.Security.Cryptography.DataProtectionScope]::CurrentUser)
    $key = [System.Security.Cryptography.CngKey]::Import(
      $priv, [System.Security.Cryptography.CngKeyBlobFormat]::EccPrivateBlob)
    $signer = New-Object System.Security.Cryptography.ECDsaCng($key)
    $signer.HashAlgorithm = [System.Security.Cryptography.CngAlgorithm]::Sha256
    $probe = [System.Text.Encoding]::UTF8.GetBytes('gnlink acl hardening probe')
    $sig = $signer.SignData($probe)

    $xyHex = ([System.IO.File]::ReadAllText((Join-Path $dir 'public_xy.hex'))).Trim()
    $xy = New-Object byte[] 64
    for ($i = 0; $i -lt 64; $i++) { $xy[$i] = [Convert]::ToByte($xyHex.Substring($i * 2, 2), 16) }
    $verifyBlob = New-Object byte[] 72
    [Array]::Copy([byte[]](0x45,0x43,0x53,0x31), 0, $verifyBlob, 0, 4)
    [Array]::Copy([System.BitConverter]::GetBytes([int]32), 0, $verifyBlob, 4, 4)
    [Array]::Copy($xy, 0, $verifyBlob, 8, 64)
    $vk = [System.Security.Cryptography.CngKey]::Import(
      $verifyBlob, [System.Security.Cryptography.CngKeyBlobFormat]::EccPublicBlob)
    $verifier = New-Object System.Security.Cryptography.ECDsaCng($vk)
    $verifier.HashAlgorithm = [System.Security.Cryptography.CngAlgorithm]::Sha256
    $ok = $verifier.VerifyData($probe, $sig)
    $probe[0] = $probe[0] -bxor 0x01
    $rejects = -not $verifier.VerifyData($probe, $sig)
    Write-Output ("SIGN   verify={0} rejects-tampered={1}" -f $ok, $rejects)
    if (-not ($ok -and $rejects)) { throw 'the key no longer signs and verifies as expected' }
    $signer.Dispose(); $verifier.Dispose(); $key.Dispose(); $vk.Dispose()
  }
  finally {
    if ($priv) { [Array]::Clear($priv, 0, $priv.Length) }
  }

  # ---------------------------------------------------------------- (c) nobody unexpected
  $expected = @($mySid.Value, 'S-1-5-18', 'S-1-5-32-544', $me.Name, 'NT AUTHORITY\SYSTEM', 'BUILTIN\Administrators')
  $after = Get-Acl $dir
  Write-Output "AFTER  owner=$($after.Owner)  protected=$($after.AreAccessRulesProtected)"
  $unexpected = @()
  foreach ($ace in $after.Access) {
    $who = $ace.IdentityReference.Value
    Write-Output ("AFTER  ACE {0} | {1} | inherited={2}" -f $who, $ace.FileSystemRights, $ace.IsInherited)
    if ($expected -notcontains $who) { $unexpected += $who }
  }
  foreach ($n in $names) {
    $fa = Get-Acl (Join-Path $dir $n)
    foreach ($ace in $fa.Access) {
      $who = $ace.IdentityReference.Value
      if ($expected -notcontains $who) { $unexpected += "$n -> $who" }
    }
  }
  if ($unexpected.Count -gt 0) {
    Write-Output ("UNEXPECTED " + ($unexpected -join '; '))
    throw 'principals other than the three named ones still have access'
  }
  Write-Output "UNEXPECTED none"
}
catch {
  Write-Output "FAILED: $_"
  # Back to what was there. This restores permissions; it does not delete anything.
  try {
    $orig = Import-Clixml -Path $backup
    Set-Acl -Path $dir -AclObject $orig
    Write-Output "RESTORED the original ACL from $backup"
  } catch {
    Write-Output "RESTORE ALSO FAILED: $_  -- original ACL is in $backup"
  }
  exit 1
}
