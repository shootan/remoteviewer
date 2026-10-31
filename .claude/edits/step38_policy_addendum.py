import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

ROOT = r'D:\remote\remote'
edit(ROOT + r'\docs\history.md', [
("""- 검증: 문서 변경만(빌드/테스트 없음). `docs/구현계획.md` 변경 없음(정책 항목 없음).
- 다음: 계획 B(S13 held-resume pending 회귀 수정, 0.2.103 후보) — 같은 task.""",
 """- 검증: 문서 변경만(빌드/테스트 없음). `docs/구현계획.md` 변경 없음(정책 항목 없음).
- 추가(같은 task, Codex 요청·검증용 전달, 후속 docs(policy) 커밋): 검증용 역할 항목에 "계획 변경이 필요하다고 판단한 부분은 Codex 의 수정·확정 전에는 작업용에게 위임하지 않는다. 변경 없이 OK 한 독립 범위는 바로 위임할 수 있다." 를 `AGENTS.md`·`CLAUDE.md` 에 그대로 명시 — 검증용의 임의 수정안 선위임 방지. 계획 A 의 조건 3건은 Codex 승인.
- 다음: 계획 B(S13 held-resume pending 회귀 수정, 0.2.103 후보) — 같은 task."""),
])
print('policy addendum history ok')
