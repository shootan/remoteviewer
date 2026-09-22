# Compares what the installer carries with what the release ships.
#
# GNLinkSetup.exe embeds the other nine files as RT_RCDATA resources, and the update replaces the
# same nine on disk. Those two sets can disagree: the installer links against a staging directory
# CMake fills, while the release payload is copied out of the build tree by the release script. A
# stale staging copy, a target that did not relink, a payload assembled from a different build --
# each produces an installer that installs something other than what was gated, signed and
# published, and nothing downstream notices because both halves are internally consistent.
#
# 0.2.133 was checked this way by hand. This is that check.
#
#   gnlink_check_installer_payload.ps1 -Setup <GNLinkSetup.exe> -Payload <payload dir>
#
# Exit 0 = all nine match, byte for byte. Anything else = do not publish.

param(
  [Parameter(Mandatory = $true)][string]$Setup,
  [Parameter(Mandatory = $true)][string]$Payload
)

$ErrorActionPreference = 'Stop'

# Ids from apps/native_poc/installer/installer_ids.h, names from the table in installer_main.cpp.
# Kept in the same order as that table so a diff against it reads straight.
$expected = @(
  @{ Id = 200; Path = 'GNLinkHost.exe' },
  @{ Id = 201; Path = 'GNLinkStream.exe' },
  @{ Id = 202; Path = 'GNLinkInputService.exe' },
  @{ Id = 203; Path = 'GNLinkCapture.exe' },
  @{ Id = 204; Path = 'GNLinkClient.exe' },
  @{ Id = 205; Path = 'GNLinkViewer.exe' },
  @{ Id = 208; Path = 'GNLinkUpdater.exe' },
  @{ Id = 206; Path = 'ui\shell.html' },
  @{ Id = 207; Path = 'ui\macro.html' }
)

if (-not (Test-Path -LiteralPath $Setup))   { Write-Output "no installer at $Setup"; exit 1 }
if (-not (Test-Path -LiteralPath $Payload)) { Write-Output "no payload at $Payload"; exit 1 }

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class GnlinkRes {
  [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
  public static extern IntPtr LoadLibraryEx(string lpFileName, IntPtr hFile, uint dwFlags);
  [DllImport("kernel32.dll", SetLastError = true)]
  public static extern bool FreeLibrary(IntPtr hModule);
  [DllImport("kernel32.dll", SetLastError = true)]
  public static extern IntPtr FindResource(IntPtr hModule, IntPtr lpName, IntPtr lpType);
  [DllImport("kernel32.dll", SetLastError = true)]
  public static extern IntPtr LoadResource(IntPtr hModule, IntPtr hResInfo);
  [DllImport("kernel32.dll")]
  public static extern IntPtr LockResource(IntPtr hResData);
  [DllImport("kernel32.dll", SetLastError = true)]
  public static extern uint SizeofResource(IntPtr hModule, IntPtr hResInfo);

  // LOAD_LIBRARY_AS_DATAFILE: map it for reading, do not run anything in it.
  public const uint AsDataFile = 0x00000002;
  public static readonly IntPtr RT_RCDATA = new IntPtr(10);

  public static byte[] Read(IntPtr module, int id) {
    IntPtr info = FindResource(module, new IntPtr(id), RT_RCDATA);
    if (info == IntPtr.Zero) return null;
    uint size = SizeofResource(module, info);
    IntPtr data = LoadResource(module, info);
    if (data == IntPtr.Zero) return null;
    IntPtr p = LockResource(data);
    if (p == IntPtr.Zero) return null;
    byte[] bytes = new byte[size];
    Marshal.Copy(p, bytes, 0, (int)size);
    return bytes;
  }
}
'@

$setupFull = (Resolve-Path -LiteralPath $Setup).Path
$module = [GnlinkRes]::LoadLibraryEx($setupFull, [IntPtr]::Zero, [GnlinkRes]::AsDataFile)
if ($module -eq [IntPtr]::Zero) {
  Write-Output "could not open $setupFull as a resource file"
  exit 1
}

$sha = [System.Security.Cryptography.SHA256]::Create()
$failures = 0
$matched  = 0
try {
  foreach ($item in $expected) {
    $file = Join-Path $Payload $item.Path
    if (-not (Test-Path -LiteralPath $file)) {
      Write-Output ("MISSING  {0}  (id {1}) is not in the payload" -f $item.Path, $item.Id)
      $failures++
      continue
    }
    $embedded = [GnlinkRes]::Read($module, $item.Id)
    if ($null -eq $embedded) {
      Write-Output ("MISSING  resource {0} for {1} is not in the installer" -f $item.Id, $item.Path)
      $failures++
      continue
    }
    $onDisk = [System.IO.File]::ReadAllBytes($file)
    $a = ([System.BitConverter]::ToString($sha.ComputeHash($embedded)) -replace '-', '').ToLowerInvariant()
    $b = ([System.BitConverter]::ToString($sha.ComputeHash($onDisk)) -replace '-', '').ToLowerInvariant()
    if ($a -eq $b) {
      $matched++
      Write-Output ("ok       {0,-24} id {1}  {2}B  {3}" -f $item.Path, $item.Id, $onDisk.Length, $a.Substring(0, 16))
    } else {
      # Sizes too: "different bytes, same length" and "a different build entirely" are worth
      # telling apart when this fires.
      Write-Output ("DIFFERS  {0,-24} id {1}  installer {2}B {3}  payload {4}B {5}" -f
                    $item.Path, $item.Id, $embedded.Length, $a.Substring(0, 16),
                    $onDisk.Length, $b.Substring(0, 16))
      $failures++
    }
  }
} finally {
  [void][GnlinkRes]::FreeLibrary($module)
  $sha.Dispose()
}

Write-Output ("{0}/{1} embedded payloads match the release payload" -f $matched, $expected.Count)
if ($failures -gt 0) {
  Write-Output "$failures mismatch(es): the installer does not carry what this release ships"
  exit 1
}
exit 0
