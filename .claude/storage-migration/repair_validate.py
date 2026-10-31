import os,json,subprocess
from pathlib import Path

SRC=Path('D:/remote/remote'); DST=Path('C:/work/remote')
REPORT=Path('E:/remote-output/migration/20261031-storage')
before=json.loads((REPORT/'worktrees_before.json').read_text(encoding='utf-8'))
layout=json.loads((REPORT/'source_layout.json').read_text(encoding='utf-8'))

def git(root,*args):
    p=subprocess.run(['git','-C',str(root),*args],capture_output=True)
    if p.returncode: raise RuntimeError(p.stderr.decode(errors='replace'))
    return p.stdout

def save(name,data):
    (REPORT/name).write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')

def mapped(p):
    try:return DST/p.relative_to(SRC)
    except ValueError:return p

paths=[mapped(Path(x['worktree'])) for x in before if Path(x['worktree'])!=SRC]
for p in paths:
    if not p.is_dir(): raise RuntimeError('Missing worktree: '+str(p))
repair=git(DST,'worktree','repair',*[str(p) for p in paths])
(REPORT/'repair.stdout').write_bytes(repair)
results=[]
for info in before:
    p=mapped(Path(info['worktree']))
    head=git(p,'rev-parse','HEAD').decode().strip()
    common=git(p,'rev-parse','--path-format=absolute','--git-common-dir').decode().strip()
    status=git(p,'status','--porcelain=v1','-z','--untracked-files=all').hex()
    # Migration files are intentionally new, untracked operational records in the primary worktree.
    old_entries=set(bytes.fromhex(info['status_hex']).split(b'\0'))-{b''}
    new_entries=set(bytes.fromhex(status).split(b'\0'))-{b''}
    added=new_entries-old_entries; removed=old_entries-new_entries
    allowed=all(x.startswith(b'?? .claude/storage-migration/') for x in added) if p==DST else not added
    ok=(head==info['head'] and Path(common).resolve()==(DST/'.git').resolve() and not removed and allowed)
    results.append({'path':str(p),'head':head,'common_dir':common,'preserved':ok,
        'added':[x.decode('utf-8',errors='replace') for x in sorted(added)],
        'removed':[x.decode('utf-8',errors='replace') for x in sorted(removed)]})
save('worktrees_after.json',results)
if not all(x['preserved'] for x in results):raise RuntimeError('Worktree state changed unexpectedly; inspect worktrees_after.json')
print('Git worktrees verified: '+str(len(results)),flush=True)

# Existing C worktrees have WebView2 junctions to the previous common source root.
# Preserve the old junction itself under an archival name; never traverse/delete its target.
changes=[]
for p in paths:
    if p.is_relative_to(DST):continue
    link=p/'third_party/webview2'
    if not os.path.isjunction(link):continue
    raw=os.readlink(link); target=Path(raw.removeprefix('\\??\\').removeprefix('\\\\?\\'))
    try: new_target=DST/target.relative_to(SRC)
    except ValueError:continue
    backup_dir=DST/'.claude/storage-migration/junction-backups'
    backup_dir.mkdir(exist_ok=True)
    archive=backup_dir/(p.name+'-webview2')
    if os.path.lexists(archive):raise RuntimeError('Archive path exists: '+str(archive))
    os.rename(link,archive)
    quote=lambda s:"'"+str(s).replace("'","''")+"'"
    cmd='New-Item -ItemType Junction -Path '+quote(link)+' -Target '+quote(new_target)+' -ErrorAction Stop | Out-Null'
    proc=subprocess.run(['powershell','-NoProfile','-Command',cmd],capture_output=True)
    if proc.returncode or not os.path.isjunction(link):raise RuntimeError('New junction failed: '+str(link))
    changes.append({'path':str(link),'old_target':raw,'new_target':str(new_target),'old_junction_preserved':str(archive)})
save('junction_changes.json',changes)
print('Existing WebView2 junctions redirected: '+str(len(changes)),flush=True)

fsck=git(DST,'fsck','--full','--no-dangling')
(REPORT/'git_fsck.log').write_bytes(fsck)
save('repair_verdict.json',{'worktrees':len(results),'webview_junctions':len(changes),'git_fsck':'PASS'})
print('Git fsck PASS',flush=True)
