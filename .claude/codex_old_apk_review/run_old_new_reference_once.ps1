# One reference only. Run old first; new once only after a valid old observation and confirmed cleanup.
param(
 [ValidateSet('old','new')][string]$Reference='old',
 [string]$LaneRoot='C:\work\gnlink-wt\old-apk-lane', [string]$OldRoot='C:\work\gnlink-wt\old-ref-163',
 [string]$LaneHead='', [string]$OldHead='', [string]$BuildDir='', [string]$HostSha='', [string]$ServerJs=''
)
$ErrorActionPreference='Stop'
$pins=@'
{
  "old": {
    "apps/native_poc/CMakeLists.txt": "b5cf8e3040c88b8dcf7f42e69b3c4ae7f31ee0efd48710da8c097c22e0f3981c",
    "apps/native_poc/src/host_args.cpp": "1d7e10e45a93271de6ab264f4a908c4b46daa303474e9cd1c24b39cfa14a4b04",
    "apps/native_poc/src/host_args.hpp": "c44bc8fb4d983dad778c1bc46898ecf1296a20ef839cb0e00d7c036de024d914",
    "apps/native_poc/src/host_log.hpp": "5b08054c195157311fd78ee4b33ddcf106077281b443ee2de4faaafce818b8ed",
    "apps/native_poc/src/host_power_policy.hpp": "84b2574d60393bd727e3a46c379aa9677aacdc456eb72a32b099caefa34951d7",
    "apps/native_poc/src/host_startup_control.cpp": "3b40ed56b516a50abaf061d8dcb140fb95cebfe9b0376e5002ffe8e5172a370d",
    "apps/native_poc/src/native_video_host_main.cpp": "adc19e9665451c9b983f15c51ed598d6510f80abcd52d66df76d210d94e7daf5",
    "apps/native_poc/src/udp_control_channel.cpp": "8705e92bc3097f865c4b74653485c9e03cd62476a9d5835fe42d1e52f0ad8b00",
    "apps/native_poc/src/udp_control_channel.hpp": "834a5d37a41e7dffeb86dd7527df760ef471d4af5d25ad47fc44b42f987b6f86",
    "apps/native_poc/src/udp_control_close_diag_test.cpp": "eae00e2cfbc42291e7887b3eccfe5982132367b3e30e52455997ff5c863482ab"
  },
  "new": {
    "apps/native_poc/CMakeLists.txt": "a7f9ce9ceda7f9f2777df9f683a6a03ad52ec20564d43572ab7832a890203c52",
    "apps/native_poc/src/host_startup_control.cpp": "a4898780d2d20026d422bf496c680afacf628d5294b0cd151f32d636d34a8dab",
    "apps/native_poc/src/udp_control_channel.cpp": "8705e92bc3097f865c4b74653485c9e03cd62476a9d5835fe42d1e52f0ad8b00",
    "apps/native_poc/src/udp_control_channel.hpp": "834a5d37a41e7dffeb86dd7527df760ef471d4af5d25ad47fc44b42f987b6f86",
    "apps/native_poc/src/udp_control_close_diag_test.cpp": "eae00e2cfbc42291e7887b3eccfe5982132367b3e30e52455997ff5c863482ab",
    "automation/old_apk_lane/oa_avd.py": "12ea33253c6ba77a485e67d05c657612e05992e65a53d0a477bb2b69664461c8",
    "automation/old_apk_lane/oa_pick_test.py": "1ab670a75082b79066363a04d8aced936db67dcc9ad1eecaadedda58bc42a974",
    "automation/old_apk_lane/oa_ref_builddir.ps1": "8131edacb7c63cdd95f64fcf4e5f0b66397906b2272052cf78563259b315c648",
    "automation/old_apk_lane/oa_relay_trace_server.py": "6b5f862cf5e166800fd7a6fb4a5862e4c73027acb06c17dc69ce7b380921b092",
    "automation/old_apk_lane/oa_slot.ps1": "9e5fe31a72b261409028bf71eb1b33decfd4585aa0aaaa796ca983d2fd260b63",
    "automation/old_apk_lane/relay_trace/relay_trace_53fced4.diff": "9883b4db456b3ac2f63305c85b29fddd4638853167170c03e9c1226da41a96a3",
    "automation/old_apk_lane/relay_trace/relay_trace_fae0e1f.diff": "e7d993ebeffe3fc44704bcfea3bd966ddea8a1ad0a9bdc9fec0a62d717b2e865",
    "automation/old_apk_lane/relay_trace/relay_trace_snap_test.js": "2cf7195192a6216c917844afff6f4b3defd214054035bb82c7ed61c8f6d122b6"
  }
}
'@ | ConvertFrom-Json
$tools=@'
{
  "remote60_mobile_abr_session_e2e_test.exe": "caf933cd668a20e5882d4dccab08d46b7a329a075f9f8259266bac31c48309b6",
  "e2e_job_run.exe": "248d9829004aa1b43f3bec4ab03b1b4b14b39c3b2604d74286b28c09f618f727",
  "remote60_ka_standby_holder_tool.exe": "e9ae941514db0c3f2c967136a94573103aafa354c7e982bb792841aa231e4084",
  "GNLinkViewerKaTest.exe": "6806dd41acb7f2736736569c2fcd6024c5c977ca65ee7dfa73bb367aa22e2a5d"
}
'@ | ConvertFrom-Json
$deps=@'
{
  "accounts.js": "e69e862e9b53d0144e49fdbcacd8084a9011bd73a7ee89195303b41849514ad3",
  "device_credentials.js": "d5ca2991250013159594282a93f1d703d9718f8492804e5a7e84237c58f186f5",
  "relay_diag.js": "0fe629172bf9caf8dfc7b66734b01159c6d9f5e98268b43384609322e9edb199",
  "update_manifest.js": "a11254aa3fb007b6c00f184d6ad7aaa36f1a1e181ae083ad238e2eb658b641bf",
  "version_compare.js": "5117b0a4a825c85c5da07ab3b09ef96a591d3cf62e3a9ba70423e775f54139d3",
  "wake_target.js": "48d3b480ffe710976b94c40eb0148cb6771d7c906255fac2e2eee494f8936c45",
  "package.json": "ceafbdf2a1d14a4fab07701427cb760aee7e9f1cdc2e373211f741fe0da40680"
}
'@ | ConvertFrom-Json
function Canonical([string]$path) {
 $t=[Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($path)).TrimStart([char]0xfeff).Replace("`r`n","`n")
 $a=[Security.Cryptography.SHA256]::Create()
 try { return [BitConverter]::ToString($a.ComputeHash([Text.Encoding]::UTF8.GetBytes($t))).Replace('-','').ToLowerInvariant() } finally { $a.Dispose() }
}
function CheckTree([string]$repo,[string]$head,[string]$base,$expected) {
 if ($head -notmatch '^[0-9a-fA-F]{40}$' -or (git -C $repo rev-parse HEAD) -ne $head) { throw "wrong pinned HEAD: $repo" }
 git -C $repo merge-base --is-ancestor $base $head
 if ($LASTEXITCODE -ne 0) { throw "wrong reference base: $repo" }
 $names=@(git -C $repo diff --name-only $base $head)
 $allowed=@('apps/native_poc/src/udp_control_channel.cpp','apps/native_poc/src/udp_control_close_diag_test.cpp')
 if ($LASTEXITCODE -ne 0 -or $names.Count -ne 2 -or @($names | Where-Object {$_ -notin $allowed}).Count -ne 0) { throw "only the exact two-file first-loss patch is approved: $repo" }
 if (@(git -C $repo status --porcelain --untracked-files=no).Count -ne 0) { throw "tracked changes: $repo" }
 foreach($v in $expected.psobject.Properties) { if ((Canonical (Join-Path $repo $v.Name)) -ne $v.Value) { throw "source hash mismatch: $repo / $($v.Name)" } }
}
CheckTree $OldRoot $OldHead 'ff98f192ad4aca617a28ead8912ac5f2bf6e50aa' $pins.old
CheckTree $LaneRoot $LaneHead '7f6c7feacd184f451a18e6cfa2b4702661609a94' $pins.new
if ($BuildDir -notmatch '^[A-Za-z]:[\\/]' -or $ServerJs -notmatch '^[A-Za-z]:[\\/]') { throw 'build/server paths must be drive-absolute' }
$build=[IO.Path]::GetFullPath($BuildDir);$server=[IO.Path]::GetFullPath($ServerJs)
if (-not $build.StartsWith("$LaneRoot\.claude\oa_ref_build\",[StringComparison]::OrdinalIgnoreCase) -or
    -not $server.StartsWith("$LaneRoot\.claude\oa_relay_server\",[StringComparison]::OrdinalIgnoreCase)) { throw 'build/server outside the approved fixture anchors' }
foreach($v in $tools.psobject.Properties) { if ((Get-FileHash -LiteralPath "$build\$($v.Name)").Hash.ToLower() -ne $v.Value) { throw "tool mismatch: $($v.Name)" } }
if ($HostSha -notmatch '^[0-9a-fA-F]{64}$' -or (Get-FileHash -LiteralPath "$build\GNLinkStreamPinnedSelectTest.exe").Hash.ToLower() -ne $HostSha.ToLower()) { throw 'recorded rebuilt host SHA is required' }
$hostBytes=[Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes("$build\GNLinkStreamPinnedSelectTest.exe"))
foreach($marker in @('close-diag-first-loss-v1','diag{ack seen=','host power management=off','pinned_test_seam_refused')) { if (-not $hostBytes.Contains($marker)) { throw "wrong test host / old diagnostic binary: $marker" } }
$serverSha=if($Reference -eq 'old') {'196405dd60a82c46bab2e57a563b19822517710a92e87d17f62591d3801088d5'} else {'18f6b6d211f0337d9f38db1b689ea20e74261225ab0827f5a0132708205e879e'}
if ((Get-FileHash -LiteralPath $server).Hash.ToLower() -ne $serverSha) { throw 'wrong relay trace reference' }
foreach($v in $deps.psobject.Properties) { if ((Get-FileHash -LiteralPath (Join-Path (Split-Path $server) $v.Name)).Hash.ToLower() -ne $v.Value) { throw "directory dependency mismatch: $($v.Name)" } }
$apk='C:\work\gnlink-wt\old-apk-lane\apk_out\apk-0232-fixture-r2.apk'
if ((Get-FileHash -LiteralPath $apk).Hash.ToLower() -ne '50080b2bd586ee66ce446ab8f647ce4e5c11fafc22593d15bef7e948d2816962'.Replace(' ','')) { throw 'wrong S APK' }
if (@($LaneRoot,$OldRoot,$build,$server,$apk | Where-Object {$_ -match '\s'}).Count -ne 0) { throw 'this reviewed launcher requires the existing space-free paths' }
$pin='7035cf5ec7f99f7b5662a9f5395f9dccfc3b6fe42583d0b9550124a0c3496606'
$scope=if($Reference -eq 'old') {'to-3w'} else {'full'}
$tag="ref-$Reference-$(Get-Date -Format yyyyMMddHHmmssfff)-$PID"
$slot='C:\work\gnlink-wt\SLOT_REQUEST.txt';$mine=$false;$safeEnd=$false;$deadline=(Get-Date).AddMinutes(2)
while(-not $mine -and (Get-Date) -lt $deadline) {
 try {$f=[IO.File]::Open($slot,'CreateNew','Write');try{$b=[Text.Encoding]::UTF8.GetBytes("reference $tag lane=$LaneHead old=$OldHead");$f.Write($b,0,$b.Length)}finally{$f.Dispose()};$mine=$true}catch{Start-Sleep -Seconds 5}
}
if(-not $mine){throw 'slot busy; nothing run'}
try {
 Remove-Item env:OA_TEST_FAKE_ADB -ErrorAction SilentlyContinue
 $env:KE_RELAY_TRACE='1';$env:OA_OBSERVE_SEC='30';$env:OA_PM_CLEAR_TIMEOUT_SEC='60'
 $launchArgs=@('-NoProfile','-ExecutionPolicy','Bypass','-File',"$LaneRoot\automation\old_apk_lane\oa_slot.ps1",'-Tag',$tag,'-Arm','S','-Apk',$apk,'-BuildDir',$build,'-JobRun',"$build\e2e_job_run.exe",'-ServerJs',$server,'-ServerSha256',$serverSha,'-Scope',$scope,'-AdbSha256',$pin)
 $child=Start-Process -FilePath powershell.exe -ArgumentList $launchArgs -WindowStyle Hidden -Wait -PassThru
 if($null -eq $child -or $null -eq $child.ExitCode){throw 'unknown launcher result; no successful reference claimed'}
 $roots=@(Get-ChildItem -LiteralPath "$LaneRoot\.claude\oa_runs" -Directory | Where-Object {$_.Name.StartsWith($tag+'-')})
 if($roots.Count -ne 1){throw 'cannot identify this run; cleanup unconfirmed, slot retained'}
 $text=Get-Content -LiteralPath "$($roots[0].FullName)\slot.txt" -Raw
 $safeEnd=($text -match 'job_run: child_exit=-?[0-9]+ residual=0 forced=[01] ended_confirmed=1')
 "reference=$Reference source=$LaneHead oldSource=$OldHead host=$HostSha server=$serverSha scope=$scope run=$($roots[0].FullName) rc=$($child.ExitCode) cleanupConfirmed=$safeEnd"
 if(-not $safeEnd){throw 'cleanup unconfirmed; slot retained, next reference forbidden'}
 exit ([int]$child.ExitCode)
} finally {
 Remove-Item env:KE_RELAY_TRACE -ErrorAction SilentlyContinue
 if($mine -and $safeEnd){Remove-Item -LiteralPath $slot -Force}else{Write-Warning "slot retained for cleanup verification: $slot"}
}
