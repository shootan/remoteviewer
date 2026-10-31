import concurrent.futures, datetime, hashlib, json, os, pathlib, subprocess, urllib.request

repo = pathlib.Path(__file__).resolve().parents[2]
out = pathlib.Path(__file__).resolve().parent
release = repo / '.claude/rel/0.2.157'
def sha(b): return hashlib.sha256(b).hexdigest()
def command(args, **kw):
    return subprocess.run(args, cwd=repo, capture_output=True, timeout=60, **kw)

result = {'checked_at_utc': datetime.datetime.now(datetime.timezone.utc).isoformat()}
result['origin_main'] = command(['git', 'ls-remote', 'origin', 'refs/heads/main']).stdout.decode().strip()
result['release_commit'] = command(['git', 'rev-parse', '5e0e82f']).stdout.decode().strip()
result['source_commit'] = command(['git', 'rev-parse', '09266e5']).stdout.decode().strip()
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
    return {'name':name, 'url':url, 'http':code, 'bytes':len(data), 'sha256':sha(data), 'local_sha256':sha(local), 'match':code==200 and len(data)==size and sha(data)==digest and data==local}
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    result['artifacts']=list(pool.map(fetch,artifacts))
dist=pathlib.Path('D:/remote/remote/dist/GNLinkSetup-0.2.157.exe')
result['preserved_dist']={'path':str(dist), 'sha256':sha(dist.read_bytes()), 'matches_setup':sha(dist.read_bytes())==next(x[2] for x in artifacts if x[0]=='GNLinkSetup.exe')}
result['payload_versions']=[]
for exe in sorted((release/'payload').glob('*.exe')):
    data=exe.read_bytes()
    result['payload_versions'].append({'name':exe.name, 'new_utf16_count':data.count('0.2.157'.encode('utf-16le')), 'old_utf16_count':data.count('0.2.156'.encode('utf-16le'))})
data=(release/'payload/GNLinkStream.exe').read_bytes()
markers=['GNLINK_STREAM_TEST_FAIL_COLLECTOR_FILE','GNLINK_STREAM_TEST_PAUSE_PUBLISH_FILE','REMOTE60_CLAMP_BURST_TEST_SEAM','REMOTE60_NATIVE_FORCE_CLAMP_BURST']
result['shipping_stream']={'rsds_count':data.count(b'RSDS'), 'test_markers':{s:data.count(s.encode())+data.count(s.encode('utf-16le')) for s in markers}}
result['all_checks_pass']=all(x['match'] for x in result['manifest_files']+result['artifacts']) and p.returncode==0 and result['preserved_dist']['matches_setup'] and result['origin_main'].startswith('82bdf0145be78148e49fc7599974add62e1940d5') and not any(x['old_utf16_count'] for x in result['payload_versions']) and result['shipping_stream']['rsds_count']==0 and not any(result['shipping_stream']['test_markers'].values())
(out/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'all_checks_pass':result['all_checks_pass'],'artifacts':len(result['artifacts']),'manifest_files':result['manifest_files'],'signature_check':result['signature_and_tamper_check'],'origin_main':result['origin_main'],'dist':result['preserved_dist'],'shipping_stream':result['shipping_stream']},ensure_ascii=False,indent=2))
raise SystemExit(0 if result['all_checks_pass'] else 1)
