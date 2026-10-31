import sys
sys.path.insert(0, r'D:\remote\remote\.claude\edits')
from edit_util import edit
from append_history import append_history

PLAN = r'D:\remote\remote\docs\구현계획.md'
edit(PLAN, [
("""- [ ] **P2** NACK 재전송 — **[진단 완료·Windows 경로 미연결·구현 전 교차 확인 대기]** **2026-08-07 우선순위 하향.**""",
 """- [ ] **P2** NACK 재전송 — **[2026-09-07 Windows 실제 수신 경로 연결 + assembler in-order hold + 실제 경로 통합테스트 6/6 PASS (history #392); 실기 10‰ 측정만 미완(RDP)]** **2026-08-07 우선순위 하향.**"""),
("""    - [ ] 호스트 링버퍼 (8프레임 또는 2MB)
    - [ ] 클라이언트 손실 검출 (8~15ms 재정렬 유예, 프레임당 NACK 2회/32청크 상한)
    - [ ] `kUdpFeatureVideoNack` 협상
    - [ ] `kMaxConcurrentVideoAssemblies` 3 → 5~6 (60fps에서 3개는 약 50ms로 재전송 왕복에 부족)
""",
 """    - [x] 호스트 링버퍼 (8프레임 또는 2MB) — 0.2.95 최근 AU 24개 캐시 + byte 예산(0.2.96)
    - [x] 클라이언트 손실 검출 (8~15ms 재정렬 유예, 프레임당 NACK 2회/32청크 상한) — 구현값: hole 25ms / tail 120ms 유예, AU당 3라운드×48청크, in-order hold 120ms; **Windows GNLinkViewer 경로 연결 2026-09-07**
    - [x] `kUdpFeatureVideoNack` 협상 — Windows 뷰어도 요청·ack 저장(2026-09-07)
    - [x] `kMaxConcurrentVideoAssemblies` 3 → 5~6 (60fps에서 3개는 약 50ms로 재전송 왕복에 부족) — hold 모드 8(legacy 3 유지)
"""),
])
append_history(r'D:\remote\remote\docs\history.md', r'D:\remote\remote\.claude\edits\history_392.md')
