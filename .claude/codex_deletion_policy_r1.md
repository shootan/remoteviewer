# 삭제 후보 운영 정책 문서 전문 검토 r1

2026-10-27 / task t-94tjfghd / 검증용 remote#yj8mrmrx.

**OK — main의 해당4파일만 Git MCP Conventional Commit 진행. 단독 push 없음.** 제품/삭제 실행은 없다.

고정 main HEAD 5970f046f56dd8f55b98b4ab971ed7376c6d5848 위 미커밋 diff .claude/deletion_policy_r1.diff 전문과 실제 AGENTS.md/CLAUDE.md/docs/history.md/docs/구현계획.md를 확인했다. 실제 diff는4파일 +26/−1, 다른 tracked 변경 없음, git diff --check rc0. 네 파일 모두 CRLF이며 lone LF0을 직접 확인했다.

- 상위 root 밖 삭제 금지를 유지하고 작업 중 중간 삭제 권한 질문을 후보 보존/기록으로 대체한다. 사용자의 정리 요청 때 한 번에 제시하고 명시 승인분만 삭제한다.
- CLAUDE를 상세 정본으로 삼아 중앙 목록·소유/조건/상태·중복 방지·검증용 한 창구·삭제 직전 대상/실경로/링크·진행 중 보존을 명시한다. 후보/언제든은 승인이라는 뜻이 아님을 분명히 했다.
- 상시 예외는 시험이 CreateNew로 직접 만든 C:/work/gnlink-wt/SLOT_REQUEST.txt 한 인스턴스 종료 삭제뿐이다. 타인/stale 슬롯·TEMP·scratch/worktree로 확대하지 않는다.
- 계획은 checkbox1줄만, history에 목표·변경·검토/다음 담당만 기록한다. 슬롯 규칙은 검증용10-15 설정이었다는 출처 정정도 유지한다. 기존 후보 목록/증거는 삭제하지 않는다.

커밋 직전 허용된 작은 정리: AGENTS의 “유일한 예외”를 CLAUDE와 같은 **“유일한 상시 예외”**로 맞추면 미래의 사용자가 개별 일괄 승인하는 삭제와 더 명확히 구분된다. history 검토 줄에는 Codex 전문 검토 OK·diff-check/CRLF 결과를 완료 메타로 갱신할 수 있다. 이 두 문구 보완에 새 전문 검토 왕복은 필요 없다. 다른 정책·역할·게시 권한은 변경하지 않는다.

agents-policy-updater 스킬을 적용하되 사용자 저장소의 history.md/계획 checkbox·문서2역할/Git MCP·단독 push 보류가 일반 주간 이력/즉시 push 지침에 우선한다. 통과한4파일 commit hash·최종 diff 범위/상태를 같은 task로 회신하면 종료 기록과 사용자 안내를 연결한다.

검증용이 사용자 정리 요청 전 후보만 관리하며, 이 문서 commit을 실제 정리 승인으로 해석하지 않는다. APK0.2.32 진행과 별도이고 방해하지 않는다. Codex 제품/정책 tracked 파일 편집·빌드·삭제·commit/push 없음, 미추적 검토서/인계만 작성했다. 사용자 추가 조치 없음.

## 최종 commit 확인

main **2137f3a3a261422b548236398ae61e4dcd011e0b** = parent5970f04 + 해당4파일26+/1-. Codex가 실제commit전체diff·허용된상시예외/history완료메타만변경·working tree tracked clean·diff-check0을 직접확인했다. origin/main은5970f04로 이task단독push없음. 실제삭제·제품변경없음. 이전seq14220 turn-ended 알림은최신commit상태가아니므로추가commit/재검토하지않는다.
