"""Create backup-only commits via Git MCP without changing original worktrees."""
import asyncio
import datetime
import hashlib
import json
import re
import shutil
import subprocess
import tomllib
from pathlib import Path
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

ROOT = Path('C:/work/remote')
OUT = Path('E:/remote-output/migration/20261031-storage/git-backup')
TEMP = ROOT / '.claude/storage-migration/backup-worktree'


def git(*args, cwd=ROOT):
    return subprocess.check_output(['git', '-C', str(cwd), *args])


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


async def main():
    audit = json.loads((OUT / 'initial_audit.json').read_text(encoding='utf-8'))
    plan = json.loads((OUT / 'backup_plan.json').read_text(encoding='utf-8'))
    # These three matches were reviewed in context: passwords for localhost tester accounts.
    reviewed = {'dd452966712f1e3ba20c869596198ac3b3d80cd0',
                '1cf90af0e9d06b80123622549dbaf56bc05e1e51',
                '9a3016af27a480517656e5412be24ab703d03e45'}
    assert all(x['object'] in reviewed and x['types'] == ['credential-value'] for x in plan['committed_secret_pattern_hits'])
    generic = re.compile(rb'''(?i)["'](?:token|secret|authorization|credential|pw|auth_token|account_token|device_credential)["']\s*:\s*["']([A-Za-z0-9_./+!@#%=-]{16,})["']''')
    jwt = re.compile(rb'\beyJ[A-Za-z0-9_-]{8,}\.eyJ[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]{12,}')
    for row in plan['plans']:
        keep = []
        for item in row['files']:
            p = Path(row['worktree']) / item['path']
            data = p.read_bytes()
            if generic.search(data) or jwt.search(data):
                plan['excluded'].append({'worktree': row['worktree'], 'path': item['path'], 'reason': 'literal-auth-data-local-only', 'bytes': len(data)})
                continue
            assert digest(p) == item['sha256'], 'Source changed after audit: ' + str(p)
            keep.append(item)
        row['files'] = keep
    # Include the backup tools themselves, which were created after the initial inventory.
    primary = next(x for x in plan['plans'] if Path(x['worktree']) == ROOT)
    known = {x['path'] for x in primary['files']}
    for p in (ROOT / '.claude/storage-migration').glob('*.py'):
        rel = p.relative_to(ROOT).as_posix()
        if rel not in known:
            primary['files'].append({'path': rel, 'bytes': p.stat().st_size, 'sha256': digest(p)})
    plan['plans'] = [x for x in plan['plans'] if x['files']]
    (OUT / 'final_backup_plan.json').write_text(json.dumps(plan, ensure_ascii=False, indent=2), encoding='utf-8')
    run = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
    prefix = 'backup/' + run
    state = {'run': run, 'prefix': prefix, 'detached_refs': [], 'snapshots': [],
             'reviewed_test_fixture_objects': sorted(reviewed), 'original_worktrees_untouched': True}
    for row in audit['worktrees']:
        if not row['branch']:
            branch = prefix + '/detached-' + Path(row['path']).name
            git('branch', branch, row['head'])
            state['detached_refs'].append({'branch': branch, 'head': row['head'], 'worktree': row['path']})
    bundle = OUT / ('all-refs-before-push-' + run + '.bundle')
    git('bundle', 'create', str(bundle), '--all')
    git('bundle', 'verify', str(bundle))
    state['bundle'] = str(bundle)
    state_path = OUT / 'execution.json'
    state_path.write_text(json.dumps(state, ensure_ascii=False, indent=2), encoding='utf-8')
    assert TEMP.is_relative_to(ROOT) and not TEMP.exists()
    cfg = tomllib.loads(Path('C:/Users/shota/.codex/config.toml').read_text(encoding='utf-8-sig'))['mcp_servers']['git']
    assert Path(cfg['args'][-1]) == ROOT
    async with stdio_client(StdioServerParameters(command=cfg['command'], args=cfg['args'])) as (r, w):
        async with ClientSession(r, w) as session:
            await session.initialize()

            async def call(name, args):
                result = await session.call_tool(name, args)
                if result.isError:
                    raise RuntimeError(result.model_dump_json())
                return result

            for i, row in enumerate(plan['plans']):
                source = Path(row['worktree'])
                label = 'remote' if source == ROOT else source.name
                branch = prefix + '/wip-' + label
                if i == 0:
                    git('worktree', 'add', '-b', branch, str(TEMP), row['base_head'])
                else:
                    git('branch', branch, row['base_head'])
                    await call('git_checkout', {'repo_path': str(TEMP), 'branch_name': branch})
                for item in row['files']:
                    src = source / item['path']
                    assert digest(src) == item['sha256'], 'Snapshot source changed: ' + str(src)
                    dst = TEMP / item['path']
                    assert dst.is_relative_to(TEMP)
                    dst.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(src, dst)
                paths = [x['path'] for x in row['files']]
                for offset in range(0, len(paths), 300):
                    await call('git_add', {'repo_path': str(TEMP), 'files': paths[offset:offset + 300]})
                staged = set(git('diff', '--cached', '--name-only', '-z', cwd=TEMP).decode().strip('\0').split('\0'))
                assert staged == set(paths), 'Unexpected staged snapshot files'
                await call('git_commit', {'repo_path': str(TEMP), 'message': 'chore(backup): snapshot untracked work from ' + label})
                commit = git('rev-parse', 'HEAD', cwd=TEMP).decode().strip()
                assert git('rev-parse', 'HEAD^', cwd=TEMP).decode().strip() == row['base_head']
                actual = set(git('diff-tree', '--no-commit-id', '--name-only', '-r', '-z', commit, cwd=TEMP).decode().strip('\0').split('\0'))
                assert actual == set(paths)
                state['snapshots'].append({'branch': branch, 'commit': commit, 'source_worktree': str(source), 'files': len(paths)})
                state_path.write_text(json.dumps(state, ensure_ascii=False, indent=2), encoding='utf-8')
                print(json.dumps(state['snapshots'][-1], ensure_ascii=True), flush=True)
    assert not git('status', '--porcelain', cwd=TEMP).strip(), 'Snapshot worktree is not clean'
    git('worktree', 'remove', str(TEMP))
    state['temporary_worktree_removed'] = True
    state_path.write_text(json.dumps(state, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'snapshot_branches': len(state['snapshots']), 'detached_refs': len(state['detached_refs']),
                      'snapshot_files': sum(x['files'] for x in state['snapshots']), 'bundle': str(bundle)}, ensure_ascii=True), flush=True)


asyncio.run(main())
