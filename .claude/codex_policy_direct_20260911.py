from pathlib import Path
import json

root = Path('D:/remote/remote')
e = json.loads((root / '.claude/codex_policy_direct_20260911.json').read_text(encoding='utf-8'))
def section(text, start, end, replacement):
    assert text.count(start) == 1 and text.count(end) == 1
    a, b = text.index(start), text.index(end)
    assert a < b
    return text[:a] + replacement + text[b:]

ap = root / 'AGENTS.md'
cp = root / 'CLAUDE.md'
a = ap.read_text(encoding='utf-8')
c = cp.read_text(encoding='utf-8')
assert e['priority'].splitlines()[0] not in a
a = a.replace('## Mandatory Workflow (Always)', e['priority'] + '## Mandatory Workflow (Always)', 1)
a = section(a, '## Agent Role Separation', '## 배포 완료 기준', e['rolesA'])
old = '- **실패·권한 부족·미검증은 working 또는 명시적 blocked** 로 둔다. 배포 성공처럼 완료 처리하지 않는다.'
assert old in a
a = a.replace(old, '- **실패·권한 부족·미검증은 영향 범위별로 기록**한다. 남은 자동 작업은 working 또는 명시적\n  blocked로 두고, 사용자 UAC 실기만 남으면 위 완료 경계의 예외에 따라 최종 보고한다.')
c = section(c, '## 보고 규칙', '## 테스트 —', e['topC'])
c = section(c, '## 연속 수행 —', '## 범위와 질문', e['continuous'])
c = section(c, '## 3역할 절차에서 두 Claude', '## 로그를 읽는 법', e['rolesC'])
for path, text in ((ap, a), (cp, c)):
    with path.open('w', encoding='utf-8', newline='\n') as f:
        f.write(text)
for rel, key in [('docs/history.md', 'history'), ('docs/history/history_2026-W37.md', 'weekly')]:
    path = root / rel
    with path.open('a', encoding='utf-8', newline='\n') as f:
        f.write(e[key])
plan = root / 'docs/구현계획.md'
p = plan.read_text(encoding='utf-8')
old = '- [x] Agent Role Separation — Codex 계획·관리·감독 / Claude 실행(파일 변경·빌드·테스트·설치본·커밋). `AGENTS.md` 명문화 2026-09-07 (history #398)'
assert old in p
p = p.replace(old, '- [x] Agent Role Separation — 제품 3역할 / 문서·정책 2단계 / 통합 A2A·중복 조회 금지 / Codex 최종 안내 책임 적용 (2026-09-11, history #522)')
with plan.open('w', encoding='utf-8', newline='\n') as f:
    f.write(p)
print('Updated two policies and three required records; no product files changed.')
