# keepalive-r2(e) / apk-decoder-liveness r1 — ② SH-off E2 결과 + ① 설계 요지

정본: `.claude/codex_apk_decoder_liveness_design_r1.md`. 조사 보고는 `report_e2_relay_investigation.md`(r21 정정 절 포함, sha256 `94f54e24…`).

## ② 현재 boot 의 SH pipeline-off E2 (실행 2회: 1회 INVALID, 1회 유효)

### 고정
- 코드: feat-apk-0233 HEAD `9f9a91c`, tracked clean.
- artifact(실행 직전 재확인, 모두 일치): job `DC4C5049`, pinned Host `D806E375`, 하네스 `722BF981`, APK off `8C6422FE` / on `64A02118`.
- 스크립트: r7 snapshot `.claude/caplife/avd_guard_r7_snapshot/`(읽기 전용 + SHA256SUMS). r6 대비 바뀐 것은 **ke_avd.py 하나**다(`ed820566…` → `854743873e618259af661b80bf0a60f31f79f0ccd086a8427d8a5f8db34d53e0`).
- 추가된 것:
  - `KE_PLAN=e2-shoff` 일 때 E1 다음에 `e2_shoff()` 만 실행한다. 기본값 `full` 은 기존 흐름 그대로다. 모르는 값이면 RuntimeError 를 낸다.
  - `env_note()`: uptime, loadavg, boot_id 를 기록만 한다.
  - E3–E13b 는 NOT RUN 으로 따로 기록한다(PASS 에 넣지 않는다).
- ke_slot, fixture, job, guard, 슬롯은 변경하지 않았다. 호출은 ke-r23 과 같다(`sh_run.ps1 -NativeExe powershell … ke_slot.ps1 -Tag …`). 실행 환경에 `KE_PLAN=e2-shoff` 만 더했다.
- clipboard 를 바꾸는 단계는 없다(결정 ① c 유지).

### SH pipeline off 의 정의와 실제 상태
- 정의: 앱 Settings 의 `settingsSharpOffButton`(`SharpnessLevel.OFF`)이다. 이것은 decoder 가 TextureView 에 바로 쓰는 경로이고, GL 을 s=0 으로 돌리는 것이 아니다(SharpnessLevel.kt:13–14). 강도 0 이나 AUTO_PREPARE 와는 다르다.
- 실제 상태(앱 log):
  - `[sharpness_select] level=off s=0.0 origin=settings path=NONE effective=off`
  - 선택 때 `[sharpen_path] path=direct requested=off effective=off fallback=none`
  - `sharpen_start` 0건
- 순서: 로그인 → A 카드 → Targets → Settings 탭에서 끄기 → Windows 탭 → 대상 선택. ke-r23(곧바로 선택)과 비교하면 연결과 선택 사이에 약 40 s 의 Settings 조작이 들어간다.

### 실행 1 — ke-r24-shoff: **INVALID**(기능 0)
- run 폴더: `runs\ke-r24-shoff-20261029192304-36656`
- `slot error: * daemon not running; starting now at tcp:5037` 가 났다. 실행 직전 확인과 실행 사이에 adb 서버가 내려가 있었다. ke_slot 의 에뮬레이터 확인(ke_slot.ps1:110–112)이 adb 의 안내문을 오류로 받아 AVD 단계 전에 멈췄다.
- fixture: `job_run: child_exit=1 residual=0 forced=0 ended_confirmed=1`. 기기 조작은 0 이다.

### 실행 2 — ke-r25-shoff: E2 첫 화면 **PASS**. 그 뒤 세션이 **client 쪽 peer_lost** 로 끝남
- run 폴더: `runs\ke-r25-shoff-20261029192425-17816`. bundle rc 3(fixture 1, avd 1).
- 결과:
  - E1 2 PASS
  - SH-off 설정 PASS
  - **E2 A selection → first frame 498 ms PASS**
  - direct 경로만 사용 PASS
  - 그다음 `to_list('E2')` 에서 `RuntimeError: not on screen: viewerMenuButton` 이 났다. 그 시점 viewer 는 이미 닫혀 있었다(exception.png = My PCs, "Connecting to ke-pc-a…").
  - E2 의 PC 목록 판정 2개는 실행되지 않았다. `run end: 5/5 PASS` 는 그때까지의 assert 집계일 뿐이다.
- Host a OFF 판정과 job 잔여는 정상이다(`residual=0 ended_confirmed=1`, leftover 0). 슬롯은 자기 파일만 제거했다(`slot removed=True`).
- **세션 종료 원문**:
  - 앱 log 는 실행 뒤 기기의 파일을 읽기만 해서 가져왔다(`avd/app_after_run.log`). 이유: 하네스의 logcat 수집이 10:26:03 에 끊겼다(adb 서버가 다시 내려감).
  - 10:26:35.330 `select_ready ms=498` → 10:26:36 VIEWER.
  - `video_debug in=4→255 out=3→252` 가 10:26:36–10:27:00.967 동안 이어졌다. decode 와 출력이 계속됐다.
  - 10:26:46.829 한 번 `codec=off`(transport discontinuity 재설정), 48.773 다시 on.
  - **10:27:01.628 `[keep_end] handle=1 host=ke-pc-a reason=peer_lost`** → HOSTS.
  - Host a(KST, 앱 UTC 와 약 0.2 s 차):
    - `keyframe-request` seq=1 19:26:32.462, seq=2 46.484(+ throttled 1).
    - `clientReports` 3회: 40.8 무렵, 46.4 무렵, **52.4 무렵**. 그 뒤 `clientReportAgeMs` 가 9409 ms 까지 늘었다.
    - **19:27:02.584 `udp control session ended epoch=1 resumed=0 reason=none`**. Host 쪽에서는 peer-lost 가 아니다. 클라이언트가 먼저 닫았다.
  - Host 송신은 `sentFrames` 21–27/s 다. 앱 queue 는 약 10/s 다(in 4→255 / 약 25 s). 앱이 처리한 수가 송신의 절반 이하다. 손실인지 앱 쪽에서 버린 것인지는 이 자료로 가릴 수 없다.
  - relay: 세션 줄은 `active; helloAck in 2ms` 까지 있다. fixture 가 60 s idle 전에 끝나 닫힘 줄과 합계가 없다.
- 판정:
  - **SH-off 에서는 ke-r23 의 실패 단계(③ codec 준비 10 s 지연 → 첫 frame 0)가 재현되지 않았다.** sharpen `start_timeout` 경로가 ke-r23 의 첫 frame 실패를 유발한 계기였다는 가설과 맞는다. 단 1회이고, 같은 boot 안에서도 시각에 따른 부하가 다르다. 그래서 trigger 분리의 참고일 뿐 liveness 수정의 증거가 아니다.
  - **새 사실**: SH-off 에서도 영상이 decode 되는 중에 **control 만 끊겨 client 가 peer_lost** 로 끝났다. ke_r21 B(영상 정상, control peer-lost)와 같은 모양이다.
    - ke-r23 과 달리 decoder 정지 구간은 없었다(in 이 계속 증가).
    - Host 가 마지막으로 받은 클라이언트 신호는 report(≈19:26:52.4)다. Host 는 남은 미응답 메시지가 없어 peer-lost 를 내지 않았다.
    - 따라서 **client→Host 방향(c2h) 제어가 약 19:26:55 부터 Host 에 닿지 않았거나, Host 의 ACK 가 client 에 닿지 않았다.** 어느 쪽인지는 relay 의 방향별·시각별 계수가 없어 가릴 수 없다.
    - 이번 자료에 이것과 decoder lock 을 잇는 근거는 없다. relay/emulator NAT/UDP 손실 가능성도 배제하지 않는다.
- 부하·노출 정규화:
  - 에뮬레이터 emulator-5554 = `gnlink_kbd_test`(`-no-window -no-audio -no-boot-anim -no-snapshot-save -gpu swiftshader…`), Android 14 `sdk_gphone64_x86_64 UE1A.230829.050 userdebug`.
  - boot_id `37551b86-0677-429f-85dd-d893965da481`. uptime 196286 s(약 54.5 h, ke-r23 과 같은 boot).
  - APK `versionCode=31 versionName=0.2.32`(ke-off.apk 의 내장 이름).
  - loadavg(1/5/15 분): 실행 전 유휴 0.62/0.76/0.95(19:22). E2 직전 5.45/2.33/1.47. E2 직후 5.36/2.84/1.70.
  - logcat 은 E2 구간(10:26:31–10:27:02)을 **덮지 못했다**(10:26:03 에 끊김). 그래서 1 s 이상 dispatch 수와 EGL 지연은 이 구간에 대해 **없음**이다.
    - 참고로 실행 앞부분(로그인~설정)에서 1 s 이상 slow dispatch 는 0건, EGL app_time_stats p50 615 ms.
    - ke-r23 은 같은 지표 p50 676 ms, 1 s 이상 24건이다. 하지만 관찰 길이가 서로 달라 원시 개수를 비교하지 않는다.
- **adb 서버 불안정(환경)**:
  - 19:22 확인 때는 정상이었다. 19:23 run 직전, 19:23:4x, 실행 중(logcat 10:26:03 끊김), 실행 후 19:28 무렵에 `daemon not running` 이 반복됐다.
  - 이 PC 에는 SDK adb 외에 `C:\LDPlayer\LDPlayer9\adb.exe`, `C:\LDPlayer\OSLink\bin\adb.exe`, Desktop 의 `platform-tools-latest` adb 가 있다. 서로 다른 버전의 adb 가 서버를 끊고 다시 띄우는 경우일 수 있다(**가능성**, 확인 안 함).
  - 처음 확인 때 보였던 `emulator-5562`, `127.0.0.1:5563` 은 서버가 다시 뜬 뒤 목록에서 빠졌다. 나는 adb kill-server 를 실행하지 않았다.
  - 이 불안정은 하네스 단계 실패와 logcat 끊김의 원인이 될 수 있다. 영상과 제어 UDP 는 adb 를 지나지 않는다.

### 재시작 판단
- 아직 하지 않았다. 이유:
  - SH-off 에서 첫 frame 은 통과했다.
  - 반면 제어 peer_lost 라는 별도 증상이 나왔다.
  - 에뮬레이터 재시작으로는 adb 서버 불안정과 relay 경로 의문이 풀리지 않는다.
- 다음 비교를 의미 있게 하려면 **fixture(시험) 쪽 relay 의 방향별·시각별 계수**(c2h/h2c 마지막 수신 시각, 1 s 단위 계수)가 먼저 필요하다고 본다. 시험 쪽만 바꾸는 계측이다. 다만 OS 효과나 경계를 바꾸지 않더라도 fixture 서버 동작을 바꾸는 것이므로 SOURCE 확인 경로를 따른다.
- 재시작이 필요하다고 정해지면 계약대로 한다: identity·유휴·슬롯 독점 확인, 같은 userdata 로 정상 stop/start, wipe·삭제 없음.

## ① 설계 요지 (구현 전, 9f9a91c 기준)

### 지금 같은 mutex `mu_` 를 쓰는 진입점 (android_video_decoder.hpp:174–186 `LockedState`)
| 호출 thread | 진입점(호출 위치) | sink 안에서 하는 일 |
|---|---|---|
| RX(VideoReceiveMain. control OnPacket/Tick 도 맡음, session.cpp:1482/:1500) | `OnEncodedH264Frame`(session.cpp:1363) | gate, csd, **EnsureCodec(create/configure/start)**, Pump(dequeue/queue/release)(decoder.cpp:173–243, 704–883) |
| RX | `ConsumeDecoderKeyframeRequest`(:1366), `OnVideoDiscontinuity`(:1376 → ResetCodec stop/delete) | |
| session worker(control 처리) | `KeyframeOwedFor`(:924), `OnWindowSelectionControlResultFor`(:1117), `ConsumeDecoderKeyframeRequest`(:1120), `DrainPresentationStats`(:872), `OnDecodeEpisodeStart`(:844), `OnVideoStreamReset`(:763, :1831 → ResetCodec), `PollLocalSelection` → `CommitLocalSelectionFor`(:346) | 모두 `mu_` |
| UI/JNI | `SetSurface`(native_bridge.cpp:642 → ResetCodec + **EnsureCodec**), `Prepare/AbortWindowSelectionAs`(:683/:692 → ResetCodec), `DebugStatus`/`VideoSizePacked`/`ReadySelectionGeneration`/`LastOutputPresentationUs`(:670–708, 뒤의 셋은 **PumpCodecLocked**), `Bind/Unbind`(:121–122 → ResetCodec), `RequestWindowSelect` 등 → `CurrentSelectionTag`/`BeginLocal`/`Refuse`(session.cpp:229–319) | |
| output pump thread | `OutputPumpMain` 3 ms 마다 `DrainOutputLocked`(decoder.cpp:125–136) | |
- 이미 lock 없이 읽는 것: `DrainDecodeStats`/`DecodeGeneration`(seqlock snapshot, :440–471), `HandoffInfoForTimestamp`, `CodecEpoch`, `NotifyFrameDisplayed`.

### 바꾸는 구조 (Android sink 한정. session.cpp 와 공통 수신 모델은 그대로)
1. **codec 소유 worker 하나**: 지금의 output pump thread 를 codec worker 로 바꾼다.
   - 이 thread 하나가 `codec_`, `window_`, `pendingFrame_`, `bootstrapFrame_`, `playoutClock_`, held output, `codecAwaitsKey_`, csd 를 소유한다.
   - **모든 AMediaCodec_* 호출과 ANativeWindow_release 는 이 thread 에서만** 한다.
2. **상태 lock `stateMu_`(짧은 구간 전용)**:
   - 담는 것: binding_, gate_(SelectionAckGate), readySelectionGeneration_, decoderKeyframeRequest_, 선택 tag, 화면 보고용 계수(in/out/stale/inGen/outGen/size/codec 상태), presentation gap 창, 세대(epoch) 셋, bounded inbox(frame queue + 명령).
   - 규칙: **이 lock 을 쥔 채 codec API·JNI·대기·sleep·join 을 하지 않는다.**
   - worker 의 순서: lock 안에서 결정 → lock 밖에서 codec 호출 → lock 안에서 결과 반영(세대 재확인).
3. **RX `OnEncodedH264FrameAs`**:
   - `stateMu_` 안에서 owner 확인, gate `Admit`(stale 계수와 inGen 갱신은 지금과 같은 의미), 세대 tag 부착.
   - bounded queue 에 넣고 worker 를 깨운 뒤 바로 반환한다. codec 은 기다리지 않는다.
4. **bounded queue**(상한 8 frame / 4 MB 를 제안. 값은 구현 때 근거와 함께 확정):
   - 가득 차면 가장 오래된 delta 부터 버린다.
   - IDR 을 버리게 되면 `gate_.KeyLost` 와 keyframe 요청 플래그를 세운다. 지금의 "codec 없음 → KeyLost"와 같은 의미다.
   - surface 나 codec 이 준비되지 않은 상태는 worker 가 지금 규칙대로 처리한다(:206–210 KeyLost, :217–229 deltaBeforeKey).
   - **IDR 의 빚은 enqueue 로 갚지 않는다.** 지금처럼 `queueInputBuffer` 성공 뒤(:865–868)에, worker 가 `stateMu_` 안에서 세대가 그대로일 때만 `KeyDelivered` 한다.
5. **control hook**:
   - 짧은 `stateMu_` 구간만 쓰는 것: `ConsumeDecoderKeyframeRequest`, `KeyframeOwedFor`, `CurrentSelectionTag`, `OnWindowSelectionControlResultFor`, `Begin/LocalBound/Refuse/CommitLocalSelectionFor`, `DrainPresentationStats`, `OnDecodeEpisodeStart`.
   - `Prepare`, `Abort`, `OnVideoStreamReset`, `OnVideoDiscontinuity`, `Bind`, `Unbind`: gate·세대·계수를 즉시 바꿔 옛 출력의 게시를 바로 막는다. codec reset 은 **worker 에 명령으로만** 넘기고 기다리지 않는다.
   - `DebugStatus` 등 UI 조회는 mirror 만 읽는다. `PumpCodecLocked` 를 부르지 않는다(지금 :686/:694/:700 의 동기 pump 제거).
6. **세대와 수명**:
   - `bindingEpoch`, `surfaceEpoch`, `selectionEpoch` 를 frame, 명령, 출력 결과에 붙인다. worker 가 결과를 반영할 때 세대가 바뀌었으면 버린다.
     - 늦게 끝난 codec 생성은 새 owner/surface 에 붙지 않고 즉시 reset 대상이 된다.
     - 옛 selection 의 첫 출력은 `readySelectionGeneration_` 을 올리지 않는다.
   - ANativeWindow: UI 가 `ANativeWindow_fromSurface` 로 ref 를 얻어 명령에 담아 넘긴다. 옛 window 는 worker 가 codec stop 뒤에 release 한다.
   - 소멸자: stop 플래그 → worker join(lock 밖) → 남은 codec·window 해제.
   - 시험 seam 의 detached thread(:1100–1106, 출하물에 없음)는 그대로 두고 별도로 표시한다.
7. **codec 실패 보고**:
   - worker 가 codec 상태(none/creating/started/failed + 이유, codecEpoch)를 mirror 에 게시해 `video_debug` 에 보인다.
   - control session 의 진행과는 무관하다. 재전송과 peer-loss 예산은 바꾸지 않는다.

### 세대·수명 표
| 객체 | 소유 thread | 만드는 곳 | 끝내는 곳 | 늦은 결과 차단 |
|---|---|---|---|---|
| `AMediaCodec*` | codec worker | worker 의 Ensure(csd+surface+IDR) | worker 의 Reset(명령: surface/선택/binding/stream reset/discontinuity/실패) | 생성 시작 때 (binding, surface, selection) 세대를 기록하고 start 직후 대조한다. 다르면 즉시 stop/delete |
| `ANativeWindow*` | UI 가 ref 를 얻어 명령으로 worker 에 넘김 | `SetSurface(non-null)` | worker(codec stop 뒤 release), 소멸자 | `surfaceEpoch` |
| queue 안의 frame | RX → worker | `OnEncodedH264FrameAs` | worker 가 queue 하거나 버림 | (binding, selection) 세대 + gate `PermitsQueue` 재확인(지금의 DecoderPendingSlot 규칙) |
| gate / keyframe 빚 | `stateMu_` | Prepare/Ack/BeginLocal | Abort/Reset/Unbind | `KeyDelivered` 는 queue 성공 + 세대 동일일 때만 |
| output(held buffer) | codec worker | dequeueOutput | release/Reset | `codecEpoch` 가 다르면 release 하지 않는다(지금 :929–931 규칙) |
| decode snapshot | worker 가 publish | `PublishDecodeState` | — | binding 이 들어간 일관된 copy(지금 그대로) |

### 열린 결정 (Codex 확인 요청)
- **D1 surface detach 의 동기성**:
  - Kotlin 은 `nativeSetSurface(null)` 이 "codec stop 과 window release 가 끝난 뒤 반환"이라고 가정한다(MainActivity.kt:6192–6198 주석). sharpen renderer 가 같은 BufferQueue 에 producer 로 붙기 전에 필요한 조건이다.
  - 선택지:
    - (a) **UI 만** worker 의 detach 완료를 기다린다. RX·control 은 기다리지 않는다. UI 의 대기는 지금과 같은 크기이고, 기록한다.
    - (b) 제한 시간 대기 뒤 실패를 반환하고, Kotlin 이 ProducerStuck 처럼 다룬다(Kotlin 변경 필요).
  - 제안은 r1 에서 (a)다. 계약 1 이 "UI 진입 경로"도 적고 있어 확인을 받는다.
- **D2 queue 상한과 overflow 정책**: 8 frame / 4 MB, 오래된 delta 먼저, IDR 을 버리면 KeyLost.
- **D3 범위**: session.cpp 는 바꾸지 않는다. RX 는 지금처럼 sink 를 동기 호출하지만 sink 가 즉시 반환한다.

### ③ 결정적 회귀 (구현과 함께)
- 기반: 이미 있는 **stand-in NDK**(`src/test_fake_ndk/fake_ndk.cpp`)로 **제품 `android_video_decoder.cpp` 를 Windows 에서 그대로 build** 한다(remote60_android_decoder_binding_test 와 같은 방식, CMakeLists.txt:1116–1126).
  - barrier 는 그 **fake codec 의 create/configure/start/stop** 안에 둔다. 제품 코드에 seam 이 없고, 출하물에도 없다.
- 대조 1(component, 실제 sink):
  - fake codec create 를 8 s 붙잡은 동안 다음을 동시에 돌린다: RX thread 30 fps frame 투입, control thread 의 hook 반복 호출, UI thread 의 SetSurface 교체.
  - 각 호출의 최대 지연이 상한(예: 50 ms) 아래여야 한다.
  - barrier 해제 뒤 같은 owner·surface·선택에서 새 IDR 로 첫 출력이 나와야 한다.
  - surface 교체·없어짐, owner 교체, 선택 취소, queue 포화, codec 실패, teardown 에서 옛 생성 결과나 frame 이 새 owner 에 나타나지 않고, queue·자원이 쌓이지 않아야 한다.
- 대조 2(제품 수신/control 경로): 실제 `ClientSessionController`(UDP control) + 실제 sink(fake NDK) + loopback fake host.
  - fake host 가 video 와 신뢰 control 을 보내는 동안 codec barrier 를 8 s 건다. 재전송 예산 24×250 ms = 6 s 를 넘는 값이다.
  - control ACK/probe 가 계속 진행되고 양쪽 peer_lost 가 0 이어야 한다. 정상 control 대조와 최소 지연 시간을 기록한다.
  - 기존 udp_control_e2e_test / client_udp_recv_loop_test 의 fake host 를 재사용할 수 있는지 구현 때 확인한다.
- **음성 대조**:
  - 같은 시험을 9f9a91c 의 sink(지금 구조)로 돌리면 대조 1 의 지연 상한과 대조 2 의 peer_lost 0 이 **FAIL** 해야 한다.
  - mutant 2종도 FAIL 해야 한다: worker 결과 반영에서 세대 대조 제거, queue 상한 제거.

## 구현 결과 (① + ③) — SOURCE 요청
- commit: **`27d59665bb41cb78ebbfc6abc2bcea5c3595c211`**
  - branch `fix/apk-decoder-liveness`, worktree `C:\work\gnlink-wt\apk-decoder-liveness`, base `9f9a91c`(feat-apk-0233). tracked clean.
  - 범위: 제품 2(`android_video_decoder.cpp/.hpp`), 시험 10(CMakeLists, fake NDK 2, 기존 decoder 시험 4, 새 시험 2, SettledSink 1), 문서 0.
- 제품 sink 해시: cpp `f9e1a90b…fd76`, hpp `8ce7de00…b334`
- 증거: `.claude/caplife/liveness_evidence/`
  - 실행 출력 6개(exe sha256·exit 포함)
  - `SHA256SUMS.txt`: sink, release .so 4종, APK, 음성 입력, 출력
  - 음성 입력: `liveness_neg/old`(9f9a91c 원본), `m1`·`m2`, `make_mutants.py`

### 설계 요지와 달라진 점 (명시)
- **세대 셋 → 실제는 둘**: `codecResetSeq_`(codec 을 무효화하는 모든 요청 = surface·binding·selection·stream reset·discontinuity) + `surfaceSeq_`(surface 인계·detach 완료 대기용).
  - owner 는 지금처럼 진입 시 `binding_` 토큰으로 거른다.
  - selection 은 gate(`Admit`/`PermitsQueue`)가 판정한다.
  - 늦은 결과 차단은 모두 "결과 반영 시 `codecResetSeq_` 가 그대로인가" 한 번의 비교로 한다(Ensure 결과, queue 뒤 빚 처리, output 게시와 ready 표시).
  - 세대를 셋으로 나누지 않은 이유: 셋 다 결과가 같기 때문이다(codec 폐기). 비교 지점을 하나로 두면 빠뜨릴 곳이 줄어든다.
  - mutant m1 이 바로 이 비교를 전부 지운 것이다. 이때 3 FAIL([2] 옛 surface 에 frame, [4] 옛 owner 에 frame, [5] 새 선택이 옛 picture 로 ready).
- **inbox 상한 근거**: 8 frame 은 30 fps 에서 약 266 ms 다. codec 이 그보다 오래 멈추면 오래된 frame 을 버리고 다음 IDR 을 기다리는 쪽이 낫다(어차피 그 delta 들은 늦은 화면이다). 4 MB 는 IDR 2~3개(1080p 고화질 기준 수백 KB~1.5 MB)가 들어가는 크기다. 메모리 상한이 된다.
- **keyframe 요청 시점 변화**: 요청 플래그(`decoderKeyframeRequest_`)를 이제 worker 가 비동기로 세운다.
  - RX 의 즉시 소비(session.cpp:1366, frame 을 넘긴 직후)는 대개 **다음 frame** 때 그 플래그를 본다(30 fps 기준 약 33 ms 뒤).
  - 선택 응답 경로(session.cpp:1120)와 빚 경로(KeyframeOwedFor, :924)는 그대로다.
  - 플래그는 소비될 때까지 남으므로 요청을 잃지 않는다. frame 이 전혀 오지 않으면 다음 frame 이나 control 경로를 기다린다.
- **D1 (a) 적용**: `SetSurface(null)` 만 worker 의 detach 완료를 기다린다. 50 ms 이상이면 `video surface detach waited ms=…` 를 기록한다. UI 대기 길이는 기존과 같은 크기(느린 기기에서 ANR 노출 동일)이고, 이제 기록된다.
- worker 는 codec 이 없어도 3 ms 마다 깨어난다. 기존 pump 도 시작 후에는 같았다. 비용은 작지만 개선 여지로 남긴다.

### 결과 (Windows, 제품 sink + stand-in NDK; 숫자는 exit code 와 마지막 요약 줄로 확인)
| 시험 | 새 sink(27d5966) | 9f9a91c sink | m1 | m2 |
|---|---|---|---|---|
| android_decoder_liveness_test (hold 8 s) | **35/35 PASS ×5**, exit 0 | **19 FAIL**, exit 1 (RX·control·UI 최대 12 s 정지, codec 을 RX/UI thread 가 호출) | 3 FAIL | 3 FAIL([1]/[6] inbox 224·201, 빚 미발생) |
| android_decoder_session_liveness_test (hold 8 s, 제품 VideoReceiveMain + 양쪽 UDP control, 제품 timings 6 s 예산) | **9/9 PASS ×5**, exit 0. hold 동안 control 최대 공백 252 ms(양방향), peer-lost 0 | **4 FAIL**, exit 1. control 8 s 동안 0건, **Host peer-lost**(ke-r23 과 같은 모양) | — | — |
| binding / owed_key / geometry / decode_stats (SettledSink) | 17/17, 32/32, PASS, PASS 각 ×5 | — | — | — |
- 대조 1 의 경우 목록: [0] 기준 · [1] create 8 s · [2] surface 교체 · [3] detach · [4] owner 교체 · [5] 새 선택 · [6] 포화 · [7] 생성 실패 · [8] teardown.
- "codec threads=2" 는 [0] 과 [1] 의 서로 다른 sink 의 worker 둘이다(누적 집합). 판정 대상은 "호출자 thread 가 codec 을 부른 적 없음"이다.
- **한계**: 두 시험 모두 hold 뒤의 IDR 은 송신 쪽의 주기 IDR(1 s)에서 온다.
  - sink 의 IDR 재요청이 Host 까지 가서 새 IDR 로 돌아오는 경로는 이 harness 에서 끝까지 돌지 않는다(session worker 송신 없음).
  - 요청 플래그가 서는 것은 대조 1 [6] 에서 확인했다(`ConsumeDecoderKeyframeRequest` true).

### APK build
- `assembleDebug` / `assembleRelease` / `assembleDebug -Pgnlink.shTestSeam=true`, 4 ABI, 모두 BUILD SUCCESSFUL.
- release stripped .so 4종: `decoder inbox full` 문자열 있음(새 코드), `WaitIdleForTest` 없음(시험 hook 미포함).
- **기기·AVD 실행은 하지 않았다**(SOURCE 뒤 EVIDENCE). ke-off/ke-on 시험 APK 도 아직 만들지 않았다.

### 범위 (이 수정이 다루지 않는 것)
- 이 수정은 **ke-r23 의 메커니즘**(공유 lock 위의 codec 작업 → control 정지)을 없앤다.
- ke-r25(SH-off)와 ke_r21 B 의 "**영상은 decode 되는데 control 만 peer_lost**"는 설명하지 않는다. 그 두 경우에는 decoder 정지가 없었다. 별도 미결이다.
- 미검증:
  - 실제 NDK/기기 동작(4 ABI)
  - SH on→off surface 전환
  - KA 복귀
  - 느린 기기에서 D1(a) UI 대기 길이
  - E2 실기
  - 시험 APK 버전 이름(0.2.32 내장)

## r2 (Codex codex_apk_decoder_liveness_design_r2.md) — (a) 측정, 회귀 두 축, 시험 계측
- commit **`ec016f0bb603d869e192e12c77cd320f5ddaabb5`**(27d5966 위, tracked clean)
  - 범위: 시험 2(`android_decoder_idr_recovery_test.cpp`, CMake), 계측 9, gate 1, 문서 0.
  - 계측 9: `client_liveness_trace.hpp`(새), session cpp/hpp, sink cpp/hpp, native_bridge, MainActivity, NativeSessionBridge, gradle.
  - gate 1: `gnlink_check_fixed_server.py` 표지 추가.
  - **출하 동작은 바뀌지 않는다**: 기록은 `GNLINK_LIVENESS_TRACE` 일 때만 한다. release 의 JNI 는 "" 를 반환한다. Kotlin 은 `BuildConfig.LIVENESS_TRACE=false` 다.
- 보고 정정: `report_e2_relay_investigation.md`
  - 맨 앞에 "현재 결론" 하나를 두고, 본문의 강한 단정(relay·fixture 원인 아님, 회귀 아님, control 정지 원인)을 보충 수준으로 낮췄다.
  - r21 B 는 미분류다. 미설명 2구간은 그대로 남겼다.

### (a) 측정 결과 — 정책 변경 없음
- owed 재요청은 1 s 간격 8회다(native_video_client_session.cpp:930–936). 식별자(`gate_.OwedToken()`)가 바뀌면 budget 을 초기화한다(:925–929). 식별자는 `KeyLost`(이미 owed 면 그대로)와 `Rearm`(surface 요청 때, decoder.cpp SetSurface)에서만 바뀐다.
- 축 2 결과: **준비 전 budget 소모는 실제로 일어난다**(U1 8회, U4 idle 10회). 그래도 준비 뒤 회복은 기존 장치로 이뤄졌다:
  - U1(surface 가 늦음): surface 요청 때의 `Rearm` 이 새 식별자를 만든다 → +5 ms 에 요청 → IDR queue +18 ms → 표시.
  - U2/U4(codec 생성이 늦음): 생성을 촉발한 IDR 은 worker 가 처리 중이고, 그 뒤 IDR 은 inbox 에 남아 있다. 준비 즉시 queue(+2 ms)되고 표시된다. 재요청이 필요 없었다.
- 따라서 r2 의 "(b) 를 고친 뒤 기존 (a) 가 정상 진행하면 추가 정책 변경은 불필요"에 해당한다고 판단해 **(a) 는 바꾸지 않았다**.
- 남는 경계(미검증): 아래 두 조건이 겹치는 경우에는 회복이 delta 경로나 다음 surface 에 달려 있다.
  - codec 생성 **실패**(KeyLost 뒤 owed 유지, 식별자 불변)
  - budget 소진 + idle host
- mutant m3(surface `Rearm` 제거)이 U1 idle 에서 회복 실패하는 것으로 그 의존을 확인했다.

### 회귀 두 축
| 축 | 시험 | 새 코드(ec016f0) | 9f9a91c sink | mutant |
|---|---|---|---|---|
| 1 slow lifecycle + control 지속 | android_decoder_liveness_test / android_decoder_session_liveness_test | 35/35, 9/9 (r1 그대로) | 19 FAIL / 4 FAIL(Host peer-lost) | m1 3 FAIL, m2 3 FAIL |
| 2 unready 에서 잃은 IDR → ready 뒤 회복 | android_decoder_idr_recovery_test (제품 session 전체: control loop + owed + limiter + UDP 수신, **control over video socket**, 지연 12 s) | **36/36**, exit 0 | **6 FAIL**, exit 1: decoder 가 묶인 동안 keepalive 가 Host 에 12–30 s 미도달(U2/U4/U3). U3 에서 대체된 세대의 IDR 이 queue 됨 | m3 **2 FAIL**(U1 idle 미회복) |
- 축 2 의 판정 기준(준비 시점부터): 현재 세대 IDR 이 codec 의 첫 input(<3.5 s), 그 picture 표시(selection ready), 양쪽 control 열림, 미준비 동안 클라이언트 keepalive 공백 <1 s. 요청 수(준비 전/후)를 함께 기록한다.
- fake host 에 대한 정정: 응답만 하는 fake 라서 양쪽 모두 재전송할 것이 없으면 peer-lost 가 드러나지 않는다(9f9a91c 에서 worker 까지 sink 를 기다리기 때문). 그래서 keepalive 도달 공백을 판정에 넣었다.

### ② 시험 계측 (다음 AVD 실행용, `-Pgnlink.livenessTrace=true`)
- 앱이 500 ms 마다 자기 thread(UI 아님)에서 `liveness_trace` 를 진단 로그(파일)에 남긴다. adb·logcat 이 끊겨도 남는다.
- 한 줄 구성(Windows 축 2 실행의 실제 줄은 evidence 에 있다):
  - `rx=<phase>/<ms> tid=`: 수신 thread 단계(wait/control/tick/assemble/deliver/keyconsume)와 그 시작부터의 시간
  - `dg= ctl= tick=`: 마지막 datagram / control packet / Tick 경과
  - `n=`: datagram/control 수, `dmax=`: 최장 sink 전달
  - `wk=<phase>/<ms> tid= pass=`: control thread 단계(pass/sinkhook/exchange)와 마지막 pass
  - `owed=token/attempts next±ms`, `kq/kr`: limiter 가 받은/거절한 요청
  - `kf=seq/reason ok= age= tmsg=`: 마지막 keyframe 요청. **seq 는 Host 의 `keyframe-request seq=` 와 같은 값**이다.
  - `ch sent/recv/retx/nack/pend closed reason`
  - `sink pass= codecCall= tid= inbox=`
- 금지 항목 준수: payload·비밀 0, wire 변경 0, 수신 경로는 relaxed atomic 저장만(lock·I/O·할당 없음), 운영 배포 없음.
- 표지 확인: `liveness-trace` 는 trace APK 의 .so 4/4 에만 있다. plain debug 0/4, release 0/4. release 에 flag 를 주면 gradle 이 거부한다.
- Kotlin 의 log tag 문자열 `liveness_trace` 는 release dex 에도 있다(BuildConfig 조건문 안, 실행되지 않음). gate 표지와는 다른 문자열이다.
- 관찰(계측으로 처음 보임, 판정 아님): Windows 실행에서 `tick=-1`. 영상과 control 이 100 ms 보다 촘촘히 들어오면 수신 loop 의 Tick(:1482)은 돌지 않는다. 그동안 클라이언트 재전송 타이머는 control thread 의 Read 대기(udp_control_channel.cpp:487–506)에만 달려 있다. control-only peer_lost(ke-r25, r21 B) 조사에서 볼 항목으로 기록만 한다.

### 다음
- 고정 SOURCE 요청: **27d5966 + ec016f0**.
- SOURCE 뒤 EVIDENCE: trace 시험 APK(ke-off/ke-on 계열에 `-Pgnlink.livenessTrace=true`)로 E2(기본 SH)와 SH-off 를 각 1회 실행한다. 필요하면 정상 재시작 뒤 새 boot 에서 1회 더 한다(r1/r2 허용 범위).
- 미검증: 기기 NDK 동작, SH on→off, KA 복귀, D1(a) UI 대기 실측, 미설명 2구간, ke-r25·r21 B 의 control-only peer_lost.

## r3 (Codex codex_apk_decoder_liveness_design_r3.md) — D1 bounded detach, D2 계측, D3 adb 고정
- commit **`7a79b09df02017831515d342733452809b022fd2`**(ec016f0 위, tracked clean).
  - 범위(제품): decoder cpp/hpp, native_bridge, MainActivity, NativeSessionBridge, SharpenPathPolicy, directory server.js(계측만), udp_control_channel cpp/hpp(계측만), session cpp(trace 줄)
  - 범위(시험): SharpenPathPolicyTest, liveness_test, settled_sink, relay_trace_test.js(새)
  - 문서 0
- 고정 SOURCE 요청: **27d5966 + ec016f0 + 7a79b09**.

### D1 — bounded detach (Codex D1 (b))
- native(`AndroidVideoDecoderSink::SetSurface`)의 반환값:
  - `> 0`: 분리 완료 또는 연결 접수
  - `0`: 거부(이전 분리가 아직 대기 중이거나, 놓아야 할 window 가 4개 이상)
  - `< 0`: 분리가 기한(`kDetachDeadlineMs = 2000`) 안에 끝나지 않음(-seq)
- 대기는 handoff 당 한 번이다. 이전 분리가 대기 중이면 다시 기다리지 않는다. RX·control 은 기다리지 않는다. 거부된 연결 요청의 window 는 그 자리에서 돌려준다(worker 에 넘어간 적 없는 참조).
- 완료 신호: `SurfaceReleasedSeq()`(lock 없음, JNI `nativeSurfaceReleasedSeq`)가 seq 에 도달하는 것. 이는 그 surface 의 codec 이 stop/delete 되고 옛 window 가 release 된 뒤에만 오른다(worker 의 release 단계, `MarkSurfaceReleasedLocked`). enqueue·gate 무효화·명령 접수는 완료가 아니다.
- Kotlin:
  - `DetachDecoder` 가 음수를 받으면:
    - 같은 명령 묶음의 나머지(StartRenderer, BindDirect, Attach)를 실행하지 않는다.
    - 넘겼던 Java Surface 는 보관하고 release 하지 않는다.
    - 같이 온 `ReleaseRenderer` 는 미룬다(renderer 의 input 에 codec 이 아직 붙어 있을 수 있으므로).
    - 현재 surface 는 `BLOCKED` 로 둔다(`SharpenPathPolicy.decoderDetachTimedOut`).
  - UI handler 에서 released seq 를 감시한다(100 ms 간격, 10 s 이후 500 ms). 늦은 완료가 오면 보관한 Surface 를 한 번 release 하고, 미룬 정리만 실행한다(`decoderDetachCompleted`).
    - bind 는 하지 않는다. 막힌 surface 는 사라질 때까지 BLOCKED 이고, 그 사이 생긴 새 surface 는 다음 bind 를 기다린다(늦은 ACK 가 자동 attach 하지 않는다).
    - UX 결과(확인 요청): 화면이 다시 붙는 시점은 다음 surface 생성 또는 다음 bind(장면 전환 등)다.
  - `onSurfaceTextureDestroyed`: decoder 분리가 대기 중인 surface 면 `false` 를 반환한다(앱 소유). 그 SurfaceTexture 는 마지막 보유자(renderer, decoder)가 놓을 때 한 번만 release 한다.
  - native 가 연결을 거부(0)하면 아무것도 연결하지 않는다(`decoderAttachRefused`, Surface 는 즉시 release. 넘긴 적이 없다).
- 시험:
  - liveness_test [3]: create 를 8 s hold 한 상태에서 측정했다.
    - UI 의 detach 가 **2.0 s** 에 pending(-2)으로 돌아왔다.
    - 두 번째 detach 는 0 ms(-3).
    - 새 surface 는 0(거부)이고 window 는 즉시 반환(획득 1/해제 1)됐다.
    - hold 중에는 완료로 보고되지 않았다.
    - 해제 뒤 released seq 가 도달했다.
    - 늦은 완료는 아무것도 연결·투입·configure 하지 않았다. 붙잡혔던 create 는 끝난 뒤 void 로 즉시 삭제됐다.
    - 그 뒤 새 surface 가 접수되고 IDR 이 그 surface 의 codec 으로 들어갔다.
    - RX·control 은 계속 50 ms 미만이었다.
  - 전체 결과:
    - **40/40**
    - 음성: m4(기한 제거) **5 FAIL**(UI 12 s 정지), 9f9a91c **25 FAIL**
  - SharpenPathPolicyTest 22/22(D1 신규 5개):
    - direct 시간 초과 → 생산자 연결 0, 늦은 완료로 bind 0
    - renderer 시간 초과 → renderer release 를 미룬 뒤 그 release 가 와도 unblock 0
    - decoder 가 붙은 채 surface 파괴 → ST 1회 release
    - 대기 중 새 surface → 다음 bind 때 연결
    - 연결 거부
  - Kotlin 단위 시험 26묶음, 실패 0.
- 미검증: MainActivity 배선은 단위 시험이 아니라 build 와 정책 시험으로만 확인했다. 실제 기기의 TextureView 콜백 순서와 감시자는 EVIDENCE 에서 본다.

### D2 — active control 종료 계측 (시험 전용)
- 클라이언트 reliable head(`UdpControlChannel::Stats` 추가 필드, 기록은 GNLINK_LIVENESS_TRACE 일 때만):
  - `txq`, `head=seq/attempts`, `first`/`last`(head 첫 송신·마지막 송신 경과), `acked=`(마지막 ACK seq), `ack=`(경과), `ackOk/ackIgn/nackRx`
  - 이 값들이 `liveness_trace` 한 줄에서 RX/control phase·tick·kf seq 와 함께 나온다.
- local fixture relay(server.js, **`REMOTE60_RELAY_TRACE=1` 일 때만**. fixture 가 `KE_RELAY_TRACE=1` 로 켠다. 운영 배포 0):
  - flow·방향별로 수신(rx)과 우리 send 에 대한 socket 응답(sent / sendErr + 오류 코드)을 header kind 별로 센다. kind: video, ctlData, ctlAck, ctlNack, hello, helloAck, punch, standby, cursor, videoNack, resume, unknown. payload 는 읽지 않는다.
  - 1 s bucket(프로세스 monotonic, `mono0`·`wall0`·`wall` 함께), 방향별 마지막 시각, host-side 거부·tuple 변경 event, 미매칭 c2h 수를 남긴다.
  - 5 s sweep 마다 snap(bucket 없음), close 때 bucket 전체를 남긴다.
  - send 성공은 socket 의 말일 뿐, 원격 수신의 증거가 아니다(코드 주석에 명시).
  - 꺼져 있으면 계수 0 이고, send 는 이전과 같다(callback undefined).
- 시험 `relay_trace_test.js`(새):
  - 결과: ALL PASS ×3.
  - off: trace 줄 0. on: 양방향 last 시각, clock 이름, close bucket(`c2h/h2c.rx|sent.kind`), 자격 증명 0. relay 동작은 둘 다 relay_test 통과.
  - 첫 실행의 off 단계에서 relay_test 의 grace 타이밍 항목이 1회 실패했다(replies=0). 이후 3회는 재현되지 않았다. 기록만 한다.
- directory 전체 suite 는 relay 단계 전의 `device_credential_test`(옛 서버 6e5cd70 사례, `fetch failed`)에서 멈춘다. **9f9a91c 에서도 같다**(이번 변경과 무관). 그래서 relay_trace_test 를 단독으로 실행했다.

### D3 — adb 고정 (하네스 r8, feat-apk-0233\.claude\e2e, 미추적)
- snapshot: `.claude/caplife/avd_guard_r8_snapshot/`(SHA256SUMS `d6092772…`)
- 바뀐 것: ke_slot, ke_avd, ka_avd, ke_fixture. 나머지는 r7 과 같다.
- 내용:
  - 모든 호출이 SDK adb 절대 경로 하나를 쓴다(PATH fallback 없음). 기기 호출은 serial 을 명시한다.
  - `ke_slot` 이 sha256(`7035cf5e…`)과 `adb version` 을 기록한다. `-AdbSha256` 로 pin 하면 다른 파일은 시작 전에 거부되고, `ka_avd` 도 같은 pin(`KE_ADB_SHA256`)을 import 때 확인한다.
  - 5037 listener 의 pid·path·시작 시각·주소를 실행 전, 준비 확인 때, AVD 뒤 세 번 **읽기만** 해서 기록한다.
  - 준비 확인은 rc/stdout/stderr 를 따로 보존하는 helper 로 한다. daemon 안내문은 실패가 아니다. 최대 20 s 동안 `get-state=device`, `qemu=1`, `boot_completed=1` 을 확인하고, 아니면 fail-closed 한다.
  - logcat 이 끝나면 그 즉시 `LOGCAT ENDED` 를 step 에 남긴다(공백이지 사건 없음이 아니다).
  - e2-shoff 계획에 첫 frame 뒤 **60 s 무조작 관찰**을 추가했다. 첫 `keep_end`/HOSTS 를 그 시각과 함께 기록하고, 그 뒤 메뉴 단계가 실패해도 최초 원인이 남는다. 관찰 구간의 `liveness_trace` 줄 수를 센다.
  - `-ApkOff/-ApkOn`(설치할 APK), `KE_SERVER_JS`(다른 worktree 의 server.js, 절대 경로·존재 확인), `KE_RELAY_TRACE=1` 을 추가했다.
- 전용 adb server port 는 아직 쓰지 않는다. 기록을 본 뒤 계속 불안정하면 Codex 허용 범위에서 구성한다.
- 금지 사항 준수: kill-server, adb/LDPlayer 종료, PATH·설치 변경 모두 0.
- 확인:
  - 구문: PS parse 0 오류, py ast OK
  - redact 시험 7/7
  - 오케스트레이션(stub, 기기 0) 19/19, 잔여 0
  - 첫 실행은 내 병렬 build 부하로 stub host 의 5 s 준비가 2건 늦어 17/19 였다. 부하 없이 재실행해 19/19 다.

### 다음 실행 계획 (SOURCE OK 뒤, 1회)
- 슬롯: `sh_run.ps1 -NativeExe powershell … ke_slot.ps1 -Tag ke-r26-shoff-lt -ApkOff <apk_out\ke-off-lt.apk> -ApkOn <apk_out\ke-on-lt.apk> -AdbSha256 7035cf5e…`
- 환경: `KE_PLAN=e2-shoff`, `KE_RELAY_TRACE=1`, `KE_SERVER_JS=C:\work\gnlink-wt\apk-decoder-liveness\apps\directory\server.js`
- APK: ke-off-lt `a7816b3c…`, ke-on-lt `aa15aad4…`(7a79b09, testDirectoryUrl + livenessTrace)
- Host 는 지금과 같은 pinned Host(`D806E375`, feat-apk-0233 build)를 쓴다. 이번 수정은 APK 쪽이다.
- 앞서 adb 안정 확인: 실행 직전 listener 와 device 상태를 30 s 관찰한다. 서버가 다시 뜨거나 offline 이 반복되면 INVALID 로 기록하고 반복 실행하지 않는다.
- 판정·보고:
  - E1, SH-off 설정, E2 첫 frame, 60 s 유지
  - 끊기면 최초 종료 시각에 대해 앱 `liveness_trace`(RX/control phase, head/ACK, kf seq), relay trace(방향별·종류별 bucket·send 결과·close), Host 로그(keyframe-request seq, peer-lost)를 같은 시각축(각 clock offset 명시)으로 대조한다.
- 1회 무발생이어도 r21 B·ke-r25 항목은 닫지 않는다.

### 확인 4 — `.detach()` 스레드 (verifier 질의, 27d5966:1303 = 7a79b09:1351)
- 정체: SH 시험 seam(`#ifdef GNLINK_SH_TEST_SEAM`, debug + `-Pgnlink.shTestSeam=true` 전용)의 "늦은 handoff 게시" 스레드다. 9f9a91c 에도 있던 코드이고 이번 작업이 만든 것이 아니다.
- 하는 일: 최대 2 s 잔 뒤 sink 의 handoff ring(atomic 4개)에 값을 쓴다. lock, codec, window 에는 손대지 않는다.
- 수명:
  - 출하물에는 없다(gate 표지 `sh-test-seam`, release .so 0/4).
  - APK 에서 sink 는 프로세스 전역 `g_video_decoder_sink`(native_bridge.cpp:33)다. 일반 session teardown 에서는 해제되지 않는다. 다만 destructor 는 worker 만 join 하고 이 publisher 스레드는 추적하지 않는다. 따라서 **프로세스 종료·라이브러리 해제 시 수명은 미검증이다(UAF 가능성 배제 못 함).** (r4 정정: 예전 문구 "static 소멸 경합뿐" 은 범위를 좁혀 말한 것이었다.)
  - Windows 시험은 seam 을 compile 하지 않는다.
  - 코드 주석(:1338–1339)의 "teardown 중 해제된 sink 를 건드릴 수 있음 — seam 이라 수용" 은 sink 를 지역 객체로 만드는 경우를 말한다. 그 경우에도 안전은 보장되지 않는다(미검증).

## r4 (Codex codex_apk_decoder_liveness_src_r1.md NEEDS_CHANGES) — N1 예약/확정, N2 상한, N3 생성 실패 회복, D1 획득 실패

**보수 commit `0a108b3`** (fix/apk-decoder-liveness, 부모 7a79b09).
- 바뀐 파일: 제품 4개 + 시험 5개입니다.
  - 제품: `android_video_decoder.cpp/.hpp`, `selection_ack_gate.hpp`, `decoder_pending_slot.hpp`
  - 시험: liveness / idr_recovery / pending_slot 시험, fake NDK
- 느린 codec API를 state lock 아래로 되돌린 곳은 없습니다. wire·transport·owed 예산은 바뀌지 않았습니다. SH seam은 손대지 않았습니다.

### N1 — gate 결정과 실제 queue 사이 (Codex 재현 반례)

**원인 (Codex 판정 그대로)**
- worker는 `dequeueInputBuffer`와 `queueInputBuffer`를 mu_를 풀고 부릅니다. 그런데 그 전에 확인한 `PermitsQueue`는 바이트가 실제로 들어가는 순간을 보장하지 못했습니다.
- 게다가 unbound local 선택의 candidate를 **RX 쪽 `Admit`이 바꿨습니다.** 그래서 101을 dequeue하는 사이 202가 candidate를 가져갔고, 둘 다 codec에 들어갔습니다.

**보수 (`selection_ack_gate.hpp`) — queue를 예약으로 바꿨습니다**
- `Admit`은 IDR을 통과만 시키고 아무것도 바꾸지 않습니다. `KeyLost`도 다시 빚(owed)으로 둘 뿐, candidate는 건드리지 않습니다. 즉 RX는 예약을 움직일 수 없습니다.
- worker가 queue할 frame에 대해 `ReserveQueue(gen, isKey)`를 합니다. unbound local 선택의 candidate는 이때 worker가 고릅니다.
- `CommitQueue`는 예약 시점의 epoch가 그대로일 때만 owed IDR을 지불하거나 bind합니다. `AbortQueue`는 그 epoch 안에서만 candidate를 놓습니다.
- `Prepare` · `Reset` · `BeginLocal` · `Refuse` · `OnAck`는 epoch를 올립니다. 디코더에서는 이 변경마다 codec reset도 요청됩니다.

**교환 단위 계약 (`TryQueueFrameLocked` 주석에 적었습니다)**
- dequeue가 끝난 뒤 queue 전에 변경을 보면: frame을 **queue하지 않습니다.** 그 codec은 무효가 되고(reset이 이미 pending이거나 여기서 요청), dequeue한 buffer도 그 codec과 함께 버려집니다.
- queue 호출 도중에 변경이 오면: 바이트는 이미 무효가 된 codec에 들어간 것입니다. owed 지불·bind·bootstrap은 0이고, 그 codec의 그림은 ready로 세지 않습니다.
- 변경 뒤에 시작한 교환은 새 상태로 판정합니다.
- 예약이 stale인데 reset이 없는 경우는 도달하지 않는다고 봅니다. 그래도 그때는 codec을 무효로 하고 로그를 남깁니다.
- unbound local 선택은 **처음 codec에 들어간 IDR이 bind**합니다(클래스 주석 그대로). 다른 세대는 자기 queue에서 거부됩니다.
- 최초 출력이 선택을 ready로 만드는 것은 그 세대가 확정됐을 때(`expected != 0`)뿐입니다.

**Codex probe 기준과의 차이 (한 번만 적습니다)**
- `candidate_probe.cpp`는 "A1이 들어가면 누출"로 판정합니다. 202가 이기는 쪽을 전제한 기준입니다.
- 이번 계약에서는 A1만 들어가고 bound=101, B2는 거부되는 것이 정상입니다.
- 그래서 시험 [9]는 "누출"을 **최종 bound와 다른 세대가 codec에 들어감**으로 정의했고, B2가 들어가지 않았는지도 따로 봅니다.

### N2 — inbox 상한
- `inbox_.size() > 1` 예외를 없앴습니다. 이제 총량 ≤ 4 MB가 항상 지켜집니다.
- 상한보다 큰 단일 frame은 입구에서 거부합니다. 처리는 버린 frame과 같습니다: IDR이면 KeyLost(다시 owed), key 요청, gap 처리. 별도 계수 `inboxRefused`와 로그 1줄을 남깁니다.
- **값의 근거**: 4 MB는 클라이언트가 요구하는 최고 bitrate 1초 분량보다 큽니다(`kRuntimeBitrateMax` 30 Mbit/s = 3.75 MB, viewer_constants.hpp:149). 조립기 자체는 16 MB까지 받습니다(`kMaxUdpAssembledPayloadBytes`, native_video_client_shared_core.cpp:34).
- **inbox 밖 사본** (`android_video_decoder.hpp` 주석): 처리 중 1 + pendingFrame_ 1 + bootstrapFrame_ 1 + replay 임시 사본 1이고, 각각 상한 이내입니다. 따라서 sink 전체 frame 메모리는 최대 4 + 4×4 = **20 MB**입니다.

### N3 — 생성 실패 + owed 예산 소진 + idle Host
- **7a79b09에서 실제로 회복하지 못합니다**(아래 음성: U5 FAIL, ready 뒤 요청 0, codec 없음, control은 살아 있음).
- **보수**: worker가 생성 실패 뒤 스스로 다시 만듭니다.
  - 1초 간격, surface·선택·binding 하나당 최대 20회입니다.
  - 이렇게 만든 codec이 올라오면 `gate_.Rearm()`을 부릅니다. 새 token이 생기고, 세션은 **같은** 8회 예산으로 다시 요청합니다. 새 surface에 이미 있던 규칙과 같습니다.
  - 20회가 지나면 `codecState=failed_terminal`이 되고 로그 1회를 남깁니다. 그 뒤에는 새 surface·새 선택·Host가 보낸 IDR만 회복시킵니다(IDR이 오면 항상 생성을 시도합니다).
  - 무제한 재요청이나 transport 예산 증가는 0입니다. 재시도는 로컬 생성뿐이고, Host로 가는 요청은 Rearm 1회당 기존 예산 그대로입니다.
- **한계**: `pending_ == 0`(선택 없음)에는 owed token이 없어서 Rearm이 요청을 만들지 않습니다. APK는 항상 선택을 거쳐 보므로 해당하지 않는다고 봅니다. 이것은 코드 경로 판단이고 기기에서 확인하지는 않았습니다.

### D1
- **획득 실패**: non-null Surface에서 `ANativeWindow_fromSurface`가 실패하면 **0(거부)**을 반환합니다. surface·codec·detach 상태는 바뀌지 않습니다. 예전에는 detach로 처리돼 seq를 반환했습니다(음성 [13]).
- **문구**: detach가 마감을 넘긴 Surface는 **교체될 때까지 BLOCKED**입니다. 늦은 완료는 정리만 하고 아무것도 attach하지 않습니다. 같은 blocked Surface는 다음 bind만으로 풀리지 않습니다. 대기 중 새 Surface가 왔다면, 완료 뒤 **그 새 Surface의 다음 bind**에서 연결됩니다. 앞 r3 절의 "다음 bind" 표현은 이것으로 바로잡습니다.
- 사용자에게 열린 명시 재시도 경로가 어떤 surface lifecycle을 만드는지는 실제 배선 EVIDENCE가 없어 **미검증**입니다.

### D2 문구 정정
- off build에서도 비용이 0은 아닙니다.
  - `UdpControlChannel::Stats/Outbound`와 `ClientSessionController`에는 진단 필드가 항상 있습니다(크기·초기화 비용).
  - `GetStats`는 기존 mutex 아래에서 txQueue head와 lastAcked를 O(1)로 조회하고, 더 큰 Stats를 복사합니다.
- 새 network send나 계측 I/O는 없습니다. PC/Host 소비 target은 같은 header로 다시 빌드해야 합니다.
- trace on에서 logger가 `GetStats`의 mutex를 읽습니다. "무-lock snapshot"은 session atomics 부분에만 맞는 말입니다. 따로 부르는 `GetStats`/`IsClosed`/`CloseReason`은 하나의 원자 snapshot이 아닙니다.
- 앞 절의 "출하 코드·비용 변화 0" 표현은 이것으로 바로잡습니다.

### SH seam 수명 (보충 「확인 4」에 대한 정리, 코드 변경 없음)
- 앞 절의 "제품 sink가 전역이므로 static 소멸 경합뿐"은 정정했습니다. 일반 session teardown 에서 해제되지 않는다는 것까지만 맞습니다. 제외 근거는 **출하 build에서 seam이 compile되지 않는다**는 범위뿐입니다.
- seam을 켠 build에서는 publisher가 raw this를 잡고 join·완료 추적이 없습니다. 따라서 **프로세스 종료·라이브러리 해제 시 수명은 미검증입니다(UAF 가능성 배제 못 함).** seam을 켠 지역 sink도 같습니다. 2000 ms는 sleep 설정의 상한일 뿐, 스레드 완료 기한을 보장하지 않습니다.
- 이번 보수 범위 밖이며 미검증으로 남깁니다.

### 시험 (기기 0, stand-in NDK — 모두 pure-logic + threads; idr_recovery는 loopback network)

새로 추가한 대역 기능입니다(시험 전용):
- fake NDK가 `dequeueInputBuffer`(bit 16 = Codex review probe와 같은 위치)와 `queueInputBuffer`(bit 32)를 붙잡을 수 있습니다.
- `ANativeWindow_fromSurface` 실패를 흉내 낼 수 있습니다(`windowAcquireFails`).

| 시험 | r4 (`0a108b3`) | 7a79b09 sink+gate (음성) |
|---|---|---|
| android_decoder_liveness_test | **63/63** rc0, 반복 ×3 모두 63/63 | **10 FAIL** rc1 |
| android_decoder_idr_recovery_test | **49/49** rc0 (최종 binary 재실행 49/49) | **3 FAIL** rc1 |
| decoder_pending_slot_test | 21/21 (gate 예약 pure-logic 11건 추가) | — |
| client_session_prelogin_select_test | 60/60 | — |
| owed_key / binding / decode_stats / geometry / session_liveness | 32 / 17 / 42 / 10 / 9, 모두 rc0 | — |
| NDK `:app:buildCMakeDebug` (4 ABI) | BUILD SUCCESSFUL (APK 미생성) | — |

**새 대조와 음성 결과** (음성 = 7a79b09 sink + gate, 위 표의 FAIL 열)
- [9] Codex 스케줄
  - r4: bound=101, codec에는 `A1 A1`만 들어갑니다(두 번째는 같은 IDR의 bootstrap replay). B2는 들어가지 않습니다.
  - 음성: `A1 B2 B2`, bound=202로 **Codex 반례가 재현됩니다.**
- [9b] 거울 순서 (101이 input buffer를 기다리며 slot에 보류, 202가 대체)
  - 202만 들어가고 bind됩니다. 음성에서도 PASS입니다(원래 있던 계약).
- [10] dequeue 중 새 pick
  - 옛 frame은 queue되지 않고, 새 선택의 IDR이 **새** codec의 첫 입력이 됩니다.
  - 음성: 옛 frame 1개가 queue되고 새 codec이 만들어지지 않습니다.
- [11] queue 호출 중 새 pick
  - 시작된 교환은 옛 codec에 들어가고 그 codec은 삭제됩니다. ready는 0이고, 새 IDR은 새 codec으로 갑니다.
  - 음성에서도 PASS입니다(reset 계약은 r1부터 있었습니다). 계약 기록용입니다.
- [12] 5 MB frame
  - r4: 거부되고(inboxBytes 0, refused 1) owed와 key 요청이 남습니다. 1.5 MB frame을 연속으로 넣어도 inbox 최대는 3.0 MB입니다. 새 pick은 옛 손실을 owed로 물려받지 않습니다.
  - 음성: owed와 key 요청이 없습니다(행동 FAIL). inboxBytes·refused는 옛 build에 없는 필드라서, 이 두 FAIL은 "보고 없음"일 뿐 행동 증거가 아닙니다.
- [13] 획득 실패
  - r4: 0을 반환하고, 다음 frame은 A의 codec으로 갑니다. 삭제 0, released seq 그대로입니다.
  - 음성: result=2(detach로 처리), codec 1개 삭제.
- U5 생성 실패 + idle
  - r4: ready 전 요청 8회가 모두 소진되고, ready 뒤 **+96 ms**에 현재 IDR이 첫 입력으로 들어가며 +98 ms에 표시됩니다(새 token으로 요청 1회).
  - 음성: 요청 0, 영구 black, control은 alive입니다.
- U5b 26 s 실패
  - `failed_terminal`이 되고, 3초 동안 자체 회복 0·요청 0입니다. 새 surface 뒤 +14 ms에 회복합니다.
  - 음성: terminal 상태 자체가 없습니다(FAIL). 새 surface 뒤 회복은 원래 경로라 PASS입니다.
- 음성 build가 **옛 gate**를 썼다는 근거: 7a79b09 decoder는 인자 1개짜리 `PermitsQueue`를 부릅니다. 새 gate에는 그 서명이 없으므로, 옛 gate가 아니었다면 compile되지 않았습니다. sink dir `.claude/caplife/liveness_neg/r4_7a79b09`(5파일)는 include 순서상 src보다 먼저 옵니다.

### 바뀐 기대와 이유
- **unbound local 선택의 candidate**: "RX가 통과시킨 마지막 IDR"이던 것을 "worker가 처음 codec에 넣은 IDR"로 바꿨습니다.
  - 기존 pending_slot의 "G held → H 도착 → H만 bind" 대조는 그대로 통과합니다. held frame을 IDR이 대체하는 규칙은 바뀌지 않았기 때문입니다.
  - prelogin_select 60건도 그대로 통과합니다.
- **`PermitsQueue`에 `isKey` 인자를 추가**했습니다. unbound local 선택은 IDR만 허용합니다. 호출부는 pending slot 2곳과 bootstrap 1곳입니다.

### 미검증
- 실제 MediaCodec(기기·에뮬레이터)에서 dequeue와 queue 사이 경합 빈도, `failed_terminal`의 사용자 화면, 명시 재시도의 실제 surface lifecycle은 확인하지 않았습니다.
- 새 APK는 만들지 않았습니다(Codex 순서: SOURCE OK 뒤 r8 snapshot, 새 APK, ke-r26). ke-r26은 계속 보류입니다.
- 기존 전체 시험을 ×5로 반복하지 않았습니다(Codex 지시). 새 race 대조만 liveness ×3으로 반복했습니다.
- dequeue 중 **owner 변경(Bind)** 은 별도 case가 없습니다. `ResetForBindingLocked`가 [10]과 같은 reset 요청과 gate epoch 증가(`gate_.Reset()`)를 거치므로 같은 경로라고 판단했고, 실행으로는 확인하지 않았습니다.

## r5 (Codex codex_apk_decoder_liveness_src_r4.md — N3만) — 생성 실패 뒤 retry 예산·기한 공유

**보수 commit `ac587dd`** (부모 0a108b3).
- 바뀐 파일: 제품 `android_video_decoder.cpp/.hpp`, 시험 `android_decoder_liveness_test.cpp`, 주석만 바뀐 `selection_ack_gate.hpp`.
- N1 · N2 · D1과 획득 실패는 Codex가 닫았으므로 손대지 않았습니다(gate 동작 변경 0).

### N3 원인 (Codex 재현 그대로)
- 응답된 선택에서 IDR의 생성이 실패한 뒤 delta 40개를 50 ms 간격으로 넣으면, 2초 동안 생성 시도가 41회였고 `codecRetries=0`이었습니다.
- `ProcessFrameLocked`가 frame마다 `EnsureCodecLocked`를 불렀고, 실패할 때마다 timer를 now+1 s로 밀었습니다. 그래서 예산·간격·terminal이 모두 우회됐습니다.

### 보수
- 생성 시도는 `EnsureCodecLocked` 한 곳으로만 들어가고, 실패 뒤에는 frame 경로와 worker timer 경로가 **같은 예산**(`kCodecAutoRetryMax` 20)을 셉니다.
- 예산을 넘으면 `codecTerminal_`이 되어 **누구도 시도하지 않습니다**(IDR 포함). 새 surface·새 선택·새 binding·stream reset에서만 다시 셉니다.
- 1초 기한 전에는 **IDR만** 시도할 수 있고, delta와 timer는 기한을 기다립니다.
  - 처음에는 IDR도 기한을 기다리게 했습니다. 그런데 `android_decoder_owed_key_test` 2절의 기존 계약("codec이 회복되면 다음 IDR이 바로 queue되어 빚을 지불")이 3건 FAIL했습니다.
  - 그래서 Codex가 제시한 두 번째 안("실패 상태에서 delta가 생성을 직접 일으키지 못하게") + 공통 예산으로 좁혔습니다.
  - IDR의 시도도 예산에서 빠지므로 terminal은 우회되지 않습니다.
- 어느 경로든 retry로 만들어진 codec은 owed IDR을 다시 무장(Rearm)합니다. 새 token 1개, 세션의 같은 8회 예산입니다.
- idle U5와 U5b 성공은 그대로 유지됩니다.

### 시험 (기기 0)

| 시험 | ac587dd | 0a108b3 sink (음성) |
|---|---|---|
| android_decoder_liveness_test | **68/68** rc0 | **3 FAIL** rc1 — [14] 2초에 41회, 30초에 340회, terminal 없음 |
| android_decoder_idr_recovery_test | **49/49** rc0 (U5 +107 ms, U5b terminal → 새 surface) | (r4에서 이미 확인) |
| owed_key / pending_slot / prelogin / binding / decode_stats / geometry / session | 32 / 21 / 60 / 17 / 42 / 10 / 9, 모두 rc0 | — |
| NDK `:app:buildCMakeDebug` 4 ABI | BUILD SUCCESSFUL, APK 미생성 | — |

- **[14]** (Codex 스케줄):
  - 처음 2초(IDR 1 + delta 40) 동안 생성 시도 3회입니다.
  - 그 뒤 frame을 계속 넣어도 정확히 1+20 = **21회**에서 terminal이 됩니다(9.0 s).
  - terminal 중에는 생성이 가능해진 뒤에도 IDR까지 포함해 시도 0입니다.
  - 새 surface를 주면 그 IDR이 새 codec에 들어갑니다.
- **[7]** 바뀐 기대: 생성 실패 직후 200 ms 뒤에 오는 IDR이 아니라, 기한 뒤 retry가 codec을 만들고 다음 IDR이 들어가는 것을 봅니다. 기존 "다음 IDR 즉시 회복"은 owed_key 2절이 그대로 확인합니다.
- **음성 build 근거**: 0a108b3 sink 5파일(`.claude/caplife/liveness_neg/r5_0a108b3`)로 build했습니다. 그 binary에는 새 terminal 로그 문구가 0건이고 옛 문구가 1건입니다.

### 문구 정정 (앞 r4 절을 이것으로 바로잡습니다; 코드 주석도 같이 고쳤습니다)
- **N2**: "4 MB는 최고 bitrate 1초 분량보다 크다"는 **평균**입니다. 30 Mbit/s에서도 **개별 IDR이 4 MiB 미만이라는 보장은 없습니다**(rate control 창, 세밀한 화면). 그런 IDR은 오는 때마다 거부되고 다시 owed가 되며, 세션의 예산이 다하면 화면이 서게 됩니다. 이것은 남은 위험으로 기록합니다.
- **N2**: "sink 전체 frame 메모리 최대 20 MB"는 **논리 payload 예산**입니다. vector capacity, allocator overhead, 조립기와 codec 자체의 buffer는 포함하지 않으므로 실제 프로세스 메모리 상한이 아닙니다.
- **N1**: "모든 control-side 변경이 codec reset도 요청한다"는 단정을 고칩니다.
  - **reset을 요청하는 것**: Prepare · Abort · stream reset · Bind/Unbind.
  - **요청하지 않는 것**: OnAck · BeginLocal · Refuse. 이 셋은 awaiting 상태에서만 일어나고, awaiting 동안은 `PermitsQueue`가 아무것도 허용하지 않으므로 그 사이에 예약이 존재할 수 없습니다.
  - reset 없이 stale commit이 되는 경우는 도달하지 않는다고 보며, `voidExchange`가 그 **방어 경로**입니다.
- **D1**: 정지 상태(quiescent)에서도 destructor는 worker를 join한 뒤 **codec stop/delete와 window release를 inline으로 부릅니다**(mu_ 아래, 파괴하는 스레드에서). 그때 mu_를 기다리는 다른 호출자는 없지만, 그 호출 자체는 다른 곳만큼 느릴 수 있습니다.

### 미검증
- 실기에서 생성 실패가 얼마나 자주 나는지, terminal 화면, 4 MiB 초과 IDR의 실제 빈도는 확인하지 않았습니다.
- ke-r26과 새 APK는 계속 보류입니다.

## ke-r26 준비 (Codex codex_apk_decoder_liveness_src_r5.md — ac587dd SOURCE OK, 실행 전)

### 정책 문구 정정 (r4·r5 절)
- 생성 실패 retry 정책은 **"최초 1 + 추가 20 = 연속 실패 21회"**입니다.
- 기한(1 s) 전 예외는 IDR뿐이지만 IDR도 예산을 씁니다. 따라서 terminal까지 걸리는 시간은 정해져 있지 않습니다([14]에서는 9.0 s).
- 앞 절의 "1 s 간격으로 최대 20회", "약 20 s"는 delta와 timer에만 맞는 말입니다. "항상 1 s 간격 / 20 s 소요"로 읽지 않습니다.

### 새 trace APK (ac587dd, detached 아님: fix/apk-decoder-liveness HEAD, tracked clean)
- `apk_out\SHA256SUMS_r5.txt`(sha256은 회신에 적음)

| APK | sha256 | build |
|---|---|---|
| ke-off-lt-r5.apk | `054d9861b5f1866707ddf6ca31b474fd2c60796a79931f3ca32b7ff041e655f2` | `assembleDebug -Pgnlink.testDirectoryUrl=http://10.0.2.2:18410 -Pgnlink.livenessTrace=true` |
| ke-on-lt-r5.apk | `8829254538befd3db23e7084ac09363c435d6fb76c551421205b12190e1d99a3` | 위 + `-Pgnlink.testStandbyAutoPrepare=true` |
| plain-debug-r5.apk (경계 대조용, 실행에 안 씀) | `32bb788f3972cb7b3603d6f0d6907efcd503853dcbfc5b55fadcd427e27579ec` | testDirectoryUrl만 |

- **BuildConfig**: `DIRECTORY_URL=http://10.0.2.2:18410`, `DIRECTORY_MIGRATABLE_ORIGINS=""`(r3 lt build와 같은 fixture 조건), `LIVENESS_TRACE=true`, `STANDBY_AUTO_PREPARE`는 off=false / on=true, `UPDATE_MANIFEST_URL=""`, versionName 0.2.32(31).
  - versionName은 이 branch가 아직 올리지 않은 값 그대로이며, 시험 APK입니다.
- **native (4 ABI 모두)**:
  - `liveness-trace` 1, `sh-test-seam` 0
  - ac587dd 코드가 들어 있음을 확인했습니다: r5 terminal 문구 "selection, binding or stream" 1, r4 문구 "during the dequeue" 1.
  - dex에는 `10.0.2.2:18410`이 1이고 `gnlink.shotan.net`이 0입니다.
- **경계 재확인 (ac587dd)**:
  - plain debug는 `liveness-trace` 0/4입니다.
  - `assembleRelease -Pgnlink.livenessTrace=true`는 "is for debug builds"로 **거부**됩니다(BUILD FAILED).
  - 7a79b09 APK(a7816b3c/aa15aad4)는 재사용하지 않습니다.

### 하네스 r9 — 기본 SH용 E1+E2+60 s subset (`KE_PLAN=e2-sh`)
- snapshot `.claude/caplife/avd_guard_r9_snapshot/`(SHA256SUMS `86728e9b…357b7d`).
- r8과 다른 파일은 **`ke_avd.py` 하나**뿐입니다: `17e003f8…` → `35cfbfb5…`, diff는 `.claude/caplife/avd_guard_r9_ke_avd.diff`(111줄, `a1851d03…`).
- 나머지 ke_slot·ke_fixture·ka_avd·stubs·guard·job·슬롯·adb pin은 r8과 바이트가 같습니다.
- **변경 내용**:
  - `e2_shoff()`를 `e2_watch(sh_off)`로 바꿨습니다. `KE_PLAN=e2-sh`는 Settings를 건드리지 않고(정리된 앱의 기본 SH) E1 → E2 → 첫 영상 → 60 s 무조작 관찰 → PC 목록까지만 하고, E3 이후는 NOT RUN입니다. `e2-shoff`는 기존과 같습니다.
  - `full`은 그대로 남아 있지만 이번 두 run에는 쓰지 않습니다.
  - e2-sh는 앱 로그의 `sharpen_path/start/fallback/producer_stuck`에서 **requested / effective / fallback**을 step으로 기록합니다. effective가 direct이면 "NOT an SH-on render check"를 남기고, 판정은 "경로를 알 수 있다"까지만 합니다.
- **확인**: py ast OK. 그 외 대조는 하지 않았습니다.
  - 이 분기는 기기 쪽 단계라 stub 오케스트레이션이 덮지 않습니다. slot·fixture가 r8과 같으므로 r8의 stub 19/19가 그쪽 근거입니다.
  - 새 분기 자체는 **기기 없이 실행되지 않았습니다.**

### fixture 고정 (실행 때 slot·fixture가 다시 기록)
- server.js: `C:\work\gnlink-wt\apk-decoder-liveness\apps\directory\server.js` `0b656c06…34c3`(마지막 변경 7a79b09, relay trace 포함)
- Host: `GNLinkStreamPinnedSelectTest.exe` `d806e375…2eaa`(feat-apk-0233 build, 이전과 같음)
- harness: `remote60_mobile_abr_session_e2e_test.exe` `722bf981…17bd`
- adb: SDK `7035cf5e…` pin

### 실행 (검증용이 슬롯 차례 때; old-apk-lane S/P 다음)
1. 30 s adb 안정 관찰(listener pid/path·serial·boot). 교체나 offline이 반복되면 INVALID로 두고 반복하지 않습니다.
2. **기본 SH**: 환경 `KE_PLAN=e2-sh`, `KE_RELAY_TRACE=1`, `KE_SERVER_JS=C:\work\gnlink-wt\apk-decoder-liveness\apps\directory\server.js`
   `sh_run.ps1 -NativeExe powershell … C:\work\gnlink-wt\feat-apk-0233\.claude\e2e\ke_slot.ps1 -Tag ke-r26-sh-lt -ApkOff C:\work\gnlink-wt\apk-decoder-liveness\apk_out\ke-off-lt-r5.apk -ApkOn C:\work\gnlink-wt\apk-decoder-liveness\apk_out\ke-on-lt-r5.apk -AdbSha256 7035cf5ec7f99f7b5662a9f5395f9dccfc3b6fe42583d0b9550124a0c3496606`
3. **Settings SH-off**: 위와 같고 `KE_PLAN=e2-shoff`, `-Tag ke-r26-shoff-lt`만 다릅니다.
- 각각 별도 run ID이고 자동 반복은 없습니다.
- 판정은 E1 → E2 첫 영상 → 60 s 영상·control 진행을 나눠서 합니다. 끊기면 최초 종료 시각을 기준으로 앱 `liveness_trace`·relay trace·Host 로그를 clock offset과 함께 대조하고, 원문을 보존한 뒤 그 arm을 끝냅니다.
- 1회 무발생으로 r21 B/r25 control peer_lost 항목을 닫지 않습니다.
- 두 arm의 준비 시간과 부하(env_note: uptime/loadavg/boot_id) 차이를 기록합니다.

### 남는 EVIDENCE
- terminal 실제 UI, 명시 retry의 surface lifecycle, 실 NDK, 4 MiB 초과 IDR의 영향과 메모리, control 종료.
- APK 0.2.33 출하 보류는 그대로입니다.

## ke-r26 결과 (2026-10-29 23:46–23:56 KST, 슬롯 1개에 2 run, 기기: 공용 시험 AVD emulator-5554)

- **슬롯**: `sh_run.ps1 -NativeExe powershell … ke_r26_batch.ps1`(CreateNew). 23:46:31에 시작했고 **23:56:08에 해제**했습니다(`slot removed=True`, 잔여 프로세스 0). 내 파일만 지웠습니다.
- **30 s adb 관찰**(7회, 5 s 간격): STABLE. 5037 listener는 내내 `pid=25608 C:\LDPlayer\LDPlayer9\adb.exe`(21:52:39 시작)로 같았고, emulator-5554는 계속 device / boot_completed=1 / 같은 boot_id `37551b86…`였습니다.
  - 공용 5037 서버가 LDPlayer 것이라는 점은 기록만 합니다. SDK client 경로와 pin은 `7035cf5e…`입니다.
- **run 폴더**: `C:\work\gnlink-wt\feat-apk-0233\.claude\e2e\runs\ke-r26-sh-lt-20261029234709-6732`, `…\ke-r26-shoff-lt-20261029235145-37360`
- **요약 스크립트**: `.claude/caplife/ke_r26_summary.py`, 출력 `ke_r26_summary.txt`

| | ke-r26-sh-lt (KE_PLAN=e2-sh) | ke-r26-shoff-lt (KE_PLAN=e2-shoff) |
|---|---|---|
| AVD | **7/7 PASS, 18 NOT RUN**, avd exit 0 | **8/8 PASS, 18 NOT RUN**, avd exit 0 |
| bundle rc | 3 — fixture exit 1: b·c Host harness exit 1 | 3 — 같은 이유 |
| SH requested / effective / fallback | weak / **direct** / `no_output_after_ready` (sharpen_start gen=1 → fallback) → **SH-on 렌더 검증 아님** | off / direct / none (Settings OFF, sharpen_start 0) |
| 첫 영상 (pick → first frame) | 13 700 ms | 5 138 ms |
| 60 s 관찰 | 종료 0 (keep_end·HOSTS 0), app liveness_trace 108줄 | 종료 0, 105줄 |
| app control (watch / session 최대) | retx 0 · nack 0 · pend 0 · closed 0; ctl age 678 / 1233 ms; head attempts 2 / 10 | retx 0 · nack 0 · pend 0 · closed 0; ctl age 718 / 1605 ms; head attempts 6 / 6 |
| app sink (watch / session 최대) | codecCall 276 / 2238 ms; worker pass 457 / 2813 ms; inbox 6 / 8 | codecCall 151 / 1633 ms; worker pass **2761** / 2761 ms; inbox 8 / 8 |
| Host a | peer-lost 0; 세션 종료는 E2 PC 목록 때 `reason=none` 1회 | 같음 |
| relay trace (fixture) | snapshot 35, sendErr 0 | snapshot 37, sendErr 0 |
| env loadavg (E2 전 → 끝) | 7.42 → 9.84 | 8.59 → 11.56 |

- **bundle rc 3의 원인**: 쓰지 않은 b·c Host의 harness가 "host가 무엇을 캡처했는지 말한 적 없음"으로 FAIL했기 때문입니다. E2-only 계획에서는 b·c에 client가 붙지 않습니다. 같은 하네스로 돌린 ke-r23 · ke-r25-shoff도 `b exit 1; c exit 1`이었습니다.
  - 그래서 이것은 계획에 딸린 산물이며 제품 판정이 아닙니다. 다만 번들 rc는 그대로 3으로 기록합니다.
- **새 e2-sh 분기**: 기기에서 처음 돌렸고, 분기 자체의 오류는 0이었습니다(INVALID 없음).
- **control peer_lost 항목**: 이번 1회씩 발생하지 않았지만, 이것으로 r21 B / r25 control-only peer_lost 항목을 닫지 않습니다.

### ⚠️ 새로 보인 것 — 정상 시청 중 inbox 넘침과 keyframe 요청 연속
- 두 run 모두 영상이 나오는 동안 디코더 inbox(8 frame)가 계속 넘쳤습니다(관측).
  - SH-off: `inboxDrops` 65 → 97(약 25 s), `gapDrops` 174 → 284. 넘칠 때마다 다음 IDR까지 delta를 버렸습니다.
  - SH: `inboxDrops` 156 → 180, `gapDrops` 83 → 196.
  - 출력은 계속 나왔습니다(in≈out).
- 그 결과 app이 key를 계속 요청했습니다.
  - SH run의 trace에서 `owed=0/0`(owed 반복이 아님)인데 `kq`(송신)는 약 1/s, `kr`(limiter 거부)은 약 3/s로 늘었습니다.
  - Host `keyframe-request reason=2`는 SH 179회 / 117 s, SH-off 174회 / 119 s였습니다.
- **대조**: 같은 하네스·Host에서 9f9a91c APK(inbox 없음)로 돌린 ke-r23은 6회 / 5 s, ke-r25-shoff는 2회 / 14 s였습니다.
  - 그 run들은 시청 시간이 짧아 비율 비교에는 한계가 있지만, 크기 차이는 분명합니다.
- **가설(미검증)**: 27d5966의 bounded inbox(8 frame ≈ 270 ms @30 fps)가 worker의 codec 호출·pass 지연(관찰 구간 최대 276 ms / 2.7 s)을 견디지 못한 것으로 보입니다. 예전에는 RX가 codec에 직접 묶여 소켓·조립기가 완충 역할을 했습니다.
  - 이 부하(loadavg 7–11)가 원인인지, worker pass가 길어진 이유(codec 호출 밖)가 무엇인지는 확인하지 않았습니다.
- **제품 코드는 고치지 않았습니다**(Codex 판정 대상). 화질 영향(초당 수 frame의 delta 폐기와 IDR 반복)과 bandwidth 영향은 APK 0.2.33 출하 판단에 걸리는 항목입니다.

### ke-r26 결과 보정 (Codex codex_apk_decoder_inbox_decision_r1.md — 위 표와 "새로 보인 것"을 이것으로 고칩니다)
- **`kq`**: 일부 경로에서 `KeyframeRequestState::Request(...).queued`가 성공한 횟수입니다. 실제 송신 총수나 Host 수신 수가 아니고, pipeline callback·held 재시도 등 모든 경로를 같은 방식으로 세지도 않습니다. wire reason=2만으로 모든 요청의 원인이 inbox라고 할 수 없습니다.
- **"retx 0"**: `fragmentRetransmits`(SendFragments에 `only`가 주어진 선택적 fragment 재전송)만 센 값입니다. Tick의 timeout head 재전송은 여기에 들어가지 않습니다.
  - 따라서 retx 0과 head attempts 6/10은 양립합니다. head가 실제로 재전송됐다는 뜻입니다.
  - "제어 재전송·대기 0"이라는 요약은 틀렸습니다.
  - **`pend`**는 rxPending(수신 쪽)이며 txq와 다릅니다.
- **worker "pass 최대 2.7 s"**: outer loop snapshot의 경과 시간입니다. 안쪽 while이 계속 처리하는 동안에도 늘어나므로 **2.7 s 동안 worker가 멈췄다는 뜻이 아닙니다**.
- **"codecCall 최대 276 ms"**: 500 ms 표본에서 본 진행 중 호출의 경과 시간이지, 호출 지연 분포가 아닙니다.
- **RX dmax(425/634 ms)**: lock 대기·실행·스케줄 지연을 나누지 않은 경과 시간입니다.
- **relay `sendErr 0`**: 그 socket send callback에 오류가 없었다는 것뿐입니다. 목적지 수신·ACK 채택·무손실의 증거가 아닙니다.
- **5037 listener**: SDK client를 썼다는 사실과, listener가 LDPlayer pid 25608로 안정됐다는 사실은 별개입니다. listener를 원인으로 단정하지 않습니다.
- **비교 배율**: 이전 run(5 s / 14 s)과 이번 run(117 s / 119 s)의 총수를 나눈 값을 **회귀 배율로 확정하지 않습니다.**
  - 이번 Host 요청은 60 s watch 구간에 SH 60회 / SH-off 67회, 전체(startup + watch + 목록)는 179 / 174회입니다.
  - 이전 run과는 실제 active 시간이 달라 구간별 비교가 필요하며, 같은 조건 비교는 A/B로 합니다.
- **IDR 증폭 고리**("요청 → 큰 IDR → inbox 바이트 증가 → 더 넘침")는 **가설**입니다.
- **10-29 두 raw bundle**: rc 3 / fixture 1로 보존합니다. b·c capture FAIL은 E2-only 계획과 맞지 않는 시험 결함입니다. raw rc를 0으로 바꾸지 않고, bundle PASS라고 하지 않습니다.
- `ke_r26_summary.py`의 라벨을 위 의미로 고치고 `ke_r26_summary.txt`를 다시 생성했습니다.

## apk-decoder-inbox r1 — 설계 요지·계측·기준선·A/B 계획 (정책 변경 없음, 기기 0)

- **설계**: `.claude/caplife/design_apk_decoder_inbox_r1.md` (`b5eb2b68…eaec`). 결정 단위 D1–D8로 적었습니다. 핵심은 다음과 같습니다.
  - chain을 아는 bounded FIFO입니다. 상한은 그대로 두고, overflow 때 `[head..첫 usable IDR)`을 버립니다. "IDR + suffix"가 상한에 맞는 가장 이른 IDR을 고르고, 없으면 전부 버린 뒤 gap episode를 엽니다.
  - episode 표시는 tail에 두고, 끊긴 지점 **뒤에** enqueue된 IDR만 episode를 닫습니다.
  - key want는 원인별로 나누고 episode 하나당 요청 1회로 합칩니다. retry는 도착 전에만 1/2/4 s 간격으로 최대 3회 하며, 회복 시점은 실제 codec queue입니다.
  - 새 pick·Bind·reset은 episode를 지웁니다. selection owed 경로는 그대로 둡니다.
  - D8(session의 held 요청 처리)은 Codex에 묻는 점으로 남겼습니다.
  - limiter·상한·wire·예산·N1 계약은 바꾸지 않습니다. presentation 생략과 fps/bitrate 적응은 C 단계 선택지입니다.
- **계측 commit `8868fb5`** (부모 ac587dd; 동작 불변).
  - **sink**: key want를 원인별로 셉니다(항상, `keyWant=`).
  - **trace 전용**: inbox high-water, pop/codec-queue 시점의 age histogram, pass당 처리 frame과 시간, RX의 mu_ wait/hold, codec API 종류별 count·평균·최대.
  - **session**: `Request(2)` 5개 경로를 origin별 accepted/refused로 셉니다(`kreq`).
  - **trace 줄 라벨**: `fragRetx`(선택적 fragment 재전송), `headResend`(Tick timeout head 재전송, 새 계수), `rxPend`.
  - **출하 build에 남는 비용**: keyWant 배열과 DebugStatus 필드, InboxFrame의 `enqueueUs` 필드(값은 trace build에서만 씀), CodecCall의 kind 인자, Stats의 `headTimeoutResends` 필드. 측정 기록과 I/O는 0입니다.
- **기준선 `android_decoder_inbox_test`** (ac587dd 위, fake NDK; 기준은 설계의 수용 조건이라 FAIL이 예상됨):

| case | 결과 | 수치 |
|---|---|---|
| [1] 정상 120 frame | PASS | want 0, inboxDrops 0 |
| [2] gap 1회 (worker hold 중 delta 20개) | **FAIL** (기준 1) | **want 43**, inboxDrops 15, gapDrops 37. 끊긴 delta가 codec에 0, 다음 IDR로 재개, RX 0 ms |
| [3] queue 안 IDR + suffix 3 | **FAIL** (기준 0) | **want 2**. IDR과 suffix 3은 codec에 들어감 |
| [4] 3 s 정지, 90 frame | 상한 PASS / want **FAIL** | inbox 최대 8, 810 KB, RX 0 ms; **want 82 / 3 s** |
| [5] gap 중 새 pick | **FAIL** | 이전 want가 남음(1) |

- **회귀(동작 불변 확인)**: liveness 68 · idr_recovery 49(trace on; 줄에 kreq/fragRetx/headResend/rxPend 확인) · session 9 · owed 32 · binding 17 · decode_stats 42 · geometry 10 · pending_slot 21 · prelogin 60 · recv_loop 10 · shared_core PASS. udp_control_e2e는 SKIPPED(rc 77)라 결과가 아닙니다. NDK `buildCMakeDebug` plain과 trace 모두 OK.
- **하네스 r10** (`feat-apk-0233\.claude\e2e`): snapshot `caplife/avd_guard_r10_snapshot` (SUMS `7544f4b8…d9fe`), r9 대비 diff `caplife/avd_guard_r10.diff`(240줄, `a6706b71…8c4b`).
  - 바뀐 파일은 ke_slot · ke_fixture · ke_avd · ke_orchestration_test 넷입니다.
  - E2-only 계획은 Host a만 시작하고 readiness `hosts=a`로 정확히 맞춥니다.
  - `KE_WATCH_SEC`, 구간 표지(epoch ms), 10 s 샘플(PSS·app CPU·loadavg·qemu CPU ms), 1회 환경 기록을 추가했습니다.
  - orchestration(stub, 기기 0) **22/22**. 새 r10 case 3개를 포함합니다.
  - 첫 실행의 1건 FAIL은 내 시험 편집 결함이었습니다(bash heredoc이 `\f`를 form feed로 바꿈). 고친 뒤 22/22입니다.
  - ke_avd.py의 새 샘플링·구간 코드는 **기기 없이 실행되지 않았습니다**.
- **A/B 계획**: `.claude/caplife/plan_apk_inbox_ab_r1.md` (`ba9ce4d1…93e2`).
  - SH-off, 첫 영상 뒤 120 s, 각 1회입니다. A = 9f9a91c `ke-off.apk` `8c6422fe…`, B = ac587dd `plain-debug-r5.apk` `32bb788f…`이고, **두 arm 모두 앱 trace 없이** 대칭으로 둡니다.
  - 측정은 Host(`keyframe-request`·`[keyframe] bytes`·`wire`·`[client-present]`)와 relay trace, 부하 샘플로 하고, 구간은 startup / watch / list로 나눕니다.
  - 열린 결정 3건: trace 없는 대칭안, 가장 이른 IDR과 retry 숫자, D8 hook.

## apk-decoder-inbox r2 — 계측·plan 보정과 대조 (Codex codex_apk_inbox_plan_src_r1.md; 정책 변경 0, 기기 0)

- **commit `79b5c25`** (부모 8868fb5): 시험 build·local fixture trace만 바꿨습니다.
  - **queue age**: AU 자체(stream gen + seq, 64칸 ring)의 enqueue 시각으로 고쳤습니다. replay와 ring에서 놓친 것은 따로 셉니다.
  - **라벨**: inboxHw(trim 뒤) / inboxPeakPre(trim 전), rxLockScopeMax(hold의 상한), createCfgStart(한 구간).
  - **server.js relay trace**: 도착 UDP payload bytes를 방향별로 셉니다(`rxBytes c2h=B/n h2c=B/n`, close bucket의 `<key>B`). rx에서만 셉니다.
- **대조**:
  - `android_decoder_inbox_test` [6]: 250 ms 보류 IDR + 뒤 delta → ageQueue max **280 ms**(실제 약 283 ms) **PASS**. 8868fb5 sink에서는 **31 ms**로 Codex 재현과 같아 **FAIL**. [1]–[5] 기준선은 그대로이고, 수치는 실행마다 조금 다릅니다(gap want 43 → 39).
  - `relay_trace_test.js`: 길이를 아는 datagram(49 / 1200 B)에 대해 정확합니다. ALL PASS.
  - **회귀**: liveness 68, idr 49(trace on), session 9, owed 32, binding 17, decode_stats 42, geometry 10, NDK plain/trace OK.
- **하네스 r11**: `caplife/avd_guard_r11_snapshot` (SUMS `7348ba40…a5cea`), r10 대비 diff `caplife/avd_guard_r11.diff` (`20da6bcc…9c10`). 바뀐 것은 ke_avd.py 하나와 새 대조·fake adb입니다.
  - qemu는 emulator-5554 console port 5554의 유일한 qemu 소유자를 pid와 시작 시각으로 고정하고, 아니면 unknown입니다.
  - 샘플 호출마다 기한이 있고(파일 출력이라 손자 프로세스에 묶이지 않음), sample_ms와 miss를 남깁니다. top은 header로 %CPU 열을 찾습니다.
  - watch는 monotonic으로 재고, 첫 영상 → watch 시작 지연을 기록합니다.
  - 조기 종료 시 watch를 멈추고 더 조작하지 않습니다. 로그가 없으면 "유지"로 판정하지 않습니다.
  - `ke_sample_test.py` **9/9**. 첫 실행의 1건 FAIL은 6 s 기한이 시험 래퍼의 손자 프로세스 pipe 때문에 9.3 s까지 끌린 것이었고, 출력을 파일로 받게 바꿔 6.1 s로 끊깁니다.
- **plan r2** `caplife/plan_apk_inbox_ab_r2.md`(r1 대체): 고정값, 실행, 측정 원천과 정의(Host `[keyframe]`는 전수, `wire`는 표본이라 합계에 쓰지 않음, relay bytes는 snap 차이 또는 close bucket, UDP payload와 encoded AU 구분, client-present 창별 p95는 평균하지 않음).
- **설계 r2** `caplife/design_apk_decoder_inbox_r2.md`(r1 대체, C 계약):
  - AU 순서(seq, wrap, 중복, 재정렬)와 usable IDR을 정의했습니다.
  - ingress gap과 recovery episode를 분리했습니다. 회복은 commit에서만 확정되고, 후보가 다시 버려지면 같은 episode로 재개합니다.
  - 요청은 최대 4개, t0/+1/+3/+7 s입니다. owed와 같은 의무는 합치고 다른 원인은 보존하며, exhausted는 유한 상태로 둡니다.
  - D8: descriptor를 held와 accepted 두 시점에서 검사합니다.
- **정정**: r26 SH-off Host 로그의 IDR은 약 1.4–1.6 KB(480x270, 1.5 Mbps)입니다. 이 fixture의 inbox 넘침은 **8 frame 개수 상한**에서 일어났습니다. "큰 IDR → 바이트 상한" 증폭 가설은 이 환경에 맞지 않으므로 철회합니다.

## apk-decoder-inbox C r1 (2026-10-30) -- SOURCE 패킷
- 정본: `caplife/c_source_packet_r1.md`.
- branch `feat/apk-inbox-c`(base 79b5c25), HEAD **5fe90a9**. 제품 소스는 0832e37과 53e3fdb에 있고, APK는 53e3fdb의 깨끗한 tree에서 빌드했습니다.
- 자체 대조와 음성 대조(79b5c25 sink + mutant 11개), 경계, 검증용 120 s 계획, Codex 결정 항목(owed 소진 뒤의 요청 주체)은 모두 그 패킷에 있습니다.
- 실기, 슬롯, push는 0입니다.