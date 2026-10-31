from pathlib import Path
import hashlib
import os
import shlex
import shutil
import subprocess
import uuid

workspace = Path('D:/remote/remote').resolve()
review = workspace / '.claude/codex_maint_review'
case = (review / ('same-pair-' + uuid.uuid4().hex[:10])).resolve()
assert case.is_relative_to(workspace)
case.mkdir(parents=True)
published = case / 'published'
published.mkdir()
fixture = Path('C:/work/gnlink-wt/rel-0163/.claude/rel-final/0.2.163')
source = Path('C:/work/gnlink-wt/maint-r1/automation/gnlink_deploy.sh').read_text(encoding='utf-8')
start = source.index('publish_pair() {')
end = source.index('\n# ----------------------------------------------------------------------------', start)
function = source[start:end]
for leaf in ('windows.manifest', 'windows.sig'):
    shutil.copyfile(fixture / leaf, published / leaf)
shim = case / 'shim'
shim.mkdir()
(shim / 'mv').write_text('''#!/usr/bin/env bash
for a in "$@"; do
  case "$a" in *.sig.tmp) exit 1 ;; esac
done
exec /usr/bin/mv "$@"
''', encoding='utf-8', newline='\n')
def q(p):
    value = str(p).replace('\\', '/')
    if len(value) > 2 and value[1] == ':':
        value = '/' + value[0].lower() + value[2:]
    return shlex.quote(value)
script = f'''#!/usr/bin/env bash
set -uo pipefail
PLATFORM=windows
VERSION=0.2.163
DRY_RUN=0
MANIFEST_DIR={q(published)}
MANIFEST={q(fixture / 'windows.manifest')}
SIGNATURE={q(fixture / 'windows.sig')}
PATH={q(shim)}:$PATH
export PATH
chmod +x {q(shim / 'mv')}
step() {{ :; }}
log() {{ printf '%s\\n' "$*"; }}
warn() {{ printf '%s\\n' "$*" >&2; }}
assert_inside_root() {{ :; }}
remote_put() {{ cp -- "$1" "$2"; }}
remote_sh() {{ bash -c "$1"; }}
{function}
publish_pair
'''
script_path = case / 'probe.sh'
script_path.write_text(script, encoding='utf-8', newline='\n')
run = subprocess.run(['C:/Program Files/Git/bin/bash.exe', str(script_path)], capture_output=True, text=True)
same = all(hashlib.sha256((published / leaf).read_bytes()).digest() ==
           hashlib.sha256((fixture / leaf).read_bytes()).digest()
           for leaf in ('windows.manifest', 'windows.sig'))
verify = subprocess.run(['node', str(workspace / 'automation/gnlink_verify_manifest.js'),
                         str(published / 'windows.manifest'), str(published / 'windows.sig'), 'windows'],
                        cwd=workspace, capture_output=True, text=True)
print('case_preserved=' + str(case))
print('publish_exit=' + str(run.returncode))
print(run.stderr.strip())
print('published_pair_identical_to_signed_input=' + str(same))
print('published_signature_verify_exit=' + str(verify.returncode))
print(verify.stdout.strip())
reproduced = run.returncode == 4 and same and verify.returncode == 0 and 'clients refuse it' in run.stderr
print('false_mismatch_diagnostic_reproduced=' + str(reproduced))
raise SystemExit(0 if reproduced else 1)
