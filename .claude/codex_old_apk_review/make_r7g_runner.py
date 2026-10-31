from pathlib import Path
import hashlib
p=Path(__file__).parent
s=(p/'run_old_apk_lane_r7_reviewed.ps1').read_text(encoding='utf-8-sig')
s='''param(
  [ValidatePattern('^[0-9a-fA-F]{40}$')][string]$CandidateHead = '',
  [string]$TraceServerJs = ''
)
# Reviewed b11fc0d + exact watch/stop and stub-split patches; only the two approved files may change.
'''+s
s=s.replace("$ErrorActionPreference = 'Continue'", "$ErrorActionPreference = 'Stop'")
s=s.replace('# Reviewed r7: pinned 548d199, same five exe gates; first nonzero arm stops, finally retains slot cleanup.', '# Based on reviewed r7 runner 5533a518; five exe gates, first-nonzero stop, and finally slot cleanup retained.')
old="if ((git -C $wt rev-parse HEAD) -ne '548d19911d85c503f4b650d8bfd5ac1e0afada19') { \"worktree not at 548d199 -- nothing run\" | Tee-Object -Append $log; exit 2 }"
new=r'''$baseHead = 'b11fc0d40b49e086ed3df9bcbfcaa467fd7318a2'
if ((git -C $wt rev-parse HEAD) -ne $CandidateHead) { "worktree not at the pinned candidate -- nothing run" | Tee-Object -Append $log; exit 2 }
git -C $wt merge-base --is-ancestor $baseHead $CandidateHead
if ($LASTEXITCODE -ne 0) { "candidate is not based on reviewed b11fc0d -- nothing run" | Tee-Object -Append $log; exit 2 }
$changed = @(git -C $wt diff --name-only $baseHead $CandidateHead)
$diffRc = $LASTEXITCODE
$allowed = @('automation/old_apk_lane/oa_avd.py', 'automation/old_apk_lane/oa_pick_test.py')
$unexpected = @($changed | Where-Object { $_ -notin $allowed })
if ($diffRc -ne 0 -or $changed.Count -ne 2 -or $unexpected.Count -ne 0) {
  "candidate contains changes outside the two approved patches -- nothing run" | Tee-Object -Append $log; exit 2
}
$sourcePath = "$wt\automation\old_apk_lane\oa_avd.py"
$text = [Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($sourcePath)).TrimStart([char]0xfeff).Replace("`r`n", "`n")
$sha = [Security.Cryptography.SHA256]::Create()
try { $sourceSha = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($text))).Replace('-', '').ToLowerInvariant() } finally { $sha.Dispose() }
if ($sourceSha -ne 'c2b238d66aaaec05c77c22b730b528e5379ce0369b323e0a829c8bcc3577088b') {
  "candidate's oa_avd.py is not the approved UTF-8/LF source -- nothing run" | Tee-Object -Append $log; exit 2
}
$testPath = "$wt\automation\old_apk_lane\oa_pick_test.py"
$testText = [Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($testPath)).TrimStart([char]0xfeff).Replace("`r`n", "`n")
$testAlg = [Security.Cryptography.SHA256]::Create()
try { $testSourceSha = [BitConverter]::ToString($testAlg.ComputeHash([Text.Encoding]::UTF8.GetBytes($testText))).Replace('-', '').ToLowerInvariant() } finally { $testAlg.Dispose() }
if ($testSourceSha -ne 'de8fbf819af2664bdef0d96e7da9927b1e777230e0e648b9d4dc9a9fae4fec1f') {
  "candidate's stub split is not the approved UTF-8/LF source -- nothing run" | Tee-Object -Append $log; exit 2
}
'''
assert s.count(old)==1;s=s.replace(old,new)
needle='$server = "$wt\\apps\\directory\\server.js"'
guard=r'''$traceSha = '52918c04b8fef8f4e66af5c6abe4a7332429416397004432accb79d8c231492a'
if ($TraceServerJs -notmatch '^[A-Za-z]:[\\/]') { "trace path must be drive-absolute -- nothing run" | Tee-Object -Append $log; exit 2 }
$traceFull = [IO.Path]::GetFullPath($TraceServerJs)
if (-not $traceFull.StartsWith("$wt\.claude\oa_relay_server\", [StringComparison]::OrdinalIgnoreCase) -or
    -not (Test-Path -LiteralPath $traceFull -PathType Leaf) -or
    (Get-FileHash -LiteralPath $traceFull).Hash.ToLower() -ne $traceSha) {
  "trace copy is outside the fixture anchor or not pinned -- nothing run" | Tee-Object -Append $log; exit 2
}
$plainRawSha = (Get-FileHash -LiteralPath $server).Hash.ToLower()
$plainText = [Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($server)).TrimStart([char]0xfeff).Replace("`r`n", "`n")
$plainAlg = [Security.Cryptography.SHA256]::Create()
try { $plainSourceSha = [BitConverter]::ToString($plainAlg.ComputeHash([Text.Encoding]::UTF8.GetBytes($plainText))).Replace('-', '').ToLowerInvariant() } finally { $plainAlg.Dispose() }
if ($plainSourceSha -ne '2742143d72bb9482429f3046109b0a833bbf8a5049a3b26a8e1eac2f586c4bb7') {
  "plain candidate server changed -- nothing run" | Tee-Object -Append $log; exit 2
}
'''
assert s.count(needle)==1;s=s.replace(needle,needle+'\n'+guard)
needle="  $env:OA_PM_CLEAR_TIMEOUT_SEC = '60'"
assert s.count(needle)==1;s=s.replace(needle,needle+"\n  $env:OA_OBSERVE_SEC = '30'\n  \"pinned source $CandidateHead oa_avd.py=$sourceSha stub=$testSourceSha trace=$traceSha plainRaw=$plainRawSha plainLF=$plainSourceSha\" | Tee-Object -Append $log")
needle='    $t0 = Get-Date'
assert s.count(needle)==1
s=s.replace(needle,"""    $env:KE_RELAY_TRACE = if ($arm.arm -eq 'S') { '1' } else { '0' }
    $armServer = if ($arm.arm -eq 'S') { $traceFull } else { $server }
    $t0 = Get-Date""")
needle="'-ServerJs', $server, '-AdbSha256'"
assert s.count(needle)==1;s=s.replace(needle,"'-ServerJs', $armServer, '-AdbSha256'")
needle="    if ($arm.sb) { $al += '-WithStandby' }"
assert s.count(needle)==1;s=s.replace(needle,"    if ($arm.arm -eq 'S') { $al += @('-ServerSha256', $traceSha) }\n"+needle)
out=p/'run_old_apk_lane_r7g_reviewed.ps1';out.write_text(s,encoding='utf-8',newline='\n')
print(out,hashlib.sha256(out.read_bytes()).hexdigest())
