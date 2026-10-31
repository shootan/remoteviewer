import datetime, hashlib, json, os, pathlib, subprocess, urllib.request, zipfile
import xml.etree.ElementTree as ET

out = pathlib.Path(__file__).resolve().parent
repo = out.parents[1]
release = repo / '.claude/rel/android-0.2.29'
expected_apk = '942b898ff8a3eb73f47e9ed6ba77f0041e79fa5895f754dd2fe480b60191b020'
expected_cert = 'dcc806aeb30b2e3c53e4a0b96b9675f9e25af2bf071dbb37a3cb09f6b8ec2990'
expected_manifests = {
    'android.manifest': 'f82318f9d02abb4cc58ea0d03daefe2553a1e2ee607ed2634ba3a65e7bb11523',
    'android.sig': 'd5534f81e74cd94aaf8a8de1e31b252ec00eb7076831ebe5aa6926d92cd77897',
    'windows.manifest': '78df5305b70cc429e6779d484a4a534bf13875d81e05e2fc6e5f8da263eb9306',
    'windows.sig': '6dce1ca98d483da0e2a7dafa70077d3f25d340724baef415fb2b301c99a0f77f',
}

def sha(b): return hashlib.sha256(b).hexdigest()
def command(args, **kw):
    p = subprocess.run(args, cwd=repo, capture_output=True, timeout=55, **kw)
    if p.returncode:
        raise RuntimeError(f'{args[0]} failed ({p.returncode}): ' + p.stderr.decode('utf-8', 'replace'))
    return p

result = {'checked_at_utc': datetime.datetime.now(datetime.timezone.utc).isoformat()}
result['source_commit'] = command(['git', 'rev-parse', '0e5ddae']).stdout.decode().strip()
result['release_commit'] = command(['git', 'rev-parse', 'de5a035']).stdout.decode().strip()
result['origin_main'] = command(['git', 'ls-remote', 'origin', 'refs/heads/main']).stdout.decode().strip()
result['source_to_release_files'] = command(['git', '-c', 'core.quotepath=false', 'diff', '--name-only', '0e5ddae', 'de5a035']).stdout.decode('utf-8').splitlines()
ssh = ['ssh', '-i', str(pathlib.Path.home()/'.ssh/remote60_deploy'), '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=8', 'gnlink@192.168.0.6']
result['nas_pairs'] = []
for name, expected in expected_manifests.items():
    data = command(ssh + ['cat /opt/gnlink/update-manifests/' + name]).stdout
    (out/name).write_bytes(data)
    local = (release/name) if name.startswith('android') else repo / '.claude/codex-final-0.2.158' / name
    result['nas_pairs'].append({'name':name, 'sha256':sha(data), 'match_expected':sha(data)==expected,
                                'match_local':data==local.read_bytes()})

env = dict(os.environ)
env.pop('GNLINK_PUBLIC_KEY_HEX', None)
p = command(['node', 'automation/gnlink_verify_manifest.js', str(out/'android.manifest'), str(out/'android.sig'), 'android'], env=env)
result['manifest_signature_and_tamper'] = {'exit':p.returncode, 'stdout':p.stdout.decode('utf-8'), 'public_key_override':False}
lines = (out/'android.manifest').read_text(encoding='utf-8').splitlines()
rows = [s[9:].split('|') for s in lines if s.startswith('artifact=')]
assert len(rows)==1
name, size, digest, url = rows[0]
assert url == 'https://gnlink.shotan.net/updates/0.2.29/GNLink-0.2.29.apk'
with urllib.request.urlopen(url, timeout=45) as response:
    data, status = response.read(), response.status
apk = out/'GNLink-0.2.29.apk'
apk.write_bytes(data)
local = (release/'payload'/name).read_bytes()
preserved = pathlib.Path('D:/remote/remote/dist/GNLink-0.2.29.apk').read_bytes()
result['external_apk'] = {'url':url, 'http':status, 'bytes':len(data), 'sha256':sha(data),
    'matches_signed':status==200 and len(data)==int(size) and sha(data)==digest==expected_apk,
    'matches_payload':data==local, 'matches_dist':data==preserved}

sdk = pathlib.Path.home()/'AppData/Local/Android/Sdk/build-tools/34.0.0'
java = pathlib.Path('C:/Program Files/Android/Android Studio/jbr/bin/java.exe')
p = command([str(java), '-jar', str(sdk/'lib/apksigner.jar'), 'verify', '--verbose', '--print-certs', str(apk)])
cert = p.stdout.decode('utf-8', 'replace')
(out/'apk-certificate.txt').write_text(cert, encoding='utf-8')
result['apk_signer'] = {'exit':p.returncode, 'same_certificate':expected_cert in cert}
p = command([str(sdk/'aapt2.exe'), 'dump', 'badging', str(apk)])
badging = p.stdout.decode('utf-8', 'replace')
(out/'apk-badging.txt').write_text(badging, encoding='utf-8')
package = next(x for x in badging.splitlines() if x.startswith('package:'))
with zipfile.ZipFile(apk) as z:
    abis = sorted({x.split('/')[1] for x in z.namelist() if x.startswith('lib/') and x.endswith('/libremote60_android_direct.so')})
result['apk_identity'] = {'package':package, 'abis':abis, 'version_match':"versionCode='28'" in package and "versionName='0.2.29'" in package}
tests = repo/'apps/android_direct_client/app/build/test-results/testDebugUnitTest'
totals = {'suites':0, 'tests':0, 'failures':0, 'errors':0, 'skipped':0}
for f in tests.glob('TEST-*.xml'):
    root = ET.parse(f).getroot()
    totals['suites'] += 1
    for k in ('tests','failures','errors','skipped'): totals[k] += int(root.attrib.get(k, '0'))
result['build_test_reports_read_only'] = totals
result['all_checks_pass'] = (
    all(x['match_expected'] and x['match_local'] for x in result['nas_pairs'])
    and all(result['external_apk'][k] for k in ('matches_signed','matches_payload','matches_dist'))
    and result['apk_signer']['same_certificate'] and result['apk_identity']['version_match']
    and abis==['arm64-v8a','armeabi-v7a','x86','x86_64']
    and 'version=0.2.29' in lines and 'versionCode=28' in lines
    and result['origin_main'].startswith(result['release_commit'])
    and sorted(result['source_to_release_files'])==['apps/android_direct_client/app/build.gradle.kts','docs/작업목록.md']
    and totals=={'suites':16,'tests':177,'failures':0,'errors':0,'skipped':0}
)
(out/'result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(result, ensure_ascii=False, indent=2))
raise SystemExit(0 if result['all_checks_pass'] else 1)
