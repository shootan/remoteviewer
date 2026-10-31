import asyncio
import hashlib
import json
import shutil
import subprocess
import tomllib
from pathlib import Path
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

ROOT = Path('C:/work/remote')
TEMP = ROOT / '.claude/storage-migration/backup-worktree'
OUT = Path('E:/remote-output/migration/20261031-storage/git-backup')


def git(*args, cwd=ROOT):
    return subprocess.check_output(['git', '-C', str(cwd), *args])


async def main():
    plan = json.loads((OUT / 'final_backup_plan.json').read_text(encoding='utf-8'))
    state = json.loads((OUT / 'execution.json').read_text(encoding='utf-8'))
    assert TEMP.is_relative_to(ROOT) and TEMP.is_dir()
    assert not git('status', '--porcelain', cwd=TEMP).strip()
    cfg = tomllib.loads(Path('C:/Users/shota/.codex/config.toml').read_text(encoding='utf-8-sig'))['mcp_servers']['git']
    async with stdio_client(StdioServerParameters(command=cfg['command'], args=cfg['args'])) as (r, w):
        async with ClientSession(r, w) as session:
            await session.initialize()
            async def call(name, args):
                result = await session.call_tool(name, args)
                if result.isError:
                    raise RuntimeError(result.model_dump_json())
                return result
            for row in plan['plans'][len(state['snapshots']):]:
                source = Path(row['worktree'])
                label = 'remote' if source == ROOT else source.name
                branch = state['prefix'] + '/wip-' + label
                # The MCP branch-creation implementation accepts branch names only;
                # create this new ref with Git, retaining MCP for staging and commits.
                git('branch', branch, row['base_head'])
                await call('git_checkout', {'repo_path': str(TEMP), 'branch_name': branch})
                for item in row['files']:
                    src = source / item['path']
                    assert hashlib.sha256(src.read_bytes()).hexdigest() == item['sha256']
                    dst = TEMP / item['path']
                    assert dst.is_relative_to(TEMP)
                    dst.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(src, dst)
                paths = [x['path'] for x in row['files']]
                for offset in range(0, len(paths), 300):
                    await call('git_add', {'repo_path': str(TEMP), 'files': paths[offset:offset + 300]})
                staged = set(git('diff', '--cached', '--name-only', '-z', cwd=TEMP).decode().strip('\0').split('\0'))
                assert staged == set(paths)
                await call('git_commit', {'repo_path': str(TEMP), 'message': 'chore(backup): snapshot untracked work from ' + label})
                commit = git('rev-parse', 'HEAD', cwd=TEMP).decode().strip()
                assert git('rev-parse', 'HEAD^', cwd=TEMP).decode().strip() == row['base_head']
                actual = set(git('diff-tree', '--no-commit-id', '--name-only', '-r', '-z', commit, cwd=TEMP).decode().strip('\0').split('\0'))
                assert actual == set(paths)
                state['snapshots'].append({'branch': branch, 'commit': commit, 'source_worktree': str(source), 'files': len(paths)})
                (OUT / 'execution.json').write_text(json.dumps(state, ensure_ascii=False, indent=2), encoding='utf-8')
                print(json.dumps(state['snapshots'][-1], ensure_ascii=True), flush=True)
    assert not git('status', '--porcelain', cwd=TEMP).strip()
    git('worktree', 'remove', str(TEMP))
    state['temporary_worktree_removed'] = True
    (OUT / 'execution.json').write_text(json.dumps(state, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'snapshots': len(state['snapshots']), 'files': sum(x['files'] for x in state['snapshots']), 'detached_refs': len(state['detached_refs'])}), flush=True)


asyncio.run(main())
