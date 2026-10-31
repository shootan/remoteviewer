import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
from append_history import append_history

ROOT = r'D:\remote\remote'
PLAN = ROOT + r'\docs\구현계획.md'

edit(PLAN, [
("""- [ ] **P2** NACK 재전송 — **[2026-09-07 Windows 실제 수신 경로 연결 + assembler in-order hold(시간·개수·바이트 상한) + 실제 경로 통합테스트 12/12 PASS (history #392~#396), 0.2.100 설치본; 실기 10‰ 측정·현장 효과만 미완(RDP)]**""",
 """- [ ] **P2** NACK 재전송 — **[2026-09-07 Windows 실제 수신 경로 연결 + assembler in-order hold(시간·개수·바이트 상한) + 실제 경로 통합테스트 12/12 PASS (history #392~#396), 0.2.100 설치본; 실기(09-07 14:15~14:33, 릴레이 경유): 호스트 도달 reason=2 요청 0건이나 뷰어 로그 미확보로 NACK 효과 미입증 (history #399)]**"""),
("""- [x] **P5** 키프레임 폭주 감쇠 — **[2026-09-07 완성 IDR 앞 seq gap 은 추가 요청 없이 재동기 + Congested/keywait/무수신 시간 기준 재요청(500ms→2s backoff, NACK 진행 중 유예 상한 300ms) — gate 테스트·실제 경로 통합테스트 PASS (history #393, #396), 0.2.100; 실기 미측정]**""",
 """- [x] **P5** 키프레임 폭주 감쇠 — **[2026-09-07 완성 IDR 앞 seq gap 은 추가 요청 없이 재동기 + Congested/keywait/무수신 시간 기준 재요청(500ms→2s backoff, NACK 진행 중 유예 상한 300ms) — gate 테스트·실제 경로 통합테스트 PASS (history #393, #396), 0.2.100; 실기(09-07): 25분 세션 폭주 0, 재접속 시 reason=7 1~2회 (history #399)]**"""),
("""- [ ] **P8** 텍스트/인터랙티브 모드 (GMux 등 깨짐·느림) — **2026-09-02 실기 (history #345), 체감 큼**""",
 """- [ ] **P9** UAC(secure desktop) 클릭 좌표 스케일 오차 — **2026-09-07 실기 (history #399, `docs/field_test_2026-09-07_0.2.100.md` 4절)**
  - 증상: UAC 프롬프트 클릭이 우하단으로 위치 비례 어긋남(x×1.165, y×1.141). 일반 클릭은 정상.
  - 원인(코드·로그 일치): `host_startup_graphics.cpp:218/234` 가 프로세스 시작 시 1회 읽은 주 모니터 rect(RDP 중 시작 → 2236x1232)를 `SetTargetRect` 로 넘기고 갱신 안 함; 에이전트의 live `virt=1920x1080` 은 정확.
  - 완료조건: [ ] 캡처 (재)시작·기하 확정·WM_DISPLAYCHANGE 시 target rect 재갱신 + 로그 [ ] secure_input.log 에서 `target==virt`, `mapped==in` 실기 확인
- [ ] **P10** 로그 업로드 401 — 재등록/재로그인·서버 재시작 뒤 업로드 영구 실패 — **2026-09-07 실기 (history #399, 보고서 5절)**
  - 증상: 13:48:39 부터 host 업로드 401 연속(배치 폐기) → 실기 구간 NAS 로그 공백. 클라 Bearer 세션은 서버 재시작(in-memory)으로 소멸.
  - 완료조건: [ ] 업로더 토큰 갱신(재등록 뒤) [ ] 401 시 재인증/x-host-token 폴백 [ ] 서버 세션 persist 또는 클라 자동 재로그인 [ ] 실패 배치 재시도
- [ ] **P8** 텍스트/인터랙티브 모드 (GMux 등 깨짐·느림) — **2026-09-02 실기 (history #345), 체감 큼**"""),
])
append_history(ROOT + r'\docs\history.md', ROOT + r'\.claude\edits\history_399.md')
