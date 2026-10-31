from pathlib import Path
import re,json,hashlib,subprocess
root=Path('D:/remote/remote'); docs=root/'docs'; out=root/'.claude/audit-20260912-direct'
names=['host_pc_thread_recovery_audit_2026-09-12.md','host_pc_thread_recovery_inventory_2026-09-12.md','host_pc_thread_recovery_probes_2026-09-12.md']
for name in names:
 p=docs/name; text=p.read_text(encoding='utf-8')
 assert '\ufffd' not in text and '????' not in text,name
 assert text.count('```')%2==0,name
 assert not any(line.rstrip()!=line for line in text.splitlines()),name
 for target in re.findall(r'\]\(([^)]+)\)',text):
  target=target.split('#',1)[0]
  if target and not target.startswith('http'): assert (p.parent/target).exists(),(name,target)
 print(name, len(text.splitlines()),'lines',p.stat().st_size,'bytes: UTF-8/links/fences/whitespace OK')
main=(docs/names[0]).read_text(encoding='utf-8')
ids=re.findall(r'^\| ([HVB]\d\d) /',main,re.M)
assert len(ids)==len(set(ids))==36,ids
assert sum(x.startswith('H') for x in ids)==13
assert sum(x.startswith('V') for x in ids)==13
assert sum(x.startswith('B') for x in ids)==10
m=json.loads((out/'source-manifest.json').read_text(encoding='utf-8'))
inventory=(docs/names[1]).read_text(encoding='utf-8')
assert len(m['files'])==364
for r in m['files']: assert r['sha256'] in inventory,r['path']
for p in [docs/'history.md', docs/'구현계획.md']:
 text=p.read_text(encoding='utf-8-sig')
 assert names[0] in text
 assert 'Codex ?? Host/PC' not in text
changed=[r['path'] for r in m['files'] if hashlib.sha256((root/r['path']).read_bytes()).hexdigest()!=r['sha256']]
print('36 unique findings; 364 source hashes; history/checklist references OK')
print('Concurrent source changes:',json.dumps(changed))
print('Current HEAD:',subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip())
