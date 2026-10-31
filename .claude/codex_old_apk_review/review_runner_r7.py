from pathlib import Path
import hashlib
p=Path('D:/remote/remote/.claude/verify_0164/run_old_apk_lane.ps1')
raw=p.read_bytes()
assert hashlib.sha256(raw).hexdigest()=='ace239bf9c338c15251738a892f59a99b3a9f4e48957f2d95cfdca9e9c0b94ca'
s=raw.decode('utf-8-sig').replace('\r\n','\n')
s=s.replace("-notlike '548d199*'", "-ne '548d19911d85c503f4b650d8bfd5ac1e0afada19'")
anchor='# Codex r6: the existing five targets'
assert s.count(anchor)==1
s=s.replace(anchor,r'''$dirty = @(git -C $wt status --porcelain --untracked-files=no)
if ($dirty.Count -ne 0) { "tracked worktree changed -- nothing run" | Tee-Object -Append $log; exit 2 }
'''+anchor)
needle='    "arm $($arm.arm) tag=$($arm.tag) rc=$($p.ExitCode) secs=$([int]((Get-Date)-$t0).TotalSeconds)" | Tee-Object -Append $log'
assert s.count(needle)==1
s=s.replace(needle,needle+r'''
    if ($null -eq $p -or $null -eq $p.ExitCode -or $p.ExitCode -ne 0) {
      "NOT RUN: remaining arm after a nonzero/unknown result; inspect cleanup before any independent retry" | Tee-Object -Append $log
      if ($null -eq $p -or $null -eq $p.ExitCode) { exit 2 }
      exit ([int]$p.ExitCode)
    }''')
out=Path(__file__).parent/'run_old_apk_lane_r7_reviewed.ps1'
out.write_text('# Reviewed r7: pinned 548d199, same five exe gates; first nonzero arm stops, finally retains slot cleanup.\n'+s,encoding='utf-8')
(Path(__file__).parent/'r7_runner_parent.sha256').write_text(hashlib.sha256(raw).hexdigest()+'\n',encoding='ascii')
print(hashlib.sha256(out.read_bytes()).hexdigest())
