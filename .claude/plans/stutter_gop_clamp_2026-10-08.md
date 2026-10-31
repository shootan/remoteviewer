# 버벅임(주기 멈춤) 수정 — 검증용 안 r1 (2026-10-08, remote#gakap8v9 → Codex 검토)

사용자 우선순위: **잠금해제·재부팅(prelogin)보다 위**. 사용자 보고 "APK 로 회사 PC 접속 시 주기적으로 멈췄다 움직인다".

## 증거 (NAS, 출처 고정)
- 기기: 회사 PC host `51bd6657…`(DEV-SHOTAN-L, 0.2.153 pid 67388, 17:12:20~) ← 폰 SM-S948N-b7378c(APK) **relay 175.207.45.151:29190**.
  `runtime-config-applied bitrate=1500000 keyint=120 fps=30`, `wirecap applied bitrate=1500000`, encodeSize 1920x1080
  (DesktopText 바닥 kTextFloorPixels — encode_resolution_ladder.hpp:49, 의도된 동작).
- `h264 gop-config backend=mft_enum_hw requestedGop=120 setHr=0x00000000 readbackOk=1 readbackGop=30`(fps20 일 때 20, 22 일 때 22)
  — 회사 PC MFT 가 **GOP 를 프레임레이트(=1 s)로 clamp**. 누적 80회 전부 readback<requested.
  대조: 집 PC host `8ec6ecb1…` 는 requestedGop=60→readback 60 (125회), 30→30 — 정상.
  인코더 벤더/MFT 이름은 로그에 없음(미확인).
- `[keyframe] reasons=encoder_gop` 가 **정확히 30 seq 간격**(1.0~2.0 s)으로 30~110 KB. 17:18:43~55 에도 40~72 KB 지속.
- 1.5 Mbps(≈187 KB/s) hard cap 에서 IDR 1개 = 0.16~0.58 s 송신. 그동안 `decide_encode_admission` 이 delta 입력을 SkipOverloaded
  (host_encode_admission.hpp:48-49) → `client-present` gapMax 0.27~0.52 s(최대 1.3 s), IDR 직후마다(17:18:51.7 key→52.5 gap 275 ms,
  53.3 key→54.5 gap 523 ms). 폰 복구 키 요청 `reasons=viewer` 17:12:31·17:13:26·27·33 이 추가로 겹침.
- 0.2.138 시절(17:08~17:11)에도 동일 → 신규 회귀 아님. host 의 자체 scheduled key 는 realInputsSinceKey 가 매 IDR 마다 0 이라
  발동 안 함(host_stage_encode_send_h264.cpp:286, _au.cpp:577) — 주기 IDR 전부 인코더 자체 GOP.
- 참고 이력: 10-01 hard-cap 협의에서 Codex 가 "IDR 예외 burst" 를 **불채택**(plans/bitrate_hard_cap_2026-10-01.md:112).
  mf_h264_codec.cpp:1441-1445 — 더 긴 GOP 를 시도했다가 viewer_udp_recovery_test 가 4주기에서 회복 실패해 되돌린 기록.

## 검증용 안 (우선순위 순)
1. **인코더 자체 주기 IDR 제거(핵심)**: GOP clamp 감지(readback < requested) 시 대체 설정을 순서대로 시도하고 readback·실제 IDR
   간격을 로그로 확정 — (a) SetOutputType **전** 에 AVEncMPVGOPSize 설정(현재는 type 설정 뒤 :1447), (b) 0 / 0xFFFFFFFF(무한 의미
   인지 벤더별), (c) 그래도 clamp 면 MFT 이름·벤더를 로그하고 해당 인코더에서 **저비트레이트(cap ≤ ~4 Mbps)일 때만** 다른 방법
   (아래 3) 적용. 주기 IDR 없이도 회복은 viewer 요청 키(이미 동작)로 — 위 recovery_test 회귀를 이번에 재확인해야 함.
2. **IDR 크기를 cap 에 맞춤**: IDR 를 상한 안에서 짧게 끝내도록 key 전용 QP 하한/크기 목표(예: cap 의 ~150 ms 분량)를 둔다.
   hard cap 은 유지(예외 burst 아님). 방법: CODECAPI_AVEncVideoEncodeQP per-frame / MinQP·MaxQP 를 key 직전에만 조정 —
   MFT 지원 여부를 readback 으로 확인.
3. (1 이 막히는 인코더 한정 선택지) 저비트레이트에서 mft_enum_sw 폴백 또는 해당 MFT 의 GOP 를 fps 의 N 배로 맞추려 **입력 fps 를
   낮춰 보고**(MF_MT_FRAME_RATE 를 실제보다 작게 선언하면 clamp 가 프레임 수 기준이라 간격이 늘어날 수 있음 — 부작용 큼, 검토만).
4. 관측 보강: MFT friendly name/vendor id, 실제 encoder_gop 간격 통계, IDR 송신 시간(cap 기준)과 그 동안 skip 수를 한 줄로.

## 수용 기준(사용자 행동)
- 회사 PC host + APK relay 1.5 Mbps, 정지 화면/타이핑에서 `encoder_gop` IDR 이 **≥ 10 s 간격 이하로 줄거나 0**, client-present
  gapMax > 250 ms 가 IDR 주기와 무관해짐. 집 PC(clamp 없음) 회귀 없음. viewer_udp_recovery_test·abr·hard-cap 회귀 통과.
- 회사 PC 하드웨어는 이 PC 에서 재현 불가 → **게시 후 사용자 실기 1회**(APK 접속, 1분)로 확인. 그 전 자동 증거는 집 PC + 결정적
  시험(가짜 clamp MFT 경로)까지.

## Codex 에 묻는 것
- 1 의 시도 순서/값과 recovery 위험(주기 IDR 없이 회복 계약이 충분한가), 2 가 hard-cap 계약(예외 burst 불채택)과 맞는지,
  3 을 범위에 넣을지, 그리고 놓친 원인(예: 1080p DesktopText 바닥을 저비트레이트에서 유지하는 것이 맞는지).

## 추가 증거 (17:23~17:24, 중계 없음)
- 폰이 direct `211.218.222.1:43000|public` 로 붙고 APK 비트레이트 3 Mbps(`wirecap applied bitrate=3000000`). 그래도 `encoder_gop` IDR 이
  30 seq 간격(1.2~1.7 s)으로 90~143 KB → 3 Mbps(375 KB/s)에서 0.24~0.38 s 송신, client-present gapMax 280~490 ms 가 IDR 직후마다.
  → **relay 와 무관**. 원인은 경로가 아니라 "clamp 된 주기 IDR × hard cap". 비트레이트를 2배로 올려도 IDR 도 커져(1080p) 멈춤 폭이 비슷.
- 집 PC host 8ec6ecb1(0.2.152, GOP 60 정상 반영, 6 Mbps, PC 뷰어)도 `encoder_gop` IDR 이 60 seq 마다 ~150 KB(6 Mbps 에서 ≈0.2 s 송신):
  간격 분포 <2.5 s 75회 · 2.5~5 s 841회 · >5 s 322회(정지 화면은 프레임이 적어 길어짐). → **clamp 없는 PC 도 같은 구조, 빈도만 절반 이하**.
  범위는 "clamp 우회" 가 아니라 **주기 IDR 일반**(모든 인코더)으로 잡아야 함.

## r2 원인 정정 (사용자: "138 은 안 그랬다, 153 으로 올리자마자") — 위 "0.2.138 에도 동일, 회귀 아님" 은 **틀렸다**
같은 회사 PC·같은 폰, NAS host.log:
| | 0.2.138 (17:08:52~17:11:05) | 0.2.153 (17:12:20~17:30) |
|---|---|---|
| encoder_gop IDR | 111개, 평균 58 KB, 평균 1044 ms 간격 | 547개, 평균 66 KB, 평균 1569 ms |
| IDR `sendDurUs` | **3~7 ms** (79 KB 7064 us) | **290~580 ms** (116 KB 580547 us, queueWait 40 ms) |
| client-present 창 중 gapMax>250 ms | **3% (3/100)** | **34% (283/838)** |
→ 주기 IDR 은 138 에도 똑같이 있었다. 달라진 것은 **0.2.148 hard wire cap**(plans/bitrate_hard_cap_2026-10-01.md, 사용자 요청 "6 Mbps 설정인데 회선 15 Mbps")
이 IDR 을 상한 속도로 직렬화하는 것 — 10-02 에 이미 "0.2.148 회귀 ~0.23 s 멈춤" 으로 기록했고 현장에서 확정. 회귀다.
수정 안 재정리(Codex 의견 요청):
- (A) 즉시 완화: cap 기본 OFF(kill switch REMOTE60_NATIVE_WIRE_CAP 이미 있음, host_startup_config.cpp:317)로 0.2.154 — 138 동작 복귀, 대신 설정 초과 회선량 문제 재발.
- (B) cap 유지 + 주기 IDR 제거(모든 인코더; clamp 인코더는 우회 시도) + 요청 IDR 크기 제한 → 키는 드물게만, 그때 짧은 멈춤 감수.
- (C) cap 유지 + IDR 만 상한 예외 burst 후 뒤 delta 로 상환 — 10-01 Codex 불채택(250 ms 창 위반). 재검토 여부.
검증용 권고: 사용자 체감 회귀이므로 **A 를 먼저 게시**(빠름, 138 과 동일 송신)하고 B 를 정식 수정으로. 단 cap 은 사용자 본인 요청이었으므로 A 는 사용자 확인 사항.

## Codex 반론/합의

2026-10-08, 검증용 **추가 증거/r2 원인 정정까지** 반영한 설계 회신. 소스 기준은 출하 main `b7c9164`와 GOP 변경 이력 `60251d3`이다. 이 저장소 root checkout은 다른 과거 브랜치이므로 그 HEAD를 출하본으로 사용하지 않았다. NAS 수치는 검증용이 위에 제시한 자료를 인용하며, Codex가 원 NAS 로그/회사 PC를 직접 실행·측정한 것은 아니다.

### 결론과 선택지

**회귀로 취급하고 prelogin보다 먼저 고치는 데 동의한다. 원인은 'clamp만'으로 한정하지 않는다. 큰 주기 IDR을 hard cap으로 직렬화하는 공통 경로가 수정 대상이며, clamp는 그 빈도를 키우는 요인이다.** 138에도 IDR이 있었다는 사실만으로 '회귀 아님'이라고 할 수 없다. 동일 기기에서 제시된 sendDur/출력 간격 차이는 그 정정을 뒷받침한다. 다만 최종 증거에는 NAS 원문 경로·해시와 해당 seq/시간 구간을 연결한다.

| 안 | Codex 판단 |
|---|---|
| A: cap 기본 OFF를 우선 게시 | **기본값을 전 제품에서 자동으로 끄는 안은 채택하지 않는다.** 사용자가 회선 상한 초과를 허용하고 즉시 완화를 선택하면, 기존 kill switch를 이용한 대상 host/session의 임시 완화는 가능하다. 그 선택과 적용 범위·복원 방법을 명시해야 하며 '1.5/6 Mbps 상한 유지'라고 보고하지 않는다. prelogin에서 선택한 A안은 이 별도 A의 승인이 아니다. |
| B: cap 유지 + 주기 키 조정 + IDR 비용 축소 | **정식 수정의 권고안. 다만 '모든 주기 IDR을 즉시 제거'를 '유한 GOP/보완 복구를 검증하며 주기 키 빈도와 크기를 줄임'으로 고친다.** 아래 회복·품질 계약을 먼저 충족한다. |
| C: IDR 예외 burst 뒤 delta로 상환 | 기존 250 ms/1 s 상한 계약에서는 계속 불채택. 뒤에 적게 보내도 이미 초과한 창은 복구되지 않는다. 이를 원하면 사용자가 '짧은 burst 허용 평균 bitrate'라는 별도 정책을 선택해야 한다. |

사용자가 아직 stutter A/B를 선택하지 않았다면, 위 비용을 사용자 창구에서 설명하고 **즉시 완화 선택만** 확인한다. B의 독립 조사·격리 검증은 그동안 진행할 수 있다. 기존 보안 수정은 유지하며 0.2.138 전체 바이너리로 되돌리는 안은 제안하지 않는다. 버전/게시 범위는 후보와 통합 검증 뒤 정한다.

### 1. GOP 설정: 유한 값의 적용 순서를 시험하고, 자동 키와 host 주기 키를 함께 다룸

- 현재 `mf_h264_codec.cpp:1600–1605`는 type 구성 뒤 `apply_low_latency_codec_api`를 부른다. **같은 유한 requestedGop를 type 전/후에 넣어 보는 비교 실험은 동의**한다. 새 transform 인스턴스마다 pre-type/post-type/최종 설정과 readback을 구분하고, 실제 출력 NAL의 IDR 간격으로 효과를 판정한다. 마지막 기존 설정이 앞의 성공 값을 다시 덮지 않게 한다. 이 순서 변경이 해당 MFT에서 반드시 통한다는 보장은 아직 없다.
- `AVEncMPVGOPSize`는 **최대** GOP이며 scene change 등에 더 짧아질 수 있다. `0`은 encoder-dependent/자동 선택이지 주기 키 OFF가 아니다. `0xFFFFFFFF`도 공통 API가 보장하는 '무한' sentinel로 쓰지 않는다. IsSupported/IsModifiable 및 range/enumeration을 확인하고 지원되는 **유한** 후보부터 시험한다. [GOP 속성 계약](https://learn.microsoft.com/en-us/windows/win32/codecapi/avencmpvgopsize-property).
- 첫 비교는 requested=120의 실제 적용 여부다. 현재 30 fps에서 120 accepted inputs는 약 4초다. **10초 이상 간격을 목표로 하면 effective GOP/host schedule을 별도로 바꾸는 정책**이며 로그에 configured/effective를 구분한다. 우선 10초 정도의 유한 후보를 격리 시험하되 미검증 상태로 전체 backend에 적용하지 않는다.
- 특히 host의 `host_stage_encode_send_h264.cpp:286`은 `realInputsSinceKey >= activeKeyint`이면 scheduled key를 강제한다. 자동 GOP만 없애면 이 조건이 대신 발동하므로 **주기 IDR 제거가 되지 않는다**. `_au.cpp`의 모든 key에서 계수를 초기화하는 동작과 함께 설계한다. 첫 화면·선택/epoch 전환·복구 요청은 정상 주기 조정으로 지연시키지 않는다.
- 동작 불량 판정은 vendor 이름이나 cap≤4 Mbps만으로 하지 않는다. 실제 key 간격·wire 비용·생성/송신 backlog를 기준으로 한다. clamp 없는 집 PC도 포함하되, 기존 설정에서 문제가 없는 조합에 불필요한 재초기화를 하지 않는다. 시도 횟수·실패 복귀를 유계로 두고 실행 중 매 IDR마다 MFT를 다시 만들지 않는다.

### 2. 주기 키 제거 전의 회복 계약 — 현재 'viewer가 요청하니 충분'은 성립하지 않음

`60251d3`의 원 기록은 **4배 GOP에서 recovery 시험 4~8건 실패, 1배 복귀 후 3/3 통과**이며, 당시 APK decode health가 충분하지 않아 주기 키를 보완 복구로 남겼다고 적는다. 이를 단순히 옛 시험 잡음으로 지우지 않는다.

현재 APK에는 pending delta를 버렸을 때 `decoderKeyframeRequest_`를 올리고 shared session에서 소비하는 경로가 있다(`android_video_decoder.cpp:220,225,307`, `native_video_client_session.cpp:679–689`). 따라서 'APK는 요청이 전혀 없다'고 할 수도 없다. 그러나 모든 codec 초기화 실패·출력 정체·화면 복귀를 이 두 플래그 설정 지점이 포괄한다는 증거도 없다. **정상 자동 키 0을 기본값으로 삼으려면 이 누락까지 닫아야 한다.** 이번 첫 보수에서는 유한 보완 주기/유계 recovery를 유지한다.

필수 회복 증거:

1. viewer의 요청 생성 → 실제 control 송신 → host 수신/메일박스 → ForceKeyFrame 설정 HRESULT → **어느 ProcessInput이 실제 accept됐는지** → 해당 epoch의 NAL type 5 IDR(+필요 SPS/PPS) → 수신/디코드/후속 delta 정상 출력을 한 timeline으로 연결한다. `MFSampleExtension_CleanPoint`나 'key=1' 로그만으로 IDR 회복을 끝내지 않는다. 현 codec의 ForceKeyFrame setter 반환은 무시되고 있다(:1832–1834).
2. ForceKeyFrame은 다음 ProcessInput에 적용되는 계약이다. async output은 이전 input일 수 있으므로 force 호출 직후 반환 AU를 그 요청의 응답이라고 보지 않는다. pending/NOTACCEPTING/drain/retry를 포함한다. [ForceKeyFrame 문서](https://learn.microsoft.com/en-us/windows/win32/medfound/codecapi-avencvideoforcekeyframe).
3. 긴 GOP에서 손실/손상된 delta, 손상·꼬리 손실 IDR, 첫 요청 누락 후 재요청, 선택/epoch 전환, 정지 화면에서 마지막 frame 손실 후 **새 capture가 없는 복구**를 확인한다. 짧은 주기 키가 우연히 구해 주면 실패다. 강제 키를 무시/제거한 부정 대조는 같은 회복 단언에서 실패해야 한다. in-flight IDR 중복 억제와 송신 완료 뒤 진짜 추가 복구 요청 보존도 유지한다.
4. `viewer_udp_recovery_test`의 실제 receiver/FrameGate/decoder 경로는 재사용한다. 다만 FakeHost가 viewer의 pending request를 직접 소비하는 시험(:312,800 등)이므로 **host까지의 control 왕복 증거는 별도**다. PC rig 통과만으로 APK MediaCodec까지 통과라고 하지 않는다. APK를 바꾸지 않는 host 보수라면 현재 설치 APK와의 호환을 확인하고, shared/Android 복구를 바꾸면 APK 검증·게시가 추가 범위다.

유한 GOP의 프레임 수는 frame gating 중의 wall-clock 복구 상한과 다르다. 10초를 실제 상한으로 주장하려면 요청 timer와 held-frame/kick 또는 별도 유한 보완 경로로 그 시간을 입증한다. 주기 키를 길게 하는 대신 무응답을 10초 이상 방치하거나 복구 요청 자체를 rate-limit으로 삼키는 것은 수용하지 않는다.

### 3. IDR 크기: hard cap과 양립하지만 QP는 byte 상한 보장이 아님

**인코딩 전에 IDR 비용을 줄이는 방향은 hard cap과 양립한다.** 전송 limiter·입력 전 gate·NACK/FEC의 공통 예산은 그대로 유지한다. 이미 인코딩한 AU를 자르거나 참조 frame을 임의 drop하거나 key만 예외 전송하지 않는다.

1.5 Mbps에서 150 ms에 쓸 수 있는 총량은 약 **28,125 bytes**다. payload 목표는 여기서 packet header/FEC 및 앞서 대기 중인 전송·재전송 비용을 뺀 값이어야 한다. 30~110 KB AU의 `8*S/R`는 헤더·FEC·대기 등을 뺀 **직렬화 기준값**이지 실측 전송 지연이 아니다. 실제 token credit과 wire counter/송신 시작·완료로 대조한다. FEC 비율을 고정 25%로 가정하지 않는다.

QP 설정에는 초안대로 적용할 수 없는 부분이 있다.

- `AVEncVideoEncodeQP`는 VT_UI8이며 기본 QP만 사용하는 encoder도 허용된다. Microsoft H.264 encoder 문서상 Quality/fixed-QP 맥락의 속성을 현재 PeakConstrainedVBR에서 key 한 장만 조이는 기능이라고 가정하면 안 된다. [EncodeQP 계약](https://learn.microsoft.com/en-us/windows/win32/medfound/codecapi-avencvideoencodeqp), [H.264 encoder 설정](https://learn.microsoft.com/en-us/windows/win32/medfound/h-264-video-encoder).
- 선택적 `AVEncVideoEncodeFrameTypeQP`는 I/P/B별 VT_UI8 설정이지만 fixed-QP의 계약이다. MinQP/MaxQP는 전체 picture type 범위다. 전역 값을 'key 직전 올렸다 다음 호출에서 복원'하는 것만으로 async MFT의 **자동 발생 IDR 한 장**에만 적용됐다고 할 수 없다. 자동 IDR은 encode 뒤에 알게 될 수도 있다. [FrameTypeQP 계약](https://learn.microsoft.com/en-us/windows/win32/medfound/codecapi-avencvideoencodeframetypeqp).
- QP가 지원돼도 readback은 byte 상한의 증거가 아니다. 사용 RC mode, 실제 QP/출력 크기, 후속 delta 품질을 확인한다. 새 속성을 generic u32 helper로 넣거나 이 실험 때문에 전체 스트림을 무조건 fixed-QP로 전환하지 않는다. key 전용 제어가 실제로 지원되는 경로만 채택한다.

따라서 150 ms는 **최적화 목표**다. 모든 1080p 화면에서 strict cap·작은 IDR·텍스트 품질을 동시에 보장한다고 약속하지 않는다. 품질 한계 안에서 목표에 못 들어오면 그 사실을 기록하고 아래 대안/사용자 선택으로 연결한다.

### 4. 1080p 텍스트 바닥·fallback

- **이번 첫 보수에서 DesktopText의 1080p 면적 바닥은 유지**한다. 기존 코드는 비트레이트만으로 텍스트를 720p로 줄여 글자를 오인한 이력이 있어 이 바닥을 두었다(`encode_resolution_ladder.hpp:34–49`). source보다 확대하지 않는 원칙과 **검증된 지속 압력에서만 기존 ABR low가 바닥 아래로 내리는 예외**도 그대로다.
- 픽셀 수를 유지해도 key QP를 과도하게 올리면 글자가 뭉개진다. 작은 한글/숫자(0/8 등)의 IDR 직후와 안정 후 읽기, idle→단일 타이핑, 창 전체 전환을 포함한다. 정지 화면은 다음 delta가 드물어 거친 IDR의 품질이 오래 남을 수 있다. 개선을 숫자상의 1080p 유지로만 판정하지 않는다.
- **실제와 다른 MF_MT_FRAME_RATE 선언은 제외**한다. clamp가 G=fps라면 선언 fps를 내리고 실제 입력률을 유지할 때 GOP 시간 G/실제입력률은 오히려 짧아진다. 둘 다 내리면 대략 1초인 것은 같다. timestamp/RC/level까지 어긋나므로 이 문제의 해법으로 쓰지 않는다. 실제 fps 감소는 delta 부하를 줄이는 보조 수단일 뿐 큰 IDR 직렬화 자체를 없애지 못한다.
- clamp/키 크기 제어가 안 되는 backend의 **소프트웨어 encoder 대안은 격리 비교에 포함**해도 된다. 1080p에서 CPU 사용·encode p95·첫 화면·복구·지속 FPS를 측정한 뒤 채택하며 단지 4 Mbps 이하라는 이유로 모두 자동 전환하지 않는다. 채택 시 encoder 교체는 epoch/barrier·SPS/PPS·first IDR을 지키고 왕복 전환을 막는다. 더 낮은 해상도를 새 기본값으로 강제하는 안은 별도 화질 tradeoff이므로 이번에 조용히 넣지 않는다.

### 5. 구현/검증 순서와 완료 기준

검증용이 다음 내용을 한 지시서로 취합하는 데 동의한다. **이 절은 설계 회신이며 아직 구현 후보/게시 승인은 아니다.**

1. 선택된 MFT의 friendly name/CLSID/vendor(조회 실패도 표시), init/reinit 원인, configured/effective GOP·설정 시점·실제 IDR 간격, force 요청/accept/output 관계를 기록한다. `mft_enum_hw`만 보고 회사 PC를 AMD/Intel/NVIDIA로 단정하지 않는다.
2. 현재 baseline과 유한 GOP 설정 순서/간격 후보를 같은 콘텐츠·rate에서 비교한다. source 회복 경로와 hard cap은 유지한다. 여기서 해결되지 않는 조합에만 QP/소프트웨어 후보를 측정한다. clamp fake MFT는 실패/복귀 로직을 증명하며 **회사 driver에서 효력이 있다는 증거는 아니다**.
3. 최소 1.5 Mbps/30 fps와 3 Mbps, 집 PC 6 Mbps 대조, 실 MFT→송신→실 receiver/decoder를 사용한다. request/epoch/NACK 회복, 250 ms/1초 wire 상한, idr/대기/skip timeline을 함께 판정한다. IDR 크기나 횟수만 줄고 복구/응답이 나빠지면 실패다.
4. 10초 간격 또는 IDR 0은 단독 성공 조건이 아니다. **활성 타이핑/스크롤/marker 중 반복되는 >250 ms 멈춤을 줄였는지**, action→표시 지연 p50/p95/max와 missed marker, 첫 화면/복구 지연을 baseline 대비 본다. 정지 화면에서 입력/화면 변화가 없어 원래 frame을 안 만든 시간은 stutter로 세지 않는다. 전체 화면 전환·첫 IDR은 steady-state와 따로 보고한다.
5. Android `client-present gap`은 현재 codec output을 surface에 넘긴 시각/예약 시각 간격이다(`android_video_decoder.cpp:730–751`). 실제 view가 표시한 frame 계수는 별도다. 이 로그만으로 실제 화면 모든 정지를 측정했다고 하지 말고, 회사 PC→현재 APK 1분 실기에서 active event와 화면 표시를 확인한다. 직접/relay 중 하나만 원인이라고 가정하지 않는다.

현재 원 기기에서 재현할 수 없는 부분은 **자동 검증된 후보·회사 PC 실기 대기**로 남긴다. 집 PC+fake PASS만으로 회사 PC 끊김 해결을 선언하지 않는다. 임시 cap 해제를 사용자가 선택했다면 '상한을 풀어 회귀를 완화'와 '상한을 지키며 근본 수정'을 별도로 보고한다.

검증용 회신에서는 **B의 위 수정안 수용 여부**, stutter A에 대한 실제 사용자 선택 유무, NAS 원문/과거 긴 GOP 실패 자료의 위치를 묶어 주면 된다. 합의된 단일 안만 작업용1에 넘기고, Codex는 작업용에 별도 병렬 지시를 보내지 않는다. 직접 수행은 출하 source/이력·공식 API 문서 읽기와 이 계획 절 작성이며 제품 수정·빌드·설치·게시·commit/push 없음.

## 검증용 합의 (2026-10-08, Codex 회신 seq 수신 후)
- **B 수정안 = Codex 절 1~5 그대로 수용.** 내 r1 의 "0/0xFFFFFFFF 무한", "key 직전 QP 조정이 자동 IDR 에 적용", "IDR 0 기본" 은 철회.
- 사용자 선택: **B(정식) — "B 진행해야지, 정식수정해"**. stutter A(cap 해제)는 **선택 안 함** → 임시 kill-switch 적용 없음.
- NAS 원문 고정 사본(해시): `C:/work/gnlink-wt/fix-stutter-keyframe/.claude/evidence/` — company_host_1708_on.log 7506d79d…, phone_apk_17h.log c8b73059…,
  home_host_16_17h.log d3522ff6…(SHA256SUMS.txt). 원본: NAS /opt/gnlink/remote60-directory/logs/shotan/{51bd6657…,SM-S948N-b7378c,8ec6ecb1…}.
- 과거 긴 GOP 실패 자료: commit 60251d3 메시지("Tried and reverted: a 4x MFT GOP made viewer_udp_recovery_test fail reproducibly (4 and 8 cases)…
  Periodic key kept … the APK reports no decode health"). 시험 로그 원본은 저장소에 없음(미보존 가능성) — 재현으로 대체.
- 합의 수용 기준(사용자 행동): 회사 PC 상당 조건(1.5/3 Mbps, 30 fps, 1080p text floor)에서 **활성 타이핑/스크롤 중 반복되는 >250 ms 멈춤이
  baseline 대비 뚜렷이 감소**, 복구·첫 화면 지연 비악화, 250 ms/1 s wire 상한 유지. 회사 PC 는 게시 후 사용자 실기 1회(APK 1분).
- Codex 인계 확인: 실제 task **t-jytruuim**, 검증용 `remote#gakap8v9` → 작업용1 `remote#6jyin6x0` 발행을 버스에서 확인(조회 시 submitted).
  `task_stutter_keyframe_r1.md`가 합의한 B 절 1~5를 계약으로 연결하고 있음을 읽었다. 증거 사본 3개의 SHA-256을 직접 계산해 SHA256SUMS.txt와 **3/3 일치** 확인했다.
  이는 사본 무결성 확인이며 NAS/회사 PC 실기 재실행은 아니다. 다음은 작업용 고정 후보·검증용 독립 결과에 대한 검토다. 사용자 B 선택 확정, 임시 cap 해제 없음.
