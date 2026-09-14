$ErrorActionPreference = 'Stop'
$probeRoot = Join-Path $PSScriptRoot ('../.claude/gate-probe-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $probeRoot -Force | Out-Null
$hostPath = Join-Path $probeRoot 'host.log'
1..10 | ForEach-Object { '[native-video-host] encodedFrames=60 mbps=12' } | Set-Content -Encoding UTF8 $hostPath
$cases = @(
  @{Name='missing'; Present=''; Gap=''; Expected=2},
  @{Name='zero-present'; Present='d3dPresentSuccess=0 gdiFallbackPresented=0'; Gap='presentGapOver1s=0'; Expected=1},
  @{Name='freeze'; Present='d3dPresentSuccess=60 gdiFallbackPresented=0'; Gap='presentGapOver1s=3'; Expected=1},
  @{Name='stutter'; Present='d3dPresentSuccess=60 gdiFallbackPresented=0'; Gap='presentGapOver1s=0'; Expected=1},
  @{Name='healthy'; Present='d3dPresentSuccess=60 gdiFallbackPresented=0'; Gap='presentGapOver1s=0'; Expected=0}
)
$failures = 0
foreach ($case in $cases) {
  $clientPath = Join-Path $probeRoot ($case.Name + '.log')
  1..10 | ForEach-Object { '[native-video-client] recvFrames=60 decodedFrames=60 ' + $case.Present + ' ' + $case.Gap } |
    Set-Content -Encoding UTF8 $clientPath
  if ($case.Name -ne 'missing') {
    $gapUs = if ($case.Name -eq 'stutter') { 400000 } else { 16667 }
    1..10 | ForEach-Object { "[native-video-client][present] seq=$_ frameGapUs=$gapUs" } | Add-Content -Encoding UTF8 $clientPath
  }
  $output = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'summarize_wan_capture.ps1') -HostInput $hostPath -ClientInput $clientPath
  $result = $LASTEXITCODE
  $output | Set-Content -Encoding UTF8 (Join-Path $probeRoot ($case.Name + '-result.txt'))
  if ($result -ne $case.Expected) { ++$failures; Write-Output "FAIL $($case.Name) rc=$result expected=$($case.Expected)" }
  else { Write-Output "PASS $($case.Name) rc=$result" }
}
Write-Output "Evidence=$probeRoot Failures=$failures"
if ($failures) { exit 1 }
