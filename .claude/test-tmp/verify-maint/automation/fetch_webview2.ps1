# Fetches the WebView2 SDK the Windows client's interface is drawn with.
#
# Not vendored, because it is 45 MB of which the build uses two files: a header directory and a
# static loader. The CMake config skips the WebView2 targets when this has not been run, so a
# checkout without it still builds everything else.
#
# The SDK is the build-time half. The other half is the WebView2 runtime, which renders the pages
# at run time and ships with Edge -- present on Windows 11 and on any Windows 10 that has been
# updated this decade. The installer has to handle the machine where it is missing.

param(
  # Pinned rather than "latest": a silent SDK bump is not something a build should do on its own.
  [string]$Version = "1.0.4129.50",
  [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
$destination = Join-Path $repoRoot "third_party\webview2"
$marker = Join-Path $destination "build\native\include\WebView2.h"

if ((Test-Path $marker) -and -not $Force) {
  Write-Host "[webview2] already present at $destination (use -Force to refetch)"
  exit 0
}

$url = "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/$Version/microsoft.web.webview2.$Version.nupkg"
# A .nupkg is a zip, but Expand-Archive refuses the extension, so it is copied to one first.
$archive = Join-Path ([System.IO.Path]::GetTempPath()) "microsoft.web.webview2.$Version.zip"

Write-Host "[webview2] downloading $Version"
Invoke-WebRequest -Uri $url -OutFile $archive -TimeoutSec 300

if (Test-Path $destination) { Remove-Item -Recurse -Force $destination }
New-Item -ItemType Directory -Force -Path $destination | Out-Null
Expand-Archive -Path $archive -DestinationPath $destination -Force
Remove-Item $archive -Force

if (-not (Test-Path $marker)) {
  throw "unpacked but $marker is missing; the package layout may have changed"
}
Write-Host "[webview2] ready at $destination"
Write-Host "[webview2] re-run cmake configure so the WebView2 targets appear"
