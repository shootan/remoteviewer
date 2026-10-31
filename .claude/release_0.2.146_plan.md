# 0.2.146 (PC) 릴리스 계획 — file-copy 네트워크 (검증용, 2026-09-30 20:3x 작성, 조건부)

전제: 36a0e22(r8) 검증용 독립 실행 통과 + Codex(lidp0c77) OK. 둘 중 하나라도 아니면 r9 로 돌아가고 이 계획은 보류.
사용자 지시(17:0x): "작업 마무리되면 게시까지 진행해".

1. 병합: main worktree(main-stability-integration, HEAD a450565)에 `git merge --no-ff 36a0e22` (merge-tree 사전 확인: 충돌 0). 브랜치의 뒤 commit 은 없음(36a0e22 가 tip).
2. main 통합 빌드·영향 회귀: build-verify 와 별개 build dir 에서 gate·net e2e·helper e2e·clip_image e2e·bar e2e 1회.
3. push origin main (분류기가 막으면 우회 없이 사용자에게 알림).
4. 버전 commit: product_version.hpp 0.2.145 → 0.2.146, 제목 `release: 0.2.146 -- ...`(Git MCP, 버전 줄만).
5. `automation/gnlink_release.sh --worktree <main wt> --version 0.2.146 --sign` → 페이로드 버전 문자열(UTF-16 "0.2.146") 확인, payload 에 GNLinkClipHelper.exe 포함 확인, dist/GNLinkSetup-0.2.146.exe 보존.
6. `automation/gnlink_deploy.sh --release-dir .claude/rel/0.2.146 --verify-only` → `--dry-run` → 실행 → 외부 https 전체 해시(스크립트 + 검증용 독립 curl 1개 이상).
7. 버전 commit push, origin/main == 게시 commit 확인. 기록: state_2026-09-30.md, 작업목록(C11-N 완료·V 항목: 두 PC 실기 Ctrl+C/V) — 문서 편집은 Codex 검토 뒤 커밋.
8. 사용자 최종 보고: 해결 내용 / 버전·URL·해시 / 미검증(실기: 탐색기 Ctrl+C→V 두 PC, 도우미 첫 출하라 Setup 설치 권장) / 사용자가 할 일.
