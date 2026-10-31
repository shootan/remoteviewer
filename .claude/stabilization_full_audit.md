# 0.2.100~0.2.103 변경 코드 전수 재검토 — 검증용 Claude (remote#1528xbin), 2026-09-08

요청: Codex remote#zsag5trg task t-cb7l51pj "작업한 코드 전부 다시 읽고 문제점 파악". 범위 고정 4206161..edb1ec9 (33 commits, 71 files, +7195/-276).
성격: 분석만. 제품/추적 문서 수정·구현 위임 없음. 빌드·테스트·라이브 호스트 중지·설치·push·배포 없음. 과거 "구현 OK" 를 근거로 생략한 파일 없음.

## 0. 방법
- 검증용이 제품·빌드 파일의 diff 와 현재 원문을 직접 읽음(아래 표의 "직접"). 테스트 파일은 배선·거짓 PASS 관점으로 읽기 전용 에이전트 1개, NACK/liveness/watchdog/shutdown 과 uploader/host loop/codec/CMake 는 2차 시각으로 에이전트 2개에 맡긴 뒤, **에이전트가 낸 모든 지적은 검증용이 해당 줄을 다시 열어 확인한 것만 채택**(표의 "에이전트+재확인"). 재확인 못 한 항목은 "미확정" 으로 남김.
- 표적 검사: 코드 읽기 외에 Windows Application 이벤트 로그 조회(읽기 전용) 1회, 그리고 Codex 교차검토 뒤 `.claude/probe/`(미추적, 제품 소스를 절대 경로로 컴파일만) 프로브 3개 실행 — 제품 uploader 의 401 busy loop 실측, 제품 어셈블러+NACK 스케줄러 상태추적, codec FIFO deque 모델 + 실제 epoch gate. 추적 파일·build-local 무변경. 상세 8절.

## 1. 결함 목록 (심각도 순)

### 새 확인 결함 (이번 범위에서 도입/노출, 검증용 원문 확정)

**F1 HIGH — epoch FIFO 가 "빈 출력" 경로에서 한 칸씩 영구 밀림**
- 위치: `apps/native_poc/src/mf_h264_codec.cpp:1827-1834` (도입 1607404, edb1ec9 까지 유지)
- 트리거: MFT `ProcessOutput` 이 바이트 없는 샘플을 낸 경우(`produced && !haveBytes`). 0.2.97 ebe313b 가 "이때 timestamp 를 소비하지 않으면 뒤 AU 가 한 프레임씩 영구히 밀린다" 는 관측으로 그 pop 을 넣었으므로 발생 조건은 실재.
- 영향: 그 블록은 `pendingInputSampleTimesHns_`·`pendingInputSynthetic_` 만 pop 하고 `pendingInputEpoch_` 는 pop 하지 않음(정상 경로 1864-1870, overflow 1931-1934 는 셋 다 pop). 이후 모든 AU 가 이전 입력의 epoch 태그를 달고 나옴(N번 발생 → N칸). flush(UAC 전환·재시작·기하 변경·인코더 reset) 뒤 새 epoch 의 강제 IDR 이 old 태그 → `epoch_gate_judge` DropOldEpoch(재요청 없음, `host_stage_encode_send_h264_au.cpp:165-168`) → 뒤 P 가 DropAwaitingKey 로 forceKeyNext → 여분 IDR·프레임 드롭; 밀림이 커지면 kAwaitMaxDropped=12/700ms 로 ResetEncoder(FIFO clear 로 자가 치유, 3회/10s 상한). 현장의 "UAC 복귀 뒤 혼잡/지연" 을 호스트 쪽에서 재생산할 수 있는 경로.
- 증거: 위 세 블록 대조. 빈도 계측 불가 — 해당 카운터 `sampleTimeOutputTimestampFallbackCount_` 는 어디에도 노출되지 않음(stats 는 `pendingOverflow` 만, `host_stage_encode_send_h264.cpp:443-444`). `host_encode_epoch_test` 에 빈 출력 케이스 없음(grep empty/haveBytes 0건).
- 수정 방향: 1832 뒤 `if (!pendingInputEpoch_.empty()) pendingInputEpoch_.pop_front();` + 세 deque 를 `struct{ts,synthetic,epoch}` 하나의 FIFO 로 통합해 lockstep 을 구조로 강제 + fallback 카운터 stats 노출 + epoch test 에 "빈 출력 1회 뒤 flush → 새 IDR AcceptKey" 회귀. (Codex seq 1145 선보고.)

**U3 HIGH — 401 일시정지 뒤 큐 ≥ batchMaxBytes 면 uploader worker busy loop + diag 파일 폭증 (Codex seq 1142/1147 지적 → 검증용 실측 재현, 상세 8절 Q1)**
- 위치: `log_upload.cpp:251-253` (predicate `stopping || queuedBytes >= batchMaxBytes || wakeSeq` 가 authRejected/credentials 무관) · `:198` (`next_job_locked` 은 authRejected 면 false) · `:271` (continue) · `:266-270` (30회마다 diag).
- 트리거: P10 의 의도된 401 일시정지 + 큐가 192KB(기본 batchMaxBytes)에 도달(호스트 로그 수 초 분량). 미설치 코드(0.2.101+), 0.2.103 후보 포함.
- 영향(실측, 제품 기본값): 프로세스 CPU 코어 97%, `log_upload.diag` +772KB/s(3초 2.37MB), 7초 126만 사이클 → 코어 점유 + 디스크 고갈.
- 수정 방향: predicate 에 `credentials && !authRejected` 조건, next_job false 사이클은 flushInterval 대기, idle-alive diag 시간 스로틀.

**N1 HIGH(기능) — 뒤에 완료 AU 가 있는(라이브 스트림) 꼬리(tail) 손실은 NACK 로 복구되지 않음: tail grace = hold = 120ms, drain 이 NACK 보다 먼저**
- 위치: `udp_video_nack.hpp:34,101,106-107,125-126` (tailGraceUs 120000) · `viewer_context.hpp:38`/`viewer_startup.cpp:113` (holdUs 기본 120000) · `native_video_client_shared_core.cpp:695-697` (hold 만료 `nowUs < firstPacketUs + holdMaxUs_`) · `viewer_video_receiver.cpp:209-210, 330-332` (`drain_deliveries()` → `maybe_send_nack()` 순서)
- 트리거: 60fps 스트림에서 AU N 의 마지막 청크(또는 `missing[i] >= highWater` 인 꼬리 구간) 손실, FEC 미복구, **그리고 N 뒤에 완료된 AU(N+1 등)가 hold 에 있을 것**. 후속 완료 AU 가 없으면(정적/희소 화면) PopDelivery 가 `!anyComplete → false` 로 head 를 두므로 120ms 에 tail NACK 이 나가고 복구 가능 — Codex 지적으로 범위 한정.
- 영향: 꼬리는 age ≥ 120ms 에야 NACK 대상이 되는데 같은 순간 hold 도 만료. 매 루프 drain 이 먼저 돌아 PopDelivery 가 N 을 erase 하고 N+1 을 gap 으로 배달 → `OldestIncomplete` 에서 N 이 사라져 꼬리 NACK 은 영원히 안 나감 → discontinuity → 디코더 reset + IDR 요청. 순서를 바꿔도 재전송 RTT 동안 hold 가 이미 없음. 즉 라이브(연속 프레임) 스트림에서 NACK 는 highWater 아래 구멍만 고치고 꼬리 손실은 IDR 비용; 후속 AU 가 없는 희소 스트림에서만 tail NACK 이 동작.
- 증거: 위 상수·순서 대조(검증용 직접). 회귀 테스트는 `viewer_udp_recovery_test.cpp:808,862,958` idx==1 / `:893` idx==2 만 떨어뜨림 — 마지막 청크를 떨어뜨리는 시나리오 없음.
- 수정 방향: hold 를 tail grace + 1 round + RTT 여유보다 길게(예: tail 60ms, hold 200~250ms) 하거나 그 AU 에 NACK 이 나갈 때 hold 마감을 연장(`max(first+hold, lastNack+round+rtt)`), 또는 꼬리 판정을 "마지막 수신 청크 이후 간격" 기준으로. 회귀: 다중 청크 P 의 마지막 청크 드롭 → NACK 복구·IDR 0.

**U1 MEDIUM(기존 결함, 범위 이전 배치 — 이번 범위는 계약만 새로 씀) — uploader 는 제품에서 한 번도 stop 되지 않아 종료 시 joinable std::thread 파괴 → std::terminate**
- 위치: `log_upload.cpp:115,137-140` (함수 정적 `UploaderState` 안의 `std::thread worker`), `log_upload_stop` 522 의 제품 호출자 없음(grep: 테스트만). 두 셸은 정상 return(`host_app_main.cpp:1391,1476`, `client_shell_main.cpp:662`).
- 영향: CRT 정적 소멸에서 `~thread()` 가 joinable → terminate → fastfail 0xc0000409. 헤더가 약속한 "stop 시 마지막 배치 송신"(`log_upload.hpp:95`)은 제품에서 죽은 코드 → 마지막 ≤2s 분 로그 유실.
- 증거: 코드 확정(정적 joinable thread 배치는 4206161 이전 log_upload.cpp 와 동일 → 기존 결함). 이번 범위(f911845)는 헤더에 "only stop() joins / 마지막 배치 송신" 계약을 새로 쓰면서 제품 호출자를 넣지 않음. 런타임 근거는 **귀속하지 않음**: Application 이벤트 로그의 GNLinkHost 2026-09-03 0xc0000409 1건은 fastfail 서명일 뿐 종료 시점·원인 미확인이라 증거로 쓰지 않고, GNLinkClient/Viewer 는 14일간 0건.
- 수정 방향: 두 wWinMain 종료 직전 `log_upload_stop()` 호출 + 최종 pass 를 유계로(서버 불통 시 4MB 큐 × 6s 연결 타임아웃이면 분 단위 지연 → 마지막 1배치만 시도하거나 직전 결과가 Transient 면 생략).

**U2 LOW(잠재, API 동시호출) — stop 진행 중 configure 가 worker 를 되살려 join 영구 대기 (Codex 교차검토로 강등: 제품에 stop 호출자가 없어 현재 제품 경로에서는 발생 불가)**
- 위치: `log_upload.cpp:522-532` (stopping=true, worker move-out, lock 밖 join) vs `:415,455-467` (`s.running && !s.stopping` 거짓 → else 분기에서 stopping=false·running=true·새 worker 생성)
- 영향(잠재): 옛 worker 의 유일한 종료 조건(`if (s.stopping) return`)이 다시 거짓 → stop 의 join 영구 대기 + worker 2개 공유 상태; 이후 configure 가 joinable `s.worker` 에 대입하면 terminate. **정정**: 제품 코드에 `log_upload_stop` 호출이 0건(grep)이라 지금은 어떤 제품 경로에서도 일어나지 않음 — "종료(WM_DESTROY) vs configure 겹침" 이라는 1차 노트 표현은 잘못. U1 수정으로 stop 을 호출하게 될 때 함께 막아야 하는 API 잠재 결함으로 분류.
- 수정 방향: configure 에서 `s.stopping` 이면 거부(reason "stopping") 또는 stop 완료까지 `mu` 로 직렬화.

**N2 MEDIUM-LOW — `OldestIncomplete` 는 도착(deque)순, hold 는 seq 순 → AU 간 재정렬 시 NACK 이 엉뚱한 AU 를 쫓음**
- 위치: `native_video_client_shared_core.cpp:707` (`for (const Assembly& a : assemblies_)` 첫 미완성 반환) vs `:667-670` (PopDelivery min_element by seq)
- 트리거: N+1 의 청크가 N 의 첫 청크보다 먼저 도착(릴레이/Wi-Fi 재정렬), 둘 다 미완성.
- 영향: 스케줄러가 N+1 에 라운드를 소진하는 동안 실제 배달 차단자 N 은 NACK 없이 hold 만료 → IDR.
- 수정 방향: PopDelivery 와 같은 비교자로 최소 seq 미완성 AU 선택.

**N3 MEDIUM-LOW — NACK 라운드 소진 뒤 후속 AU 가 없으면 head 가 고정되고 아무것도 능동 복구하지 않음**
- 위치: `udp_video_nack.hpp:113-119` (소진 → false) · `native_video_client_shared_core.cpp:675-677` (`if (!anyComplete) return false;` 가 hold 만료 판정보다 앞) · `viewer_frame_gate.cpp:547-559` (tick 은 waitForKey/Congested 일 때만 재요청)
- 트리거: 정적 화면에서 마지막 AU 손실 + 호스트 미응답(캐시 24 AU 초과·바이트 예산).
- 영향: gap 이 "배달" 된 적이 없어 waitForKey 가 서지 않고, hold 만료도 후속 완료 AU 가 있어야만 적용 → 호스트의 다음 AU 가 완성될 때까지 이전 그림 유지. 후속 AU 는 보장되지 않음 — secure desktop 중에는 kick 이 차단되므로(호스트 정책) "kick 주기로 유계" 라고 볼 수 없음(Codex 정정). NACK 실패가 조용함.
- 수정 방향: head 에서 라운드 소진 또는 hold 만료 시 후속 없이도 포기하고 discontinuity 경로(waitForKey + reason 2) 진입.

**C2 LOW — FIFO overflow 는 태그를 비우지 않고 밀어서, 주석("gate 를 열 수 없다")과 반대**
- 위치: `mf_h264_codec.cpp:1859-1862` (주석) vs `:1930-1934` (overflow 시 가장 오래된 항목 pop → 뒤 출력이 더 새 입력의 epoch 를 받음)
- 트리거: 출력 없이 입력 64개 수용(starvation/stale reset 이 먼저 발동할 가능성 큼 — 미확정).
- 영향: pre-flush AU 가 현재 epoch 로 태그돼 key 면 gate 를 열 수 있음(P11 형상). 수정 방향(제안, Codex 보완): deque clear 만으로는 불충분 — clear 뒤 새 입력이 push 되면 MFT 에 남아 있던 옛 출력이 그 새 태그를 소비해 다시 어긋남. overflow 시 "provenance 무효" 래치를 세워 이후 출력을 epoch 0(fail closed)으로 내보내고, 인코더 flush/reset 으로 입력·출력을 함께 재동기한 뒤 래치를 해제해야 함. F1 과 원인 별개.

**E1 LOW — force-key in-flight 창 300ms 보다 인코더 보유가 길면 flush 당 IDR 1장 추가**
- `host_stage_encode_send_h264.cpp:269-274` + `_au.cpp:179` (DropAwaitingKey 마다 forceKeyNext). P11 현장 보유 800ms 형상에서는 두 번째 IDR 제출. 대역 비용만, 정확성 무관.

**E2 LOW/미확정 — kAwaitMaxDropped=12**
- `host_epoch_gate.hpp:53-54`. 비동기 MFT 보유 깊이가 12 이상이면 flush 마다 ResetEncoder(재초기화+IDR). `pendingDepth` 실측(호스트 stats `host_stage_encode_send_h264.cpp:443`)으로 판정 필요 — 로컬 로그에 값 없음.

**S1 LOW(잔여 경쟁, 영향 미확정) — secure dispatch 의 두 번째 snapshot 확인과 send 사이 창**
- 위치: `host_secure_target_rect.hpp:80-88` (두 번째 snapshot 은 lock 안, `p.send` 는 lock 밖) · snapshot 갱신은 `host_loop_helpers.cpp:241` restart_capture_session **성공 뒤**와 `host_stage_stats.cpp:111` 1Hz.
- **정정**: 1차 노트의 "sender 처럼 문서화된 예외" 는 오류 — 헤더(13-22행)는 "target changed → not sent" 만 말하고 sender hpp 같은 허용 시점/예외 문구가 없음. 실제 창은 µs 가 아니라 **target 이 바뀌는 시점(`host_loop_helpers.cpp:480-485` windowMode/selectedWindowId 갱신) 부터 restart 성공 뒤 version 증가(241)까지의 restart 구간 전체**.
- 두 번째 snapshot 검사(lock 안)와 `p.send`(lock 밖) 사이에 target 이 바뀔 수 있고, 이 창은 sender 의 문서화된 예외와 달리 **어디에도 계약으로 적혀 있지 않음**. 영향은 미확정 — 완화 요인(현 구현): (a) rect 는 매 이벤트 live `GetMonitorInfo`, (b) monitorHandle 은 항상 primary(기존 ledger) 라 restart 로 바뀌지 않음, (c) 창 모드 전환은 control 세션 `desktopMode`(`host_control_session.cpp:414-416`) 가 restart 전에 거부. 이 완화는 현 구현의 우연에 기대므로 잔여 경쟁으로 남김.
- 조치 방향(제안): 두 번째 검사와 write 를 같은 임계 구역에 두거나 write 뒤 version 을 재확인해 불일치 시 폐기 로그; restart 진입 시(`host_loop_helpers.cpp:480-485`) 먼저 version 을 올려 restart 중 dispatch 를 거부; 헤더에 잔여 창을 명시.

**W1~W5 LOW (watchdog/NACK 통계, 에이전트 지적 → 검증용 재확인)**
- W1 `viewer_session_watchdog.cpp:29-34`: `controlEverConnected` 를 첫 poll 관측으로만 세움 → control 스레드가 첫 poll 전에 죽으면 sessionDead 영구 불가(커버리지 공백). 수정: main 이 세운 controlReady 로 초기화.
- W2 `viewer_session_watchdog.cpp:88-96` + `viewer_window_proc.cpp:233-236`: dead session 이 WM_CLOSE → exit 0 → 셸은 "idle"(`client_shell_main.cpp:563-567` 은 exitCode≠0 만 오류 표시). join 타임아웃 경로는 44 로 표면화되는데 dead session 은 아님. 수정: 별도 코드(예 45) 또는 상태 표시 뒤 지연 close.
- W3 `viewer_recv_liveness.hpp:130-131`: TCP-control 직접 경로에서 스트림을 의도적으로 멈추면(picker) datagram 이 없어 link-silent 가 5s 마다 로그(로그만).
- W4 `viewer_video_receiver.cpp:64-65` kHoldMaxConcurrentAssemblies=8: env HOLD_US(최대 2s)와 무관하게 60fps 에서 ~117-133ms 에 head evict → env 노브가 오도. 수정: holdUs·fps 로 산출.
- W5 `udp_video_nack.hpp:107-118`: tail 단계 전환 시 `exhaustedCounted_=false`(같은 AU 2회 집계)·`lastUs_=0`(hole 라운드 직후 tail 라운드 즉시). 유계, 통계만.

### 기존 ledger / 범위 이전 (확인 결과)
- **모니터 선택이 주모니터로 되돌아감(ledger #402)** — 더 강함: `host_stage_selection.cpp:110-131` 은 선택 즉시 `CreateItemForPrimaryMonitor(nullptr,…)` 로 item 을 만들고, `RestartCaptureSessionImpl` 은 `host_capture_session.cpp:426` monitorInfo=primary(이번 diff 가 DXGI 전용 → 모든 데스크톱 백엔드로 확장), `:449` WGC refresh 도 primary, `:520` DXGI config.monitor=monitorInfo(primary). 즉 "첫 재시작 뒤" 가 아니라 **같은 선택 처리 안에서** 주모니터로 대체되며 `selectedMonitorId` 는 저장·로그만. P9 rect 는 실제 캡처(주모니터)와 일치하므로 클릭 매핑은 정합 — P9 결함 아님. 다중 모니터 실기 장비 없음.
- **WSAECONNRESET 비재시도**(`native_socket.hpp:183-190`, 에이전트 지적, 범위 이전, 미확정): 호스트 프로세스 재시작 중 ICMP unreachable 로 recv 가 WSAECONNRESET → run_udp 종료 → exit 0("idle"), watchdog 우회. `SIO_UDP_CONNRESET` 미설정(grep). 수정: connect 뒤 SIO_UDP_CONNRESET=FALSE 또는 UDP 에서 재시도 가능으로 분류.
- `log_upload.diag` 무제한 append(`log_upload.cpp:32-49`, 재시도 줄만 10s 스로틀) — 서버 장기 불통 시 1~2MB/일. 회전/스로틀 필요. 429 는 60s 창 안에서 3회 재시도돼 사실상 폐기(`server.js:247-257`) — 비정상 폭주에서만.
- `logs_test.js:91-92` traversal 검사가 LOG_DIR 두 단계 위를 봄(../../etc 는 한 단계 위로 정규화) → 부재 검사 공허. `restart_test.js:70` sessionToken 부재 절은 세션 미영속이라 항상 참. 둘 다 테스트 강도 문제.

## 2. 테스트 배선·거짓 PASS 검토 (에이전트 보고 → 검증용 원문 재확인)
- 공통: 모든 CHECK 류가 실패 카운터로 exit code 에 닿음, 조용한 skip 경로 없음(setup 실패는 ++gFailures 또는 비0 종료). 제품 코드 링크 확인(CMake): recovery 테스트 = 실제 `VideoReceiver::run_udp`→어셈블러(hold)→FrameGate→MF 디코더, 실 UDP 소켓, 협상 함수 동일; sender 테스트 = 실제 `SenderState::StartThread`·펜스·permission point(hook); log_upload 테스트 = 실 TCP 서버 + 제품 `http_post`; frame_gate/shared_core/liveness/target_rect/mapping = 제품 함수 직접.
- T1 `viewer_udp_recovery_test.cpp:1262-1281` S14: "gate 가 pre-flush AU 폐기" 는 printf 만(1266), `droppedOld>=1` 단정 없음 → 동기 인코더에서도 PASS. verdict 처리는 테스트 사본(ResetEncoder→forceKeyPending_). S15 도 IDR-next 를 뷰어 wire 에서 단정하지 않음.
- T2 `native_video_client_shared_core.cpp:189-217` 요청 리미터(120ms 최소 간격·토큰) 때문에 "요청 0 / storm ≤ +2" 단정은 gate 판단이 아니라 리미터 통과분. 1~1.5s 창에서 단일 오요청은 드러나므로 위험 낮음.
- T3 `host_sender_epoch_test.cpp:190-236` [B]: pacing 없이 큰 AU 라 flush 가 "송신 중" 에 걸렸는지 미증명. `inputEpochDropCount+1`·[B'] hook 이 계약을 덮음.
- T4 `host_encode_epoch_test.cpp:239-266` [2]: 재현 블록 조건부 → 동기 백엔드에선 `droppedOldEpoch==0` 으로 퇴화(이 PC HW MFT 에선 재현됨). 빈 출력(F1) 커버리지 없음.
- T5 `host_input_target_rect_test.cpp:35-52` [1] "재시작 뒤 재도출" 은 무상태 함수 두 번 호출 — 실제 재도출(`sync_input_target_rect`, Win32)은 미링크.
- T6 `log_upload_test.cpp:379-380` "no fourth attempt" 는 150ms sleep 이 가상의 4번째(160ms)보다 짧아 관측 불가; 실 근거는 discardedBatches==1·failedSends==3.
- T7 N1 관련: 마지막 청크 드롭 시나리오 부재. T8 rig.gate.* 비원자 읽기(pump 뒤라 낮음), present 폴링 2ms 는 false FAIL 방향.

## 3. 확인 OK (검증용 직접)
- sender enqueue 는 `_au.cpp:346-420` 단일 경로(UDP+H264), `inputEpoch` 는 `mediaEpoch` 와 같은 lock 아래 stamp(375); kick/bootstrap 도 인코더를 거쳐 동일 경로(`host_stage_encode_send_h264.cpp:281-282`); sender 내부 enqueue 없음. 펜스는 dequeue + 첫 datagram 직전(permission point) 두 단계, 예외 = 이미 허용된 AU 1개(문서화). `inputEpochRef` 는 startup 에서 연결(`host_startup_capture.cpp:332`).
- `inputEpoch{1}` 초기값·gate 초기 awaitingKey→첫 IDR 수락, `ResetTimelineAnchors` 의 모든 호출자가 flush/restart/reset. ResetEncoder 는 AuFlow::Break 로 배치 중단·재판정.
- rect 없는 broker 직접 호출 잔존 없음(4 경로 모두 secure_dispatch_*). snapshot 은 `inputTargetMu` 아래 version 증가, 조회 실패 시 0x0 로 거부. 진단 예산은 에피소드/분 상한.
- `fg.tick` 은 타임아웃(211)·datagram(333) 양쪽에서 호출; PopDelivery 는 evict/만료 뒤 다음 배달에 gap 을 실음; 완료 IDR 뒤 old 정리는 keyResync(요청 없음); seq wrap 은 signed diff; 호스트 seq 리셋은 TCP 재접속 경로뿐이라 generation 변경과 어셈블러 Ignored 규칙 충돌 없음.
- recv→UI 는 PostMessage/InvalidateRect 만 → bounded join 과 교착 없음; 소켓 close 가 join 앞; 타임아웃 시 TerminateProcess(44) 로 UAF 없음. liveness 원자 필드 단일 writer.
- uploader: owner epoch/configGeneration 펜싱, 401 일시정지+보관 4개, 재시도 3회/2·4s/60s, 큐 4MB 상한에 held 포함, 토큰은 diag/reason 에 없음, 콜백은 lock 밖.
- 프레임 게이트: 실제 콘텐츠 시계, keyWaitDrop 제외, P11 pending 수명(admit 에서만), 복구 타이머 backoff·defer 유계 — 이전 task 에서 직접 실행 검증(frame_gate/recovery PASS).

## 4. 직접 검사 vs 기존 보고 구분
- 검증용 직접: 위 1·3 절의 모든 file:line 대조, 이벤트 로그 조회. 이번 감사에서 새로 실행한 빌드/테스트 없음.
- 에이전트 보고 중 채택: 원문 재확인한 것만(N1·N2·N3·U1·C2·W1~W5·T1~T7·WSAECONNRESET(미확정 표시)). 채택하지 않은 것 없음(모두 재확인 가능했음).
- 기존 보고(작업용 t-5ahw0t5x 회귀 수치)는 이번 결함 판정의 근거로 쓰지 않음.

## 5. 잔여 한계
- F1·C2 빈도: 계측 미노출로 판정 불가 → 카운터 노출 뒤 실기 로그 필요. E2 는 pendingDepth 실측 필요.
- U1 런타임 서명 부분 일치(host 1건), 클라 0건 → 실제 발생 경로 미확정.
- N1~N3 은 코드로 확정된 설계 공백이나 현장 발생률(꼬리 손실·재정렬·호스트 미응답)은 미계측.
- 다중 모니터·WSAECONNRESET·GDI 캡처 환경 실패 원인은 장비/재현 없음.

## 6. 권고 우선순위 (구현 위임 아님, Codex 판단용)
1. F1 (+C2 통합, 카운터 노출, 회귀) 2. N1 hold/tail 재설계 + 꼬리 드롭 회귀 3. U1+U2 (stop 호출·유계 최종 pass·stopping 거부) 4. N2·N3 5. W2·W1 6. 테스트 강도 T1·T4·T5·T7.

## 7. 커버리지 표
| 파일 | +/- | 검토 방식 | 상태 |
|---|---|---|---|
| AGENTS.md | +10/-0 | 직접(이전 task) | 완료 |
| CLAUDE.md | +14/-0 | 직접(이전 task) | 완료 |
| apps/directory/test/logs_test.js | +37/-0 | 에이전트+재확인(86-94) | 완료 |
| apps/directory/test/restart_test.js | +25/-5 | 에이전트+재확인(66-72) | 완료 |
| apps/native_poc/CMakeLists.txt | +108/-0 | 직접(diff 전체) | 완료 |
| client_shell_main.cpp | +10/-2 | 직접 | 완료 |
| host_app_main.cpp | +15/-5 | 직접 | 완료 |
| host_capture_session.cpp | +13/-4 | 직접 + 444-449/517-522 | 완료 |
| host_capture_session.hpp | +19/-1 | 직접 | 완료 |
| host_control_session.cpp | +14/-9 | 직접 | 완료 |
| host_encode_epoch_test.cpp | +449/-0 | 에이전트+재확인(236-268) | 완료 |
| host_encoded_sender.cpp | +25/-0 | 직접 + 95-145 | 완료 |
| host_encoded_sender.hpp | +22/-0 | 직접 | 완료 |
| host_encoder_manager.hpp | +8/-0 | 직접 | 완료 |
| host_epoch_gate.hpp | +165/-0 | 직접(전문) | 완료 |
| host_input_router.hpp | +24/-0 | 직접 | 완료 |
| host_input_target_rect.hpp | +81/-0 | 직접(전문) | 완료 |
| host_input_target_rect_test.cpp | +228/-0 | 에이전트+재확인(35-52) | 완료 |
| host_loop_helpers.cpp | +132/-0 | 직접(diff 전체) + 325-345 | 완료 |
| host_main_loop.hpp | +6/-0 | 직접 | 완료 |
| host_secure_target_rect.hpp | +91/-0 | 직접(전문) | 완료 |
| host_sender_epoch_test.cpp | +354/-0 | 에이전트+재확인(190-236) | 완료 |
| host_stage_encode_send_h264.cpp | +1/-0 | 직접 + 261-274 | 완료 |
| host_stage_encode_send_h264_au.cpp | +67/-1 | 직접(diff 전체) + 340-425 | 완료 |
| host_stage_stats.cpp | +4/-0 | 직접 | 완료 |
| host_startup_capture.cpp | +1/-0 | 직접 | 완료 |
| host_startup_graphics.cpp | +4/-3 | 직접 | 완료 |
| log_upload.cpp | +409/-63 | 직접(diff 전체) + 522-536 | 완료 |
| log_upload.hpp | +62/-7 | 직접 | 완료 |
| log_upload_test.cpp | +598/-0 | 에이전트+재확인(375-384) | 완료 |
| mf_h264_codec.cpp | +11/-0 | 직접(diff + 1815-1840, pop 지점 전수) | 완료 |
| mf_h264_codec.hpp | +7/-0 | 직접 | 완료 |
| native_video_client_session.cpp | +8/-58 | 직접 | 완료 |
| native_video_client_shared_core.cpp | +119/-17 | 직접(diff 전체) + 735-815 + 703-712 | 완료 |
| native_video_client_shared_core.hpp | +41/-0 | 직접 | 완료 |
| native_video_client_shared_core_test.cpp | +301/-0 | 에이전트만 | 완료(재확인 생략: 결함 지적 없음) |
| product_version.hpp | +1/-1 | 직접 | 완료 |
| secure_input_broker.cpp | +47/-0 | 직접 | 완료 |
| secure_input_broker.hpp | +10/-0 | 직접 | 완료 |
| secure_input_diag_budget.hpp | +48/-0 | 직접(전문) | 완료 |
| secure_input_mapping_test.cpp | +67/-0 | 에이전트만 | 완료(결함 지적 없음) |
| secure_input_service_main.cpp | +8/-3 | 직접 | 완료 |
| udp_video_nack.hpp | +155/-0 | 직접(전문) | 완료 |
| viewer_constants.hpp | +12/-0 | 직접 | 완료 |
| viewer_context.hpp | +7/-0 | 직접 | 완료 |
| viewer_frame_gate.cpp | +132/-6 | 직접(diff 전체) | 완료 |
| viewer_frame_gate.hpp | +14/-2 | 직접 | 완료 |
| viewer_frame_gate_state.hpp | +43/-0 | 직접(이전 task 포함) | 완료 |
| viewer_frame_gate_test.cpp | +518/-3 | 직접(이전 task diff) + 에이전트 | 완료 |
| viewer_liveness_test.cpp | +188/-0 | 에이전트만 | 완료(결함 지적 없음) |
| viewer_recv_liveness.hpp | +144/-0 | 직접(전문) | 완료 |
| viewer_recv_stats.hpp | +3/-0 | 직접 | 완료 |
| viewer_session_state.hpp | +12/-0 | 직접 | 완료 |
| viewer_session_watchdog.cpp | +100/-0 | 직접(전문) | 완료 |
| viewer_session_watchdog.hpp | +23/-0 | 직접(전문) | 완료 |
| viewer_shutdown.cpp | +41/-2 | 직접 + 42-62 | 완료 |
| viewer_startup.cpp | +46/-17 | 직접 | 완료 |
| viewer_state.hpp | +2/-0 | 직접 | 완료 |
| viewer_thread_join.hpp | +32/-0 | 직접(전문) | 완료 |
| viewer_udp_recovery_test.cpp | +1427/-0 | 직접(FakeHost·S12/S13/S14·edb1ec9 diff) + 에이전트 전문 | 완료 |
| viewer_udp_session.hpp | +61/-0 | 직접(전문) | 완료 |
| viewer_video_receiver.cpp | +175/-55 | 직접(run_udp diff 전체) | 완료 |
| viewer_video_receiver.hpp | +11/-2 | 에이전트(NackOptions 구조체) | 완료(직접 미독) |
| viewer_video_receiver_frame.cpp | +12/-3 | 직접 | 완료 |
| viewer_window_proc.cpp | +3/-0 | 직접 + 230-238 | 완료 |
| docs/history.md | +182/-0 | #408~#412 직접 | 문서(코드 감사 외) |
| docs/history/history_2026-W36.md | +6/-0 | 미독 | 문서 |
| docs/field_test_2026-09-07_0.2.100.md | +154/-0 | 미독(로그 grep 만) | 문서 |
| docs/구현계획.md | +18/-6 | P9/P10/P11 직접 | 문서 |
| docs/뷰어_리팩터_발견사항.md | +6/-1 | F-22 직접 | 문서 |
| docs/작업목록.md | +4/-0 | 직접 | 문서 |


## 8. 교차검토 보강 (Codex 후보 3건 + 정정, 2026-09-08 09:5x)
프로브: `.claude/probe/` (미추적, 제품 소스는 절대 경로로 컴파일만, 추적 파일 무변경). 산출: `probe_uploader_spin.out`, `probe_nack_trace.out`, `probe_fifo_model.out`.

**Q1 U3 HIGH (신규, 실측 재현) — 401 일시정지 뒤 큐 ≥ batchMaxBytes 면 worker busy loop + diag 파일 폭증**
- 위치: `log_upload.cpp:251-253` wait predicate `stopping || queuedBytes >= batchMaxBytes || wakeSeq != seen` 은 `authRejected`/`credentials` 와 무관 → `:198` next_job_locked 이 authRejected 로 false → `:271` continue → 즉시 재진입. 30회마다 `diag("idle alive …")` fopen/append.
- 재현(제품 log_upload.cpp+directory_client.cpp 링크, 401 만 답하는 loopback 서버, 제품 기본값 batchMaxBytes=192KB·flush 2000ms, LOCALAPPDATA 격리): A) 401 뒤 큐<batch 3초 → CPU 0%, diag 0B. B) 큐 ≈200KB 로 채운 뒤 3초 → **프로세스 CPU 2.91s(코어 97%), diag +2,372,858B(772KB/s), idle-alive 34,385줄, 7초간 1,261,170 사이클**.
- 트리거: P10 의 의도된 401 일시정지(토큰 회전 뒤 재로그인 전). 호스트 로그 수 초 분량이면 192KB 도달. 아직 미설치 코드(0.2.101+), 0.2.103 후보에 포함.
- 영향: 코어 1개 점유(캡처/인코드 경쟁) + %LOCALAPPDATA%\GNLink\log_upload.diag 가 시간당 GB 단위로 증가(디스크 고갈).
- 왜 테스트가 못 잡았나: `log_upload_test.cpp:327` [3] 은 queueCapBytes=600 으로 batchMax 미달.
- 수정 방향: predicate 를 `stopping || wakeSeq != seen || (credentials && !authRejected && queuedBytes >= batchMaxBytes)` 로; next_job 이 false 인 사이클은 최소 flushInterval 대기; idle-alive diag 는 시간 기준(예 60s) 스로틀.

**Q2 N1 확정(상태추적, 실제 어셈블러+스케줄러, 실 MFT/실네트워크 아님)** — `probe_nack_trace.out` [S-tail]: seq 10 마지막 청크 손실, 11·12 완성 → 25ms 타임아웃 틱에서 hold(120ms) 만료 시 drain 이 먼저 10 을 evict, 11 이 gap 배달 → **NACK 0건**, discontinuity → waitForKey. 비교 [S-hole]: 구멍 손실은 첫 틱(+25ms)에 NACK round 0, 재전송 도착 시 10 정상 배달(순서 유지). → NACK 는 highWater 아래 구멍만 고치고 꼬리는 항상 IDR 비용.

**Q3 N2 확정(상태추적)** — [S-reorder]: 31 의 첫 청크가 30 보다 먼저 → NACK 가 31 을 3라운드 추적, seq-최소 차단자 30 은 NACK 0 → 30 hold 만료로 32 가 gap 배달 → IDR 요청.

**Q4 N3 변형 확정(상태추적)** — key 대기 중 미완성 P(20) 가 미완성 IDR(21) 앞에 있으면: 후속 P 22 가 오면 20 evict 뒤 21 에 NACK 3라운드(≈+40ms 지연), **후속이 없으면(정적 화면) 이 경로로는 IDR NACK 이 나가지 않음**(NACK 0·pending 2; PopDelivery 는 `!anyComplete → return false` 로 hold 만료를 적용하지 않고, Poll 은 deque-첫 미완성이 P 라 Reset). 단 이 시나리오는 waitForKey=1 이므로 키프레임 복구 타이머(`FrameGate::tick` reason 7, 500ms→2s backoff)가 **별도 폴백**으로 IDR 을 재요청한다 — 프로브에는 tick 이 없어 그 경로를 재현하지 않았음(Codex 정정: "영구 정지" 아님). Normal 상태(waitForKey=0)의 후속 없는 NACK 소진 공백은 N3 이 별도로 다룸.

**Q5 F1·C2 (deque 모델 + 실제 `epoch_gate_judge`, 실 MFT 아님)** — `probe_fifo_model.out` [A]: 빈 출력 1회 뒤 flush → 새 IDR(입력 5) 태그 1 → drop-old-epoch, P(6) drop-awaiting-key(재요청), IDR(7) accept → flush 당 IDR +1·드롭 2(빈 출력 N회면 밀림 N). [B]: 65입력 overflow 뒤 pre-flush KEY(입력 40, 진짜 epoch 1) 가 태그 2 로 **accept-key** → "overflow 는 unknown/fail-closed" 계약 불성립 확정. 단 실제 stage 는 old 드롭 12회에서 ResetEncoder(FIFO clear) 가 먼저 발동하므로 잘못된 key 수락은 pre-flush key 가 overflow 뒤 첫 ~11개 출력 안에 있고 바로 뒤에 flush 가 있는 드문 조건에서만 가능 → C2 는 LOW 유지, 원인은 F1 과 별개(Codex 지적대로 구분).

**정정 요약**: U2 → LOW 잠재(제품 stop 호출 없음). S1 → 실질 결함 아님·계약 문구 정정(헤더에 예외 문구 없음, 창은 restart 구간). E1/E2(정책 임계값 가정)·F6(정상 호출자는 identity 채움)은 결함 목록이 아닌 한계/가정으로 이동.

**최종 결함 목록(조치 가치 있는 확인 결함)**: F1 HIGH(코드+모델), U3 HIGH(코드+실측), N1 HIGH-기능(라이브 스트림 한정, 코드+상태추적), U1 MEDIUM(기존 결함·코드 확정, 런타임 증거 미귀속), N2·N3(코드+상태추적) MED-LOW, C2·W1·W2·W4 LOW. 잠재/문서: U2, S1. 기존: 모니터 선택 primary 회귀, WSAECONNRESET(미확정), diag 무제한·429·JS 테스트 약점.


## 9. Codex 필수 정정 반영 (2026-09-08)
1. U3(401 busy loop)을 1절 결함 목록에 추가(8절 Q1 실측과 연결). 2. N1 을 "뒤 완료 AU 존재 시(라이브)" 로 한정, 후속 없는 희소 스트림은 tail NACK 가능으로 명시. 3. C2 수정 방향을 clear 단독 → provenance 무효 래치 + 인코더 flush/reset 재동기로 교체. 4. S1 을 "잔여 경쟁·영향 미확정" 으로, "문서화됨/조치 불요" 삭제. 5. U1 을 기존 결함으로 분류, 9/3 fastfail 은 원인 증거로 귀속하지 않음. 6. N3 의 "kick 주기로 유계" 삭제(secure 중 kick 차단). 제품 수정·전수 테스트 없음.
7. Q4(key 대기 중 앞 P + 후속 없음): "영구 정지" 삭제 → "이 경로로는 IDR NACK 불가, waitForKey=1 이라 fg.tick 복구 타이머가 별도 폴백(프로브 미포함)". 사용자 보고 중심: F1·U3·N1(라이브 한정)·N2·N3(Normal 상태 NACK 소진 공백).
