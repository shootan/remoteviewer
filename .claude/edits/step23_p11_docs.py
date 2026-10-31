import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
from append_history import append_history

ROOT = r'D:\remote\remote'

edit(ROOT + r'\docs\구현계획.md', [
("""- [ ] **P11** UAC(캡처 백엔드 전환 WGC↔DXGI) 연관 뷰어 decode_queue 혼잡 2회 — 원인 후보/미확정 — **2026-09-07 15:15 실기 관측 (history #400, 보고서 9.3)**""",
 """- [~] **P11** UAC(캡처 백엔드 전환 WGC↔DXGI) 연관 뷰어 decode_queue 혼잡 2회 — **2026-09-07 15:15 실기 관측 (history #400, 보고서 9.3) → 호스트 측(복귀 flush 뒤 전환 전 AU 송출) 실 MFT 재현·epoch gate 로 수정, 뷰어 보유 재개 프레임 보정 (history #405), 설치본·실기 대기**"""),
("""  - 완료조건: [ ] 호스트에서 재개/전환 첫 프레임의 capture stamp 출처(WGC/DXGI 시계·flush 보유·캐시 재인코딩) 를 로그로 구분 [ ] 원인 확정 뒤 stamp 보정 또는 게이트 유예 결정 [ ] 회귀 테스트""",
 """  - 완료조건: [x] 출처 구분: 인코더 FIFO 보유(비동기 MFT, surface 경로)가 원인 — `capture-switch first-frame … auEpoch/curEpoch/holdUs`, `epoch-gate` 로그로 구분 (history #405) [x] 호스트 epoch gate: 새 epoch 첫 송출은 새 IDR, 전환 전 AU 폐기, P 선행 시 재요청, 상한 초과 시 encoder reset [x] 뷰어: hold>300ms 인 재개 프레임은 anchor 아님(pending 8프레임/500ms 상한, 진짜 backlog 유지) [x] 회귀: host_encode_epoch_test(실 MFT 재현), frame_gate +2, recovery S13/S14 (14/14) [ ] 실기: UAC 2회에서 `epoch-gate dropped-old`·뷰어 Congested 0 [ ] **미해결·별도**: secure desktop 중 kick 차단으로 실제 AU 가 0.5s 보유되는 형상(seq 24469)의 호스트 측 — secure kick 허용은 캐시 desktop provenance 설계 후 (Codex seq 1052)"""),
])

edit(ROOT + r'\docs\작업목록.md', [
("""1.2 [~] **P10 로그 업로드 401**""",
 """1.3 [~] **P11 UAC 복귀 혼잡** — 복귀 flush 뒤 인코더가 쥔 전환 전 AU 가 오래된 stamp 로 송출돼 뷰어가 가짜 backlog 판정 → 호스트 epoch gate(새 epoch 첫 송출=IDR) + 뷰어 보유 재개 프레임 보정 **코드 완료 (history #405)**, 0.2.101 설치본·UAC 실기 대기; secure 중 kick 차단 보유는 별도 설계
1.2 [~] **P10 로그 업로드 401**"""),
])

append_history(ROOT + r'\docs\history.md', ROOT + r'\.claude\edits\history_405.md')
print('P11 docs ok')
