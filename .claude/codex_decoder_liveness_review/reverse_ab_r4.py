from pathlib import Path
import hashlib
base=Path(__file__).parent
p=base/'ab_shoff_batch_reviewed_r3.ps1'
raw=p.read_bytes()
assert hashlib.sha256(raw).hexdigest()=='6c519f116426a43fd3ed132947c60ad8cccc5ecf4ddae22a8e8c87acff2ee627'
s=raw.decode('utf-8-sig')
needle='function Now {'
assert s.count(needle)==1
s=s.replace(needle,'$arms = @($arms[1], $arms[0])  # r4: reverse ONLY the reviewed A/B order\n'+needle)
s='# r4: ONE additional B -> A trial, all reviewed r3 pins, early-stop and cleanup rules retained.\n'+s
out=base/'ab_shoff_batch_reverse_r4.ps1'
out.write_text(s,encoding='utf-8')
print(hashlib.sha256(out.read_bytes()).hexdigest())
