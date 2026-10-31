from pathlib import Path
import hashlib
p=Path(__file__).parent
s=(p/'run_old_apk_lane_r7g_reviewed.ps1').read_text(encoding='utf-8-sig')
s=s.replace('# Reviewed b11fc0d + exact watch/stop and stub-split patches; only the two approved files may change.', '# Reviewed 98d71ef + exact public/internal IME distinction patch; three file hashes pinned.')
s=s.replace("$baseHead = 'b11fc0d40b49e086ed3df9bcbfcaa467fd7318a2'", "$baseHead = 'ff1c72a19aa2e8656d207047c5e076c656c4f540'\n$sourceBase = '98d71efca956f07d24009c8baa5416f2e5424a29'")
s=s.replace('merge-base --is-ancestor $baseHead $CandidateHead','merge-base --is-ancestor $sourceBase $CandidateHead')
s=s.replace('candidate is not based on reviewed b11fc0d','candidate is not based on reviewed 98d71ef')
s=s.replace("$allowed = @('automation/old_apk_lane/oa_avd.py', 'automation/old_apk_lane/oa_pick_test.py')", "$allowed = @('automation/old_apk_lane/oa_avd.py', 'automation/old_apk_lane/oa_pick_test.py', 'automation/old_apk_lane/stubs/fake_ui_adb.py')")
s=s.replace('$changed.Count -ne 2','$changed.Count -ne 3').replace('outside the two approved patches','outside the three approved files')
s=s.replace('c2b238d66aaaec05c77c22b730b528e5379ce0369b323e0a829c8bcc3577088b','39a0d515a03341e4e6f8b88f38dd3a23b5d558fb053d30b060bc810b71a39cdc')
s=s.replace('de8fbf819af2664bdef0d96e7da9927b1e777230e0e648b9d4dc9a9fae4fec1f','0decb28628764745a8a4a16af42c55a8bf0b38f4f2c58e829008caed2b1b1ced')
needle='$dirty = @(git -C $wt status --porcelain --untracked-files=no)'
guard=r'''$stubPath = "$wt\automation\old_apk_lane\stubs\fake_ui_adb.py"
$stubText = [Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($stubPath)).TrimStart([char]0xfeff).Replace("`r`n", "`n")
$stubAlg = [Security.Cryptography.SHA256]::Create()
try { $stubSourceSha = [BitConverter]::ToString($stubAlg.ComputeHash([Text.Encoding]::UTF8.GetBytes($stubText))).Replace('-', '').ToLowerInvariant() } finally { $stubAlg.Dispose() }
if ($stubSourceSha -ne '3dd6a073290274e7e65564ce551213d7f157e1f4d1025630a8eb28dd7def7465') {
  "candidate's fake-adb helper is not the approved UTF-8/LF source -- nothing run" | Tee-Object -Append $log; exit 2
}
'''
assert s.count(needle)==1;s=s.replace(needle,guard+needle)
s=s.replace('stub=$testSourceSha trace=', 'test=$testSourceSha helper=$stubSourceSha trace=')
out=p/'run_old_apk_lane_r7i_reviewed.ps1';out.write_text(s,encoding='utf-8',newline='\n')
print(out,hashlib.sha256(out.read_bytes()).hexdigest())
