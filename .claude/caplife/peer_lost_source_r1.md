# A run의 Host peer-lost — 판정 입력과 A/B 차이 (source 확인, 2026-10-30)

대상 run은 `ab-a-shoff-20261030014717-45932`입니다. Host 로그에 `01:50:17.775 udp control session ended epoch=1 resumed=0 reason=peer-lost`가 남았습니다.

Host 빌드의 source는 feat-apk-0233 worktree HEAD **9f9a91c**로 봅니다(A와 같은 commit). Host exe d806e375를 이 source에서 다시 빌드해 대조하지는 않았습니다. 아래 줄 번호는 9f9a91c 기준입니다.

## 1. 무엇이 끊겨야 peer-lost인가
- `udp_control_channel.cpp:430-440` `Tick()`가 판정합니다.
  - Host의 **tx queue 맨 앞 control 메시지 하나**(host→client ControlData)에 대해 따집니다.
  - `retransmitIntervalUs=250000`, `maxAttempts=24`입니다(`udp_control_channel.hpp:74-75`).
  - `++head.attempts > 24`가 되면 `Close(PeerLost)`입니다. 즉 그 메시지의 첫 송신부터 **약 6.0 s**(24 × 250 ms, 25 ms slice 오차) 동안 그 seq에 대한 **ControlAck가 오지 않은** 경우입니다.
  - NACK를 받으면 그 fragment만 재송신하고 `lastSendUs`를 갱신합니다. attempts는 리셋되지 않습니다.
- 맨 앞 메시지를 내보내는 것은 같은 seq와 같은 `txStreamId`를 가진 **ControlAck 하나**뿐입니다(`HandleAck`, 현행 cpp:330-364와 같은 논리).
  - client가 보내는 다른 c2h datagram은 이 카운터와 무관합니다. client 자신의 ControlData(ping, keyframe-request), 다른 seq의 ACK, 영상·통계 packet이 모두 여기에 해당합니다. client ControlData를 받으면 Host는 ACK를 보낼 뿐, 자기 tx head는 그대로입니다.
- 이 log 줄을 만드는 다른 경로는 배제했습니다.
  - `host_startup_control.cpp:823-824` resume 경로의 `Close(PeerLost)`는 `[control] resume id=` 줄을 남깁니다. A Host 로그에 resume 줄은 0개입니다.
  - `:1016`의 10 s read timeout은 channel을 PeerLost로 닫지 않습니다.
  - 그러므로 이번 판정은 **Tick 재송신 소진**입니다. 소진된 메시지의 첫 송신은 01:50:11.7 ± 0.3으로 추정합니다(NACK가 없었다는 가정 아래). Host 로그에 control 송신 줄이 없어서 어떤 메시지(ping 응답 등)였는지는 모릅니다.

## 2. c2h UDP가 흐르는 중에도 날 수 있는가 — **예**
- client에서 Host 메시지에 ACK를 보내는 곳은 receive thread 하나입니다. `native_video_client_session.cpp:1342 VideoReceiveMain` → `:1500 udpControl_.OnPacket` → `HandleData` → `SendAckOrNack(ControlAck)`이고, NACK도 같은 thread의 `:1482 udpControl_.Tick()`입니다. 다른 caller는 없습니다(grep 결과 `OnPacket`는 :1500 하나뿐).
- client의 ping과 keyframe-request는 **세션 worker thread**가 보냅니다(`:1005 execute_control_action`, `:1020 Ping`). `Send` → `SendFragments`로 그 자리에서 송신합니다.
- 따라서 receive thread가 멈추거나 밀리면 c2h(worker의 ControlData)는 계속 나가는데 Host 메시지의 ACK만 끊길 수 있습니다. A의 relay trace에 맞는 모양입니다. c2h는 Host 판정 뒤 16:50:18.40Z까지 계속 도착했습니다.

## 3. A(9f9a91c ke-off)와 B(ac587dd plain-debug-r5)의 차이
- **control 송수신 코드는 같습니다.**
  - `git diff 9f9a91c ac587dd`에서 `udp_control_channel.cpp`와 `native_video_client_session.cpp`의 변경은 `REMOTE60_LT(...)` / `#ifdef GNLINK_LIVENESS_TRACE` 계측입니다.
  - 그 밖의 변경은 `GetStats` 필드 추가와 `execute_control_action` 결과를 `exchanged`에 받는 동치 변경입니다. ACK·재송신·timing 논리는 바뀌지 않았습니다.
  - B는 trace-off 빌드입니다(B run `liveness_trace lines=0`).
- **다른 것은 decoder sink가 receive thread를 붙잡는지입니다.** 27d5966(9f9a91c..ac587dd 사이)이 바꿨습니다.
  - A sink `android_video_decoder.cpp:173 OnEncodedH264FrameAs`는 receive thread에서 불립니다(`session.cpp:1363`).
  - 이 함수는 sink lock을 잡은 채 `EnsureCodecLocked`(`:704`; create `:714`, configure `:743`, start `:750`)와 `PumpCodecLocked`를 그 thread에서 실행합니다.
  - output pump(`:126-137`)도 같은 lock으로 3 ms마다 drain합니다. worker의 sink hook(`session.cpp:1120` 등)도 같은 lock을 씁니다.
  - B는 RX가 bounded inbox에 넣고 바로 돌아옵니다. codec 호출은 worker 한 곳에서 lock 없이 합니다. 이것이 27d5966 commit 본문의 ke-r23 결함입니다("control ACKs stopped, the host's retransmits ran out (24 x 250 ms) and the session ended peer-lost").
  - 그때의 음성 대조: `android_decoder_session_liveness_test`를 9f9a91c sink로 빌드하면 4 FAIL이었습니다(control 8 s 침묵, Host peer-lost).

## 4. A run에서 실제로 보이는 것 (logcat 원본, pid 21847)
- receive-thread 쪽 sink 호출 thread는 **22301**입니다(`dropped stale frame`, `queued h264 frame`가 이 thread).
  - 16:50:07.484 `updated codec config`
  - 16:50:08.27 `MediaCodecList`
  - 16:50:09.255 `CCodec allocate`(codec thread 22583)
  - 16:50:10.308 `Created component`
  - 16:50:11.282 `MediaCodec started`(22301)
  - 즉 **22301이 약 3.8 s(07.48 → 11.28) 동안 codec 생성 안에 있었습니다.** 그동안 같은 thread의 `OnPacket`/`Tick`은 돌지 않습니다.
- 그 뒤 영상 소비가 송신보다 훨씬 느렸습니다.
  - `queued h264 frame count=31`이 16:50:16.582입니다. 같은 기간(selection 01:50:06.1 →) Host는 약 30 fps로 약 310 frame을 보냈습니다.
  - video_debug `in`은 14(14.83) → 29(16.37) → 47(19.10)으로, 약 7–10 fps입니다.
  - 나머지 frame이 socket 수신 backlog에 쌓였는지 sink에서 버려졌는지는, A에 trace가 없어 가를 수 없습니다.
  - 같은 시기 UI thread `Davey! duration=6335ms`(16:50:13.084)가 있었고, 에뮬레이터 loadavg는 8.16(4 vCPU)이었습니다.
- relay trace c2h `ageMs=2724`(16:50:09.670 snap): c2h가 약 16:50:06.95–09.67에 비었습니다. worker가 sink hook에서 같은 lock을 기다렸을 가능성이 있습니다(5 s 해상도라 미확정).
- 소진된 Host 메시지의 첫 송신(추정 11.7)은 codec 생성이 끝난(11.28) **뒤**입니다. 그래서 "생성 3.8 s 한 번"만으로는 6 s 예산이 설명되지 않습니다.
  - 남은 설명 후보 ① 생성 뒤에도 receive thread가 sink lock과 codec 호출에 밀려 그 메시지와 재송신을 6 s 안에 처리하지 못했다.
  - 후보 ② backlog가 SO_RCVBUF를 넘어 그 datagram과 재송신이 버려졌다.
  - 두 후보 모두 "A sink가 receive thread를 붙잡는다"는 차이 위에 있습니다. 어느 쪽인지는 A 로그로 확정할 수 없습니다.

## 5. 결론 (확인된 것 / 아닌 것)
- 확인 (source):
  - Host peer-lost = Host의 맨 앞 control 메시지 하나가 약 6 s 동안 ACK 없음.
  - c2h가 흐르는 중에도 날 수 있음.
  - A와 B의 control 송신·ACK 코드는 같음(trace 제외).
  - A와 B의 차이는 receive thread에서 codec을 다루는지(A만 그렇다)임.
- 확인 (A 로그): receive thread 22301이 codec 생성에 3.8 s 묶였음. 이후 영상 소비가 약 7–10 fps로 송신 30 fps보다 낮았음.
- 미확인:
  - 소진된 메시지가 무엇이었는지.
  - 11.7–17.7 구간에서 ACK가 늦은 것인지 버려진 것인지.
  - A 실패가 이 run의 부하에서만 생기는지(1회 표본). 역순 B→A가 이 판단에 쓰입니다.
- 실행: 이 확인 중 장치·슬롯·프로세스는 쓰지 않았습니다. git show / diff와 로그 읽기만 했습니다.

## 정정 (2026-10-30, Codex codex_apk_inbox_next_run_decision_r4.md)
- 2절 "NACK도 같은 thread의 `:1482 udpControl_.Tick()`"은 범위가 좁았습니다. client의 worker도 `:809 controlLink = UdpControlLink(&udpControl_, …)`로 control을 읽습니다. 그 Read 대기(`udp_control_channel.cpp` `EnsureInbound`)가 25 ms마다 `channel_->Tick()`을 부릅니다. 그러므로 NACK와 timeout 재송신 Tick은 **RX loop와 worker의 Read 대기 양쪽**에서 돕니다. "항상 RX뿐"이 아닙니다.
- 주결론은 그대로입니다: Host 메시지에 대한 **ACK**(`HandleData` → `SendAckOrNack(ControlAck)`)는 `OnPacket`에서만 나가고, client에서 `OnPacket`을 부르는 곳은 RX loop(`:1500`)뿐입니다.
- 3절 "다른 것은 27d5966"은 A/B 차이를 하나로 줄인 표현이었습니다.
  - 9f9a91c..ac587dd에는 27d5966(codec 전용 worker) 말고도 다음이 있습니다:
    - ec016f0: IDR 재요청
    - 7a79b09: detach 상한, test relay trace
    - 0a108b3: 큐 예약 gate, inbox 상한, codec 생성 재시도
    - ac587dd: 재시도 예산 공유
  - A와 B의 차이는 이 묶음 전체입니다. receive thread가 codec을 붙잡는지는 그 가운데 peer-lost와 직접 이어지는 차이로 본 것입니다.