import collections
import hashlib
import json
import re
import subprocess
from pathlib import Path

ROOT = Path('C:/work/remote')
OUT = Path('E:/remote-output/migration/20261031-storage/git-backup')
audit = json.loads((OUT / 'initial_audit.json').read_text(encoding='utf-8'))


def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args])


patterns = {
    'private-key-block': re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH |DSA |ENCRYPTED )?PRIVATE KEY-----[\s\r\n]+[A-Za-z0-9+/=\r\n]{40,}-----END '),
    'github-token': re.compile(rb'\b(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{35,})\b'),
    'aws-access-key': re.compile(rb'\b(?:AKIA|ASIA)[A-Z0-9]{16}\b'),
    'slack-token': re.compile(rb'\bxox[baprs]-[A-Za-z0-9-]{20,}\b'),
    'openai-key': re.compile(rb'\bsk-(?:proj-|svcacct-)?[A-Za-z0-9_-]{40,}\b'),
    'credential-value': re.compile(rb'''(?i)["']?(?:password|refresh_token|access_token|api_key|client_secret|session_token|host_token)["']?\s*[:=]\s*["']([A-Za-z0-9_./+!@#%=-]{20,})["']'''),
}


def findings(data):
    found = []
    for name, pattern in patterns.items():
        for match in pattern.finditer(data):
            # Do not treat explicit dummy strings or source variable references as credentials.
            value = match.group(1) if match.lastindex else match.group(0)
            low = value.lower()
            if any(x in low for x in [b'example', b'placeholder', b'your_', b'dummy', b'test-token', b'test_token']):
                continue
            if len(set(value)) < 7:
                continue
            found.append(name)
            break
    return found


text_ext = {'.md', '.py', '.ps1', '.sh', '.cpp', '.hpp', '.h', '.c', '.cc', '.java', '.kt', '.kts',
            '.js', '.cjs', '.mjs', '.ts', '.tsx', '.html', '.css', '.cmake', '.patch', '.diff',
            '.json', '.yml', '.yaml', '.toml', '.xml', '.txt', '.bak', '.dbg'}
generated_segment = re.compile(r'(?i)^(?:build(?:[-_].*)?|cmakefiles|_deps|node_modules|__pycache__|\.venv.*|site-packages|vcpkg_installed|\.gradle|\.cxx|apk_out.*)$')
runtime_segment = re.compile(r'(?i)^(?:oa_runs|runs|rv_runs|scenario-runs|relaunch-runs|logs|deploy-logs|deploy-tmp|rel|relverify|userdata|appdata)$')
plans, excluded, secret_hits = [], [], []
for wt in audit['worktrees']:
    chosen = []
    for row in wt['untracked']:
        rel = row['path']
        p = Path(wt['path']) / rel
        parts = rel.split('/')
        reason = None
        if p.is_dir():
            reason = 'nested-repository-or-directory'
        elif any(generated_segment.match(x) for x in parts[:-1]):
            reason = 'generated-build-or-cache'
        elif any(runtime_segment.match(x) for x in parts[:-1]):
            reason = 'runtime-or-release-evidence-local-E'
        elif '.claude/worktrees/' in rel:
            reason = 'separate-worktree-covered-independently'
        elif p.suffix.lower() not in text_ext:
            reason = 'binary-media-or-generated-state-local-E'
        elif row['bytes'] is None or row['bytes'] > 10 * 1024 * 1024:
            reason = 'oversized-local-artifact'
        elif p.name.lower() in {'settings.local.json', 'settings.json', 'local.properties', '.env', 'credentials.json'}:
            reason = 'machine-local-configuration'
        elif p.suffix.lower() == '.txt' and any(x in p.name.lower() for x in ['log', 'stderr', 'stdout', 'process', 'runtime', 'env', 'console', 'wmic', 'netstat', 'adb']):
            reason = 'runtime-text-evidence-local-E'
        if reason:
            excluded.append({'worktree': wt['path'], 'path': rel, 'reason': reason, 'bytes': row['bytes']})
            continue
        data = p.read_bytes()
        if b'\0' in data and not data.startswith((b'\xff\xfe', b'\xfe\xff')):
            excluded.append({'worktree': wt['path'], 'path': rel, 'reason': 'binary-content', 'bytes': len(data)})
            continue
        hits = findings(data)
        if hits:
            secret_hits.append({'scope': 'untracked', 'worktree': wt['path'], 'path': rel, 'types': hits})
            excluded.append({'worktree': wt['path'], 'path': rel, 'reason': 'credential-pattern-local-only', 'bytes': len(data)})
            continue
        chosen.append({'path': rel, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
    if chosen:
        plans.append({'worktree': wt['path'], 'base_head': wt['head'], 'files': chosen})

# Scan new Git objects before publishing the already-committed branches.
heads = sorted({x['sha'] for x in audit['local_branches']} | {x['head'] for x in audit['worktrees']})
remote_heads = [sha for ref, sha in audit['remote_refs'].items() if ref.startswith('refs/heads/')]
objects = {}
for line in git('rev-list', '--objects', *heads, '--not', *remote_heads).decode().splitlines():
    sha, _, name = line.partition(' ')
    objects[sha] = name
check = subprocess.Popen(['git', '-C', str(ROOT), 'cat-file', '--batch-check=%(objectname) %(objecttype) %(objectsize)'], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
meta = check.communicate(('\n'.join(objects) + '\n').encode())[0].decode().splitlines()
blob_list = [(sha, int(size)) for line in meta for sha, kind, size in [line.split()] if kind == 'blob' and int(size) < 10 * 1024 * 1024]
reader = subprocess.Popen(['git', '-C', str(ROOT), 'cat-file', '--batch'], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
committed_hits = []
for sha, size in blob_list:
    reader.stdin.write((sha + '\n').encode()); reader.stdin.flush()
    header = reader.stdout.readline().split()
    data = reader.stdout.read(int(header[2])); reader.stdout.read(1)
    hits = findings(data)
    if hits:
        committed_hits.append({'object': sha, 'path': objects[sha], 'types': hits})
reader.stdin.close(); reader.wait()
result = {'plans': plans, 'excluded': excluded, 'untracked_secret_pattern_hits': secret_hits,
          'new_committed_blobs_scanned': len(blob_list), 'committed_secret_pattern_hits': committed_hits}
(OUT / 'backup_plan.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps({'snapshot_worktrees': len(plans), 'snapshot_files': sum(len(x['files']) for x in plans),
                  'snapshot_bytes': sum(r['bytes'] for x in plans for r in x['files']),
                  'excluded_reasons': collections.Counter(x['reason'] for x in excluded),
                  'untracked_credential_pattern_paths': secret_hits,
                  'new_committed_blobs_scanned': len(blob_list),
                  'committed_credential_pattern_paths': committed_hits}, ensure_ascii=True), flush=True)
