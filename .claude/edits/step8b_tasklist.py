import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit

p = r'D:\remote\remote\docs\작업목록.md'
t = open(p, 'rb').read().decode('utf-8')
marker = '2. [ ] **INPUT** 입력 ACK 직렬화 제거'
assert marker in t, 'task list marker not found'
insert = ('1.1 [ ] **P9 UAC 클릭 좌표 스케일 오차** — 호스트가 시작 시 1회 읽은 RDP 가상 디스플레이 rect(2236x1232)를 secure input 에이전트에 넘기고 갱신 안 함 → 캡처 (재)시작·WM_DISPLAYCHANGE 시 재갱신 (history #399, field_test_2026-09-07)\n'
          '1.2 [ ] **P10 로그 업로드 401** — 재등록 뒤 업로더 토큰 미갱신·서버 재시작 시 클라 세션 소멸로 NAS 로그 공백 → 토큰 갱신/재인증/세션 persist/배치 재시도 (history #399)\n')
if '**P9 UAC 클릭' not in t:
    t = t.replace(marker, insert + marker, 1)
    open(p, 'wb').write(t.encode('utf-8'))
    print('task list updated')
else:
    print('task list already has P9')
