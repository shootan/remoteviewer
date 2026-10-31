"""Copy final release/evidence folders to a new E-drive snapshot. Never delete."""
import datetime,hashlib,json,os,shutil,sys
from pathlib import Path
ROOT=Path('C:/work/remote')
OUT=Path('E:/remote-output')
stamp=sys.argv[1] if len(sys.argv)>1 else datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
if not stamp or any(c not in '0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-_' for c in stamp):
    raise SystemExit('Invalid snapshot name')
sources={
    'releases': ['dist','.claude/rel','.claude/rel-apk-0232'],
    'evidence': ['log','.claude/deploy-logs','.claude/verify_0163','.claude/verify_0164',
        '.claude/verify_0232','.claude/verify_c2','.claude/verify_inj','.claude/verify_ka',
        '.claude/verify_maint','.claude/verify_np','.claude/verify_dirretry'],
}
manifest=[]
def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
    return h.hexdigest()
def copy_dir(src,dst):
    dst.mkdir(parents=True,exist_ok=False)
    for entry in os.scandir(src):
        p=Path(entry.path); q=dst/entry.name
        if entry.stat(follow_symlinks=False).st_file_attributes & 0x400:
            raise RuntimeError('Output copy refuses junction/link: '+str(p))
        if entry.is_dir(follow_symlinks=False):copy_dir(p,q)
        elif entry.is_file(follow_symlinks=False):
            with p.open('rb') as a,q.open('xb') as b:shutil.copyfileobj(a,b,1024*1024)
            a_hash=sha(p); b_hash=sha(q)
            if a_hash!=b_hash:raise RuntimeError('Output hash mismatch: '+str(p))
            shutil.copystat(p,q)
            manifest.append({'source':str(p),'destination':str(q),'bytes':q.stat().st_size,'sha256':a_hash})
        else:raise RuntimeError('Unsupported output file: '+str(p))
for category,paths in sources.items():
    for rel in paths:
        src=ROOT/rel
        if src.is_dir():
            dst=OUT/category/stamp/rel
            copy_dir(src,dst)
            print('Copied and hash verified: '+rel,flush=True)
report=OUT/'migration'/stamp
report.mkdir(parents=True,exist_ok=True)
(report/'export_hashes.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
print('EXPORT PASS:',len(manifest),'files,',sum(x['bytes'] for x in manifest),'bytes',flush=True)
