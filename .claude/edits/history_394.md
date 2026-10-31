### 394) 2026-09-07 정적→활동 전환의 가짜 혼잡 제거 — 실제 콘텐츠 시계 분리 + keyframe-wait 폐기 프레임 트리거 제외 (복구 작업 3단계)
- 목표: 교차 확인에서 코드 경로 확인/현장 조건부로 분류된 #390 항목 4. synthetic(kick/refresh) 프레임이 수신 간격 시계를 갱신해 정적 구간 뒤 첫 실제 프레임 묶음이 "촘촘한 연속"으로 읽히고(recvGap<250ms → idle 재앵커 없음) 그 capture 간격(346~572ms)이 decode 적체로 계산되어 Congested+IDR 이 나던 경로를 막는다.
- 변경 파일:
  - `viewer_frame_gate_state.hpp`/`viewer_frame_gate.{hpp,cpp}`: `note_packet(nowUs, synthetic)` — heartbeat 시계(`lastPacketRecvUs`, 모든 완성 프레임)와 실제 콘텐츠 시계(`lastRealPacketRecvUs`) 분리. 실제 프레임의 recvGap 은 직전 **실제** 프레임 기준(idle 재앵커·dense 판정에 사용), synthetic 프레임은 heartbeat 기준(어차피 혼잡 트리거 제외). 실제 프레임끼리 촘촘하고 앵커가 안 움직이는 진짜 적체는 판정식이 그대로라 약화되지 않음.
  - 같은 파일: keyframe-wait 중 폐기될 non-key 프레임은 혼잡 트리거(lagTriggerStreak)에서 제외 — 대기 중엔 present 가 없어 lag 추정이 대기 시간만 재고, 그 streak 이 이미 IDR 을 요청 중인 대기 위에 Congested 진입(reset+reason 1)을 얹던 기존 동작(통합테스트 S8 로그에서 매 재개마다 관측)을 제거.
  - `viewer_video_receiver_frame.cpp`: note_packet 에 wire synthetic 플래그 전달.
  - 테스트: `viewer_frame_gate_test` 에 `test_synthetic_gap_then_real_burst_no_false_congestion`(프로브 시나리오 기대값 반전: 100ms 합성 2초 뒤 2ms 간격 실제 3장 → Normal·요청 0·reset 0; 같은 합성 구간 뒤 앵커 고정 실제 dense 25장 → Congested 진입 유지) / `test_keyframe_wait_drops_do_not_enter_congested`(대기 중 P 60장 폐기 → Normal, IDR 뒤 진짜 적체는 Congested). `viewer_udp_recovery_test` 에 S10 synthetic 150ms×12 뒤 실제 3장 burst(앵커 고정) → 혼잡 전이 0·요청 0.
- 검증: `remote60_viewer_frame_gate_test` PASS(신규 2건 포함, 총 16), `remote60_viewer_udp_recovery_test` **10/10 PASS**(S10 포함), GNLinkViewer 빌드 OK. 실기 미실행(RDP).
- 다음 액션: 4단계 recv liveness heartbeat + UI watchdog + dead-session 검출/종료.
