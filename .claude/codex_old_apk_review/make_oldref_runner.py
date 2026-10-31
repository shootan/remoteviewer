from pathlib import Path
import json,re,subprocess,hashlib
p=Path(__file__).parent;data=Path('D:/remote/remote/.claude/caplife/oldref_SHA256SUMS.txt').read_text(encoding='utf-8-sig')
pins={'old':{},'new':{}}
for line in data.splitlines():
 if not line.strip():continue
 h,label=line.split(maxsplit=1);repo,path=label.split(':',1);pins['old' if repo.startswith('old-ref-163') else 'new'][path]=h
fix=json.loads((p/'oldref_r1/fixed_source.json').read_text())
for side in pins:pins[side].update(fix['files'])
deps={}
for n in ['accounts.js','device_credentials.js','relay_diag.js','update_manifest.js','version_compare.js','wake_target.js','package.json']:
 b=subprocess.check_output(['git','show','fae0e1f:apps/directory/'+n]);assert b==subprocess.check_output(['git','show','53fced4:apps/directory/'+n]);deps[n]=hashlib.sha256(b).hexdigest()
tools={}
m=Path('C:/work/gnlink-wt/old-apk-lane/.claude/oa_ref_build/ref-old-r1/MANIFEST.txt').read_text(encoding='utf-8-sig')
for h,n in re.findall(r'(?m)^([0-9a-f]{64})  (\S+)  \(tools\)',m):tools[n]=h
assert len(tools)==4
s=r'''# One reference only. Run old first; new once only after a valid old observation and confirmed cleanup.
param(
 [ValidateSet('old','new')][string]$Reference='old',
 [string]$LaneRoot='C:\work\gnlink-wt\old-apk-lane', [string]$OldRoot='C:\work\gnlink-wt\old-ref-163',
 [string]$LaneHead='', [string]$OldHead='', [string]$BuildDir='', [string]$HostSha='', [string]$ServerJs=''
)
$ErrorActionPreference='Stop'
$pins=@'
__PINS__
'@ | ConvertFrom-Json
$tools=@'
__TOOLS__
'@ | ConvertFrom-Json
$deps=@'
__DEPS__
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
if ((Get-FileHash -LiteralPath $apk).Hash.ToLower() -ne '50080b2bd586ee66ce446ab8f647ce4e5c11fa fc22593d15bef7e948d2816962'.Replace(' ','')) { throw 'wrong S APK' }
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
'''
s=s.replace('__PINS__',json.dumps(pins,indent=2)).replace('__TOOLS__',json.dumps(tools,indent=2)).replace('__DEPS__',json.dumps(deps,indent=2))
s=s.replace('50080b2bd586ee66ce446ab8f647ce4e5c11fa fc22593d15bef7e948d2816962','50080b2bd586ee66ce446ab8f647ce4e5c11fafc22593d15bef7e948d2816962')
out=p/'run_old_new_reference_once.ps1';out.write_text(s,encoding='utf-8',newline='\n')
print(out,hashlib.sha256(out.read_bytes()).hexdigest())
