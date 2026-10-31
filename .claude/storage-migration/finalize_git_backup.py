import collections
import json
import shutil
import subprocess
from pathlib import Path

ROOT = Path('C:/work/remote')
MAIN = ROOT / '.claude/worktrees/main-stability-integration'
LOCAL = ROOT / '.claude/storage-migration'
OUT = Path('E:/remote-output/migration/20261031-storage/git-backup')


def git(*args, cwd=ROOT):
    return subprocess.check_output(['git', '-C', str(cwd), *args])


audit = json.loads((OUT / 'initial_audit.json').read_text(encoding='utf-8'))
state = json.loads((OUT / 'execution.json').read_text(encoding='utf-8'))
plan = json.loads((OUT / 'final_backup_plan.json').read_text(encoding='utf-8'))
remote = dict((ref, sha) for line in git('ls-remote', '--heads', '--tags', 'origin').decode().splitlines()
              for sha, ref in [line.split('\t')])
refs = dict((ref, sha) for line in git('for-each-ref', '--format=%(refname) %(objectname)', 'refs/heads', 'refs/tags').decode().splitlines()
            for ref, sha in [line.split()])
assert all(remote.get(ref) == sha for ref, sha in refs.items()), 'Remote mismatch'
for row in audit['worktrees']:
    p = Path(row['path'])
    assert git('rev-parse', 'HEAD', cwd=p).decode().strip() == row['head']
    assert not git('diff', '--name-only', '-z', cwd=p)
    assert not git('diff', '--cached', '--name-only', '-z', cwd=p)
assert state['temporary_worktree_removed']
verification = {'remote_branches_matching': sum(x.startswith('refs/heads/') for x in refs),
                'remote_tags_matching': sum(x.startswith('refs/tags/') for x in refs),
                'all_local_refs_exact_remote_match': True, 'original_worktree_heads_preserved': len(audit['worktrees']),
                'snapshot_branches': len(state['snapshots']), 'snapshot_files': sum(x['files'] for x in state['snapshots']),
                'detached_backup_refs': len(state['detached_refs']), 'force_push': False,
                'original_untracked_files_left_in_place': True, 'product_work_still_stopped': True}
(OUT / 'remote_verification_before_wrapup.json').write_text(json.dumps(verification, ensure_ascii=False, indent=2), encoding='utf-8')

tools = ['audit_git_backup.py', 'prepare_git_backup.py', 'snapshot_git_work.py', 'resume_git_snapshots.py', 'finalize_git_backup.py']
target = MAIN / '.claude/storage-migration'
target.mkdir(exist_ok=True)
for name in tools:
    shutil.copy2(LOCAL / name, target / name)
history = f'''
### 2026-10-31 전체 Git 작업 백업·커밋·푸시 (제품 STOP 유지)
- 목표: 사용자의 "작업물들 전부다 커밋 푸시 되어있는지 확인" 및 "안되있으면 다 푸시해놔" 지시에 따라 이번 백업 범위의 push를 명시 승인받아 원격 복구점을 만든다. 제품 구현·빌드·실기·NAS 배포는 재개하지 않는다.
- 조사: 등록 worktree 23개, 로컬 브랜치 78개를 원격에 직접 대조했다. 원격은 브랜치 2개뿐이며 고유 미푸시 커밋 253개, main은 7커밋 앞서 있었다. 기존 추적 파일의 미커밋 수정·스테이징은 0이었지만 미추적 작업·실행 자료가 있었다.
- 보존: 원본 브랜치·파일·인덱스를 유지하면서 미추적 소스·문서·검증 스크립트 {verification['snapshot_files']:,}개를 {verification['snapshot_branches']}개 WIP 백업 브랜치에 Git MCP로 커밋했다. detached HEAD {verification['detached_backup_refs']}개에도 백업 ref를 만들었다. 백업 prefix는 `{state['prefix']}/`. 런타임 인증자료·캐시·바이너리·스크린샷·실행 로그 등은 공개 저장소 업로드 대상에서 제외하고 원본 C/E에 보존했다. 원본의 미추적 상태는 그대로 남긴다.
- 보안 검사: 기존 미푸시 커밋의 신규 blob 1,172개와 선택한 미추적 파일을 비밀정보 패턴으로 검사했다. 기존 커밋의 3개 매치는 localhost tester 계정용 테스트 상수임을 문맥으로 확인했다. 로컬 인증값 패턴이 있는 미추적 파일은 업로드하지 않았다. 실제 값은 보고서나 도구 출력에 노출하지 않았다.
- 푸시·검증: 강제 push 없이 원자적 브랜치 push와 태그 push를 수행했다. 원격 ls-remote 기준 브랜치 {verification['remote_branches_matching']}개·태그 {verification['remote_tags_matching']}개가 로컬 SHA와 모두 일치했다. 원래 worktree 23개의 HEAD·추적 파일·인덱스 보존을 확인했고 임시 백업 worktree는 제거했다. 이 마무리 기록 commit도 별도로 push하고 최종 SHA를 재확인한다.
- 변경 파일: docs/history.md·docs/구현계획.md와 .claude/storage-migration의 audit_git_backup.py, prepare_git_backup.py, snapshot_git_work.py, resume_git_snapshots.py, finalize_git_backup.py. 제품 소스 변경 없음. 검사 중 MCP branch 생성이 SHA를 받지 않는 점을 확인하여 신규 branch ref만 Git으로 만들고, 스테이징·커밋은 계속 Git MCP를 사용했다.
- 복구 근거: E:/remote-output/migration/20261031-storage/git-backup의 initial_audit.json, final_backup_plan.json, execution.json, remote_verification_before_wrapup.json 및 `{state['bundle']}`. 실행 자료 제외는 파일 손실을 뜻하지 않으며 원본에 그대로 있다. 이력 전체 bundle은 최종 기록까지 포함해 한 번 더 보존한다.
- 다음: 이번 전체 브랜치·태그와 WIP 소스 백업의 원격 동기화 완료. 제품 STOP 유지. 사용자 지금 할 일 없음. 소스 검수·main 기능 머지·빌드·NAS 배포·미검증 기능 승인은 이번 백업에 포함하지 않는다.
'''
p = MAIN / 'docs/history.md'
raw = p.read_bytes()
assert history.strip().splitlines()[0].encode('utf-8') not in raw
newline = '\r\n' if raw.count(b'\r\n') > raw.count(b'\n') // 2 else '\n'
p.write_bytes(raw + history.replace('\n', newline).encode('utf-8'))
p = MAIN / 'docs/구현계획.md'
raw = p.read_bytes()
check = '''
### BACKUP. 작업 재개 전 Git 원격 보존

- [x] 등록 worktree 23개·로컬 78브랜치의 미커밋/미푸시 상태 및 비밀정보 패턴 확인
- [x] WIP 소스 10개 백업 브랜치·detached HEAD 6개·전체 로컬 브랜치/태그 원격 동기화 및 원본 작업 상태 보존
'''
assert check.strip().splitlines()[0].encode('utf-8') not in raw
newline = '\r\n' if raw.count(b'\r\n') > raw.count(b'\n') // 2 else '\n'
p.write_bytes(raw + check.replace('\n', newline).encode('utf-8'))
print(json.dumps(verification, ensure_ascii=True))
