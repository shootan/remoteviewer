import json,subprocess,datetime
from pathlib import Path

ROOT=Path('C:/work/remote')
MAIN=ROOT/'.claude/worktrees/main-stability-integration'
REPORT=Path('E:/remote-output/migration/20261031-storage')
copy=json.loads((REPORT/'copy_verdict.json').read_text(encoding='utf-8'))
repair=json.loads((REPORT/'repair_verdict.json').read_text(encoding='utf-8'))
exports=json.loads((REPORT/'export_hashes.json').read_text(encoding='utf-8'))
assert not copy['mismatches'] and not copy['missing_dirs'] and repair['git_fsck']=='PASS'
history='''
### 2026-10-31 GNLink 저장소 C 이전·E 산출물 보관 (제품 작업 중지 유지)
- 목표: D 제거 준비를 위해 저장소를 `C:/work/remote`로 복사하고 빌드 작업 폴더는 C, 최종 산출물·보관 자료는 `E:/remote-output`으로 분리한다. D 원본과 다른 프로젝트는 보존한다.
- 검증: `.git`·미추적 소스·기존 기록을 포함한 58,952개 일반 파일, 7,003,683,677 bytes를 복사하고 전 파일 SHA-256 일치를 확인했다. 내부 junction 3개를 C 대상으로 다시 만들었고, 23개 Git worktree의 HEAD·기존 status 항목 보존 및 공통 Git 디렉터리 C 연결을 확인했다. 기존 C worktree의 WebView2 junction 15개를 C 대상으로 바꿨으며 이전 link 자체는 새 저장소의 이전 기록 아래 보존했다. `git fsck --full --no-dangling` PASS.
- E 보관: 릴리스·검증 자료 1,183개, 595,057,686 bytes를 새 스냅샷에 복사하고 SHA-256을 대조했다. 원본·C 사본 삭제 0. 상세 원문은 `E:/remote-output/migration/20261031-storage`, 인계와 반복 사용 가능한 산출물 내보내기 도구는 `C:/work/remote/.claude/storage-migration`에 있다.
- 변경 파일: 이번 기록은 docs/history.md와 docs/구현계획.md뿐이다. 제품 소스·버전·기존 브랜치는 변경하지 않았다. 빌드·Host/Viewer/AVD·설치·배포·push 미실행. 기존 제품 작업 STOP 유지.
- 한계와 다음: 이 세션 Git MCP는 D 저장소만 허용하여 C 기록 커밋은 미실행이다. C에서 새 세션을 연 뒤 이 2파일만 검토·Git MCP 커밋한다. 이전 빌드 캐시·venv·과거 고정 runner에는 D 절대 경로가 남을 수 있으므로 제품 작업 재개 승인 후 새 경로에서 재생성/고정한다. D에는 다른 프로젝트·세션도 있어 이 결과만으로 D 전체 제거를 승인하지 않는다.
'''
plan='''
### ENV. 저장소 위치 이전

- [x] GNLink 저장소 C:/work/remote 복사·전 파일 해시 확인, 기존 C worktree Git/WebView2 연결 수정, E:/remote-output 릴리스·검증 보관본 확인
- [ ] C 경로 새 세션의 Git MCP 연결 확인 및 이전 기록 2파일 커밋 (제품 작업 STOP 유지)
- [ ] D의 다른 프로젝트·세션 이전 및 D 제거 범위 확인
'''
for path,text in [(MAIN/'docs/history.md',history),(MAIN/'docs/구현계획.md',plan)]:
    raw=path.read_bytes()
    marker=text.strip().splitlines()[0].encode('utf-8')
    if marker in raw:raise RuntimeError('Records already appended: '+str(path))
    newline='\r\n' if raw.count(b'\r\n')>raw.count(b'\n')//2 else '\n'
    path.write_bytes(raw+text.replace('\n',newline).encode('utf-8'))

result=f'''# 스토리지 이전 결과

- 상태: **파일 이전·Git 연결 검증 완료 / C 신규 세션 전환 및 기록 커밋 대기**
- 원본: `D:\\remote\\remote` — 보존, 삭제 없음. 이후 작업은 C에서 수행.
- 새 저장소: `C:\\work\\remote`.
- 정책/main worktree: `C:\\work\\remote\\.claude\\worktrees\\main-stability-integration`.
- 복사: {copy['files']:,} files / {copy['bytes']:,} bytes, SHA-256 불일치 0, 누락 디렉터리 0.
- 내부 junction 3개, 기존 C worktree WebView2 junction {repair['webview_junctions']}개 경로 수정.
- Git worktree {repair['worktrees']}개 HEAD·기존 status 보존 확인, common-dir C, fsck PASS.
- E 산출물/검증 보관본: {len(exports):,} files / {sum(x['bytes'] for x in exports):,} bytes, SHA-256 일치.
- E 릴리스: `E:\\remote-output\\releases\\20261031-storage`.
- E 검증 자료: `E:\\remote-output\\evidence\\20261031-storage`.
- 상세 보고서: `{REPORT}`.
- 제품 소스 변경·제품 빌드·실기·설치·배포·push·D 원본 삭제: 없음. 제품 STOP 유지.
- 커밋: 없음. Git MCP 허용 저장소가 이 세션에서 D로 고정되어 C 요청을 거부했다. CLI 우회 커밋은 하지 않았다.
- 커밋 대기 파일: 새 main worktree의 `docs/history.md`, `docs/구현계획.md` (이전 기록만).
- 제한: 과거 캐시/runner/venv의 절대 경로와 고정 해시는 그대로 보존한다. 새 C 세션에서 제품 작업 재개 승인 후 새 경로로 재생성/재고정한다.
- D 전체에는 다른 프로젝트·에이전트가 있으므로 D 파티션 제거 준비 완료를 뜻하지 않는다.

새 세션은 `.claude/storage-migration/NEW_SESSION.md`를 먼저 읽는다.
'''
(ROOT/'.claude/storage-migration/MIGRATION_RESULT.md').write_text(result,encoding='utf-8')
(REPORT/'MIGRATION_RESULT.md').write_text(result,encoding='utf-8')
print('Migration result and documentation records written. Product work remains stopped.')
