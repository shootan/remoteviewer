import json
import subprocess
from pathlib import Path

ROOT = Path('C:/work/remote')
MAIN = ROOT / '.claude/worktrees/main-stability-integration'
LOCAL = ROOT / '.claude/storage-migration'
REPORT = Path('E:/remote-output/migration/20261031-storage')
deleted = json.loads((REPORT / 'deletion_result.json').read_text(encoding='utf-8'))
assert deleted['success'] and not Path('D:/remote').exists()

history = f'''
### 2026-10-31 사용자 승인 D remote 삭제·MCP 해제 확인 (제품 STOP 유지)
- 목표: 사용자의 명시적 "삭제해. 그리고 이제 mcp연결다끊겻을거야 확인해" 지시에 따라 사전 조사한 D:/remote 하위 네 폴더를 삭제한다. D 파티션이나 다른 D 프로젝트는 범위 밖이다.
- 삭제 전: D:/remote 참조 프로세스 0, D remote 버스 세션 0, 현재 Git·파일시스템 MCP C 응답 확인. 이전 전 파일 SHA-256 조사 이후 D 원본 58,952개 파일의 추가·누락·크기·수정/생성 시각 변경이 없고 junction 3개의 원래 대상도 같은 것을 확인했다. build·remote-worktrees 원본 1,992파일과 E ZIP 보관본을 다시 SHA-256 대조해 PASS.
- 삭제: 절대 경로 D:/remote/build, download, remote-worktrees, remote를 검증한 뒤 일반 파일 {deleted['files']:,}개/{deleted['bytes']:,} bytes, 디렉터리 {deleted['directories']:,}개를 제거했다. junction 3개는 대상으로 진입하지 않고 링크 자체만 제거했다. 상위 D:/remote도 비어 있음을 확인하고 제거했다. C/E 대상 삭제 없음.
- 삭제 후: C worktree 23개 HEAD·기존 status·common-dir, 활성 junction 18개, git fsck PASS. C의 기존 비-Git 사본 누락 0. E 기존 보관본 1,183파일의 존재·크기 및 legacy-build-worktrees-20261031-212555.zip 보존 확인. 과거 D 대상 보관 junction 15개는 대상 경로를 C/E retired_junction_metadata.json으로 보존한 뒤 깨진 링크만 제거하여 Git 조회 경고를 해소했다.
- 변경 파일: docs/history.md(실행·검증 기록), docs/구현계획.md(진행 체크). 미추적 .claude/storage-migration의 삭제 도구·인계·상태를 갱신했다. 원문 근거: E:/remote-output/migration/20261031-storage/predelete_check.json, deletion_result.json, postdelete_check.json, retired_junction_metadata.json.
- 다음: 이번 D:/remote 정리 완료. 사용자가 지금 할 일 없음. 제품 소스·버전 변경, 제품 실행·빌드·실기·설치·배포·push는 수행하지 않았고 제품 STOP 유지. 과거 D 절대 경로 캐시/runner의 재생성·재고정은 제품 작업 재개 승인 뒤 진행한다. D 전체 파티션 및 다른 프로젝트 정리는 이번 완료 범위가 아니다.
'''
p = MAIN / 'docs/history.md'
raw = p.read_bytes()
committed = subprocess.check_output(['git', '-C', str(MAIN), 'show', 'HEAD:docs/history.md'])
newline = '\r\n' if raw.count(b'\r\n') > raw.count(b'\n') // 2 else '\n'
original = committed.replace(b'\r\n', b'\n').replace(b'\n', newline.encode('ascii'))
# A previous shell pipeline appended only this task's text with ASCII replacement.
# Preserve the exact committed prefix and replace only that uncommitted append.
assert raw.startswith(original)
tail = raw[len(original):]
assert not tail or (tail.lstrip().startswith(b'### 2026-10-31 ') and b'?' in tail)
p.write_bytes(original + history.replace('\n', newline).encode('utf-8'))
p = MAIN / 'docs/구현계획.md'
raw = p.read_bytes()
old = '- [ ] D 원본 저장소 참조 MCP 연결 정리 및 지정 폴더 삭제'.encode('utf-8')
new = '- [x] D 원본 저장소 참조 MCP 연결 해제 확인 및 D:/remote 지정 네 폴더·빈 상위 폴더 삭제'.encode('utf-8')
assert raw.count(old) == 1
p.write_bytes(raw.replace(old, new))

result = f'''# 스토리지 이전·D remote 정리 결과

- 상태: **C/E 이전 및 사용자 승인 D:/remote 삭제 완료**. 제품 작업 STOP 유지.
- 현재 저장소: `C:/work/remote`, Git 공통 데이터 `C:/work/remote/.git`.
- main worktree: `C:/work/remote/.claude/worktrees/main-stability-integration`.
- 현재 MCP: Git·파일시스템 모두 C에 직접 응답. 삭제 전 D 참조 프로세스·D remote 버스 세션 0 확인.
- 삭제: 사용자 명시 지시로 `D:/remote/build`, `download`, `remote-worktrees`, `remote`와 빈 상위 `D:/remote`를 제거. {deleted['files']:,} 일반 파일 / {deleted['bytes']:,} bytes. junction 3개는 링크 자체만 제거.
- 삭제 전 보존 확인: D 원본 전 파일 SHA-256 조사 이후 변경 없음. build·remote-worktrees 1,992파일은 E ZIP과 원본 SHA-256 재검사 PASS.
- 삭제 후 직접 확인: C worktree 23개·활성 junction 18개·Git fsck PASS, 기존 비-Git 사본 누락 0. E 기존 보관본 1,183파일 존재·크기 및 레거시 ZIP 보존 확인.
- E 릴리스: `E:/remote-output/releases/20261031-storage`.
- E 검증 자료: `E:/remote-output/evidence/20261031-storage`.
- 레거시 보관본: `E:/remote-output/evidence/20261031-storage/legacy-build-worktrees-20261031-212555.zip`.
- 과거 D 대상 보관 junction 15개는 `retired_junction_metadata.json`에 원래 대상을 C/E 보존하고 깨진 링크 자체만 제거했다.
- 조사·삭제 근거: `E:/remote-output/migration/20261031-storage`의 predelete_check.json, deletion_result.json, postdelete_check.json, legacy_sibling_backup.json, retired_junction_metadata.json.
- 앞선 기록 commit: 이전/MCP `60cb21f58f982f2f872d7080035704cb5ed238dd`, 삭제 사전조사 `b02ac5ae5a403171299d54334a6a47bb8fc347bc`. 삭제 결과 기록 commit은 deletion_commit_receipt.json에 고정한다.
- 제품 소스·버전 변경·제품 빌드/실기·설치·배포·push 없음. D 파티션과 다른 프로젝트 삭제 없음.
- 제한: 과거 빌드 캐시·venv·고정 runner의 D 절대 경로는 제품 재개 승인 후 재생성/재고정한다. 이번에 제품 동작을 검사하지 않았다.
- 사용자가 지금 할 일: 없음.
'''
for p in [LOCAL / 'MIGRATION_RESULT.md', REPORT / 'MIGRATION_RESULT.md']:
    p.write_text(result, encoding='utf-8')
(LOCAL / 'NEW_SESSION.md').write_text('''# C/E 저장소 이전·D remote 삭제 후 인계

## 현재 유효 경계
- 현재 경로는 `C:/work/remote`. 기본 브랜치 `refactor/viewer-split`과 기존 미커밋 작업을 유지한다.
- 제품 작업 STOP 유지. 제품 실행·빌드·AVD·adb·설치·배포·push는 재개하지 않는다.
- 사용자 명시 삭제 지시에 따라 `D:/remote` 아래 build/download/remote-worktrees/remote 및 빈 상위 폴더를 삭제했다. D 원본은 이제 존재하지 않는다. D 파티션이나 다른 프로젝트는 삭제하지 않았다.
- 현재 Git·파일시스템 MCP 모두 C 경로로 직접 응답한다. D를 참조하던 MCP 프로세스와 D remote 버스 세션은 삭제 전에 0으로 확인했다.

## 현재 위치와 근거
- Git 공통 데이터: `C:/work/remote/.git`.
- main/정책 worktree: `C:/work/remote/.claude/worktrees/main-stability-integration`.
- 기존 C worktree: `C:/work/gnlink-wt/*`, 활성 Git/WebView2 연결은 C이다.
- E 릴리스: `E:/remote-output/releases/20261031-storage`.
- E 검증·레거시 보관: `E:/remote-output/evidence/20261031-storage`.
- build·remote-worktrees 보관본: `legacy-build-worktrees-20261031-212555.zip` (1,992파일 SHA-256 확인).
- 보고서: `E:/remote-output/migration/20261031-storage`의 MIGRATION_RESULT.md, predelete_check.json, deletion_result.json, postdelete_check.json.
- 과거 D 대상 보관 junction 15개는 원래 경로를 retired_junction_metadata.json에 C/E 보존하고 깨진 링크 자체를 제거했다. 실행 연결과 혼동하지 않는다.
- 삭제 후 C worktree 23개·활성 junction 18개·Git fsck 및 비-Git 사본 누락 0 확인. E 보관본도 보존 확인.
- 후속 최종 산출물 내보내기 도구는 `.claude/storage-migration/export_outputs.py`. 매번 새 이름으로 복사하며 삭제하지 않는다.

## 재개 전 주의
- 이전 캐시/CMake/Gradle/venv·automation probe·고정 runner에는 D 절대 경로가 남을 수 있다. 제품 재개 승인 후 새 C 경로에서 재생성·재고정한다. 과거 파일을 일괄 치환하거나 같은 검토본이라고 주장하지 않는다.
- 시험 임시 폴더는 해당 worktree `.claude/test-tmp`만 사용한다. E 보관본을 cleanup root로 사용하지 않는다.
- 사용자 standing push 보류가 디스크의 다른 정책보다 우선한다.

## 제품 미결 (모두 STOP)
- PC 0.2.164 old-APK lane의 구/신 참조 SOURCE·first-loss 진단·산출물 고정·P cold-start 기한 보수는 미결이며 출하 승인이 아니다.
- APK inbox C r4 prefix: `.claude/codex_apk_inbox_prefix_selection_r1.md`. seq16277은 delivered=0이었고 STOP 이후 재송신·구현·실기를 하지 않았다.
- 원문·packet은 `.claude/caplife`, `.claude/verify_c2`, `.claude/codex_old_apk_review` 등에 보존한다. 늦은 실행 지시는 재개 승인이 아니다.
- 직접 monitor-power broadcast 사고의 `.claude/codex_oldref_review_safety_finding.md`와 안전 ledger를 유지한다. UNSAFE_original·prelogin·승인되지 않은 실제 OS 입력 시험은 계속 금지한다.

## 마무리
- 이번 저장소 이전·D remote 정리는 완료했다. 사용자가 지금 할 일은 없다.
- 삭제 결과 기록 commit은 `deletion_commit_receipt.json`을 읽는다. 이전/MCP commit 60cb21f, 사전조사 commit b02ac5a. push 미실행.
''', encoding='utf-8')
print('Deletion history, progress and current handoff updated as UTF-8.')
