$ErrorActionPreference = 'Continue'
# Wait until the verifier's batch has been quiet for 5 minutes (no slot file, no runner / harness / e2e process), then
# run the keepalive-r2 (2) product-client e2e under the usual slot handling.
$quietSince = $null
$deadline = (Get-Date).AddHours(8)
while ((Get-Date) -lt $deadline) {
  $busy = (Test-Path 'C:\work\gnlink-wt\SLOT_REQUEST.txt') -or
          @(Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match 'sh_run\.ps1|sh_filter_avd\.py|_e2e_test\.exe' }).Count -gt 0
  if ($busy) { $quietSince = $null } elseif (-not $quietSince) { $quietSince = Get-Date }
  if ($quietSince -and ((Get-Date) - $quietSince).TotalMinutes -ge 5) { break }
  Start-Sleep -Seconds 30
}
"quiet from $quietSince, starting at $(Get-Date)"
$old = 'C:\work\gnlink-wt\fix-capture-lifecycle-baseline\Release\GNLinkStream.exe'
& D:\remote\remote\.claude\caplife\sh_run.ps1 -Tag ka2d_product_1 -NativeExe C:\work\gnlink-wt\ka-r3\build\apps\native_poc\Release\remote60_host_standby_admission_e2e_test.exe -Extra "--product-client --old-host $old"
Get-Content D:\remote\remote\.claude\caplife\sh_r2\ka2d_product_1.log -ErrorAction SilentlyContinue | Select-String '^(PASS|FAIL)' | ForEach-Object { $_.Line.Substring(0, [Math]::Min(170, $_.Line.Length)) }
"KA2D-DONE"
