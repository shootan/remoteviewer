import json,os,subprocess
from pathlib import Path
SRC=Path('D:/remote/remote'); DST=Path('C:/work/remote')
REPORT=Path('E:/remote-output/migration/20261031-storage')
verdict=json.loads((REPORT/'copy_verdict.json').read_text(encoding='utf-8'))
assert not verdict['mismatches'] and not verdict['missing_dirs']
layout=json.loads((REPORT/'source_layout.json').read_text(encoding='utf-8'))
results=[]
for item in layout['links']:
    target=Path(item['target'].removeprefix('\\??\\').removeprefix('\\\\?\\'))
    new_target=DST/target.relative_to(SRC)
    dest=DST/item['path']
    if os.path.lexists(dest):raise RuntimeError('Destination already exists: '+str(dest))
    quote=lambda s:"'"+str(s).replace("'","''")+"'"
    command='New-Item -ItemType Junction -Path '+quote(dest)+' -Target '+quote(new_target)+' -ErrorAction Stop | Out-Null'
    subprocess.run(['powershell','-NoProfile','-Command',command],check=True)
    actual=Path(os.readlink(dest).removeprefix('\\??\\').removeprefix('\\\\?\\'))
    assert actual==new_target
    results.append({'path':str(dest),'target':str(new_target)})
(REPORT/'copied_junctions.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
print('Copied junctions remapped:',len(results))
