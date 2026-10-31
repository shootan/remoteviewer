from pathlib import Path
import hashlib

src=Path('D:/remote/remote/.claude/caplife/ab_shoff_batch.ps1')
raw=src.read_bytes()
assert hashlib.sha256(raw).hexdigest()=='cfd1027f280c146ef1022f9482c12e39a131be3b5ee5d1143d28b4f8941e6f39'
s=raw.decode('utf-8-sig').replace('\r\n','\n')
needle="(@($boots | Select-Object -Unique).Count -eq 1)"
assert s.count(needle)==1
s=s.replace(needle,needle+r''' -and
          (@($boots | Where-Object { $_ -notmatch "^rc=0 '[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}'$" }).Count -eq 0)''')
old='  "=== $($a.tag) end $(Now) rc=$LASTEXITCODE"'
new=r'''  $armRc = $LASTEXITCODE
  "=== $($a.tag) end $(Now) rc=$armRc"
  if ($null -eq $armRc -or $armRc -ne 0) {
    "NOT RUN: any remaining arm; stopped after the first nonzero/unknown arm rc"
    Remove-Item env:KE_PLAN, env:KE_WATCH_SEC, env:KE_RELAY_TRACE, env:KE_SERVER_JS -ErrorAction SilentlyContinue
    if ($null -eq $armRc) { exit 2 }
    exit ([int]$armRc)
  }'''
assert s.count(old)==1
s=s.replace(old,new)
out=Path(__file__).parent/'ab_shoff_batch_reviewed_r3.ps1'
out.write_text(s,encoding='utf-8')
print(hashlib.sha256(out.read_bytes()).hexdigest())
