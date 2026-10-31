$ErrorActionPreference = 'Continue'
# Wait until the verifier's independent batch (on the same AVD, slot and sh_r2 APK files) has been quiet for 5 minutes:
# no slot file, no sh_run / harness process. Then keep the candidate it judged (1d4a05f4) under its own name and run ours.
$quietSince = $null
$deadline = (Get-Date).AddHours(6)
while ((Get-Date) -lt $deadline) {
  $busy = (Test-Path 'C:\work\gnlink-wt\SLOT_REQUEST.txt') -or
          @(Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -match 'sh_run\.ps1|sh_filter_avd\.py|viewer_prelogin_login_e2e_test' }).Count -gt 0
  if ($busy) { $quietSince = $null } elseif (-not $quietSince) { $quietSince = Get-Date }
  if ($quietSince -and ((Get-Date) - $quietSince).TotalMinutes -ge 5) { break }
  Start-Sleep -Seconds 30
}
"quiet from $quietSince, starting at $(Get-Date)"
$rel = 'D:\remote\remote\.claude\rel-apk-0232'
New-Item -ItemType Directory -Force "$rel\superseded" | Out-Null
$old = "$rel\GNLink-0.2.32.apk"
$h = (Get-FileHash $old).Hash.ToLower()
Copy-Item $old "$rel\superseded\GNLink-0.2.32-$($h.Substring(0,8)).apk" -Force
"kept the previous candidate as superseded\GNLink-0.2.32-$($h.Substring(0,8)).apk ($h)"
& D:\remote\remote\.claude\caplife\rel0232_r6_batch.ps1
