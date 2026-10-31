# INJ 종료 기록 — DOC 검토

2026-10-29 · **아래 감수 패치를 반영한 최종 내용으로 DOC OK.** 작업목록 INJ 행의 실행 금지·미검증 구분은 수용한다. 같은 내용을 다시 상신할 필요 없이, 검증용은 패치 적용/내용 hash 확인 후 문서 3개만 Git MCP로 commit할 수 있다. 기존 push 보류는 유지한다.

## 고정과 판정

- main worktree HEAD **14cdde16097d253a8de840d1a06679b3acfeddfa**의 미커밋 history/작업목록을 읽었다. 원 사본 inj_closure_docs.diff SHA-256 **2ccd8ef28f4f716dc6451ca783ff750fab8569f7c47adbd9ee7ea0c52f80bd44**와 대상 2파일 +10을 확인했다.
- **실제 전체 경로·개별 생성/삭제 결과 미확인**, **제품 출하 아님/R2 main 통합 대기**, **real4 기본 REFUSED 및 prelogin 사용자 재개 전 실행 금지**, old-host 미검증을 유지하는 방향은 정본과 맞다.
- 보정은 사실 범위와 표현을 맞추는 문서 감수다. 제품/시험 코드나 별건의 DW 등 기존 항목을 변경하지 않는다.

## 확정 보정 내용

1. 실제 구현에는 제품 power option/OS 효과 경계도 있으므로 제목과 범위를 **“제품 안전 경계·시험 하네스 보수”**로 적는다. 이번 main commit은 문서뿐이고 구현의 main 반영/출하는 R2라는 구분은 유지한다.
2. **입력·전원 API의 해당 효과**를 record한다고 한정하고, 일반 OFF 하네스와 record-only/fatal의 별도 검수 조건을 구분한다. record target에 일반 OFF 두 줄을 요구하거나 모든 OS 효과의 sandbox라고 읽히게 하지 않는다.
3. r1 FAIL/INVALID 이력을 보존하고 수정 후 r2 성공을 적는다. r1 record Host 원문 삭제로 세부 재구성이 불가능하다는 한계도 유지한다.
4. TEMP는 **코드 경로/시도·도구 호출 수**와 실제 실행/개별 정리 성공을 구분한다. runtime 작업용 9회 보고/27회 시도, gate 43개 도구 호출·8세션을 실제 삭제 성공 횟수라고 하지 않는다.
5. 필수 진행 snapshot을 docs/구현계획.md에 **체크박스만** 추가한다. 브랜치의 한정 하네스 완료와 real4/prelogin/R2 통합 미완료를 나눈다. 이력 서술은 history에만 둔다.

마지막 항목은 **AGENTS.md Mandatory Workflow 2**와 [post-task-wrapup SKILL.md](C:/Users/shota/.codex/skills/post-task-wrapup/SKILL.md)의 “update docs/history.md and docs/구현계획.md” 절차다. 새 정책을 추가한 것이 아니다. Skill 본문에 깨진 경로 글자가 있으나, 설명/AGENTS의 정확한 이름과 실제 존재하는 docs/구현계획.md를 사용한다.

## 적용할 결과물과 commit 범위

- 현재 미커밋 diff 위에 적용: `.claude/inj_closure_review/apply_review.patch`
- 최종 3파일 사본: `.claude/inj_closure_review/final/docs/`
- 최종 내용 hash: `.claude/inj_closure_review/approved_hashes.json`의 **UTF-8/LF/BOM 제외 정규화 hash**. byte 형식 차이와 내용 차이를 구분하기 위한 기준이다.
- 패치의 `git apply --check` 및 현재 diff의 `git diff --check`를 직접 확인했다. Codex는 main 파일을 직접 수정하지 않았다.

최종 commit 대상은 **docs/history.md, docs/작업목록.md, docs/구현계획.md**뿐이다. untracked .claude·diff/사본·바이너리는 포함하지 않는다. 예시 Conventional Commit: `docs(test): record safety harness validation closure`.

패치 반영 뒤 diff가 이 3파일 범위와 승인 내용 hash에 맞으면 Git MCP commit 후 hash를 연결하면 된다. 빌드/시험 재실행이나 새 사용자 승인은 필요 없다. 이 DOC OK는 push/NAS 게시의 승인이 아니다.

## 반영 확인

검증용 반영 **a0f991aecdbfc39a74ab2e89703616eb76c5a080** (`docs(test): record safety harness validation closure`)을 직접 확인했다. parent 이후 변경은 문서 3개/+16이며 제품/시험 파일은 없다. 3개 committed blob의 정규화 hash가 승인 JSON과 모두 일치하고 commit diff --check도 통과했다. 해당 문서의 working diff는 없다. 합의한 한정 하네스 종료 기록 정리까지 완료됐다. 남은 미구현·금지·R2 통합/출하 상태는 기록대로 유지한다.
