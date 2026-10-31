# Reads the installer's RT_RCDATA resources as BYTES and hashes them, then compares against the
# payload files the build produced. A string search for a file name proves only that a name is in
# the binary; this proves the bytes are.
param(
  [string]$Exe = "dist\GNLinkSetup-0.2.106.exe",
  [string]$PayloadDir = "build-local\apps\native_poc\installer_payload"
)

$ErrorActionPreference = "Stop"

Add-Type -Namespace Win32 -Name Res -MemberDefinition @'
[DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
public static extern IntPtr LoadLibraryEx(string lpFileName, IntPtr hFile, uint dwFlags);
[DllImport("kernel32.dll", SetLastError=true)]
public static extern IntPtr FindResource(IntPtr hModule, IntPtr lpName, IntPtr lpType);
[DllImport("kernel32.dll", SetLastError=true)]
public static extern IntPtr LoadResource(IntPtr hModule, IntPtr hResInfo);
[DllImport("kernel32.dll", SetLastError=true)]
public static extern IntPtr LockResource(IntPtr hResData);
[DllImport("kernel32.dll", SetLastError=true)]
public static extern uint SizeofResource(IntPtr hModule, IntPtr hResInfo);
[DllImport("kernel32.dll", SetLastError=true)]
public static extern bool FreeLibrary(IntPtr hModule);
'@

$ids = @{
  200 = "GNLinkHost.exe"; 201 = "GNLinkStream.exe"; 202 = "GNLinkInputService.exe";
  203 = "GNLinkCapture.exe"; 204 = "GNLinkClient.exe"; 205 = "GNLinkViewer.exe";
  206 = "shell.html"; 207 = "macro.html"; 208 = "GNLinkUpdater.exe"
}

$LOAD_LIBRARY_AS_DATAFILE = 0x2
$h = [Win32.Res]::LoadLibraryEx((Resolve-Path $Exe), [IntPtr]::Zero, $LOAD_LIBRARY_AS_DATAFILE)
if ($h -eq [IntPtr]::Zero) { throw "could not open $Exe" }

$sha = [System.Security.Cryptography.SHA256]::Create()
$ok = 0
$bad = 0
foreach ($id in ($ids.Keys | Sort-Object)) {
  $name = $ids[$id]
  $info = [Win32.Res]::FindResource($h, [IntPtr]$id, [IntPtr]10)  # RT_RCDATA = 10
  if ($info -eq [IntPtr]::Zero) {
    Write-Output ("FAIL  resource {0} ({1}) is not in the installer" -f $id, $name)
    $bad++
    continue
  }
  $size = [Win32.Res]::SizeofResource($h, $info)
  $data = [Win32.Res]::LockResource([Win32.Res]::LoadResource($h, $info))
  $bytes = New-Object byte[] $size
  [System.Runtime.InteropServices.Marshal]::Copy($data, $bytes, 0, [int]$size)
  $embedded = ($sha.ComputeHash($bytes) | ForEach-Object { $_.ToString("x2") }) -join ""

  $file = Join-Path $PayloadDir $name
  if (-not (Test-Path $file)) {
    Write-Output ("FAIL  {0}: no payload file at {1}" -f $name, $file)
    $bad++
    continue
  }
  $onDisk = (Get-FileHash -Algorithm SHA256 $file).Hash.ToLower()
  if ($embedded -eq $onDisk) {
    Write-Output ("PASS  {0} id={1} {2} bytes sha256={3}" -f $name, $id, $size, $embedded)
    $ok++
  } else {
    Write-Output ("FAIL  {0} id={1}: embedded {2} != built {3}" -f $name, $id, $embedded, $onDisk)
    $bad++
  }
}
[void][Win32.Res]::FreeLibrary($h)
Write-Output ("{0} matched, {1} failed" -f $ok, $bad)
if ($bad -gt 0) { exit 1 }
