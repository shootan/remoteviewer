import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
from append_history import append_history

ROOT = r'D:\remote\remote'

edit(ROOT + r'\docs\구현계획.md', [
("""- [ ] **P9** UAC(secure desktop) 클릭 좌표 스케일 오차 — **2026-09-07 실기 (history #399, `docs/field_test_2026-09-07_0.2.100.md` 4절)**""",
 """- [~] **P9** UAC(secure desktop) 클릭 좌표 스케일 오차 — **2026-09-07 실기 (history #399, `docs/field_test_2026-09-07_0.2.100.md` 4절) → 코드 완료 (history #402), 설치본·실기 대기**"""),
("""  - 완료조건: [ ] 캡처 (재)시작·기하 확정·WM_DISPLAYCHANGE 시 캡처 대상의 현재 물리 rect+원점(선택 모니터·창 모드 포함)으로 target rect 재갱신 + 로그 [ ] 실기: 단일 모니터 전체 데스크톱은 `target==virt`·`mapped==in`, 다중/선택 모니터는 해당 대상 rect 기준 `mapped` 일치 (rect=0 fallback 은 단일 모니터 한정 조건부) [ ] secure_input `inject landed` 진단 예산(프로세스당 12건)을 에피소드/시간 기반으로 재충전 — 15:15 UAC 클릭은 예산 소진으로 기록 없음, 미해결 유지 (history #400)""",
 """  - 완료조건: [x] 캡처 (재)시작마다 실제로 연 모니터의 물리 rect+원점으로 target rect 재도출·변경 시 재푸시+로그 `secure-input target rect=` (`host_input_target_rect.hpp`, `sync_input_target_rect`; 창모드 0x0; WM_DISPLAYCHANGE 별도 훅 없이 재시작 경로가 RDP↔콘솔·백엔드·모니터 전환을 모두 지남) [x] 단위: RDP 2236x1232→콘솔 1920x1080 전환, 음수 원점, 창모드, 미상, 실측 클릭 회귀 (history #402) [ ] 실기: 단일 모니터 전체 데스크톱은 `target==virt`·`mapped==in` (UAC 2회 이상, 두 번째도 기록), 다중/선택 모니터는 장비 없어 미검증 [x] secure_input `inject landed` 진단 예산 12/에피소드(2s 공백 재충전)·60/분 (history #402)"""),
])

edit(ROOT + r'\docs\작업목록.md', [
("""1.1 [ ] **P9 UAC 클릭 좌표 스케일 오차** — 호스트가 시작 시 1회 읽은 RDP 가상 디스플레이 rect(2236x1232)를 secure input 에이전트에 넘기고 갱신 안 함 → 캡처 (재)시작·WM_DISPLAYCHANGE 시 재갱신 (history #399, field_test_2026-09-07)""",
 """1.1 [~] **P9 UAC 클릭 좌표 스케일 오차** — 호스트가 시작 시 1회 읽은 RDP 가상 디스플레이 rect(2236x1232)를 secure input 에이전트에 넘기고 갱신 안 함 → 캡처 (재)시작마다 실제 캡처 모니터 rect 재푸시 + 진단 예산 재충전 **코드 완료 (history #402)**, 0.2.101 설치본·UAC 실기 대기 (history #399, field_test_2026-09-07)"""),
])

append_history(ROOT + r'\docs\history.md', ROOT + r'\.claude\edits\history_402.md')
print('P9 docs ok')
