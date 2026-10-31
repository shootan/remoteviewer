# Windows 뷰어 정지·지연 복구 — 최종 보고 (t-iyzfi6ub, remote#0gk0hr8u)

작성: 2026-09-07. 브랜치 `refactor/viewer-split`. 기준 `4206161`(작업 지시) → 결과 커밋 6건 + 릴리스 커밋.

## 1. 교차 확인 결과 (구현 전, `.claude/windows_recovery_crosscheck.md`)

| # | 항목 | 분류 |
|---|---|---|
| 1 | Windows NACK 미연결 | 확인 |
| 2 | Congested 재요청 누락 | 확인 (11:20 사건을 이 결함으로 설명할 근거 없음 — 원인 미확정) |
| 3 | 완성 IDR 중복 요청 | 확인 |
| 4 | synthetic idle 가짜 혼잡 | 코드 경로 확인 / 현장 기여 조건부 |
| 5 | 11:20 장기 정지 | 미확정 유지 |

추가 발견: NACK 자동테스트 부재(Android 경로 포함), LAN 직결 미디어 소켓 recv timeout 0, assembler 즉시 폐기 vs NACK 유예 충돌, 11:20:43~59 recv 스레드 1초 라인 0건.

## 2. 커밋 (작업별)

| 커밋 | 내용 |
|---|---|
| `31393b8` | 1단계: Windows 수신 경로 NACK 협상·송신 연결, assembler in-order hold(120ms·8개), 공용 `VideoNackScheduler`, UDP 전 경로 recv timeout 25ms, 실제 수신 경로 통합테스트(`remote60_viewer_udp_recovery_test`) 신설 |
| `a3cc39a` | 2단계: `FrameGate::tick` 시간 기준 재요청(500ms→×2, 상한 2s), 완성 IDR 뒤 추가 요청 제거(`keyResync=`) |
| `ebc863f` | 3단계: 실제 콘텐츠 시계 분리(`note_packet(now, synthetic)`), keyframe-wait 폐기 프레임의 혼잡 트리거 제외 |
| `6d79450` | 4단계: recv liveness heartbeat, UI watchdog(`[liveness]` stalled/link-silent/session-dead), dead-session 검출·제한 종료 |
| `b89dccb` | 5단계: Codex 조건 6 + 불변식 2 — hold 바이트 상한·완성 IDR 즉시 해제, NACK hole/tail 라운드 예산 분리, IDR 유예 상한 300ms, shutdown bounded join+분리 종료, 공용 협상 함수, S11/S12, 문구 정정 |
| `0104bfd` | 릴리스: 0.2.100 버전, history #397/구현계획 P2·P5 상태, 설치본 `dist/GNLinkSetup-0.2.100.exe` |

## 3. 변경한 실제 실행 경로 (GNLinkViewer)

- `viewer_startup.cpp` `connect_media_socket`: `viewer_udp_hello_options()`(requestNack=env 기본 on) → `udp_hello_handshake(…, &ackFeatures)` → `viewer_apply_udp_hello_ack()`(SessionState.hostSupportsNack) → `viewer_arm_udp_recv_timeout(25ms)`; 로그 `udp hello ack features= nackNegotiated= nackHoldUs= recvTimeoutMs=`.
- `viewer_video_receiver.cpp` `run_udp`: datagram 처리 후·recv timeout 마다 `PopDelivery`(in-order hold), `VideoNackScheduler::Poll`→send, `fg.tick(now, nackScheduler.busy())`; `note_sequence_gap` 이 완성 IDR 이면 요청·reset 없이 재동기; liveness heartbeat 기록. stats 라인 `nackOn= nackSent= nackChunks= nackExhausted= pending= keyResync=`.
- `viewer_frame_gate.cpp`: `tick()` 복구 타이머(유예 상한 포함), `note_packet` 실제 콘텐츠 시계, keyWaitDrop 트리거 제외; stats `keyRetries= keyRetryDeferred= keyRetryEpisodes= keyWaitMaxUs=`.
- `native_video_client_shared_core.cpp`: assembler hold(`ConfigureInOrderHold(120ms, 8, 8MB)`, `PopDelivery`, 완성 IDR 즉시 해제). Android 경로는 hold 미사용, 동작 동일.
- `viewer_session_watchdog.cpp`(UI 타이머 1초) + `viewer_shutdown.cpp`(bounded join 3s → 미반환 시 로그 후 `TerminateProcess(44)`).

## 4. 실행한 테스트와 실패 주입 결과 (RDP Active 상태, 캡처 무관)

- `remote60_native_video_client_shared_core_test` PASS — NACK 스케줄러(grace/round/tail 예산/keyframe-wait/blocker 교체), hold(만료/keyframe-wait/seq 순서/cap/바이트 상한/wrap/중복·늦은 재전송/완성 IDR 즉시 해제/legacy 불변).
- `remote60_viewer_frame_gate_test` PASS — 기존 13 + 복구 타이머 2(Congested 60s P 3,600장 → 재요청 ~31회·프레임당 0; 무프레임 0.5/1.5/3.5s; IDR 뒤 정지; 대기별 backoff 리셋; 유예·유예 상한; off) + synthetic→real burst 반전 + keyframe-wait 폐기 트리거 제외.
- `remote60_viewer_liveness_test` PASS — L1~L10 판정 + J1 bounded join(<500ms).
- `remote60_viewer_udp_recovery_test` **12/12 PASS** (실제 `run_udp`→assembler→FrameGate→MF 디코더, 실제 UDP 소켓, 가짜 호스트 실제 H264Encoder):
  - S1 P 청크 손실 → NACK 1회·재전송 1·IDR 요청 0·Hello 에 NACK 비트 관측·heartbeat 배선
  - S2 구버전 host(ack 비트 없음) → NACK 0·IDR 폴백 복구
  - S3 복구 IDR 청크 손실 → keyframe-wait 중 NACK 복구, 요청 ≤2
  - S4 큰 IDR 50ms pacing → tail 조기 NACK 0
  - S5 청크 역순 → NACK 0·요청 0
  - S6 NACK 무응답 + 후속 AU 지속 → 3라운드 후 hold 만료 → IDR 폴백, 폭주 없음
  - S7 seq gap 뒤 완성 IDR ×3 → 요청 0
  - S8 P 전체 손실 + 요청 무시 + source 정지 2.6s → 타이머 재요청 2~4회 → 복귀
  - S9 앵커 고정 Congested + 첫 IDR 전체 손실 → 시계 재요청 → Recovering → Normal
  - S10 synthetic 150ms×12 뒤 실제 3장 burst → 혼잡 전이 0
  - S11 reset 없는 IDR 재동기의 디코더 provenance(timestamp/synthetic 단조·누락 없음)
  - S12 0 으로 채운 완성 IDR → 대기 없음·폭주 없음·다음 정상 IDR 표시(HW MFT 는 조용히 삼킴 → 감지 불가, 한계로 기록). 실제 decoder 오류 반환(reason 4)의 통합 검증이 아님 — 그 경로는 gate 단위테스트만
- 전체 Release 빌드 exit 0(GNLinkStream/GNLinkViewer/GNLinkClient/GNLinkHost/GNLinkSetup). 위 4종 외 unit 20종(host_kick/host_frame_gate/dxgi_output_selection/capture_readback/mf_h264_codec/udp_fec_interleave/udp_control_channel/video_playout_clock/host_abr/host_sender_queue_policy/capture_cadence_gate/host_backend_policy/encode_resolution_ladder/input_macro/viewer_selection_gate/viewer_picker_gesture/viewer_layout/connect_candidates/bind_port_candidates/punch_any) 모두 exit 0. 설치본 `dist/GNLinkSetup-0.2.100.exe`(3,409,920 bytes), 임베드 버전 0.2.100 확인(0.2.99 잔존 0). 미실행: gdi_capture_process_test(RDP 에서 물리적 FAIL), udp_control_e2e_test/viewer_split_e2e.sh(실제 호스트·캡처 필요).

## 5. 실기 수행 여부: 미수행

`qwinsta` 가 작업 내내 `rdp-tcp#0 Active`. CLAUDE.md 규칙에 따라 GNLinkStream+GNLinkViewer e2e·호스트 UAC A/B 는 실행하지 않았고 판정에 쓰지 않았다. 설치본 생성 ≠ 현장 검증.

## 6. 검증 완료 vs 미검증

- 완료(자동): 위 4절 전부.
- 미검증(실기): 실제 호스트·네트워크에서 NACK 효과(assembly-discontinuity↓·IDR 비율↓·present gap), UAC 뒤 readback 지연 A/B(100/0·8/0·8/2000), 11:20 유형 정지의 최초 방아쇠, dead-session 종료·bounded termination 의 실제 hang 동작(**제한된 종료이지 자동 재연결/정상 복구가 아님**), 셸 복귀 UX.

## 7. 남은 미확정 원인·위험

- 11:20 장기 정지: recv 스레드 정지 vs UDP 전달 중단 미확정 → 0.2.100 `[liveness]` 로그(stage/stageAge/loops/datagramAge)가 다음 재현에서 가른다.
- 호스트 UAC/readback: 0.2.98/99 계측 그대로, 원인 미확정, 보호 OFF·디바이스 분리 미착수.
- hold 는 손실 시 최대 120ms 추가 지연(무손실 0). 손상 완성 IDR 은 디코더가 실패를 보고하지 않으면 감지 불가 — 다음 정상 IDR 을 실제로 수신·디코드하면 회복하지만 그 도착 시간 상한은 미보장(keyint 는 프레임 개수 기준; fps/설정/GOP/손실·정적 화면·source 정지에 따라 달라짐).
- Android `ClientSessionController` 의 완성 IDR 중복 요청은 그대로(별도 항목).
- 실기 판정 기준: `nackNegotiated=1`; 손실 구간 `video-nack`/`nackSent>0`·`keyReq` 감소; `keyResync>0` 이 reason=2 없이; 정적→활동 `reason=decode_queue` 소멸; `keyRetries` 소수; 정지 시 `[liveness]` stage 구분; peer-lost 뒤 5s 내 `session-dead`·셸 복귀.
