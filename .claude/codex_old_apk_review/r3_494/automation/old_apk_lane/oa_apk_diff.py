"""rel-0164 old-APK lane r2: the effective difference between the PUBLISHED APK 0.2.32 (arm P) and the same-source
fixture build (arm S) -- read from the two APK files only (no device). Writes a markdown table to argv[3].
  python -I oa_apk_diff.py <published.apk> <fixture.apk> <out.md>"""
import hashlib
import os
import re
import subprocess
import sys
import zipfile

BT = os.path.join(os.environ['LOCALAPPDATA'], 'Android', 'Sdk', 'build-tools', '36.1.0')
pub, fix, out = sys.argv[1], sys.argv[2], sys.argv[3]


def sha(b):
    return hashlib.sha256(b).hexdigest()


def run(args):
    r = subprocess.run(args, capture_output=True, text=True, errors='replace')
    return r.stdout + r.stderr


def facts(apk):
    f = {'sha256': sha(open(apk, 'rb').read())}
    badging = run([os.path.join(BT, 'aapt2.exe'), 'dump', 'badging', apk])
    m = re.search(r"package: name='(\S+)' versionCode='(\d+)' versionName='(\S+)'", badging)
    f['package'] = f'{m.group(1)} {m.group(3)} ({m.group(2)})' if m else '?'
    f['sdk'] = ' '.join(re.findall(r"(sdkVersion|targetSdkVersion):'(\d+)'", badging) and
                        [f'{k}={v}' for k, v in re.findall(r"(sdkVersion|targetSdkVersion):'(\d+)'", badging)])
    f['debuggable'] = 'yes' if 'application-debuggable' in badging else 'no'
    f['abis'] = (re.search(r"native-code: (.*)", badging) or [None, '?'])[1].replace("'", '')
    certs = run(['cmd', '/c', os.path.join(BT, 'apksigner.bat'), 'verify', '--print-certs', '-v', apk])
    f['signer'] = ' / '.join(re.findall(r'Signer #1 certificate DN: (.*)', certs)[:1] +
                             ['sha256 ' + x[:16] for x in re.findall(r'Signer #1 certificate SHA-256 digest: (\w+)', certs)[:1]])
    f['schemes'] = ' '.join(f'v{v}' for v in re.findall(r'Verified using v(\d) scheme[^:]*: true', certs))
    z = zipfile.ZipFile(apk)
    dex = b''.join(z.read(n) for n in z.namelist() if re.match(r'classes\d*\.dex$', n))
    f['dex_sha'] = sha(dex)[:16]
    for key, needle in (('dir_https', b'https://gnlink.shotan.net'), ('dir_fixture', b'http://10.0.2.2:18410'),
                        ('migratable', b'https://rem.shotan.net'), ('seam_dex', b'SharpenTestSeam')):
        f[key] = dex.count(needle)
    libs = {}
    for n in z.namelist():
        if n.startswith('lib/') and n.endswith('.so'):
            b = z.read(n)
            libs[n] = (sha(b)[:16], len(b), b.count(b'sh-test-seam'), b.count(b'liveness-trace'))
    f['libs'] = libs
    return f


a, b = facts(pub), facts(fix)
rows = [('sha256', 'APK file'), ('package', 'package / version'), ('debuggable', 'debuggable'),
        ('signer', 'signer (cert #1)'), ('schemes', 'signature schemes'), ('sdk', 'sdk / target'),
        ('abis', 'native ABIs'), ('dex_sha', 'dex (all classes*.dex) sha256'),
        ('dir_https', 'dex: "https://gnlink.shotan.net" occurrences'),
        ('dir_fixture', 'dex: "http://10.0.2.2:18410" occurrences'),
        ('migratable', 'dex: "https://rem.shotan.net" occurrences'), ('seam_dex', 'dex: SharpenTestSeam refs')]
lines = ['| item | published (arm P) | fixture (arm S) | same |', '|---|---|---|---|']
for k, label in rows:
    lines.append(f'| {label} | {a[k]} | {b[k]} | {"yes" if a[k] == b[k] else "**no**"} |')
lines += ['', '| native lib | published sha256 / bytes / sh-test-seam / liveness-trace | fixture | same |', '|---|---|---|---|']
for n in sorted(set(a['libs']) | set(b['libs'])):
    x, y = a['libs'].get(n), b['libs'].get(n)
    lines.append(f'| {n} | {x} | {y} | {"yes" if x == y else "**no**"} |')
open(out, 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
print('\n'.join(lines))
