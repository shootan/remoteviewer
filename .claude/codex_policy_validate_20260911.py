from pathlib import Path
import json
root = Path('D:/remote/remote')
files = json.loads((root/'.claude/codex_policy_git_add.json').read_text(encoding='utf-8'))['files']
for rel in files:
    text = (root/rel).read_text(encoding='utf-8')
    assert '\ufffd' not in text, rel
a = (root/'AGENTS.md').read_text(encoding='utf-8')
c = (root/'CLAUDE.md').read_text(encoding='utf-8')
assert a.index('## 최우선 운영 수칙') < a.index('## Mandatory Workflow')
assert c.index('## 최우선 실행') < c.index('## 테스트')
assert '문서 전용 작업의 편집·커밋은 위 예외로 허용' in a
assert '한 task에 미결 상태 조회는 하나만' in c
assert '최종 사용자 안내는 Codex가 책임진다' in c
assert '저장소 추적 파일을 수정·커밋하지 않으며' not in a
assert '사용자 UAC 실기만 남으면' in a
assert '## 3역할 절차에서 두 Claude' not in c
print('Policy precedence, role exception, reporting, encoding and scope checks passed.')
