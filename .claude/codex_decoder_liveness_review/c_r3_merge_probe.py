from pathlib import Path

s=Path(__file__).with_name('c_r2_merge_probe.py').read_text(encoding='utf-8')
s=s.replace('c_d504_review','c_2b492_review')
assert s.count('bad==0')==2
s=s.replace('bad==0','(bad&kKeyCauseRecovery)!=0')
exec(compile(s,'same-real-sink-counterexamples-after-r3','exec'))
