import concurrent.futures, datetime, hashlib, json, os, pathlib, subprocess, urllib.request

repo = pathlib.Path(__file__).resolve().parents[2]
out = pathlib.Path(__file__).resolve().parent
release = repo / '.claude/rel/0.2.159'
def sha(b): return hashlib.sha256(b).hexdigest()
def command(args, **kw):
    return subprocess.run(args, cwd=repo, capture_output=True, timeout=60, **kw)

result = {'checked_at_utc': datetime.datetime.now(datetime.timezone.utc).isoformat()}
result['origin_main'] = command(['git', 'ls-remote', 'origin', 'refs/heads/main']).stdout.decode().strip()
result['release_commit'] = command(['git', 'rev-parse', '64177a7']).stdout.decode().strip()
result['source_commit'] = command(['git', 'rev-parse', 'a428abf']).stdout.decode().strip()
result['manifest_files'] = []
ssh = ['ssh', '-i', str(pathlib.Path.home()/'.ssh/remote60_deploy'), '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=8', 'gnlink@192.168.0.6']
for name in ['windows.manifest', 'windows.sig']:
    p = command(ssh + ['cat /opt/gnlink/update-manifests/' + name])
    if p.returncode: raise RuntimeError(p.stderr.decode())
    (out/name).write_bytes(p.stdout)
    local = (release/name).read_bytes()
    result['manifest_files'].append({'name':name, 'nas_sha256':sha(p.stdout), 'local_sha256':sha(local), 'match':p.stdout == local})

env = dict(os.environ)
env.pop('GNLINK_PUBLIC_KEY_HEX', None)
p = command(['node', 'automation/gnlink_verify_manifest.js', str(out/'windows.manifest'), str(out/'windows.sig'), 'windows'], env=env)
result['signature_and_tamper_check'] = {'exit':p.returncode, 'stdout':p.stdout.decode(), 'stderr':p.stderr.decode(), 'public_key_override':False}
artifacts = []
for line in (out/'windows.manifest').read_text().splitlines():
    if line.startswith('artifact='):
        name,size,digest,url = line[9:].split('|')
        artifacts.append((name,int(size),digest,url))
def fetch(row):
    name,size,digest,url = row
    with urllib.request.urlopen(url, timeout=45) as response:
        data=response.read()
        code=response.status
    local=(release/'payload'/pathlib.Path(name.replace('\\','/'))).read_bytes()
    if name in ('GNLinkStream.exe','GNLinkSetup.exe'): (out/name).write_bytes(data)
    return {'name':name, 'url':url, 'http':code, 'bytes':len(data), 'sha256':sha(data), 'local_sha256':sha(local), 'match':code==200 and len(data)==size and sha(data)==digest and data==local}
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    result['artifacts']=list(pool.map(fetch,artifacts))
dist=pathlib.Path('D:/remote/remote/dist/GNLinkSetup-0.2.159.exe')
result['preserved_dist']={'path':str(dist), 'sha256':sha(dist.read_bytes()), 'matches_setup':sha(dist.read_bytes())==next(x[2] for x in artifacts if x[0]=='GNLinkSetup.exe')}
result['payload_versions']=[]
for exe in sorted((release/'payload').glob('*.exe')):
    data=exe.read_bytes()
    result['payload_versions'].append({'name':exe.name, 'new_utf16_count':data.count('0.2.159'.encode('utf-16le')), 'old_utf16_count':data.count('0.2.158'.encode('utf-16le'))})
data=(release/'payload/GNLinkViewer.exe').read_bytes()
markers=['Remote60StatsChainTarget','remote60 stats chain target','viewer_stats_chain_e2e_test','--watchdog-sec']
result['shipping_viewer']={'rsds_count':data.count(b'RSDS'), 'test_markers':{s:data.count(s.encode())+data.count(s.encode('utf-16le')) for s in markers}}
result['android_unchanged']=[]
for name in ('android.manifest','android.sig'):
    q=command(ssh+['cat /opt/gnlink/update-manifests/'+name])
    if q.returncode: raise RuntimeError(q.stderr.decode())
    expected=(repo/'.claude/codex-final-apk-0.2.30'/name).read_bytes()
    (out/name).write_bytes(q.stdout)
    result['android_unchanged'].append({'name':name,'sha256':sha(q.stdout),'match':q.stdout==expected})
g=command(['python','automation/encoder_poll_build_gate.py',str(out/'GNLinkStream.exe')])
result['shipping_stream_gate']={'exit':g.returncode,'stdout':g.stdout.decode()}
result['source_to_release_files']=command(['git','diff','--name-only','a428abf','64177a7']).stdout.decode().splitlines()
android_row=next(x[9:].split('|') for x in (out/'android.manifest').read_text().splitlines() if x.startswith('artifact='))
aname,asize,adigest,aurl=android_row
with urllib.request.urlopen(aurl,timeout=45) as response:
    adata=response.read(); astatus=response.status
result['android_artifact_unchanged']={'url':aurl,'http':astatus,'bytes':len(adata),'sha256':sha(adata),'match':astatus==200 and len(adata)==int(asize) and sha(adata)==adigest=='e6a6eecbf129f92998d5c4d4e2844057485315489c001023b24f5966a93608cf'}
result['all_checks_pass']=result['android_artifact_unchanged']['match'] and all(x['match'] for x in result['android_unchanged']) and g.returncode==0 and result['source_to_release_files']==['apps/native_poc/src/product_version.hpp'] and len(artifacts)==11 and 'version=0.2.159' in (out/'windows.manifest').read_text().splitlines() and all(x['match'] for x in result['manifest_files']+result['artifacts']) and p.returncode==0 and result['preserved_dist']['matches_setup'] and result['origin_main'].startswith(command(['git','rev-parse','a622f04']).stdout.decode().strip()) and not any(x['old_utf16_count'] for x in result['payload_versions']) and result['shipping_viewer']['rsds_count']==0 and not any(result['shipping_viewer']['test_markers'].values())
(out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'all_checks_pass':result['all_checks_pass'],'artifacts':len(result['artifacts']),'manifest_files':result['manifest_files'],'signature_check':result['signature_and_tamper_check'],'origin_main':result['origin_main'],'dist':result['preserved_dist'],'shipping_viewer':result['shipping_viewer']},ensure_ascii=False,indent=2))
raise SystemExit(0 if result['all_checks_pass'] else 1)
