import collections
import datetime
import json
import subprocess
from pathlib import Path

ROOT = Path('C:/work/remote')
OUT = Path('E:/remote-output/migration/20261031-storage/git-backup')
OUT.mkdir(exist_ok=True)


def git(*args, cwd=ROOT):
    return subprocess.check_output(['git', '-C', str(cwd), *args])


remote = {}
for line in git('ls-remote', '--heads', '--tags', 'origin').decode().splitlines():
    sha, ref = line.split('\t', 1)
    remote[ref] = sha
local = []
for line in git('for-each-ref', '--format=%(refname)%00%(objectname)%00%(upstream)', 'refs/heads').decode().splitlines():
    ref, sha, upstream = line.split('\0')
    local.append({'ref': ref, 'sha': sha, 'upstream': upstream,
                  'remote_exact_match': remote.get(ref) == sha,
                  'remote_ref_exists': ref in remote})
worktrees = []
for block in git('worktree', 'list', '--porcelain').decode().strip().split('\n\n'):
    row = dict(line.split(' ', 1) if ' ' in line else (line, True) for line in block.splitlines())
    path = Path(row['worktree'])
    status = git('status', '--porcelain=v1', '-z', '--untracked-files=all', cwd=path).split(b'\0')
    staged, modified, untracked = [], [], []
    i = 0
    while i < len(status):
        entry = status[i]
        i += 1
        if not entry:
            continue
        flag, name = entry[:2].decode(), entry[3:].decode()
        if flag == '??':
            p = path / name
            untracked.append({'path': name, 'bytes': p.stat().st_size if p.is_file() else None})
        else:
            if flag[0] != ' ':
                staged.append({'status': flag, 'path': name})
            if flag[1] != ' ':
                modified.append({'status': flag, 'path': name})
            if 'R' in flag or 'C' in flag:
                i += 1
    ext = collections.Counter(Path(x['path']).suffix.lower() for x in untracked)
    summary = {'path': str(path), 'head': row['HEAD'], 'branch': row.get('branch'),
               'staged': staged, 'modified': modified, 'untracked': untracked,
               'untracked_extensions': ext.most_common(),
               'untracked_bytes': sum(x['bytes'] or 0 for x in untracked)}
    worktrees.append(summary)
    print(json.dumps({'path': str(path), 'branch': row.get('branch'),
                      'staged': len(staged), 'modified': len(modified),
                      'untracked': len(untracked), 'untracked_bytes': summary['untracked_bytes'],
                      'extensions': ext.most_common(8),
                      'tracked_change_paths': [x['path'] for x in staged + modified]}, ensure_ascii=True), flush=True)
all_heads = sorted({x['sha'] for x in local} | {x['head'] for x in worktrees})
remote_heads = [sha for ref, sha in remote.items() if ref.startswith('refs/heads/')]
unpushed = git('rev-list', '--count', *all_heads, '--not', *remote_heads).decode().strip()
result = {'time': datetime.datetime.now().isoformat(), 'origin': git('remote', 'get-url', 'origin').decode().strip(),
          'remote_refs': remote, 'local_branches': local, 'worktrees': worktrees,
          'unique_commits_not_reachable_from_remote_branches': int(unpushed)}
(OUT / 'initial_audit.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps({'local_branches': len(local), 'branches_without_exact_remote_match': [x['ref'] for x in local if not x['remote_exact_match']],
                  'remote_branches': len(remote_heads), 'unpushed_unique_commits': int(unpushed),
                  'report': str(OUT / 'initial_audit.json')}, ensure_ascii=True), flush=True)
