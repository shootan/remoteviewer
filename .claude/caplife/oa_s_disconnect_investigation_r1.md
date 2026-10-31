# old-apk-lane S run 끊김 조사 (2026-10-30, run `oa-s-20261030025800-36232`, 0.2.32 b554e76 ↔ 0.2.164 Host)

원문은 `D:/remote/remote/.claude/worktrees/verify-0164/.claude/oa_runs/oa-s-20261030025800-36232`입니다. 실기는 실행하지 않았습니다. 로그와 b554e76 소스를 읽기만 했습니다.

## 1. 시각표 (출처를 고정)
앱 시각은 AVD 시계(UTC)이고, Host와 relay 시각은 이 PC 현지 시각(UTC+9)입니다. 둘 다 같은 PC 위에서 나온 값이지만, 대조할 때는 시계 차이가 있을 수 있다고 둡니다.

| 시각 (UTC) | 출처 | 사건 |
|---|---|---|
| 17:59:48.200 | 앱 diag | `[sharpen_fallback] requested=weak effective=off reason=no_output_after_ready`. 0.2.32 기본 설정 weak의 sharpening 경로가 실패해 direct 경로로 전환 |
| 17:59:48.196 | logcat 27601(UI) | `video surface cleared` |
| 17:59:49.6 → 51.169 | logcat | codec 재생성(`CCodec allocate` 50.003 … `MediaCodec started` 51.169). 시작 로그는 **tid 27601 = UI 스레드**입니다 |
| 17:59:51.169 | 앱 diag | `[sharpen_path] path=direct requested=weak effective=off fallback=no_output_after_ready` |
| 02:59:51.561 (현지) | Host | `keyframe-request seq=7`. Host가 **마지막으로 받은 control 메시지** |
| 17:59:52.06 / 53.47 / 54.62 / 56.33 / 57.79 / 58.72 | 앱 video_debug | `in`이 2 / 4 / 10 / 22 / 46 / 57로 늘어남. 수신은 계속되지만 처음에는 매우 느림(1.4 s에 +2) |
| 17:59:54.689 | logcat | 하네스의 ④ tap이 영상이 아니라 **soft keyboard** 위에 떨어짐(`TouchInteractionService ACTION_DOWN/UP`, 검증용 확인) |
| 17:59:59.260 | 앱 diag | `[desktop_backend_ui] state=NOT_CONNECTED` |
| 18:00:00.349 / .371 | 앱 diag | `[scene] HOSTS`, `[status] error` |
| 03:00:01.723 (현지) | Host | `udp control session ended epoch=1 resumed=0 reason=none` |
| 이후 약 60 s | relay | `closed reason=client silent state=active c2h=148/13113B h2c=1603/1066945B` |

## 2. 각 줄이 무엇을 뜻하는지 (소스)
- **Host의 `reason=none`**:
  - Host의 control 읽기는 10 s 동안 메시지가 없으면 끝납니다(`host_startup_control.cpp:1016` `UdpControlLink link(..., 10000)`). 이때 channel을 PeerLost로 닫지 않으므로 이유는 `none`입니다.
  - 02:59:51.561에서 03:00:01.723까지는 **10.16 s**입니다.
  - 따라서 Host는 51.56 뒤로 **새 control 메시지를 하나도 받지 못했습니다**. ping도 없었습니다. 클라이언트는 평소 약 1 s마다 ping을 보냅니다.
- **relay의 `client silent`**: 마지막 client 패킷에서 60 s가 지나면 닫힙니다(`server.js:537` `RELAY_IDLE_TTL_MS`, `:1759-1765`). 끊긴 **뒤의 결과**이지 원인이 아닙니다. 이 run은 relay trace를 끄고 돌았으므로 c2h의 시각별 흐름은 없습니다.
- **앱의 `[status] error`와 HOSTS**(b554e76, subagent가 소스로 확인):
  - `renderStatus`가 `nativeGetStatus()`를 읽습니다(MainActivity.kt:4324).
  - 연결이 아니면 `homeScene()` = HOSTS로 갑니다(4349-4357, 4681-4685).
  - `[status]`(5927-5928)와 `NOT_CONNECTED`(2561, 2613, DesktopBackendPresentation.kt:117)는 이 polling의 결과입니다.
  - native 쪽 `status="error"`는 `SignalRuntimeFailure`(native_video_client_session.cpp:1557-1569)가 만듭니다. 이 경우에 맞는 호출 지점은 `"control link closed"`(:960, `!controlLink->Alive()`)나 `"control loop failed"`(:984)입니다.
  - **실패 이유(`lastError`)는 파일 로그에 남지 않습니다.** `nativeGetLastError`는 UI 표시와 첫 연결 실패에만 쓰입니다(4434-4447, 2467). native의 stderr도 잡히지 않습니다. 그래서 둘 중 어느 쪽이었는지 이 run에서는 가를 수 없습니다.
- **클라이언트 control의 구조**(b554e76):
  - 한 번에 **맨 앞 메시지 하나만** 나갑니다(udp_control_channel.cpp Send 220-245).
  - 맨 앞 메시지가 ACK를 못 받으면 250 ms마다 다시 보내고, 24회 뒤 PeerLost로 끝납니다(약 6 s, .hpp:74-75). 그러면 `Alive()`가 false가 되어 `"control link closed"`입니다.
  - 그 뒤의 ping과 입력은 **맨 앞 메시지 뒤에 줄을 서서 나가지 않습니다**.
  - Host가 보내는 ACK는 **영상과 같은 수신 스레드**가 처리합니다(VideoReceiveMain 1475-1480 `udpControl_.OnPacket`). frame 전달(`OnEncodedH264Frame`, android_video_decoder.cpp:173)은 sink의 `mu_`를 잡습니다.
- **0.2.32 sink의 lock**:
  - `SetSurface`는 UI 스레드에서 `mu_`를 잡은 채 codec을 create / configure / start합니다(android_video_decoder.cpp:148-171 → EnsureCodecLocked 628ff).
  - 같은 lock을 수신 스레드(:173, ConsumeDecoderKeyframeRequest 411-416), output pump(3 ms마다, 126-138), control loop(KeyframeOwedFor 매 pass 429-432 등), UI의 DebugStatus polling(557)이 잡습니다.

## 3. 원인 (가능성이 가장 큼 — 확정 아님)
1. sharpening fallback이 surface를 바꿉니다(48.2).
2. 0.2.32는 **UI 스레드에서 lock을 잡은 채 codec을 다시 만듭니다**(약 1.6 s, 49.6–51.17). 그동안 수신 스레드와 control loop도 같은 lock을 기다립니다.
3. 풀린 직후 클라이언트가 keyframe request를 보냅니다(Host 수신 51.56).
4. 에뮬레이터 부하(UI의 Davey 1.2–2.2 s 반복)와 lock 경합 속에서 수신 스레드의 처리가 늦습니다(in이 1.4 s에 +2). 그래서 **Host가 보낸 ACK가 영상 backlog 뒤에 머물러** 약 6 s 안에 처리되지 않습니다.
5. 클라이언트의 맨 앞 메시지가 PeerLost가 됩니다(약 57.6). 그 뒤의 ping과 입력은 나가지 못하고, 세션은 error(59.26)로 끝납니다.
6. Host는 새 메시지가 오지 않으니 10 s 뒤 `reason=none`으로 끝납니다(01.72).

- 이것은 apk-decoder-liveness가 ke-r23과 A run(9f9a91c)에서 본 것과 **같은 꼴**입니다. 수신 스레드가 codec에 묶여 ACK가 끊기는 것으로, 27d5966 이후 묶음에서 고쳤고 0.2.32에는 그 수정이 없습니다. 이번에는 Host 쪽 peer-lost 대신 클라이언트 쪽 head가 소진되는 방향으로 나타났습니다.
- **새 Host(0.2.164)가 보낸 무엇 때문이라는 근거는 없습니다.**
  - Host 로그에는 03:00:01.723 전에 오류, resume, 거부 줄이 없습니다. 마지막 control 교환(keyframe-request seq=7, IDR 응답)도 정상입니다.
  - Host는 시간 제한(10 s 무메시지)으로 끝났습니다.
  - 다만 0.2.164가 보낸 ACK나 메시지의 형식이 0.2.32와 맞지 않았을 가능성을 이 로그만으로 **완전히 배제할 수는 없습니다**. 앞선 seq 1–7의 교환은 같은 형식으로 정상이었습니다.
- **socket 오류**: 수신 실패라면 `"udp video receive failed"`(:1427)인데, 이 경로에는 stderr 줄(:1419-1426)만 있고 이 run에서는 볼 수 없습니다. 하지만 video_debug는 58.72까지 in이 늘었으므로 수신 socket이 그 전에 죽었다는 근거는 없습니다.
- **클라이언트의 12 s read timeout**(`kUdpControlReadTimeoutMs = 12000`, session :39/:799): 마지막 정상 교환 51.5에서 12 s 뒤면 약 03.5입니다. 실제 error(59.26)보다 늦으므로 이번 원인일 가능성은 낮습니다.

## 4. IME → key 송신 경로 (b554e76)
- viewer의 `ImeCaptureView`가 받은 입력을 Host로 보냅니다(MainActivity 1775-1795).
  - 글자는 `onCommitText` → `nativeQueueInputText` → `enqueue_control_input_text`로 갑니다.
  - 특수키는 `queueViewerSpecialKey` → `nativeQueueInputEvent`(kind 5/6)로 갑니다.
  - 둘 다 ping, keyframe request와 **같은 control FIFO**를 탑니다(`NextAction(..., &inputQueue_, ...)` 942-944).
- 이번 keyboard는 로그인 Password 입력의 IME(inputType=0x81)였습니다. 54.689의 tap이 어느 view에 글자를 넣었는지는 앱 로그에 없습니다. 0.2.32는 key와 text를 파일 로그에 남기지 않고, 실패만 남깁니다(4153).
- 어느 쪽이든 그 시점에는 맨 앞 메시지(51.5에 보낸 것)가 ACK를 못 받은 상태였으므로, 입력은 **줄 뒤에서 나가지 못했습니다**. Host도 51.56 뒤로 아무 메시지를 받지 못했습니다. 키보드 tap이 이 끊김의 원인이라는 근거는 없습니다.
- Host의 `[input-trace]`는 pointer(kind 1–4)만 기록하므로, key와 text가 도착했는지는 Host 로그로도 확인할 수 없습니다.

## 5. 남은 미확정과 다음 run에서 얻을 수 있는 것 (제안. 실행은 검증용 또는 Codex 판단)
- **fixture의 relay trace**: oa fixture에도 `REMOTE60_RELAY_TRACE=1`을 켜면 c2h와 h2c의 시각별 흐름(ACK와 ping이 언제 끊겼는지)을 얻습니다. 제품 변경 없이 fixture만 바꾸면 됩니다.
- **SH 설정을 off로 한 대조**: 0.2.32의 기본 SH weak에서 fallback으로 인한 surface 교체가 계기라면, Settings에서 SH를 끈 S run에서는 같은 끊김이 나지 않아야 합니다. 같은 boot에서 1회 대조하면 가설을 시험할 수 있습니다.
- **클라이언트 `lastError`**: 0.2.32 바이너리는 바꿀 수 없으므로, 화면의 오류 문구(`connectErrorText`)를 끊긴 직후 UI dump로 남기면 `control link closed`인지 `control loop failed`인지 가를 수 있습니다. 하네스만 바꾸면 됩니다.
- **이 결과가 old-APK lane 판정에 주는 의미**: 0.2.32 클라이언트의 알려진 구조 결함(재연 조건: 부하 + surface 교체)이 원인일 가능성이 큽니다. 0.2.164 호환성 결함의 근거는 지금 없습니다. 판정에서 이것을 "0.2.32 결함"으로 확정할지는 위의 대조 뒤에 Codex가 정합니다.
