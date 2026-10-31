# keepalive-r2(e) — E2 "relay 영상 미도달" 조사 1단계 (재실행 0, 남은 자료만)

대상: ke-r23 run 폴더 `feat-apk-0233\.claude\e2e\runs\ke-r23-20261029184026-36368`(Host a log, 앱 `_cur.log`, `logcat.txt`, fixture `dir/server.stdout.txt`)와 ke_r16–r21 보존 자료. 코드 기준은 feat-apk-0233 HEAD `9f9a91c`.
시계: 앱 로그와 logcat 은 UTC(09:42), Host 는 KST(18:42)다. 앱 `select_request` 09:42:18.935 와 Host `window-select … pending` 18:42:18.757 을 맞대 보면 앱이 약 +0.2 s 앞선다(전송 지연 포함).

## 현재 결론 (r2 정리: 아래 "보충"·"ke_r21 정정" 절과 같은 확정 수준으로 맞춘 하나의 결론)
> 이 절이 이 보고의 결론이다. 아래 본문의 옛 표현 중 이보다 강한 것(relay·fixture "원인 아님", "회귀 아님", "control 정지 원인")은 이 절의 수준으로 읽는다.
1. **relay**: "relay 가 영상 전체를 막았다"는 가설은 AU/delta 가 sink 까지 도달한 사실과 맞지 않아 **기각**한다. 그러나 단순 forward source(server.js:2120–2150)와 세션 합계만으로 **모든 IDR/chunk 가 제때 전달됐다거나 relay·UDP·fixture 의 기여가 없었다고는 확정하지 않는다**. 수신 loop 정지 때 socket backlog/drop 이 있었을 가능성도 구간 결과에 포함한다. `client silent` 닫힘은 60 s 무통신 뒤의 결과다(server.js:537, :1759–1765). 통과 세션도 같은 줄로 닫혔다.
2. **영상 0 의 직접 단계**: 약 19.6–29.6(10 s) 동안 surface/codec 이 준비되지 않았다. 그 사이 도착한 gen-2 IDR 은 `KeyLost` 됐다(현재 정책이며, 그것만으로 새 결함이라고 하지 않는다). codec 이 준비된 뒤에는 delta 만 와서 실제 queue 는 0 이었다. 자세한 내용은 보충 절의 ③·④ 다.
3. **control 이 끊긴 이유는 확정하지 않았다.** 유력 가설은 source 로 확인된 제품 구조(codec 작업과 control 진행이 같은 lock·loop 에 묶임)이고, 26.4–29.6 구간을 설명할 수 있다. 그 구간의 직접 RX wait 계측은 없다. 설명되지 않는 구간이 둘 있다:
   - ≈25.0–26.4 의 무응답
   - 30.65 재개 뒤 `keyframe-request seq=7` 이 Host 에 보이지 않은 것. 이것은 그 관측 지점에서 안 보였다는 사실일 뿐이고, wire 에서 유실됐다고 단정하지 않는다.
4. **회귀 여부**: decoder·server 파일이 ke_r16 이전 그대로라는 것은 그 파일들이 같다는 근거일 뿐이다. 호출 순서·build·fixture·부하 전체에서 회귀가 없었다는 증명은 아니다. 참고로 ke_r18 은 같은 sharpen fallback 경로에서 codec 재생성이 0.6 s, ke-r23 은 6.0 s 였다.
5. **ke_r21**: A 의 E7g peer_lost 는 의도된 탈취 종료다. B 의 후속 Host 종료(08:32:27 peer-lost, 영상 decode 중)와 하네스의 메뉴 중단은 **미분류**다. ke-r23 과 원인이 같다고도 다르다고도 하지 않는다.
- 유발 조건(관측): 에뮬레이터 과부하(EGL 4.2 s, slow dispatch 2.6 s). 하네스는 그 구간에 UI 조작이 없었다. 기여 근거는 없지만 배제를 증명한 것도 아니다.
- 제품 대응: Codex r2 결정에 따라 (b) RX/control 의 codec 동기 의존 제거를 구현했다(27d5966). (a) 준비 뒤 IDR 회복은 계측·대조로 확인한다(report_decoder_liveness_r1.md).

## 구간별 (Host 송신 → relay → AVD 수신 → 조립 → decoder)
| 구간 | 근거 | 판정 |
|---|---|---|
| Host 송신 | `window-select applied=1 streamGen=2`(18:42:18.785), `selection first keyframe sent`(18.807). viewer `keyframe-request reason=2` seq 1–6 을 받아 그때마다 keyframe 을 보냈다(18.875 … 24.030, 1.6–1.7 KB / 3 chunks). wire 송신은 31.2 까지 계속됐다(`encodedFrames=24 sentFrames=24`/s). | 정상 |
| relay 수신·전달 | `bound`, `first c2h`, `first h2c`, `active; helloAck in 2ms`. 닫힘은 `client silent state=active c2h=155/14021B h2c=927/583535B`. 전달 함수에 크기 제한이나 버림이 없다(server.js:2120–2150). | 정상. 닫힘은 결과다 |
| AVD 수신 | 수신 스레드(tid 14121)가 Host 정지(31.227) **뒤에도** 09:42:30.65–38.91 동안 delta 를 계속 처리했다(`delta before the codec's first IDR dropped … drops=1→31→61→91→121`). 즉 패킷은 도착해 소켓에 쌓여 있었다. | 도착함 |
| 조립 | 위 drop 줄은 조립된 AU 가 decoder sink 까지 갔다는 뜻이다(android_video_decoder.cpp:217–229). | 정상 |
| decoder 입력 | `video_debug in=0`. 20.52 에는 `surface=off codec=off`, 31.30 에야 `surface=on codec=on`. 그 사이 keyframe 은 surface/codec 이 없을 때 도착해 `KeyLost` 로 버려졌다(:206–210). codec 이 켜진 뒤에는 delta 만 와서 `codecAwaitsKey_` 에서 버려졌다(:217–229). | **여기서 0** |

## 제어(ping/ack)와 영상의 시각 대조
- 앱 → Host 마지막 신호는 keyframe-request seq=6, Host 18:42:24.030 이다. 이 요청은 worker 가 decoder lock 을 거쳐 꺼낸다(native_video_client_session.cpp:1120 → android_video_decoder.cpp:473–474). 요청이 여기서 멈춘 것은 worker 도 같은 lock 에서 기다렸다는 것과 **맞는다**(직접 증거는 아니다).
- Host 에서 응답 없는 재전송이 시작된 시각은 늦어도 약 25.2 다(31.227 − 24×250 ms). 즉 **제어 ack 는 25.2 무렵 이미 끊겼다.** 영상(h2c)은 Host 가 31.2 까지 보냈고 relay 가 그대로 전달했다.
- 수신 스레드는 앱 30.65(≈ Host 30.45)에 다시 움직였다. 그래도 Host 는 31.227 에 peer-lost 를 냈다. 수신 스레드가 쌓인 약 7 s 분량의 datagram 을 순서대로 처리하고 있었기 때문으로 보인다. Host 의 재전송 패킷은 그 대기열 뒤에 있었다. drop 계수가 38.91 까지 오른 것이 그 대기열이다(**추론**).
- 앱 `keep_end peer_lost` 는 09:42:40.711 이다. 대기열 처리가 끝난 뒤 앱 쪽 control 이 닫힌 것을 알아챘다.
- 결론: **둘 다 끊겼다.** Host 송신 기록과 relay 합계로 보면 영상은 끝까지 보내졌다(전달·수신의 증거는 아니다). control 이 끊긴 원인은 위 "현재 결론" 3번 수준(유력 가설, 미설명 2구간)이다.

## 에뮬레이터 부하 (유발 조건)
- 09:42:21.588 앱 `EGL_emulation app_time_stats avg=4241.91ms`, 21.722 `Davey! duration=4287ms`
- 09:42:20.119 wmshell `Slow dispatch took 2608ms`, 20.253 system_server `Slow dispatch took 2421ms`
- 09:42:25.998 systemui `Skipped 464 frames`
- 1000 ms 이상인 Slow dispatch/delivery 수: **ke-r23 24건**(logcat 18,864줄, 짧은 실행) vs **ke_r19 2건**(76,216줄, 전체 실행)
- 에뮬레이터는 10-26 부터 계속 떠 있었다(report_avd_r5.md 기록). 부하 원인은 이 자료로 확정하지 않는다.

## ke_r16–r19 대비 바뀐 것
- **경로**: 모두 relay 다(`[directory_chosen] 10.0.2.2:18443|relay`, r16–r21 과 r23 같음).
- **server.js**: 90115a2(10-27 14:15) 이후 변경이 없다. ke_r16 보다 앞선다. 현재 sha256 `b329e222…`.
- **decoder**: android_video_decoder.cpp 는 8731255(10-27 14:21) 이후 변경이 없다. MainActivity 의 sharpen 코드도 10-27 20:00 이후 변경이 없다(`-G sharpen` 0건).
- **native_video_client_session.cpp 를 바꾼 commit**은 모두 `VideoReceiveMain`(:1342~) 밖이다:
  - 3ef6ee6, 50340ce, 7f07f1a: X fence. `QueueInputEvent`, `WorkerMain` 입력 부분, `ResetUnlocked`.
  - 16a1817/21f9f17: activation socket. `StopWorker`, `SignalRuntimeFailure`, `ConnectUdpVideo`, `CloseSocketsUnlocked`.
- **그 밖의 feat 변경**은 APK·standby·하네스 쪽이다: NP early relay·dirretry(10-27), KA e r3 f51e8c6/6451ec1/1aecc48, X fence, guard 병합 48d096a/ccf6455/3a3c77f/9f9a91c, N3 9fec1f9, job 래퍼 de2d60a/4f04415. 수신·decoder 경로를 바꾼 것은 없다.
- **fixture**: r23 은 job 래퍼(e2e_job_run)와 shared guard 를 쓴다. Host 실행에 `--input-injection-mode none --host-power-management off` 가 붙는다. 앱 수신 경로와는 관계가 없다.
- **하네스**: 18:42:16 tap 다음 18:42:41 FAIL 까지 UI 조작이나 dump 가 없다. `select()` 는 0.5 s 마다 앱 로그를 읽기만 한다(ke_avd.py:240–260). 통과 실행과 같다.

## 분류
- 위 "현재 결론"으로 대신한다(옛 표현인 "fixture/relay 원인 아님", "제품 원인"은 철회하고 그 수준으로 낮췄다).

## 2단계 제안 (시험 쪽만, 실행 전 보고)
- (a) 실행 직전과 각 시나리오 사이에 에뮬레이터 부하 기준을 기록한다(logcat 의 1 s 이상 `Slow dispatch` 수, `EGL app_time_stats` 평균). 과부하는 "환경 조건"으로 표시하고 제품 결과와 구분한다.
- (b) 같은 에뮬레이터 상태에서 sharpen off 로 E2 를 1회 돌려 유발 경로를 분리한다. 하네스에서 설정할 수 있는지 먼저 확인한다. 실행은 슬롯 규칙(sh_run.ps1)대로 한다.
- (c) 에뮬레이터 재시작으로 부하를 해소하는 것은 사용자 기기 상태를 바꾸는 결정이라 별도로 확인받는다.
- 제품 쪽 계측(수신 스레드 stage, lock 대기 시간)과 수정은 Codex 판정 뒤에 한다.

## 보충 (Codex codex_avd_ke_r23_triage_scope.md 의 구분, 재실행 0)
소스는 9f9a91c 의 android_video_decoder.cpp 다(워킹트리 diff 0). 아래 줄 번호는 이 파일 기준이다. Codex 가 인용한 :861/:181/:199 와 같은 문장이다.

### 계수 정의와 reset
- `in` = `inputFrameCount_`. `AMediaCodec_queueInputBuffer` 가 성공한 뒤에만 오른다(:855–861). 그래서 **in=0 은 "실제 queue 0"이지 "수신·조립 0"이 아니다.** 본문 표의 "decoder 입력" 칸은 이 뜻으로 읽는다.
- `stale` = `staleFrameDropCount_`. sink owner 를 통과한 뒤 gate 가 거부한 수다(:180–192).
- `inGen` = gate 를 통과한 frame 의 세대다(:199).
- `deltaBeforeKey` 는 codec 이 생긴 뒤 첫 IDR 전에 버린 delta 수다(:217–229).
- reset 지점: `ResetForBindingLocked`(:248–261), `PrepareForWindowSelectionAs`(:579~, :591–594), `AbortWindowSelectionAs`(:605~, :617–620). 이번 선택의 reset 은 logcat `selection prepare localGen=1` 09:42:18.913 이다. `decoder unbound` 는 39.6 이후에야 나온다.
- ⚠️ `video_debug` 는 **값이 바뀔 때만** 기록된다(MainActivity.kt:6792–6796). 그래서 20.522→31.301 사이에 줄이 없는 것은 "그동안 값이 같았다"는 뜻이다. 정지의 증거로 쓰지 않는다. 본문에서도 그 근거로 쓰지 않았다.

### 단계별 분리 (앱 시각 UTC, Host 는 KST 이고 앱이 약 +0.2 s)
| 단계 | 근거(시각·thread) | 판정 |
|---|---|---|
| ① sink callback 도달 | 18.965 tid14121 `dropped stale frame streamGen=1 expected=0 pendingSel=1 drops=1`. 19.573 tid14121 `updated codec config csd0=37 csd1=8`(gen-2 IDR 의 SPS/PPS, gate 통과 뒤 :202). 30.650–38.910 tid14121 delta drop 1→121 | 도달함 |
| ② 선택 gate | `stale` 은 18.913 reset 뒤 19.048=1, 20.522=8. **그 뒤 39.579 까지 8 그대로**다. 거부된 것은 선택 직전 세대 gen 1 이다(18.965 줄). gen 2 는 19.573 이전에 통과했다(20.522 `inGen=2 expectGen=2`). 통과 실행 ke_r19 에도 stale=3 이 있었다. | 정상 동작. 막힌 곳이 아니다 |
| ③ IDR/codec 준비 | 20.522 `surface=off codec=off codecs=0`. sharpen 대기(20.039 start → 23.627 `start_timeout`) 동안 decoder 에 surface 가 없었다. 23.609 `video surface cleared`. codec 생성은 26.675 `allocate` → 29.575 `Surface configure completed`(main thread 13691 의 `MediaCodecList` 26.409 부터). 31.301 에야 `codecs=1 surface=on codec=on`. 그 사이 gate 를 통과한 gen-2 frame 은 :206–210 에서 codec 없이 반환됐다. IDR 이면 `KeyLost`(:209)로 다시 요청 대상이 됐다. | **여기서 막혔다(약 19.6–29.6, 10 s)** |
| ④ 실제 queue | `in=0`, `inDeq=0` 이 끝까지 그대로다. codec 이 생긴 뒤에는 delta 만 왔다. `awaitsKey=1`, `deltaBeforeKey` 7(31.301) → 121(39.579). | 0 |

- Host 가 받은 `keyframe-request` seq 1–6(18:42:18.875 … 24.030, 약 1 s 간격)은 ③ 구간에서 IDR 이 codec 없이 계속 `KeyLost` 된 것과 **맞는다**. Host 는 매번 IDR 을 보냈다. 마지막은 `[keyframe] seq=163` 18:42:24.041 이고, 그 뒤 Host IDR 은 없다.
- 그 마지막 IDR 이 앱에 닿은 시점(≈ 앱 24.2)은 surface 를 비운(23.609) 뒤이고 codec 생성(26.675) 전이다. 그래서 codec 이 생긴 뒤 들어온 것은 delta 뿐이었다.

### "gate 거부(stale 8 / inGen 2)"와 "lock 대기 → control 정지"를 함께 설명하면
- 둘은 **다른 단계, 다른 시각**의 일이다.
  - stale 8 은 선택 직후(≤20.522) 이전 세대 gen 1 을 정상으로 거른 것이다. inGen 2 는 새 세대가 gate 를 통과했다는 것이다. **stale 8 은 정상 필터링으로 구분한다.** 다만 이 값만으로 gate·rearm·회복 논리 전체를 배제하지는 않는다(회복은 r2 축 2 대조로 확인).
  - 영상이 0 인 직접 이유는 ③ 이다. sharpen 시작 대기와 느린 codec 생성 때문에 약 10 s 동안 codec 이 없었다. 그 사이 IDR 이 모두 `KeyLost` 됐다.
  - control 정지는 그다음 일이다. Host 가 IDR 을 더 보내 주려면 control(keyframe-request, ack)이 살아 있어야 했는데, 그것이 ③ 의 뒷부분에 끊겼다.
- control 정지 원인에 대해 확인된 것은 하나다. main thread 가 decoder lock 을 쥔 채 codec 을 만든 구간(26.409–29.575)이다. 이 구간에 수신 스레드(control 담당)와 worker(:473–474)가 같은 lock 을 쓴다. 이것은 **추론**이다.
- **설명되지 않는 구간**이 둘 있다.
  - Host 재전송 규칙(24회 초과)으로 역산하면, 응답받지 못한 head 메시지는 Host 기준 약 18:42:24.98 에 처음 나갔다. 그런데 lock 대기가 확인되는 것은 26.409 부터다. ≈25.0–26.4 의 무응답은 이 자료로 설명되지 않는다.
  - 수신 스레드가 30.650 에 다시 움직여 delta drop 으로 keyframe 요청을 세웠을 텐데, Host 는 31.227 전에 `keyframe-request seq=7` 을 기록하지 않았다. 원인을 모른다(대기열 처리 지연, worker 정지, limiter 중 하나일 수 있다).

### control 과 video 의 마지막 정상 시점 (식별자·방향·clock)
| 방향 | 마지막으로 확인된 것 | 출처·clock |
|---|---|---|
| 앱→Host control | `keyframe-request seq=6 total=6` 수신 18:42:24.030 | Host log, KST(Host 수신 기록, 앱 송신 기록 아님) |
| Host→앱 control | 응답받지 못한 head 의 식별자는 Host log 에 없다. 첫 송신 ≈18:42:24.98 은 udp_control_channel.hpp:74–75, .cpp:432–436 으로 **역산한 값**이다 | 추론 |
| Host→앱 video(송신) | `[keyframe] seq=163` 18:42:24.041. `wire seq=330 key=0` 18:42:31.175. wire log 는 표본이다(114줄) | Host log, KST. **송신 기록일 뿐 전달의 증거가 아니다** |
| Host→앱 video(앱 처리) | gate 통과의 마지막 확인은 `deltaBeforeKey=121` 09:42:39.579 다(Host 정지 31.227 보다 뒤, 대기열). codec 에 들어간 frame 은 0 이다 | 앱 log, UTC |
| relay | 세션 합계만 있다(`c2h=155 h2c=927`). 시각과 순번이 없어 구간 대조에는 쓸 수 없다 | fixture log |

- 결론 수정: 본문 핵심 2번의 "lock 대기 → control 정지"는 control 이 끊긴 이유에 대한 **부분 설명(26.4–29.6 구간)**으로 낮춘다. 영상이 decoder 에 0 이 된 직접 단계는 ③(sharpen 대기 + 느린 codec 생성으로 약 10 s 동안 codec 없음 → IDR 모두 KeyLost)이다.
- 같은 peer_lost 모양이라는 것만으로 r21 과 원인이 같다거나 relay 결함이라고 하지 않는다(본문 핵심 4번).

## ke_r21 정정 (Codex codex_apk_decoder_liveness_design_r1.md 반영, 재실행 0)
- 본문 핵심 4번의 "ke_r21 peer_lost 는 E7g 의도된 탈취뿐"을 **철회**한다.
  - 29/29 는 그때까지 수행한 assert 의 집계다. 실행 자체는 `to_list('E7e B')` 의 `RuntimeError: menu button not reached: viewerMenuHostsButton`(avd_stdout.txt, ke_avd.py:574→286→276)으로 중단됐다.
  - A 의 E7g peer_lost(앱 23:27:34)는 의도된 탈취 종료다. 이 부분은 유지한다.
- **B 의 별도 종료(의도되지 않음)**:
  - Host b 기록(KST 표기, 앱보다 약 −2 s):
    - 08:32:06.070 `session epoch=5 peer=same (standby activation)`
    - 14.956 `window-select applied streamGen=6`
    - 16.216/16.815 keyframe-request seq 1–2
    - 그 뒤 control 기록이 없다.
    - 27.075 IDR seq=4788 송신
    - **27.249 `udp control session ended epoch=5 reason=peer-lost`**(directoryhost_host.log:2356)
    - abr-trace 는 08:32:20 에 `clientReports=38 clientReportAgeMs=81783` 이다. 이 세션 동안 클라이언트 보고가 새로 오지 않았다.
  - 앱(emulator 시계):
    - 23:32:19.399 `select_ready ms=2457`
    - 그 뒤 decode 가 정상이었다: `video_debug in=13→93 out=12→93`(20.8–28.7). 수신 경로 thread 32140 이 `queued h264 frame count=91`(28.048)과 `released output frame`(28.104)을 기록했다.
    - 29.564 `[keep_end] handle=15 host=ke-pc-b reason=peer_lost` → HOSTS.
    - 하네스는 그 뒤 메뉴를 3회 찾지 못하고 중단했다(viewer 가 이미 닫혀 있었다).
- 판정:
  - ke_r21 B 는 **영상이 decoder 에 정상 queue 되는 중에 control 만 peer-lost 된 경우**다. ke-r23 처럼 codec 이 없던 구간은 없었고, 수신 경로 thread 도 그 시각에 움직이고 있었다. 그래서 ke-r23 의 "codec 준비 지연" 사슬로는 **설명되지 않는다**.
  - 두 사건의 원인이 같다고 하지 않는다. 다르다고 확정하지도 않는다(공통 요인 미배제).
  - B 는 standby activation 으로 열린 세션이다. control 과 video 가 같은 socket·같은 loop 를 지나는지는 아직 확인하지 않았다. B 의 후속 종료는 **미분류**로 둔다.
- relay 표현 정정: forward 코드와 합계 계수만으로는 모든 IDR/chunk 가 제때 전달됐다거나 relay/UDP/fixture 의 기여가 없었다고 **확정하지 않는다**. "relay 가 전체 영상을 막았다"는 가설은 AU/delta 가 sink 에 도달한 것과 맞지 않는다. 그 이상은 말하지 않는다. 수신 loop 정지 때 socket backlog/drop 이 있었을 가능성도 구간 결과에 포함한다.
- 인과 표현 정정: 제품의 구조적 취약점(codec 작업과 control 진행이 같은 lock·loop 에 묶임)은 source 로 확인된다. 그러나 "6 s 재생성 = RX 의 lock 대기 6 s" 의 직접 증거는 없다. 유력한 인과 가설로 둔다.
