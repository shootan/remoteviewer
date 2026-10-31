# C/E 저장소 이전·D remote 삭제 후 인계

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
