import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
from append_history import append_history

ROOT = r'D:\remote\remote'
append_history(ROOT + r'\docs\history.md', ROOT + r'\.claude\edits\history_408.md')

edit(ROOT + r'\docs\구현계획.md', [
("""[x] 호스트 epoch gate: 새로 wire 에 시작되는 AU 기준으로 새 epoch 첫 송출은 새 IDR(emit gate + sender dequeue 펜스; 송신 중이던 AU 1개는 완료), 전환 전·미상·미래 epoch AU 폐기(fail-closed), P 선행 시 재요청, 상한 초과 시 encoder reset(3회/10s 상한) (history #407)""",
 """[x] 호스트 epoch gate: 새 epoch 첫 송출은 새 IDR — emit gate + sender 의 첫 datagram 직전 허용 판정(flush 전에 허용된 AU 1개만 완료 예외, 인코딩 시 metadata 유지), 전환 전·미상·미래 epoch AU 폐기(fail-closed, legacy 통과는 fence 비활성 시만), P 선행 시 재요청, 상한 초과 시 encoder reset(3회/10s 상한); 실 sender 스레드 테스트 [A]/[B]/[B']/[C]/[D] + 실 sender 경유 뷰어 연결 S15 (history #407/#408)"""),
("""[x] 캡처 (재)시작마다 + 1초 tick 마다 실제로 연 HMONITOR 의 live 물리 rect+원점으로 target rect 재도출·변경 시 재푸시+로그 `secure-input target rect=` (`host_input_target_rect.hpp`, `sync_input_target_rect`; 창모드 0x0; 같은 해상도에서 원점만 바뀌는 경우도 periodic 경로가 잡음, history #404)""",
 """[x] 캡처 (재)시작마다 + 1초 tick 마다 실제로 연 HMONITOR 의 live 물리 rect+원점으로 target rect 재도출·변경 시 재푸시+로그 `secure-input target rect=` (history #404) [x] secure 전달 직전 단일 dispatch 경로(secure 분기·재probe·default fallback·텍스트): versioned 캡처 snapshot → live 조회 → 검증 rect 를 메시지에 stamp; 조회 실패/빈 rect/미상/창모드/조회 중 target 교체 시 미전송 (history #407/#408)"""),
])
print('release3 docs ok')
