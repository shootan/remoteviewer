import os, json, hashlib, subprocess, time
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor

SRC=Path('D:/remote/remote')
DST=Path('C:/work/remote')
OUT=Path('E:/remote-output')
REPORT=OUT/'migration/20261031-storage'

def git(root,*args):
    p=subprocess.run(['git','-C',str(root),*args],capture_output=True,check=True)
    return p.stdout

def save(name,data):
    (REPORT/name).write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')

def scan(root):
    files=[]; links=[]; dirs=[]
    def visit(folder):
        for ent in os.scandir(folder):
            p=Path(ent.path); rel=p.relative_to(root).as_posix()
            if ent.stat(follow_symlinks=False).st_file_attributes & 0x400:
                links.append({'path':rel,'target':os.readlink(p)})
            elif ent.is_dir(follow_symlinks=False):
                dirs.append(rel); visit(p)
            elif ent.is_file(follow_symlinks=False):
                files.append((rel,ent.stat().st_size))
            else:
                raise RuntimeError('Unsupported entry: '+str(p))
    visit(root)
    return files,links,dirs

def digest(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''): h.update(b)
    return h.hexdigest()

raw=git(SRC,'worktree','list','--porcelain').decode('utf-8')
worktrees=[]
for block in raw.strip().split('\n\n'):
    info=dict(line.split(' ',1) if ' ' in line else (line,True) for line in block.splitlines())
    p=Path(info['worktree'])
    info['status_hex']=git(p,'status','--porcelain=v1','-z','--untracked-files=all').hex()
    info['head']=git(p,'rev-parse','HEAD').decode().strip()
    info['dotgit_text']=(p/'.git').read_text() if (p/'.git').is_file() else None
    worktrees.append(info)
save('worktrees_before.json',worktrees)
files,links,dirs=scan(SRC)
save('source_layout.json',{'files':files,'links':links,'dirs':dirs})
print(f'Inventory: {len(files)} files, {sum(n for _,n in files)} bytes, {len(links)} junctions',flush=True)
cmd=['robocopy',str(SRC),str(DST),'/E','/COPY:DAT','/DCOPY:DAT','/XJ','/R:1','/W:1','/NP','/NFL','/NDL','/LOG:'+str(REPORT/'copy.log')]
p=subprocess.run(cmd,capture_output=True)
save('copy_exit.json',{'rc':p.returncode})
if p.returncode>=8: raise RuntimeError('Robocopy failed; inspect copy.log')

def check(item):
    rel,size=item; a=SRC/rel; b=DST/rel
    if not b.is_file() or b.stat().st_size!=size: return {'path':rel,'error':'missing/size'}
    x=digest(a); y=digest(b)
    if x!=y: return {'path':rel,'error':'hash','source':x,'destination':y}
    return {'path':rel,'bytes':size,'sha256':x}

checks=[]
with ThreadPoolExecutor(max_workers=4) as pool:
    for i,result in enumerate(pool.map(check,files),1):
        checks.append(result)
        if i%10000==0: print(f'Hash verification {i}/{len(files)}',flush=True)
save('copy_hashes.json',checks)
bad=[x for x in checks if 'error' in x]
missing_dirs=[x for x in dirs if not (DST/x).is_dir()]
save('copy_verdict.json',{'files':len(files),'bytes':sum(n for _,n in files),'mismatches':bad,'missing_dirs':missing_dirs})
if bad or missing_dirs: raise RuntimeError('Copy verification failed')
print('COPY HASH VERIFIED: all regular files match',flush=True)

for link in links:
    target=link['target'].removeprefix('\\??\\').removeprefix('\\\\?\\')
    target_path=Path(target)
    try: target_new=DST/target_path.relative_to(SRC)
    except ValueError: raise RuntimeError('Junction target outside source: '+target)
    dest=DST/link['path']
    if os.path.lexists(dest): raise RuntimeError('Junction destination already exists: '+str(dest))
    quote=lambda s:"'"+str(s).replace("'","''")+"'"
    command='New-Item -ItemType Junction -Path '+quote(dest)+' -Target '+quote(target_new)+' -ErrorAction Stop | Out-Null'
    result=subprocess.run(['powershell','-NoProfile','-Command',command],capture_output=True)
    if result.returncode or not os.path.isjunction(dest):
        raise RuntimeError('Junction creation failed: '+result.stderr.decode(errors='replace'))
print('Junctions remapped: '+str(len(links)),flush=True)
