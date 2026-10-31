# old-apk-lane S run 끊김 조사 r2 (2026-10-30; r1을 대체. Codex codex_old_apk_lane_ime_status_r8.md 조건 반영)

대상 run은 `oa-s-20261030025800-36232`입니다. 앱은 0.2.32(b554e76 fixture 빌드, sha256 50080b2b…)이고 Host는 0.2.164 후보의 test host입니다. 실기는 실행하지 않았고 로그와 소스만 읽었습니다. 비밀번호 같은 입력 문자열은 이 문서에 옮기지 않았습니다. 인용한 logcat 줄은 IME, 터치, 앱 진단만이며 text 값은 없습니다.

## 0. 결론 먼저
- **미확정 호환 blocker로 남깁니다.** IME 보수(r7d)로 닫히지 않습니다.
- Host의 `reason=none`은 클라이언트가 정상 종료했다는 증거가 **아닙니다**. Host가 10 s 동안 새 control 메시지를 하나도 못 받았다는 뜻일 뿐입니다.
- 가장 유력한 후보는 0.2.32 구조(receive thread와 sink lock, control은 맨 앞 메시지 하나만 전송)입니다. 하지만 아래 3절의 반대 증거 때문에 **확정하지 않습니다**.
- 0.2.164 Host가 보낸 무엇이 원인이라는 직접 증거는 없습니다. 배제한 것도 아닙니다.

## 1. 한 시계로 맞추기 (AVD 시계 기준)
- **AVD − PC 시계차 = +0.404 … +0.531 s** (인과 순서로 끼워 잡음):
  - 하한: Windows 탭 tap은 logcat AVD 17:59:31.404이고, 하네스는 tap이 끝난 뒤 PC 02:59:30.x에 기록했습니다. 따라서 차이는 0.404보다 큽니다. 카드 tap(16.308 대 PC 15.x)에서는 0.308보다 큽니다.
  - 상한: Host가 window-select `applied=1`을 PC 02:59:38.958에 냈고, 앱이 `[select_ack]`를 AVD 17:59:39.489에 기록했습니다. 따라서 차이는 0.531 이하입니다.
- Host와 PC 시각은 AVD 시각으로 바꿀 때 +0.40~0.53 s를 더합니다. 아래 표의 Host 행은 바꾼 값입니다.

| AVD 시각 | 출처 | 사건 |
|---|---|---|
| 17:58:50.0 / 17:59:00.95 / 07.81 | logcat 터치 | 하네스의 로그인 ID / Password / Sign-in tap. Password에서 IME가 뜸(onRequestShow 17:59:01.0) |
| 17:59:16.31 | logcat 터치 | PC 카드 tap. 연결 시작(Host `client connected` PC 20.198 → AVD 약 20.6–20.7; 앱 `[directory_chosen]` 20.640, `[targets_ready] gen=1` 21.887) |
| 17:59:31.40 / 37.77 | 터치 | Windows 탭 / pinned 카드 tap |
| 17:59:39.41 / 39.49 | 앱 | `[select_request] gen=1`, `[select_ack] streamGen=2`(Host applied는 AVD 약 39.36–39.49) |
| 17:59:39.6 … 51.9 | Host | keyframe-request seq 1–7을 받음(AVD 약 39.6, 42.1, 42.5, 43.2, 45.4, 49.2, **51.96–52.09**) |
| 17:59:46.488 | logcat | `InsetsController: show(ime(), fromIme=true)` — viewer로 넘어갈 때 **로그인 IME가 다시 표시됨** |
| 17:59:48.20 | 앱 | `[sharpen_fallback] requested=weak effective=off reason=no_output_after_ready`. surface 교체 |
| 17:59:49.6 → 51.169 | logcat | codec 재생성. `MediaCodec started`의 tid 27601은 **UI 스레드**입니다(0.2.32는 SetSurface 안에서 sink lock을 잡은 채 만듦) |
| 17:59:52.06 / 53.47 / 54.62 | 앱 video_debug | in = 2 / 4 / 10 (느림) |
| **17:59:54.689 / 55.008** | logcat 터치 | 하네스 ④ tap(1200,540) down / up. 영상이 아니라 **IME 위** |
| 17:59:56.33 / 57.79 / 58.72 | 앱 video_debug | in = 22 / 46 / 57. 수신이 따라잡음 |
| 17:59:59.26 | 앱 | `[desktop_backend_ui] state=NOT_CONNECTED`(= native status가 connected가 아님) |
| 18:00:00.35 / .37 | 앱 | `[scene] HOSTS`, `[status] error` |
| **18:00:02.13–02.25** | Host | `udp control session ended epoch=1 resumed=0 reason=none`(마지막으로 받은 메시지 뒤 10.16 s) |
| 약 18:01:0x | relay | `closed reason=client silent`(60 s 규칙. 끊긴 뒤의 결과) |

- **세대**:
  - 연결 1개(`targets_ready gen=1 connects=1 hellos=1`), selection gen 1, streamGen 2.
  - 끊길 때까지 새 pick, stream reset, 재연결, resume이 **없었습니다**(Host에 resume 줄 0, 새 `client connected` 0).
  - 따라서 세대가 바뀐 것이 원인은 아닙니다.
- **native 실패 이유**:
  - 0.2.32는 `lastError`를 파일 로그에 남기지 않습니다(MainActivity 4434-4447은 UI 표시만, 2467은 첫 연결 실패만).
  - native stderr도 잡히지 않습니다.
  - 그래서 `"control link closed"`(session.cpp:960), `"control loop failed"`(:984), `"udp video receive failed"`(:1427), park 계열(:515/:893) 가운데 어느 것인지 **이 run에서는 알 수 없습니다**.

## 2. 소스 근거 (b554e76)
- **Host 10 s**:
  - `host_startup_control.cpp:1016`의 `UdpControlLink(…, 10000)`. 새 메시지가 10 s 없으면 읽기를 끝내고, channel은 닫지 않으므로 `reason=none`입니다.
  - 같은 메시지의 재전송(중복)은 새 메시지로 넘어가지 않으므로 이 10 s를 되돌리지 않습니다.
- **클라이언트 control**:
  - 맨 앞 메시지 하나만 나갑니다(udp_control_channel.cpp Send 220-245).
  - 250 ms마다 재전송하고 24회(약 6 s) 뒤 PeerLost가 되며, 이어서 `"control link closed"`입니다.
  - Host가 보내는 ACK는 **영상과 같은 수신 스레드**가 frame 전달 앞에서 처리합니다(session.cpp 1475-1480). 그 frame 전달은 sink lock을 잡습니다(android_video_decoder.cpp:173).
  - 재전송 Tick은 수신 스레드가 datagram 없이 놀 때(1458), 또는 control 스레드가 읽기를 기다릴 때(EnsureInbound)만 돕니다.
- **0.2.32 sink**: SetSurface(UI)가 lock을 잡은 채 codec을 만듭니다(148-171 → 628ff). 수신 스레드, output pump(3 ms), control loop(KeyframeOwedFor 매 pass 등), UI DebugStatus가 같은 lock을 잡습니다.
- **IME → Host 입력**:
  - viewer의 ImeCaptureView만 Host로 보냅니다(text는 `nativeQueueInputText`, 키는 kind 5/6). 둘 다 ping, keyframe request와 같은 FIFO입니다.
  - 이 run의 IME는 로그인 Password 입력(inputType 0x81)에 붙어 있었습니다. 54.69의 tap이 어느 view에 입력됐는지는 기록이 없습니다. 0.2.32는 key와 text를 로그에 남기지 않습니다.

## 3. 후보 원인과 각각이 예측하는 것
| 후보 | 맞는 증거 | 맞지 않거나 미확인인 점 |
|---|---|---|
| **H1** 수신 스레드 지연 → Host ACK 처리 지연 → 맨 앞 메시지 PeerLost(약 6 s) → `control link closed` | codec이 UI에서 lock을 잡고 재생성됨(49.6–51.17). 직후 in이 느림. Host가 마지막으로 받은 것 52.0 + 6 s ≈ 58.0이고 앱 NOT_CONNECTED는 59.26(UI poll과 Davey 지연을 감안하면 맞음). Host 10 s 시각과도 맞음 | **반대 증거**: 54.6–58.7에 in이 10에서 57로 빠르게 늘어 수신 스레드가 따라잡았습니다. 원래 ACK는 그 사이에 처리됐을 것이고, 처리되지 못했다면 ACK datagram 자체가 버려졌어야 합니다(SO_RCVBUF 초과 등). 그 경우에도 클라이언트 재전송(Tick)이 새 ACK를 받아야 합니다. Tick이 돌지 않았다면(수신 스레드가 계속 바쁘고 control 스레드가 lock을 기다림) PeerLost도 선언되지 않습니다. **H1 단독으로는 설명이 완전하지 않습니다** |
| **H2** control 스레드가 sink lock(또는 다른 것)에 오래 묶임 → ping 없음 → Host 10 s | Host가 52.0 뒤로 새 메시지 0. lock 경합이 큰 시점(UI DebugStatus, pump, 수신) | 앱이 59.26에 error로 간 이유가 H2만으로는 나오지 않습니다(PeerLost나 read 실패가 따로 있어야 함). control 읽기 시간 제한 12 s(session.cpp:39, :799)라면 약 64 s여서 시각이 맞지 않습니다 |
| **H3** ④ tap이 IME에 입력 → 무언가 송신 → Host 쪽 처리 문제 | tap 54.69는 끊기기 5 s 전 | Host는 52.0 뒤로 메시지를 **받지 않았습니다**. 입력은 같은 FIFO 뒤라 나가지도 못했을 것입니다. 근거 없음 |
| **H4** 0.2.164 Host가 보낸 형식이나 순서가 0.2.32와 맞지 않음 | — | Host에 오류나 거부 줄 0. seq 1–7과 IDR 응답은 같은 형식으로 정상. **배제 근거도 확정 근거도 없음** |
| **H5** 수신 socket 오류(`udp video receive failed`) | — | video_debug in이 58.72까지 늘었습니다. 그 뒤 0.5 s 안에 실패가 났다면 가능하지만 기록은 없습니다(stderr) |

## 4. 다음에 가를 수 있는 것 (실기는 0 — 제안만, 실행은 검증용 또는 Codex 판단)
- **fixture relay trace**(`REMOTE60_RELAY_TRACE=1`, 제품 변경 없음):
  - c2h와 h2c의 5 s 단위 흐름과 마지막 c2h 시각을 얻습니다.
  - 재전송이 relay에 도착했다면 H1(ACK가 클라이언트에 안 닿음)에 가깝고, 없었다면 H2(Tick도 송신도 멈춤)에 가깝습니다.
- **끊긴 직후 UI dump**: 하네스가 화면의 오류 문구(connectErrorText = lastError)를 기록하면 :960 / :984 / :1427을 가를 수 있습니다.
- **SH off 대조**: Settings에서 sharpening을 끄면 fallback과 UI 쪽 codec 재생성이 없습니다. 같은 boot의 S run에서 끊김이 사라지면 lock과 codec 계열(H1/H2)을 지지합니다.
- **0.2.32 기본 SH weak**: 이것이 fallback의 계기입니다. 실제 사용자 환경에서도 같은 경로를 탈 수 있다는 점은 호환 판정에 영향을 줍니다(Codex 판단).
