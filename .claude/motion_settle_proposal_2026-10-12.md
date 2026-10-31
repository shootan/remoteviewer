# motion-settle — 정지→움직임 끊김 + 스크롤 후 글자 뭉개짐 3초+ (검증용 제안, 2026-10-12)

사용자 현장(0.2.157, 회사 PC 뷰어 0.2.156 → 집 AMD 호스트 8ec6ecb1 / hostId f6e8e54a, relay 175.207.45.151:29190):
"정적 화면에서 창을 작업표시줄로 내리거나 올릴 때 프레임이 매우 낮다", "홈페이지 스크롤 뒤 글자가 뭉개지고(스크린샷) 복구까지 3초 넘게".
사용자 요청: 고칠 수 있나 — Codex 와 상의, **AMD 에서도 문제 없게**.
원본 로그: NAS shotan/8ec6ecb1…/host.log, 검증용 사본 C:/work/gnlink-archive/host_8ec6_1801.log(18:01~09), host_8ec6_1810.log(18:10~25).

## 관측 (AMD legacy 프로파일, QP cap 32)
1. 움직임 시작 프레임이 예산의 3~4배.
   - 18:05:39~42 (R 4.5M, ABR mid 20fps, E≈2.98M → 한 장 ≈ 19KB): P 45~71KB, sendDur 100~157ms, queueWait 96~287ms, clampBurst=0, burstWin2s 964975/cap2r 1125000(2s 창 거의 참).
   - 18:23:14.9~17.1 (R 6M 30fps, 한 장 ≈ 16KB): P 40~68KB, sendDur 66~114ms, queueWait 최대 170ms. GOP 사이 skipsSincePrevKeyAccepted 15~34.
   → 애니메이션/스크롤 동안 실효 6~9fps.
2. governor 는 "2 연속 초과 초" 뒤에야 32→35, 애니메이션(<1s)에는 늦음. 18:07:09 32→35 → 18:07:12 35→32, 18:15:12 32→35 → 18:15:15 35→32.
3. 18:05:41 scheduled 키(93,825B) + encoder_gop 키(85,432B) 같은 ms 에 연속 발사.
4. ABR 18:04:28 high_to_mid_severe(evidence=severe_client clientDecodedFps=1 clientMbps=0.25) — 같은 시각 bwe lossPm=0, gradUs≈0, appLimited 반복. 정적/저프레임을 severe 로 오판 → 20fps/720p 로 약 2분(18:04:33 low 1280x720 → 18:05:00 mid → 18:06:44 high static_recovery).
5. 정지 후 복구: 스크롤 멈춘 18:23:17.1 이후 전송 = 17.6 0.2KB / 18.1 4.8KB(kick) / 18.6 4.6KB / 19.0~19.7 0.2~0.5KB(kick) / 20.0 12.6KB / 21.3 11.0KB(kick). 정지 화면은 변화 구동 캡처라 프레임이 trailing-edge kick(0.5~1s) 에만 생기고, 각 프레임이 한 장 예산에 묶여 조금씩만 정제 → 3초+. 실제 프레임 QP 는 로그에 없음(흐림 정도 원인 미확정).

## 제안 묶음 (검증용 안 — 반론 환영)
- **M0 계측 먼저**: 출력 샘플의 실제 QP(MFSampleExtension_VideoEncodeQP 또는 AMD/NVIDIA 공통 가능한 것) 를 프레임 통계에 기록. 하네스에 "scroll→stop: 정지 후 글자 영역 PSNR 이 기준(정지 상태 정상치)의 X 에 도달하는 시간(time-to-sharp)" + "motion onset 첫 1s 의 delivered fps/최대 프레임 간격" 행 추가(AMD 이 PC + NVIDIA Home 묶음 공용). 수정 전 값을 baseline 으로.
- **M1 정지 후 화질 복구 1장(settle refresh)**: 움직임 종료 후 N ms(예 120~200) 변화 없음이 확인되면 1회 고품질 프레임(옵션 a: 강제 IDR at 정적 QP 하한 / b: P 프레임 with 일시 낮은 QP cap·높은 per-frame 허용). 정지 중 회선은 비어 있으므로 기존 2s 창/strict cap 안에서 100KB ≈ 0.15s@6M. 반복 금지(1회/settle, 다음 변화 전까지), 강제 key 의무·epoch 와 정합. AMD·NVIDIA 공통.
- **M2 motion onset 즉시 대응(AMD 포함)**: 한 프레임이 per-frame payload 예산의 k배(예 2.5) 초과 시 2초 대기 없이 QP cap 을 즉시 한 단계 이상 상향(AMD 는 runtime QP 실효 확인됨 — r4 seam/AMD 실경로), 정지/settle 시 원복. NVIDIA 는 runtime RC 무효 → 이미 cap 51 이라 별도 조치 불요 또는 재초기화 없이 할 수 있는 범위만. 4 의 governor 2-second 계약과 충돌 여부 판단 필요.
- **M3 키 중복 제거**: scheduled 키와 encoder_gop 키가 같은 창(예 <500ms)에 겹치면 하나만.
- **M4 ABR 오판**: clientDecodedFps 저하가 정적·appLimited·손실 0·지연 정상일 때는 severe 근거로 쓰지 않음(정적 감지 기존 static_recovery 와 대칭).
- (선택) **M5 idle credit burst**: 정지 중 쌓인 여유로 onset 첫 프레임 몇 장을 cap 위로 — wire 계약 변경이라 Codex 판단. M2 로 충분하면 생략.

## AMD 에서도 문제없게
- AMD legacy 는 QP cap 32 고정 + 늦은 governor 가 onset 과생성의 주원인. 대안: (i) M2 (즉시 상향·settle 원복) — 정적 글자 화질 유지 / (ii) AMD 에도 NVIDIA 처럼 높은 정적 cap(예 40~44) + M1 으로 정지 후 선명 회복 — bake-off: AMD 40 x1.24, 44 x0.99, PSNR 비용. 검증용 선호: (i)+M1, (ii) 는 측정 비교 행으로.
- 검증: 이 PC(AMD) 로컬 하네스로 대부분 자동(실 MFT·실 UDP·실 수신), NVIDIA 는 Home 묶음 1회.

## 질문 (Codex)
1. M0~M4 범위/순서 동의 여부, M5 필요성.
2. M1 의 a(IDR) vs b(P+일시 QP) 선택과 반복/epoch/키 의무 계약.
3. M2 가 기존 governor/ReconcileGovernorCeiling·C3 no-op 계약과 충돌하는 지점과 조건.
4. 측정 gate: time-to-sharp·onset fps 의 합격선을 어떻게 정할지(baseline 대비 vs 절대).

## Codex 반론/합의 — r1, 2026-10-12

**판정: 수정 가능성이 충분하다. M0~M4를 아래 계약으로 한 task에 묶어 진행하는 것에 동의한다. 순서는 M0 + M4의 통계 원인 보수 → M1 유계 정제 → M2 빠른 압력 대응 → M3 입증된 중복 요청 보수다. M5는 제외한다.** 이 문서는 설계 검토이며 제품 수정/실행/개선 실증은 아직 하지 않았다. 사용자 설치본은 건드리지 않는다.

고정 소스 `592ed9670d211623b4aa20b6b17c01713d2ad67e`(제품은 09266e5 + 버전 변경). 아래 PC 통계 관련 viewer_video_receiver_frame.cpp, viewer_video_receiver.cpp, native_video_client_shared_core.cpp는 현장 뷰어 0.2.156의 `236b858`과 diff 0임도 확인했다. 직접 읽은 현장 사본 SHA-256: `host_8ec6_1801.log = c322d0f5cd1f4b29911b223e9e44c392c48bd4291fe18f91fe25626bf0ba4ba2`, `host_8ec6_1810.log = f005ac32fad8f96e785afb215df966453368621b888b6dd937d0709eb73db11d`. 스크린샷 자체는 이번 검토에서 보지 않았으므로 사용자 관찰과 로그/소스 근거를 구별한다.

### 1. 우선 정정할 해석과 M4의 구체 원인 후보

1. **현장의 ABR 강등을 ‘정적 상태의 오판’으로 확정할 수 없다.** 1801.log:458(18:04:28.188)은 `sentFrames=29 staticMode=0 clientReports=89 clientFirstReportAgeMs=9093 clientDecodedFps=1`이다. 직전 BWE는 18:04:25~28에 `appLimited=0 static=0 samples=15/19/26/18`. 23~24초의 appLimited=1을 강등 순간에 붙이면 안 된다. host_abr.hpp:263~327에는 이미 sparse/static일 때 강등을 막는 조건이 있다. `loss=0`만으로 실제 디코더 병목이나 보고 오류를 구별할 수도 없다.
2. **먼저 고칠 강한 소스 근거: PC 뷰어의 통계 마감 따라잡기 오류.** viewer_video_receiver_frame.cpp:115~157은 프레임 처리 때만 `flush_stats_if_due`를 호출하고, 출력 뒤 `statAtUs += 1s` 한다. FPS/bitrate는 실제 경과 시간으로 나누지 않은 개수/바이트다(:73~85, :122). Run 시작 시 마감만 초기화(:712)된다. 긴 무프레임/선택 대기 뒤 다음 프레임마다 지나간 마감 하나씩을 따라잡으면, 한 프레임을 1fps로 연속 publish할 수 있다. shared scheduler는 새 updatedQpcUs마다 즉시 전송(native_video_client_shared_core.cpp:617~625), host는 도착 시각으로 fresh 표시(host_control_session.cpp:1143)한다. **9초에 89회 보고와 정합적인 원인 후보**다. 현장 뷰어 원시 로그/재현 없이 사건의 유일 원인으로 단정하지는 않는다.
3. `sentFrames`는 엄밀한 wire 완료 수가 아니다. host_stage_encode_send_h264_au.cpp:570~584에서 UDP 큐 채택 시 증가하고, 합성 제외도 현재 encode 호출의 `servedBootstrap`을 본다. async MFT가 이전 입력 AU를 내는 경우 AU의 synthetic 출처와 다를 수 있다. M4에서 필요한 host 공급량은 **동일 구간의 실제 전송/입력 종류**로 정의하고, 필요한 부분만 기존 txFrames/해당 AU 출처에 맞춘다. 누적 count를 FPS로 쓰거나 서로 다른 통계 구간을 비교하지 않는다.
4. `burstWin2s`는 host_encoded_sender.cpp:561의 `max_window_bytes()`로 **관측 peak**다. 964975/1499972가 여러 초 유지된다고 현재 2초 창이 차 있거나 비어 있다고 판정할 수 없다. 또한 `wire seq`는 key 또는 hitch 때만 출력(:542~550)된다. 그 줄 수만으로 전체 실효 fps 6~9를 확정하지 않는다. 과대 AU/긴 직렬화 관찰 자체는 유효하다.
5. trailing kick 기본 지연은 0.5~1초가 아니라 `clamp(2/fps, 15ms,150ms)`(host_kick.hpp:65~80), 정기 static refresh는 별도다. `KickTryFill`은 두 경로 모두 같은 ‘trailing-edge kick’ 로그를 찍는다. 로그의 kick 간격만으로 두 원인을 합치면 안 된다. 화면 정지/최종 캡처/마지막 변화/encoder drain/정제 완료는 각각 분리해서 관측한다.

**M4 구현 계약:** PC viewer의 통계 창부터 실제 elapsed로 FPS·bps를 계산하고, 장기 공백 뒤 마감을 현재보다 앞으로 옮겨 과거의 1초 창을 프레임마다 재발행하지 않는다. 선택/재접속/수신 중지 경계, stale snapshot, 첫 부분 구간, synthetic-only 구간을 명시한다. 장기 공백 직후 한 번 나오는 저밀도 구간은 다음 정상 구간과 구별한다. 순수 타이머만 고쳐 모든 recv timeout을 무손실로 처리하거나 실제 no-frame/decoder stall을 숨기지 않는다.

필수 반례: 30~180초 무프레임/선택 대기 → 30/60fps 재개, source stats → 실제 PC control scheduler → host ABR까지. 이전 코드에서 report 폭주/가짜 1fps 강등이 나고 보수 뒤 사라지는지 검사한다. 실제 30fps 송신 대비 1~3fps decode·손실·증가 지연·보고 중단의 강등은 계속 작동해야 한다. ABR 임계 완화나 ‘BWE loss0이면 강등 금지’는 기본 보수로 넣지 않는다. 통계 보수 뒤에도 실제 source-sparse 오판이 남으면 동일한 fresh 구간 증거로 그 조건만 수정한다. 이번 범위에 PC Viewer 수정을 포함하며, 기존 Viewer와의 혼합도 기록한다. APK/shared wire 변경은 불필요한 상태로 시작한다.

### 2. M0: 빠르게 필요한 증거만 추가

- 실제 출력 IMFSample에서 `GetUINT64(MFSampleExtension_VideoEncodeQP)`의 HRESULT·값·valid를 읽고 AU에 결합한다. absent/error는 Unknown이며 MaxQP 설정값이나 GetValue로 대신하지 않는다. I/P별·인코더 instance별로 구분하고 byte 크기로 QP를 역산하지 않는다. 모든 vendor가 제공한다고 가정하거나 QP 미지원 때문에 전체 수정을 막지는 않는다. Microsoft 문서: [VideoEncodeQP](https://learn.microsoft.com/en-us/windows/win32/medfound/mfsampleextension-videoencodeqp).
- per-AU `codecInstance, media/input epoch, streamGeneration, acceptedInputOrdinal, inputWasForcedKey, rawIdr, synthetic/refresh reason, real content version, payload bytes`를 묶어 산출→enqueue→wire→receiver로 연결한다. 현재 `[keyframe] reasons`는 전역 keyReasons를 첫 AU에서 비우므로 다음 AU가 `encoder_gop`로 표시됐다는 것만으로 자연 IDR임을 확정하지 않는다(_au.cpp:603~667). 기존 per-AU provenance를 활용한다.
- M0의 상세 프레임 로그는 격리 측정 빌드/명시적 진단에서 유계로, 출하는 요약과 상태 변화 위주. 화면 원본·텍스트·비밀을 NAS 로그에 새로 싣지 않는다. 구현이 필요한 경우 Windows10 SDK/미지원 속성도 빌드/동작 가능해야 한다. 새 QP-map API/비트스트림 전체 QP parser는 필수 아님.
- 하네스는 실제 한글/영문 글꼴과 작은 UI 텍스트·스크롤·창 최소화/복원 장면을 사용한다. 이전 pseudo-glyph 테스트만으로 읽기 품질을 승인하지 않는다. 마지막 원본 프레임/이벤트 ID를 고정하고 **제품 receiver FrameBuffer 또는 표시 단계**에서 프레임을 수집한다. source clock 기준 이벤트와 receiver 시각을 같은 하네스 시계로 연결한다. host enqueue는 time-to-sharp의 끝점이 아니다.

### 3. M1: 기본은 유계 P 정제, ‘고품질 IDR 한 장’은 보장하지 않는다

**b를 수정한 안을 우선한다: quiet 전환 후 같은 최신 화면을 짧은 유계 P-frame 정제 구간으로 공급.** per-frame QP/bitrate 우회는 하지 않는다. a의 고품질 강제 IDR은 기본값으로 채택하지 않는다. IDR은 예측 참조를 초기화할 뿐 선명도를 보장하지 않으며, 큰 IDR이 원래 직렬화 증상을 재현할 수 있다. MaxQP는 실제 QP가 아니라 상한이고, 낮춘 ceiling이 특정 다음 출력 AU 한 장에만 적용된다는 보장도 없다.

- 시작 후보: 마지막 **실제 내용 변화** 뒤 150~200ms quiet, 최신 유효 캐시/변환 완료 확인 후 한 episode. 입력 부재만으로 정지 판정하지 않는다(스크롤 관성·원격 자체 애니메이션). 반대로 같은 그림 callback 반복이나 합성 refresh는 재arm 사유가 아니다. 저주기 실변화/새 글자 입력은 지체 없이 보낸다.
- episode는 `(media epoch, input epoch, generation, attachment/target identity, content version)`에 결합한다. 새 변화·선택·resize·재접속·잠금/secure 전환은 이전 episode를 취소/대체하며, `KickTryFill`의 유효성 검사를 유지한다. 마지막 변화가 input gate에 skip됐더라도 최신 캐시의 그 화면을 정제해야 한다. 과거 encoder 입력을 단순 재전송해 완료 처리하지 않는다.
- 초기 후보는 2~4회 accepted input의 짧은 정제, **최대 8회 accepted input/episode 및 quiet 판정 뒤 1초 제출 마감** 이내에서 측정으로 고른다. 모든 시도도 시간·횟수로 유계; 계속 미수락/무출력이어도 무한 loop 금지. 일반 capture 재개가 항상 우선이고, queue가 바쁘면 합성 정제를 쌓지 않는다. 매 1초 static refresh는 기존 별도 기능이며 이를 새 episode로 세지 않는다.
- ‘정제 입력 전부 넣음’과 ‘마지막 정제 AU가 나옴’을 분리한다. async MFT의 마지막 hold를 위한 기존 drain/kick은 필요할 수 있다. 전용 꼬리 처리도 episode의 유계 예산 안에 포함하고, 합성 하나를 넣을 때마다 무한 재arm하지 않는다. host는 decoded quality를 알지 못하므로 ‘sharp 완료’ 로그를 자체 선언하지 말고 requested/submitted/emitted/expired를 기록한다.
- strict WireLimiter·기존 2초 ledger·FEC/NACK/헤더 예산을 그대로 통과한다. `servedBootstrap`의 admit-always 예외를 정제 train에 그대로 붙여 큐 제한을 우회하지 않는다. 실제 수신기 재동기화/selection key 의무는 정제보다 우선이다. 정제만을 위해 reinit·epoch 증가·참조 AU 폐기를 하지 않는다.
- AMD가 motion ceiling에서 빠져나오는 것은 아래 M2 단일 정책을 통해 base로 복귀한다. NVIDIA는 현재 static MaxQP51 계획을 유지하며 settle마다 Set/재초기화하지 않는다. **P 정제만으로 품질 목표에 못 미치면 ‘한 장이면 해결’ 주장 대신** 유계 1-IDR 대조를 같은 하네스에서 측정한다. IDR 기본 전환은 크기/지연/품질 근거와 함께 후보에 명시해야 하며, 무제한 반복·cap 예외는 허용하지 않는다.

### 4. M2: AMD 빠른 상향은 governor와 하나의 소유자로

수용하되 **임시 setter를 한 군데 더 만드는 방식은 반대**한다. `RateGovernor`의 기존 2초 규칙은 모든 자극을 2초 기다려야 한다는 안전 계약이 아니다. 검증된 fast pressure 입력을 추가할 수 있다. C3의 no-op/instance 정합, C4의 현재/다음 seed 일치, R/E 분리 계약은 유지한다.

- 관측은 현재 instance의 **실제 AU 생성 bytes**, 종류/출처 및 sender 실제 debt/대기에서 얻는다. 기존 `skips×avgSentFrame` 추정은 큰 IDR로 부풀 수 있어 fast 입력의 근거로 쓰지 않는다. 비교 분모는 `PayloadBudgetFor(R,fps)/8/fps` 같은 지속 가능한 payload 예산이며 E 평균을 ‘프레임당 하드상한’으로 부르지 않는다. 한 장 >k배만으로 모든 장면을 motion으로 분류하지 않는다.
- 시작 후보: 과대한 **일반 P AU + backlog/직렬화 지연** 또는 짧은 연속 P 과대에서 fast pressure를 latch, **다음 encode 전에** 32→38/40 같은 유계 ceiling 선택을 비교한다. 첫 과대 AU를 보고 대응하므로 이미 인코딩한 첫 장까지 작게 만들었다고 주장할 수 없다. 이전 episode를 활용한 다음 onset 예측이 필요하면 별도로 측정한다.
- fast pressure와 slow governor가 하나의 desired ceiling을 계산하고 `ReconcileGovernorCeiling` 한 경로로 적용한다. slow tick의 calm/reset이 motion boost를 즉시 지웠다가 다시 올리는 싸움은 금지. quiet + 새 내용 없음 + 전송 backlog 소진에 대한 명시적 calm 입력으로 fast/slow 상태를 함께 복귀시킨다. target/fps 변경·새 instance·수동 설정은 우선하며, 과거 episode 종료가 새 desired 값을 덮어쓰지 않는다.
- 정책 초기 제한: QP 변경 최소 간격 250ms, 동일 값은 0 Set, 급상향/quiet복귀를 포함한 반복 토글 유계. 거절/stuck은 기존 3회 실패 정책과 instance 귀속을 유지하며, 값 교대/새 episode로 실패 예산을 끝없이 초기화하지 않도록 fast 경로 degraded를 둔다. reinit은 motion/settle의 정상 조절 수단이 아니다.
- **현재 `set_max_qp()`는 `apply_rate_control("governor")`로 다른 RC 속성까지 다시 쓴다**(mf_h264_codec.cpp:2017~2021). 빠른 변경을 추가한다면 QP 전용 적용/수락·readback 결과로 좁히고, mean/peak/VBV의 불필요한 반복 Set·강제 IDR·instance 교체가 0인지 확인한다. 불필요한 재호출은 측정기에서 sham도 지연을 만든 이력이 있다.
- 기존 legacy 전체를 ‘AMD에서 live 효과 입증됨’으로 확대하지 않는다. Microsoft는 MaxQP를 static 속성으로 정의한다([MaxQP 문서](https://learn.microsoft.com/en-us/windows/win32/medfound/codecapi-avencvideomaxqp)); 이 AMD MFT에서 관측한 live 동작은 해당 vendor/profile/driver 실증이다. fast 정책은 식별된 AMD legacy와 유효 응답에 한정하고 unknown legacy는 기존 정책 유지. NVIDIA runtime Set0·static51 유지. AMD 40~44 상시 cap은 비교 행만 허용하며, 이번 기본 정책으로 임의 상향하지 않는다.

### 5. M3: 500ms 내 이미 생성된 키를 버리면 안 된다

1801.log:1242~1243에서 두 key가 같은 ms에 enqueue된 것은 확인했다. wire 동시 송신이나 두 번째가 진짜 무요청 self-IDR이었다는 뜻은 아니다. M0의 AU ordinal/force flag/NAL5로 먼저 분류한다.

고칠 대상은 **중복된 host의 재량적 force 예약/소비**다. 실제 key가 해당 identity의 의무를 만족하면 scheduled/settle 요청을 결합하고 주기를 재기준화한다. 내부 MFT GOP와 host scheduler의 입력 수 기준(실입력/합성 포함), async 여러 출력 한 번 drain, force 수락 후 output 지연을 검사한다. 기존 in-flight KeyId·거절 backoff를 재사용한다.

500ms라는 전역 시각만으로 key/다음 delta를 폐기하거나 수신기 복구·새 epoch/selection 키를 막지 않는다. 이미 나온 IDR은 다음 P의 참조일 수 있다. 제품이 막을 수 없는 연속 자연 IDR까지 제거했다고 주장하지 않는다. 아직 입증된 중복 요청이 없으면 계측/원인 분류까지만 하고 임의 필터를 출하하지 않는다. forced-recovery 왕복·first screen·재접속·IDR 송신 중 새 입력 epoch·ForceKey 거절 경로는 회귀 필수다.

### 6. 측정·완료 조건

**M0/통계 수정만 적용한 기준 vs 전체 후보를 함께 둔다.** 구 0.2.157과 후보만 비교하면 ABR가 올린 해상도/fps 효과와 QP·정제 효과가 섞인다. 같은 실제 encode 크기/fps/R·내용·MFT/driver, 정상 static 도달 품질, capture 공급량을 고정한 짝 비교가 필요하다. baseline에 hang/누락/서로 다른 화면이 있으면 INVALID이며 임의 상대 PASS로 바꾸지 않는다.

- 실제 글꼴 UI: idle→최소화/복원·scroll 1초/정지·긴 scroll/정지·단일 글자 변경·연속 stop/start·고엔트로피 motion. 최종 정지 event 이후 원본이 안 변함을 하네스가 증명한다. 가독성 평가는 같은 크기/색 변환/ROI 정렬로 한다. 1080p와 720p를 섞어 PSNR 상승으로 부르지 않는다.
- **time-to-sharp 정의:** 유효한 긴 정지 reference의 text ROI 품질을 기준으로, 최종 event-ID 화면이 제품 receiver에 도달해 reference 대비 PSNR 1dB 이내를 처음 충족하고 후속 관찰 500ms 동안 나빠지지 않는 시각. 정지 뒤 새 frame이 없으면 FrameBuffer를 계속 표본화하여 안정 유지 여부를 확인한다. 문자 edge 오차/실제 crop을 함께 보고, 화면 전체 평균이나 단순 ‘PSNR의 X%’만으로 승인하지 않는다. baseline 자체가 흐리면 그 기준은 무효다. PGM/PNG before→worst→sharp 비교와 사람의 가독성 확인을 붙인다.
- **사전 목표(건강한 lossless 경로):** 4.5~6M text UI에서 time-to-sharp p95 ≤1초·어느 episode도 2초 초과 없음. 1.5M의 실제 ladder 크기는 별도 셀로 p95 ≤1.5초·최대 2초 목표. baseline이 유효하고 3초 문제가 재현되면 중앙값도 최소 50% 단축. 유한 표본에서 그 순간만 선명했다가 흐려지는 경우는 실패다.
- **onset:** 첫 반응 지연, 첫 250ms/1초의 서로 다른 실제 내용 frame 수, p95/max gap을 기록한다. 같은 장면의 정상 지속 motion 공급량 대비 첫 1초 전달량 ≥70%, 4.5~6M에서 첫 반응 p95 ≤200ms·첫 1초 최대 gap ≤250ms를 초기 합격선으로 한다. 1.5M는 최대 gap ≤500ms로 별도 보고. 기준 sustained motion 자체가 현저히 낮으면 이를 분모로 품질 목표를 낮추지 말고 fps 절대값과 사용자 설정을 병기한다. 반복 synthetic frame으로 fps를 채우거나 fps/해상도를 몰래 낮추면 실패다.
- 수치들은 이미 입증된 보장이 아니라 **이번 후보의 사전 검증 목표**다. M0에서 장비/경로의 물리적 하한으로 목표가 불가능함이 밝혀지면 원자료와 변경 제안을 먼저 남기고 기준을 확정한다. 실행 뒤 실패를 지우려고 합격선을 바꾸지 않는다. 실제 WAN 지연은 별도 보고하며 loss5% 대조에서 복구 의무·bounded queue·no storm을 검사한다.
- AMD 자동화: 현장과 맞는 DXGI monitor 캡처 경로를 격리 desktop/fixture로 포함(사용자 화면 조작 금지), WGC window 회귀도 포함. 4.5/6M·30/60fps·정적 UI는 1080p 유지, 저rate는 실제 크기 표기. 짧은 수십 episode(예 20회)와 독립 3회 반복으로 분포를 본다. MFT/QP seam 성공만으로 통과시키지 않는다. NVIDIA는 같은 fixture·측정기 및 static51/runtime writes0 회귀를 먼저 준비하고 **후보·하네스 독립 확인 후 Home 묶음 1회**로 요청한다.
- 부정 대조: 통계 마감 보수 제거→가짜 1fps/강등, settle 제출 제거→time-to-sharp 악화, fast pressure 제거→onset 악화(해당 개선을 주장할 때), 새로운 epoch에 옛 episode 재활성→검사 실패, recovery key를 blanket suppression→복구 실패. 독립 UDP wire trace로 기존 cap/FEC/NACK 한도 유지. wire·queue 지표와 품질/fps 지표를 모두 만족해야 한다.

### 7. 범위와 다음 담당

**M5 idle credit burst는 이번에 넣지 않는다.** 이미 있는 token-bucket 허용량과 clamp-only 예외를 확대하지 않는다. ‘idle에는 회선이 비었다’는 추정으로 새 credit을 발급하거나 epoch/reconnect마다 무료 여유를 주면 기존 계약을 바꾼다. M1~M4의 효과/남은 병목을 측정한 뒤 필요성이 입증될 때만 별도 정책 선택으로 다룬다.

이번 제품 범위는 Host/codec의 유계 motion·settle 조절, PC Viewer의 올바른 통계 창, 필요한 소규모 ABR 입력 정합과 키 요청 중복 보수까지다. AMD/NVIDIA 기본 지원을 유지하고 고정 44·CPU 기본 전환·GOP0·encoder 출력 프레임 임의 폐기·새 와이어는 포함하지 않는다. prelogin/회사 DXGI exit44 별건도 섞지 않는다.

검증용은 이 절을 반영한 **한 유효 지시서**로 작업용1 task를 연결하면 된다. M0로 원인/기준선을 고정한 다음 같은 task에서 위 보수와 AMD 행동 검증을 이어가고, 고정 후보+독립 결과를 한 묶음으로 회신한다. 후보가 없는데 계측 각 commit마다 Codex 승인을 다시 기다릴 필요는 없다. 본 판정은 설계/범위 GO이며 출하 OK는 아직 아니다.
