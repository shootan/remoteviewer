from pathlib import Path
import hashlib
p=Path(__file__).parent
s=(p/'run_old_apk_lane_r7i_reviewed.ps1').read_text(encoding='utf-8-sig')
s='# Approved independent P once: published APK, manual TCP control + direct UDP video; S remains unresolved.\n'+s
old="foreach ($arm in @(@{ tag = 'oa-s'; arm = 'S'; apk = $apkS; sb = $false }, @{ tag = 'oa-p'; arm = 'P'; apk = $apkP; sb = $true }))"
new="foreach ($arm in @(@{ tag = 'oa-p-direct-check'; arm = 'P'; apk = $apkP; sb = $true }))"
assert s.count(old)==1;s=s.replace(old,new)
needle="$baseHead = 'ff1c72a19aa2e8656d207047c5e076c656c4f540'"
pin="""if ($CandidateHead -ne 'c45fb9b768accaf3ad54561a7766e2d81cf56c86') {
  "independent P is pinned to c45fb9b -- nothing run" | Tee-Object -Append $log; exit 2
}
"""
assert s.count(needle)==1;s=s.replace(needle,pin+needle)
out=p/'run_old_apk_lane_p_direct_once.ps1';out.write_text(s,encoding='utf-8',newline='\n')
print(out,hashlib.sha256(out.read_bytes()).hexdigest())
