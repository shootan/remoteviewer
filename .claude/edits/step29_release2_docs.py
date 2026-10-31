import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
from append_history import append_history

ROOT = r'D:\remote\remote'
append_history(ROOT + r'\docs\history.md', ROOT + r'\.claude\edits\history_407.md')

edit(ROOT + r'\docs\구현계획.md', [
("""- [~] **P11** UAC(캡처 백엔드 전환 WGC↔DXGI) 연관 뷰어 decode_queue 혼잡 2회 — **2026-09-07 15:15 실기 관측 (history #400, 보고서 9.3) → 호스트 측(복귀 flush 뒤 전환 전 AU 송출) 실 MFT 재현·epoch gate 로 수정, 뷰어 보유 재개 프레임 보정 (history #405), 설치본·실기 대기**""",
 """- [~] **P11** UAC(캡처 백엔드 전환 WGC↔DXGI) 연관 뷰어 decode_queue 혼잡 2회 — **2026-09-07 15:15 실기 관측 (history #400, 보고서 9.3) → 호스트 측 메커니즘(복귀 flush 뒤 전환 전 AU 송출)을 다른 입력 경로에서 재현·epoch gate + sender 펜스로 수정(현장 BGRA 경로 원인 미확정), 뷰어 보유 재개 프레임 보정 (history #405/#407), 0.2.102 설치본·실기 대기**"""),
("""[x] 호스트 epoch gate: 새 epoch 첫 송출은 새 IDR, 전환 전 AU 폐기, P 선행 시 재요청, 상한 초과 시 encoder reset""",
 """[x] 호스트 epoch gate: 새로 wire 에 시작되는 AU 기준으로 새 epoch 첫 송출은 새 IDR(emit gate + sender dequeue 펜스; 송신 중이던 AU 1개는 완료), 전환 전·미상·미래 epoch AU 폐기(fail-closed), P 선행 시 재요청, 상한 초과 시 encoder reset(3회/10s 상한) (history #407)"""),
])

edit(ROOT + r'\docs\작업목록.md', [
("""1.0 [ ] **0.2.101 실기**""",
 """1.0 [ ] **0.2.102 실기**"""),
("""**코드 완료 (history #405)**, 0.2.101 설치본·UAC 실기 대기; secure 중 kick 차단 보유는 별도 설계""",
 """**코드 완료 (history #405, #407: sender 펜스·gate fail-closed·뷰어 pending 수명)**, 0.2.102 설치본·UAC 실기 대기; secure 중 kick 차단 보유는 별도 설계(뷰어 보정은 오탐 IDR 비용 감소일 뿐)"""),
("""**코드 완료 (history #402)**, 0.2.101 설치본·UAC 실기 대기""",
 """**코드 완료 (history #402, #404, #407: dispatch 직전 live rect 재확인·조회 실패 시 거부)**, 0.2.102 설치본·UAC 실기 대기"""),
("""**코드 완료 (history #401, #403; 서버 세션 persist 는 범위 제외)**, 0.2.101 설치본·서버 배포·실기 대기""",
 """**코드 완료 (history #401, #403; 서버 세션 persist 는 범위 제외)**, 0.2.102 설치본·실기 대기(서버 배포 없음)"""),
])
print('release2 docs ok')
