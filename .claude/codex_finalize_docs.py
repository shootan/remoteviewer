from pathlib import Path
import json

plan = Path('docs/구현계획.md')
addition = '''

## 0.2.103 직접 재검토 후속 상태 (원장: `docs/stabilization_audit_2026-09-08.md`)

- [x] Codex 직접 코드·NAS 로그 조사 및 수정 필요 원장 문서화 (history #413)
- [~] 0.2.103 실기 — 정적 정지 없음·UAC 클릭/복귀 부분 확인, 고화질·재접속 정지 미해결
- [ ] A01 401 이후 업로더 busy loop 수정 및 기본 배치 크기 회귀
- [ ] A02/A06 인코더 빈 출력·overflow provenance 정렬/무효화·재동기
- [ ] A03/A04/A05 NACK tail/hold·무후속 소진 폴백·seq 기준 대상 선택
- [ ] A07/A08/A09 무수신 대응·재접속 초기 부하·전송 예산 검증
- [ ] A10/A11/A12 정적 출력 보유 지연·cross-clock 지표·무수신 계측 개선
- [ ] 양단 NIC 캡처로 최초 UDP 유입 중단 위치 확정
- [ ] 수정 후 동일 정적→UAC→영상→재접속 실기 통과
'''
if '## 0.2.103 직접 재검토 후속 상태' not in plan.read_text(encoding='utf-8'):
    with plan.open('a', encoding='utf-8', newline='') as f:
        f.write(addition.replace('\n', '\r\n'))
task = Path('docs/작업목록.md')
with task.open('r', encoding='utf-8', newline='') as f:
    text = f.read()
old = next(x for x in text.splitlines() if x.startswith(('1.0 [ ] **0.2.103 실기**', '1.0 [~] **0.2.103 실기**')))
new = '1.0 [~] **0.2.103 실기** — 09-08 정적 정지 없음·단일 모니터 UAC 클릭/복귀 부분 확인. 고화질 영상/재접속 정지 미해결. NAS 직접 분석·수정 필요 A01~A17: `docs/field_test_2026-09-08_0.2.103.md`, `docs/stabilization_audit_2026-09-08.md` (history #413). 제품 수정 미실행.'
assert text.count(old) == 1
with task.open('w', encoding='utf-8', newline='') as f:
    f.write(text.replace(old, new, 1))

files = ['docs/history.md', 'docs/구현계획.md', 'docs/작업목록.md', 'docs/stabilization_audit_2026-09-08.md', 'docs/field_test_2026-09-08_0.2.103.md']
Path('.claude/codex_docs_add.json').write_text(json.dumps({'repo_path':'D:/remote/remote','files': files}, ensure_ascii=False), encoding='utf-8')
Path('.claude/codex_docs_commit.json').write_text(json.dumps({'repo_path':'D:/remote/remote','message':'docs: record direct 0.2.103 freeze investigation and stabilization defects'}, ensure_ascii=False), encoding='utf-8')
print('Prepared five scoped documentation files and Git MCP arguments.')
