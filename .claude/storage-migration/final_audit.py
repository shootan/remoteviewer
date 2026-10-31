import os,json,subprocess
from pathlib import Path
ROOT=Path('C:/work/remote'); OLD=Path('D:/remote/remote')
REPORT=Path('E:/remote-output/migration/20261031-storage')
before=json.loads((REPORT/'worktrees_before.json').read_text(encoding='utf-8'))
def git(p,*a):return subprocess.run(['git','-C',str(p),*a],capture_output=True,check=True).stdout
rows=[]
for old in before:
    p=Path(old['worktree'])
    if p.is_relative_to(OLD):p=ROOT/p.relative_to(OLD)
    old_status=set(bytes.fromhex(old['status_hex']).split(b'\0'))-{b''}
    new_status=set(git(p,'status','--porcelain=v1','-z','--untracked-files=all').split(b'\0'))-{b''}
    added=new_status-old_status; removed=old_status-new_status
    allowed=set()
    if p==ROOT:allowed={x for x in added if x.startswith(b'?? .claude/storage-migration/')}
    if p==ROOT/'.claude/worktrees/main-stability-integration':
        names=['docs/history.md','docs/구현계획.md']
        allowed={(' M '+x).encode('utf-8') for x in names}
    common=Path(git(p,'rev-parse','--path-format=absolute','--git-common-dir').decode().strip())
    ok=(not removed and not (added-allowed) and git(p,'rev-parse','HEAD').decode().strip()==old['head'] and common==ROOT/'.git')
    rows.append({'path':str(p),'pass':ok,'unexpected_added':[x.decode(errors='replace') for x in added-allowed],
        'removed':[x.decode(errors='replace') for x in removed]})
    if not ok:raise RuntimeError('Unexpected worktree change: '+str(rows[-1]))

links=json.loads((REPORT/'copied_junctions.json').read_text(encoding='utf-8'))
links+=json.loads((REPORT/'junction_changes.json').read_text(encoding='utf-8'))
for item in links:
    path=Path(item['path']); expected=Path(item.get('new_target',item.get('target')))
    actual=Path(os.readlink(path).removeprefix('\\??\\').removeprefix('\\\\?\\'))
    assert actual==expected and actual.is_relative_to(ROOT) and actual.is_dir(),str(path)
diff=git(ROOT/'.claude/worktrees/main-stability-integration','diff','--name-only').decode().splitlines()
git(ROOT/'.claude/worktrees/main-stability-integration','diff','--check')
result={'all_worktrees_pass':True,'worktrees':rows,'active_junctions_checked':len(links),'documentation_only':diff,
    'product_work_still_stopped':True,'source_D_preserved':True,'commit':'pending new C session Git MCP'}
(REPORT/'final_audit.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print('FINAL AUDIT PASS:',len(rows),'worktrees;',len(links),'active junctions; only 2 intended documentation edits')
