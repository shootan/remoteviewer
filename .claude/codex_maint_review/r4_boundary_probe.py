from pathlib import Path
import hashlib
import json
import os
import shlex
import shutil
import subprocess
import uuid

workspace = Path('D:/remote/remote').resolve()
candidate = Path('C:/work/gnlink-wt/maint-r1')
case = (workspace / '.claude/codex_maint_review' / ('r4-' + uuid.uuid4().hex[:8])).resolve()
assert case.is_relative_to(workspace)
case.mkdir(parents=True)
repo = case / 'repo'
for leaf in ('automation/gnlink_verify_manifest.js', 'apps/directory/update_manifest.js',
             'apps/directory/version_compare.js', 'apps/native_poc/src/update_manifest.cpp'):
    dest = repo / leaf
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(candidate / leaf, dest)
    assert dest.read_bytes() == (candidate / leaf).read_bytes()
fixture = Path('C:/work/gnlink-wt/rel-0163/.claude/rel-final/0.2.163')
manifest = case / 'windows.manifest'
signature = case / 'windows.sig'
shutil.copyfile(fixture / manifest.name, manifest)
shutil.copyfile(fixture / signature.name, signature)
tampered = case / 'tampered.manifest'
doc = manifest.read_bytes()
tampered.write_bytes(doc.replace(b'0.2.163', b'0.2.164', 1))
assert tampered.read_bytes() != doc
bad_sig = case / 'bad.sig'
bad_sig.write_text('zz', encoding='utf-8')
preload = case / 'verify_fault.js'
preload.write_text('''const c = require('crypto');
const fs = require('fs');
const original = c.verify;
let n = 0;
c.verify = function (...a) {
  ++n;
  fs.appendFileSync(process.env.REVIEW_HIT, String(n) + '\\n');
  if (process.env.REVIEW_MODE === 'throw') throw new Error('review provider unavailable');
  if (process.env.REVIEW_MODE === 'false-first' && n === 1) return false;
  if (process.env.REVIEW_MODE === 'throw-first' && n === 1) throw new Error('review first call failed');
  return original.apply(this, a);
};
''', encoding='utf-8', newline='\n')
cli = repo / 'automation/gnlink_verify_manifest.js'
checks = []
for name, m, s, fault, expected in (
    ('valid', manifest, signature, '', 0),
    ('tampered', tampered, signature, '', 3),
    ('bad-format', manifest, bad_sig, '', 3),
    ('missing-file', case / 'missing.manifest', signature, '', 4),
    ('valid-provider-throws', manifest, signature, 'throw', 4),
    ('invalid-provider-throws', tampered, signature, 'throw', 4),
    ('contradictory-false-then-valid', manifest, signature, 'false-first', 4),
    ('throw-then-valid', manifest, signature, 'throw-first', 4),
):
    env = os.environ.copy()
    env.pop('GNLINK_PUBLIC_KEY_HEX', None)
    env.pop('NODE_OPTIONS', None)
    cmd = ['node']
    hit = case / (name + '.hit')
    if fault:
        cmd += ['--require', str(preload)]
        env.update(REVIEW_MODE=fault, REVIEW_HIT=str(hit))
    cmd += [str(cli), str(m), str(s), 'windows']
    run = subprocess.run(cmd, env=env, capture_output=True, text=True, timeout=15)
    reached = not fault or (hit.exists() and len(hit.read_text().splitlines()) >= 2)
    ok = run.returncode == expected and reached
    checks.append(ok)
    print(json.dumps(dict(kind='cli', name=name, exit=run.returncode, expected=expected,
                          fault_reached=reached, ok=ok, detail=run.stderr.strip()), ensure_ascii=False))

def q(p):
    value = str(p).replace('\\', '/')
    if len(value) > 2 and value[1] == ':':
        value = '/' + value[0].lower() + value[2:]
    return shlex.quote(value)

source = (candidate / 'automation/gnlink_deploy.sh').read_text(encoding='utf-8')
start = source.index('publish_pair() {')
end = source.index('\n# ----------------------------------------------------------------------------', start)
function = source[start:end]
for mode, initial, expected_pair, expected_swap in (
    ('same-pair', True, 'verified-this-release', 'manifest-only'),
    ('missing', False, 'incomplete', 'manifest-only'),
    ('query-255', True, 'unknown', 'unknown'),
    ('find-exit-1', True, 'unknown', 'unknown'),
    ('full-output-exit-1', True, 'unknown', 'unknown'),
    ('no-end-mark', True, 'unknown', 'unknown'),
):
    folder = case / mode
    published = folder / 'published'
    shim = folder / 'shim'
    published.mkdir(parents=True)
    shim.mkdir()
    if initial:
        shutil.copyfile(manifest, published / manifest.name)
        shutil.copyfile(signature, published / signature.name)
    (shim / 'mv').write_text('''#!/usr/bin/env bash
for a in "$@"; do case "$a" in *.sig.tmp) exit 1 ;; esac; done
exec /usr/bin/mv "$@"
''', encoding='utf-8', newline='\n')
    fault_hit = folder / 'fault.hit'
    fault = ':'
    if mode == 'query-255':
        fault = f'if [[ "$1" == find* ]]; then echo reached > {q(fault_hit)}; return 255; fi'
    elif mode == 'find-exit-1':
        fault = f'''if [[ "$1" == find* ]]; then
echo reached > {q(fault_hit)}
bash -c 'find() {{ command find "$@"; return 1; }}; '"$1"
return $?
fi'''
    elif mode == 'full-output-exit-1':
        fault = f'if [[ "$1" == find* ]]; then echo reached > {q(fault_hit)}; bash -c "$1"; return 1; fi'
    elif mode == 'no-end-mark':
        fault = f'if [[ "$1" == find* ]]; then echo reached > {q(fault_hit)}; find {q(published)} -mindepth 1 -maxdepth 1 -printf "%Y %f\\n"; return $?; fi'
    script = f'''#!/usr/bin/env bash
set -uo pipefail
PLATFORM=windows
VERSION=0.2.163
DRY_RUN=0
REPO_ROOT={q(repo)}
MANIFEST_DIR={q(published)}
MANIFEST={q(manifest)}
SIGNATURE={q(signature)}
PATH={q(shim)}:$PATH
export PATH
unset GNLINK_PUBLIC_KEY_HEX NODE_OPTIONS
chmod +x {q(shim / 'mv')}
step() {{ :; }}
log() {{ printf '%s\\n' "$*"; }}
warn() {{ printf '%s\\n' "$*" >&2; }}
assert_inside_root() {{ case "$1" in {q(case)}/*) ;; *) exit 91 ;; esac; }}
remote_put() {{ cp -- "$1" "$2"; }}
remote_sh() {{ {fault}; bash -c "$1"; }}
{function}
publish_pair
'''
    script_path = folder / 'probe.sh'
    script_path.write_text(script, encoding='utf-8', newline='\n')
    run = subprocess.run(['C:/Program Files/Git/bin/bash.exe', str(script_path)],
                         capture_output=True, text=True, timeout=20)
    (folder / 'stdout.log').write_text(run.stdout, encoding='utf-8')
    (folder / 'stderr.log').write_text(run.stderr, encoding='utf-8')
    unchanged = not initial or all((published / x.name).read_bytes() == x.read_bytes()
                                   for x in (manifest, signature))
    reached = mode in ('same-pair', 'missing') or fault_hit.exists()
    ok = (run.returncode == 4 and f'pair-state {expected_pair}' in run.stderr
          and f'swap-state {expected_swap}' in run.stderr and unchanged and reached)
    checks.append(ok)
    print(json.dumps(dict(kind='publish', name=mode, exit=run.returncode, expected_pair=expected_pair,
                          expected_swap=expected_swap, bytes_preserved=unchanged,
                          fault_reached=reached, ok=ok), ensure_ascii=False))
print('preserved=' + str(case))
print('boundary_checks=' + str(sum(checks)) + '/' + str(len(checks)))
raise SystemExit(0 if all(checks) else 1)
