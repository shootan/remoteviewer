# Windows 뷰어 복구 경로 — 구현 전 독립 교차 확인 (remote#0gk0hr8u)

작성: 2026-09-07. 기준 HEAD `4206161`(제품 소스 기준 `bae7d99`/0.2.99), 작업 트리 clean(.claude/ 만 untracked).
대상 문서: `docs/windows_viewer_recovery_work_order.md`, `docs/stream_freeze_diagnosis_2026-09-06.md`.
방법: 실제 CMake 입력·소스를 직접 읽고, 기존 프로브 2종을 재실행(둘 다 exit 0 = 재현), 저장 NAS 로그 사본(`.claude/freeze-diagnosis-20260905/`)을 재검토했다. 제품 소스는 아직 수정하지 않았다.

환경 주의: `qwinsta` 결과 `rdp-tcp#0 shotan Active` — 지금 RDP 접속 중. CLAUDE.md 규칙에 따라 **캡처(DXGI/GDI)에 의존하는 실기·e2e(GNLinkStream+GNLinkViewer)는 판정에 쓰지 않는다.** 순수 로직 단위테스트와 가짜 호스트 소켓 통합테스트(캡처 무관)만 지금 실행·판정한다.

## 분류 요약

| # | 항목 | 분류 | 한 줄 근거 |
|---|---|---|---|
| 1 | Windows NACK 미연결 | **확인** | `viewer_startup.cpp:371-378` requestNack 미설정·ack features 미수신, `viewer_video_receiver.cpp` run_udp 에 NACK 0건, CMake `remote60_native_video_client_poc`(=GNLinkViewer) 소스에 `native_video_client_session.cpp` 없음. 호스트는 클라 Hello 비트가 있을 때만 재전송(`host_startup_control.cpp:297-299`, `host_startup_connect.cpp:321-322`). |
| 2 | Congested 재요청 누락 | **확인** (11:20 사건을 이 결함으로 설명할 근거 없음 — 원인 미확정) | `viewer_frame_gate.cpp:280-293` early return 이 337-350 재요청과 302-334 timeout 을 우회. run_udp timeout 경로엔 영상 타이머 없음. 손실된 복구 IDR 도 `handle_udp_discontinuity` 의 `alreadyWaiting` 조기 반환으로 재요청 0. 프로브 재실행: P 3600/3600 폐기, 재요청 0. |
| 3 | 완성 IDR 중복 요청 | **확인** | `viewer_video_receiver.cpp:173-177` 이 Completed+key 확인 전에 reset+request(2). 호스트 seq 는 인코드 시점 부여(`host_stage_encode_send_h264_au.cpp:235`)이고 EnqueueKey 가 대기 큐를 비움(315-322) → 무손실 seq gap 은 프로토콜 정상 동작. 로그 90/90 일치. 프로브 재실행 REPRODUCED. |
| 4 | synthetic idle 가짜 혼잡 | **코드 경로 확인 / 현장 기여 조건부** | 코드: `note_packet` 이 synthetic 도 수신 시계 갱신(`viewer_video_receiver_frame.cpp:164` → `viewer_frame_gate.cpp:110-119`), present 앵커는 synthetic 미갱신(`viewer_present.cpp:232`), floor 재앵커 조건 recvGap>250ms(`gate.cpp:142`). 현장 성립 조건은 "실제 프레임 사이에 kick(150ms 후행, `host_kick.hpp:51`)이 끼고 다음 실제 프레임이 묶음으로 도착" — 저장 로그 7건 모두 streamLag 0~34ms·presentBacklog=0·직전 1초 syntheticFrames 1~3·진입 recvGap 2.5~24ms 로 이 서명과 일치. 단 사건별 kick 시각 trace 는 없다. |
| 5 | 현장 근거 한계(11:20) | **확인(미확정 유지)** | 11:20:42.749 마지막 present 이후 **11:20:59 까지 recv 스레드가 찍는 1초 `udp-assembly`/`recvFrames=` 라인이 0건**. 호스트는 같은 구간 60fps 송신 지속(seq 3286 IDR 182KB 포함). 마지막 congestionState=normal. recv 스레드 정지(디코드/제어 tick 포함)와 UDP 전달 중단을 저장 로그로 구분 불가. |

## 1. 실행 경로 — 확인

- `apps/native_poc/CMakeLists.txt:150-189`: `remote60_native_video_client_poc`(OUTPUT_NAME GNLinkViewer) 소스 = viewer_*.cpp + shared_core + tcp_control + udp_control_channel + mf_h264_codec + directory_*. **`native_video_client_session.cpp` 없음.** 238행 `remote60_native_video_client_shared_core_test` 와 385행 `remote60_udp_control_e2e_test` 만 session.cpp 를 링크.
- `viewer_startup.cpp:371-378`: `UdpHelloOptions hello; hello.authToken=…; budget/slice/retry` 만 설정, `requestNack` 기본 false(`native_video_client_tcp_control.hpp:73`), `udp_hello_handshake(sock, hello, nullptr, &helloError)` 5번째 인자(outAckFeatures) 미사용 → 협상 결과가 어디에도 저장되지 않음.
- `viewer_video_receiver.cpp:49-287` run_udp: `OldestIncomplete`/`VideoNack`/round/grace 0건. recv 실패(timeout)는 84-87행 `udpControl.Tick()` 만.
- NACK 구현은 `native_video_client_session.cpp:522-580`(maybe_send_nack 람다)과 823-830(hello.requestNack=true, hostSupportsNack_) — `ClientSessionController` 전용.
- **추가 발견 A**: `native_video_client_shared_core_test.cpp` 에 `nack|OldestIncomplete|highWater` 참조 **0건**. 즉 NACK 은 Android 경로에도 자동 테스트가 없다(세션 컨트롤러 e2e 도 NACK 미검증).
- **추가 발견 B**: LAN 직결(TCP 제어) 경로의 미디어 소켓은 handshake 후 `SO_RCVTIMEO=0`(blocking, `viewer_startup.cpp:380-382`)이고, 200ms timeout 은 UDP 터널일 때만 설정(409-411). 정적 화면(무수신)에서 timeout 기반 NACK/복구 타이머를 돌리려면 UDP 전 경로에 짧은 recv timeout 이 필요하다.
- **추가 발견 C (조립 순서 충돌)**: `native_video_client_shared_core.cpp:861-865` — 새 AU 가 완성되면 그보다 오래된 미완성 AU 를 즉시 폐기. 60fps 에서 다음 P 는 ~16ms 뒤 완성되므로 NACK gap grace(25ms)+round 가 끝나기 전에 손실 AU 가 버려진다 → in-order hold(유예 동안 최신 완성 AU 보류) 없이는 NACK 이 정적 화면(희소 프레임)에서만 유효하고 영상 콘텐츠의 P 손실 복구에는 사실상 무효. 작업 지시 2-1 의 "assembler 정책과 복구 유예 충돌" 지적이 맞다.
- 구버전 peer: 호스트 HelloAck 는 항상 NACK 비트를 광고하지만 `nackEnabled` 는 클라 Hello 비트로만 켠다. 클라가 ack 비트를 확인해 hostSupportsNack=false 이면 NACK/hold 를 끄면 되므로 하위 호환은 협상만으로 성립.

## 2. 복구 재요청 — 확인 (프레임 도착 의존 + Congested 우회)

- `viewer_frame_gate.cpp:280-293`: `Congested && !keyFrame` → `DropCongested` 즉시 반환. 337-350 의 reason 3 재요청(30 드롭마다)과 302-334 의 recover_timeout(Recovering 전용) 모두 도달 불가.
- `viewer_video_receiver.cpp:156-172` `handle_udp_discontinuity`: `alreadyWaiting=gate.waitForKeyFrame` 이면 request 없이 반환 → **복구 IDR 자체가 손실돼도 새 요청 0**.
- reason 3 재요청은 "30개 드롭" 즉 프레임 개수 기준. 소스가 멈추면(무수신) 재요청도 멈춤. run_udp timeout 경로에는 영상 타이머 없음(84-87행).
- 진입 시 1회 요청(reason 1/4/5) 뒤 풀리는 유일한 경로 = 호스트가 자율적으로 보내는 IDR/refresh 가 무손실로 완성되는 것.
- 재현(재실행): `viewer_gate_probe.exe` → `CONGESTED: elapsedUs=60001200 Pframes=3600 dropped=3600 retryRequests=0 state=2`, exit 0.
- 조건: 11:20 장기 정지의 마지막 관측 상태는 normal 이고 이후 관측 공백이 있어, 이 결함으로 그 사건을 설명할 근거는 없다(원인 미확정). 이 결함이 설명하는 것은 "Congested 진입 후 첫 IDR 이 손실/미도착이면 다음 자율 IDR 까지 정지" 유형.
- 구현 방향(확인됨): FrameGate 에 프레임 도착과 독립된 `tick(nowUs)` 를 두고 `waitForKeyFrame || Congested` 동안 시간 기준(초기 500ms, 지수 backoff 상한 2s)으로 재요청. 기존 `KeyframeRequestState` 리미터(120ms/토큰 3개·300ms 리필)가 상한을 추가로 보장. run_udp 는 datagram 처리 후와 recv timeout 양쪽에서 tick 호출. P 프레임마다 요청하지 않는다.

## 3. 완성 IDR 중복 요청 — 확인

- `viewer_video_receiver.cpp:173-177`: `droppedPreviousIncomplete` 이면 disposition 확인 전에 `handle_udp_discontinuity()` → `!alreadyWaiting` 이면 `dec.decoder.reset()` + `request_keyframe(2)`. 그 다음 208행에서 같은 프레임(완성 IDR)을 디코드.
- 호스트: `host_stage_encode_send_h264_au.cpp:235` `hdr.seq = ++encoder.encodedSeq`(인코드 출력 시점) → 315-322 EnqueueKey 가 대기 큐를 clear(이미 seq 소비) / HoldForKey 가 delta 폐기 → **네트워크 무손실에서도 IDR 앞 seq gap 발생이 정상**.
- 루프: 요청(2) → 호스트 IDR(큐 clear→gap) → droppedPrev=1 → 요청(2) … 리미터(120ms·3토큰)로 속도만 제한. 09-05 15:44~15:53 로그 90/90 이 이 패턴(각 295KB).
- 재현(재실행): `assembly_gap_probe.exe` → `KEY_GAP: completed=1 key=1 droppedPreviousIncomplete=1 networkLoss=0 … REPRODUCED`, exit 0.
- 구분: 정상 IDR 수락 → 추가 요청 불필요(IDR 이 곧 복구점, DPB 리셋). IDR 디코드 실패 → `note_decode_failure`(reason 4) 경로가 이미 처리. decoder.reset 도 완성 IDR 앞에서는 불필요(MFT 대기 출력만 버림).
- 동일 결함이 `native_video_client_session.cpp:625-630`(Android 경로)에도 있음 — Windows 범위 밖이므로 이번엔 기록만(수정 시 별도 커밋 제안).

## 4. 정적→활동 전환 — 코드 경로 확인, 현장 기여 조건부

- 시계 의미: (a) `gate.lastPacketRecvUs` = synthetic 포함 모든 완성 프레임의 도착 시각(`note_packet`), (b) `frameBuf.lastPresentedCaptureUs` = 실제(비합성) 프레임이 present 된 시점의 capture 스탬프(`viewer_present.cpp:232`, 0.2.97), (c) `gate.presentAnchorFloorUs` = recvGap>250ms 인 실제 프레임의 capture(`gate.cpp:142-145`). (a) 만 synthetic 을 포함해 기준이 어긋난다.
- 재현(재실행): 2초간 100ms 합성 뒤 2ms 간격 실제 3장 → `decodeQueueLagEstUs=2006000`, reason 1 요청 1회, Congested. 프로브의 100ms 합성 cadence 는 호스트 기본 static refresh(1000ms, `host_startup_capture.cpp:350`)보다 촘촘하지만, **trailing kick 이 실제 캡처 150ms 뒤에 synthetic 을 낸다**(`host_kick.hpp:51`, 입력마다 1회) → 희소 실제 프레임(타이핑/마우스) 사이마다 250ms 이내 synthetic 이 낄 수 있어 현장 성립 가능.
- 저장 로그 7건 decode_queue 진입(15:46:05~15:52:40): streamLagUs 0~34,502(도착은 정시), decodeQueueLagUs 345,865~572,320, 진입 `in.recvGapUs` 2,503~23,870(묶음 도착), 직전 1초 stats `syntheticFrames` 1~3, 인접 present 텔레메트리 `presentBacklog=0`, user-feedback `c2eUs` 377,688~485,555(호스트 캡처→인코드 지연 = 묶음 방출 원인과 일치). 즉 실제 큐 적체가 아닌 "희소 실제 프레임 간 capture 간격"이 lag 로 계산된 가짜 혼잡 서명.
- 조건부인 이유: 프레임별 synthetic trace 가 없어 7건 각각에 "250ms 이내 kick" 이 있었음을 개별 증명할 수 없다(1초 단위 카운트만 있음).
- 구현 방향: `note_packet(nowUs, synthetic)` 으로 실제 콘텐츠 시계를 분리 — 실제 프레임의 recvGap 은 직전 **실제** 프레임 기준으로 계산(idle 재앵커·dense 판정에 사용), heartbeat 시계는 별도 유지. 실제 dense 적체(실제 프레임끼리 촘촘·streamLag 증가)는 판정식이 그대로라 약화되지 않는다. 프로브 시나리오를 기대값 반전(요청 0·Normal 유지)으로 회귀 승격.

## 5. 현장 근거 한계 — 확인(미확정 유지)

- viewer: 11:20:42.749 `present seq=3261` 이후 11:20:43.090 `input-p0`(제어 스레드 생존) → 11:20:49.566 `slow action kind=1 tookUs=6475605 … closed=1 reason=peer-lost`. **11:20:43~11:20:59 구간 recv 스레드가 datagram 처리 뒤 1초마다 찍는 `udp-assembly`/`recvFrames=` 라인 0건.**
- host: 같은 구간 `wire seq=3286 key=1 bytes=182885`(43.486) 포함 60fps 송신 지속.
- 구조적 사실: UDP 터널의 `Tick()`/`OnPacket()` 은 **recv 스레드**가 구동(`viewer_video_receiver.cpp:85,90,97-98`). recv 스레드가 디코드/조립/publish 어디서든 멈추면 제어 재전송·수신도 같이 멈춰 6.5초 뒤 peer-lost 가 된다. 반대로 UDP 전달이 끊겨도 같은 결과. 저장 로그로는 둘을 구분할 수 없다 → 어느 쪽도 단정하지 않는다.
- 구현 방향: recv 루프 진행 heartbeat(loop 반복 수, 마지막 datagram/조립/디코드 반환/publish 시각, 현재 stage+진입 시각)를 atomics 로 두고 UI 타이머(50ms, `viewer_window_proc.cpp:604`)에서 1초마다 점검·기록. 제어 채널 종료(peer-lost)+영상 무진행 N초 지속 시 상태 표시 후 제한된 조치(세션 종료로 셸 재접속 유도, env 로 off 가능). "keyframe 타이머로 해결"이라고 보고하지 않는다.

## 호스트 UAC/readback

- 이번 범위에서 코드 변경 없음. 0.2.99 계측(workerCtxWait/D3dCall, d3d-mt)과 100/0·8/0·8/2000 A/B 는 RDP 해제 후 실기 필요 → **미검증으로 남김**. 보호 OFF 복원·디바이스 분리 착수 안 함.

## 구현 계획(확인 항목만, 커밋 단위)

1. **NACK/조립**: 공용 `VideoNackScheduler`(session.cpp 람다를 클래스로 추출, 두 경로가 공유) + `UdpH264FrameAssembler` in-order hold(옵션, Push 에 nowUs, `PopDelivery`; 동시 조립 3→8 은 hold 켤 때만) + Windows hello `requestNack=true`·ack features 저장 + run_udp 에서 datagram/timeout 양쪽 NACK 구동 + UDP 전 경로 recv timeout 25ms. 단위테스트(scheduler/hold) + **실제 `VideoReceiver::run_udp` 를 링크한 가짜 호스트 소켓 통합테스트**(P 청크 손실, 복구 IDR 청크 손실, 큰 IDR tail, 재정렬, source 정지, Congested+첫 IDR 손실, 제어 응답(키프레임 요청) 소실, 구버전 peer).
2. **복구 상태 머신**: `FrameGate::tick` 시간 기준 재요청(backoff) + 완성 IDR 앞 gap 은 추가 요청/reset 없이 디코드. gate 테스트.
3. **정적 복귀**: 실제 콘텐츠 시계 분리. gate 테스트(프로브 반전).
4. **진단·회복**: recv liveness heartbeat + UI 타이머 watchdog + dead-session 검출/상태/제한 종료.
5. 문서(history/구현계획), 버전 0.2.100, 설치본 1회. 실기(RDP 해제 후)는 별도.
