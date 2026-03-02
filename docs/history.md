# remote60 창 기반 원격 모드 구현 메모

업데이트 일시: 2026-02-23

## 목표
- 데스크톱 전체 제어가 아닌 `창(Window) 단위` 원격 제어를 지원한다.
- 호스트 실제 마우스 포인터 이동을 최소화/회피하고, 선택된 창에 입력을 라우팅한다.
- 기존 데스크톱 모드를 즉시 되돌릴 수 있게 `define + env` 롤백 경로를 유지한다.

## 모드 전략
- 기존 모드: 데스크톱 캡처 + 기존 입력 경로
- 신규 모드: 선택 창 캡처 + 선택 창 입력 라우팅

### 빌드 토글(define)
- `REMOTE60_ENABLE_WINDOW_MODE`
  - `1`: 창 기반 모드 코드 활성화
  - `0`: 기존 데스크톱 모드만 사용

### 런타임 토글(env)
- `REMOTE60_WINDOW_MODE`
  - `1`: 창 기반 모드 사용(기본)
  - `0`: 기존 데스크톱 모드로 강제 롤백

## 롤백 절차
1. 즉시 롤백(재빌드 없이): `REMOTE60_WINDOW_MODE=0`으로 host 재시작
2. 완전 롤백(재빌드): `REMOTE60_ENABLE_WINDOW_MODE=0`으로 빌드 후 배포

## 구현 범위(MVP)
1. DataChannel 제어 메시지 추가
   - `window_list_request`
   - `window_select`
   - 응답: `window_list`, `window_selected`
2. Host
   - 상위 윈도우 열거 및 식별자 전달
   - 선택 창 캡처 대상 반영(사이클 재시작 기반)
   - 입력 이벤트를 선택 창으로 우선 라우팅
3. Web Client
   - 창 목록 표시
   - 창 선택/재선택 UI
   - 선택 창 제어 상태 표시

## 현재 구현된 DataChannel 제어 메시지
- 요청:
  - `{\"type\":\"window_list_request\"}`
  - `{\"type\":\"window_select\",\"windowId\":\"<uint64-string>\"}`
- 응답:
  - `window_list` (windows 배열 + selectedWindowId)
  - `window_selected` (ok/reason/title/restartRequested)

## 실행 기본값
- `automation/start_web_runtime.ps1` / `automation/run_web_runtime.ps1`에서 다음 기본값 사용:
  - `REMOTE60_WINDOW_MODE=1`
  - `REMOTE60_MESSAGE_CLICK_MODE=1`
  - `REMOTE60_PRESERVE_LOCAL_CURSOR=1`
  - `REMOTE60_SAFE_CLICK_MODE=1`
  - `REMOTE60_HIDE_LOCAL_CURSOR_FOR_INPUT=1`

## 회귀 검증
- `automation/verify_runtime_suite.ps1`
  - `continuous12s=0`
  - `reconnect1..3=0`
  - `overall_ok=True`

## 알려진 제약
- 최소화된 창은 캡처가 검게 나오거나 멈출 수 있다.
- 일부 앱은 백그라운드 메시지 입력을 제한한다.

## 다음 프로젝트 문서
- Parsec형 네이티브 저지연 설계: `NEXT_PROJECT_PARSEC_STYLE_PLAN.md`
- 네이티브 소켓 PoC(분리 프로젝트): `apps/native_poc/README.md`
  - Phase 1(video-only): `remote60_native_video_host_poc` + `remote60_native_video_client_poc`

## 네이티브 영상 PoC 현재 기준선
- 현재 안정 기준선은 `raw BGRA` 전송 경로다.
- 장점: 로컬 테스트에서 화면 깨짐 없이 반응성이 좋다.
- 단점: 대역폭 사용량이 매우 커서 외부망 서비스에는 부적합하다.
- 진행 방침: 이 기준선을 유지한 채, 인코딩/전송 최적화를 단계적으로 재도입한다.

## 네이티브 마일스톤/운영 자동화
- 마일스톤 문서: `NATIVE_VIDEO_MILESTONES_M0_M7.md`
- 실행/중지/상태:
  - `automation/start_native_video_runtime.ps1`
  - `automation/stop_native_video_runtime.ps1`
  - `automation/status_native_video_runtime.ps1`
- 검증:
  - `automation/verify_native_video_runtime.ps1`
  - `automation/wan_preflight_native_video.ps1`

## 다음 컨텍스트 인수인계 (이 섹션만 읽고 바로 진행 가능)
### 1) 현재 상태 요약
- `raw BGRA` 경로가 기준선이며, 체감 품질/지연이 가장 안정적이다.
- 이번 턴에서 `인코딩 본작업(H264 송수신)`은 하지 않았고, 인코딩 실험용 스위치/골격만 추가했다.
- 목표는 `현재 raw 성능을 기준선으로 고정`한 뒤, 다음 컨텍스트에서 인코딩 경로를 별도 실험으로 붙이는 것이다.

### 2) 이번에 실제로 수정된 파일
1. `apps/native_poc/src/poc_protocol.hpp`
- `RawFrameHeader`에 `sendQpcUs` 필드 추가.
- control 채널용 `ControlPingMessage`, `ControlPongMessage` 추가.
- `MessageType::ControlPing`, `MessageType::ControlPong` 추가.

2. `apps/native_poc/src/native_video_host_main.cpp`
- 신규 옵션:
  - `--codec` (기본 `raw`)
  - `--control-port`
  - `--tcp-sendbuf-kb`
  - `--trace-every`, `--trace-max`
- `sendQpcUs` 채워서 전송.
- optional control RTT 서버 스레드 추가(별도 TCP 포트).
- trace 로그 추가(`[native-video-host][trace] ...`).

3. `apps/native_poc/src/native_video_client_main.cpp`
- 신규 옵션:
  - `--codec` (기본 `raw`)
  - `--control-port`, `--control-interval-ms`
  - `--tcp-recvbuf-kb`, `--tcp-sendbuf-kb`
  - `--trace-every`, `--trace-max`
- `trace_recv`, `trace_present` 로그 추가.
- paint invalidation backlog 줄이기 위한 가드 추가.
- optional control RTT 클라이언트 스레드 추가(`[native-video-client][control] ...`).

4. `apps/native_poc/CMakeLists.txt`
- `REMOTE60_NATIVE_ENCODED_EXPERIMENT` 옵션 추가(기본 `OFF`).
- native video host/client에 compile definition 연결.

5. 자동화 스크립트 신규 추가
- `automation/start_native_video_runtime.ps1`
- `automation/start_native_video_runtime_impl.ps1`
- `automation/status_native_video_runtime.ps1`
- `automation/stop_native_video_runtime.ps1`
- `automation/verify_native_video_runtime.ps1`
- `automation/wan_preflight_native_video.ps1`

6. 문서 업데이트
- `apps/native_poc/README.md`
- `NEXT_PROJECT_PARSEC_STYLE_PLAN.md`
- `WORK_PROGRESS.md`
- `NATIVE_VIDEO_MILESTONES_M0_M7.md`

### 3) 확인된 실행 커맨드 (정상 동작)
1. 기본 검증(지연/RTT 요약):
```powershell
powershell -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 -RemoteHost 127.0.0.1 -Port 42030 -ControlPort 42031 -Fps 30 -HostSeconds 10 -ClientSeconds 6
```

2. 백그라운드 실행:
```powershell
powershell -ExecutionPolicy Bypass -File automation/start_native_video_runtime.ps1 -Port 42050 -ControlPort 42051 -Fps 30 -StartClient
powershell -ExecutionPolicy Bypass -File automation/status_native_video_runtime.ps1
powershell -ExecutionPolicy Bypass -File automation/stop_native_video_runtime.ps1
```

3. WAN 사전점검:
```powershell
powershell -ExecutionPolicy Bypass -File automation/wan_preflight_native_video.ps1 -RemoteHost 127.0.0.1 -VideoPort 42030 -ControlPort 42031
```

### 4) 다음 컨텍스트에서 바로 할 일 (우선순위)
1. Baseline 고정
- `verify_native_video_runtime.ps1`를 동일 조건으로 3~5회 실행.
- `LAT_AVG_US`, `LAT_P95_US`, `LAT_MAX_US`, `CTRL_RTT_*` 기록.

2. 인코딩 실험 분기 시작
- 기본 raw 경로는 절대 건드리지 말고 분기 코드로만 진행.
- 빌드 시 필요하면 `REMOTE60_NATIVE_ENCODED_EXPERIMENT=ON` 사용.

3. H264 최소 경로 붙이기
- host: capture -> encode(H264) -> send
- client: recv -> decode(H264) -> render
- 기존 trace 포맷(`capture/send/recv/present`)으로 raw와 동일 비교.

4. 승격 기준
- 화면 깨짐 없음
- connect/disconnect 안정성 유지
- raw 대비 대역폭 유의미 감소 + 지연 목표 유지

### 5) 중요 주의사항
- 지금 유저 피드백 기준으로 `raw`가 가장 만족도가 높다.
- 따라서 인코딩 작업은 반드시 `feature flag` 뒤에서만 진행하고, 기본 경로를 바꾸지 않는다.
- 이번 컨텍스트의 목적은 “밑작업 + 운영 자동화 + 인수인계”이며, 인코딩 최적화 자체는 다음 컨텍스트 작업이다.

### 6) 실행 마일스톤 (이번 범위: M8~M14)
1. M8 Baseline Lock (raw)
- `verify_native_video_runtime.ps1` 동일 조건 3~5회 실행.
- `LAT_AVG_US`, `LAT_P95_US`, `LAT_MAX_US`, `CTRL_RTT_*`, `mbps` 기준선 고정.

2. M9 Encoded 분기 스위치 완성
- `REMOTE60_NATIVE_ENCODED_EXPERIMENT` 빌드 define + `REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1` 런타임 게이트에서만 `--codec h264` 활성화.
- 기본/운영 경로는 계속 `raw`.

3. M10 H.264 최소 E2E 경로
- host: `capture -> encode(H264) -> send`
- client: `recv -> decode(H264) -> render`
- `capture/send/recv/present` trace로 raw와 동일 비교.

4. M11 깨짐/복구 안정화
- AU 경계/키프레임 재동기화 우선.
- 프레임 드롭은 최신 프레임 우선 정책 유지.

5. M12 지연 최적화
- 큐 깊이 최소화, backlog 억제, socket buffer/pacing 튜닝.
- p50/p95 지연과 체감 입력 응답 기준으로 조정.

6. M13 대역폭/FPS 튜닝
- 대역폭 절감(예: 약 1.1Mbps 목표 구간)과 FPS 상향의 균형점 탐색.
- 수치는 raw 기준선 대비로 판단.

7. M14 입력 채널 결합
- 기존 control 채널 구조를 입력 채널로 확장.
- 영상 백프레셔와 입력 경로 분리 유지.

※ 이번 범위는 `M8~M14`까지이며, `M15(WAN 리허설/승격)`은 제외한다.

### 7) 실행 결과 스냅샷 (2026-02-23)
- M8 완료: raw 기준선 5회 측정 완료.
  - `LAT_AVG_US` 대략 `14.1k ~ 16.0k`
  - `LAT_P95_US` 대략 `14.3k ~ 15.3k`
- M9~M11 완료: H264 최소 E2E + 키프레임/재동기화 가드 + decode reset 경로 연결.
- M12~M13 진행중:
  - 최신 로컬 측정에서는 H264 지연이 `1s~3s`까지 관측됨(체감/로그 모두 일치).
  - 대표 로그:
    - `automation/logs/verify-native-video-20260223-140122` (raw): `STAGE_TOTALUS_AVG_US=14831`
    - `automation/logs/verify-native-video-20260223-135408` (h264): `STAGE_TOTALUS_AVG_US=2354802`, `PRESENT_GAP_OVER_3S=1`
    - `automation/logs/verify-native-video-20260223-140044` (h264+저지연 속성): `STAGE_TOTALUS_AVG_US=1106620` (개선됐지만 여전히 1초대)
- M14 완료: control 채널에 입력 이벤트/ack 메시지 결합, 영상 경로와 분리 유지.

### 8) 왜 기존 WebRTC보다 현재 Native H264가 더 느린가 (핵심 정리)
- 결론: 전송(TCP/로컬) 문제가 아니라 `H264 코덱 파이프라인 내부 큐`가 주병목이다.
- 근거 1 (host): `cb2eUs`(캡처콜백->인코드시작)는 보통 `40~70ms`인데, `encQueueUs`가 `600~1200ms`까지 커진다.
  - 예: `automation/logs/verify-native-video-20260223-135408/host.out.log`
- 근거 2 (client): `decodeQueueLagUs`가 `1.3~1.6s` 수준으로 추가 누적된다.
  - 예: `automation/logs/verify-native-video-20260223-135408/client.out.log`
- 근거 3 (raw 비교): 같은 로컬 환경에서 raw는 `~15ms` 수준이므로 네트워크/렌더 기본 경로 자체는 정상이다.
- 해석:
  - 현재 선택한 `MF H264 MFT` 경로는 구현 난이도/개발 속도 측면에서 유리했지만,
  - 현재 설정/조건(1080p30 + 1.1Mbps)에서는 Parsec류처럼 저지연 동작을 보장하지 못한다.

### 9) 어떻게 빨라지게 만들 것인가 (우선순위)
1. 단기 운영 원칙
- 기본 경로는 `raw` 유지(회귀 방지), H264는 실험 플래그 뒤에서만 진행.
- 3초급 지연 재발 시 즉시 raw로 롤백.

2. H264 임시 개선(오늘 가능한 범위)
- `1080p30@1.1Mbps` 조합 사용 금지.
- 해상도/프레임/비트레이트를 코덱 큐가 생기지 않는 조합으로 재탐색(예: 720p, 더 높은 bitrate, 적절 fps).
- 수용 기준: localhost `p95 < 200ms`, `PRESENT_GAP_OVER_1S=0`.

3. 구조 개선(본해결)
- MF H264 의존 경로에서, 실시간 제어가 가능한 하드웨어 인코더 경로(NVENC/QSV/AMF)로 전환.
- 인코더/디코더 모두 `B-frame=0`, lookahead off, 작은 버퍼, stale frame drop 정책을 강제.
- 필요 시 전송도 저지연 프로토콜(UDP/WebRTC 계열)로 교체해 head-of-line blocking을 줄인다.

### 10) Parsec 수준 목표 전체 마일스톤 (Phase 1~3)
목표 KPI(초안)
1. 최대 해상도는 `1080p(FHD)`로 제한.
2. 기본 목표: `1080p60`에서 `E2E p95 <= 120ms`, `p99 <= 180ms`
3. 네트워크 악화 시 자동 폴백: `720p60`로 낮춰 `E2E p95 <= 100ms` 유지
4. 입력 RTT: `p95 <= 30ms`
5. 10분 연속 세션에서 프리징/끊김 0회

Phase 1: 저지연 코어 전환
1. P1-M1 파이프라인 추상화 분리
- 코덱/전송 백엔드(`MF/TCP` vs 신규 경로)를 런타임 스위치로 분리.
2. P1-M2 영상 전송 UDP 계열 전환
- 영상 경로를 TCP에서 분리해 head-of-line blocking 제거.
3. P1-M3 하드웨어 인코더 1차(NVENC 우선)
- `B-frame=0`, `lookahead=off`, low-latency preset 강제.
4. P1-M4 하드웨어 디코더 + Direct 렌더
- CPU 복사 최소화, 디코드->프레젠트 직결.
5. P1-M5 큐 상한 정책 고정
- capture/encode/recv/decode queue depth 1~2로 제한, stale frame drop.
6. P1-M6 프레임ID 기반 계측 완성
- `capture->encode->send->recv->decode->present` 전 구간 p50/p95 자동 리포트.
7. P1-Gate
- Local `1080p60`에서 `E2E p95 <= 120ms`.

Phase 2: 고프레임/고해상도 확장
1. P2-M1 캡처 경로 최적화
- WGC->GPU 경로에서 c2e 지연 최소화.
2. P2-M2 QSV/AMF 백엔드 추가
- NVIDIA/Intel/AMD 공통 API 계층 확보.
3. P2-M3 ABR 제어기 구현
- RTT/손실/큐 길이 기반 비트레이트/FPS/해상도 동적 조정.
4. P2-M4 전송 튜닝
- pacing/FEC/재전송 정책 최적화.
5. P2-M5 입력 채널 우선순위 고정
- 영상 혼잡 시에도 입력 지연 상한 유지.
6. P2-M6 고성능 프리셋 확정
- `1080p60`(기본) + `720p60`(폴백) 프리셋 검증/문서화.
7. P2-Gate
- Local `1080p60 p95 <= 120ms` 달성 + 폴백 조건에서 `720p60 p95 <= 100ms` 달성.

Phase 3: 프로덕션 안정화
1. P3-M1 연결성 강화
- NAT/relay/fallback 자동 전환.
2. P3-M2 세션 복구
- 재연결/해상도변경/모니터전환 무중단 복구.
3. P3-M3 장시간 안정성 테스트
- 30~60분 soak + 손실/지터 시뮬레이션 자동화.
4. P3-M4 보안/권한 모델 확정
- 인증/암호화/입력권한 secure-by-default.
5. P3-M5 관측/회귀 관리
- 릴리스별 KPI 대시보드/회귀 알람.
6. P3-M6 배포/롤백 전략 확정
- 기능 플래그 기반 즉시 롤백 가능.
7. P3-Gate
- 목표 KPI 달성 + 회귀 테스트 풀패스.

### 11) 히스토리 정리 상태
- 현재 히스토리는 아래 문서/로그로 분리 관리 중이다.
- 설계/결정/현재상태: `구현.md`
- 실행 단위 마일스톤: `NATIVE_VIDEO_MILESTONES_M0_M7.md`
- 날짜별 작업 로그: `WORK_PROGRESS.md`
- 실측 증거 로그: `automation/logs/verify-native-video-*`, `automation/logs/native-video-live-*`
- 운영/재현 스크립트: `automation/start_native_video_runtime.ps1`, `automation/verify_native_video_runtime.ps1`, `automation/analyze_native_video_scene_timeline.ps1`

### 12) Phase 1 진행 현황 (2026-02-23)
완료
1. `P1-M1` 전송 추상화 1차 반영
- host/client에 `--transport tcp|udp` 스위치 추가.
- 기존 TCP 경로 기본값/호환 유지.

2. `P1-M2` UDP 영상 경로(H264) 연결
- UDP 핸드셰이크(`Hello/HelloAck`) 추가.
- UDP 프레임 청크 전송/수신(재조립) 경로 추가.
- 현재 단계에서 `raw over udp`는 미지원(명시적으로 차단).

3. `P1-M5` 큐 상한 정책 일부 반영
- UDP 재조립은 단일 in-flight 프레임 기준으로 stale/drop 처리.
- 기존 클라이언트 latest-frame 렌더 경로 유지.

4. `P1-M6` 계측 유지/확장
- 기존 `trace_recv/trace_present` 유지.
- `verify_native_video_runtime.ps1`에 `TRANSPORT` 출력 추가.

검증 커맨드
1. TCP 기준
```powershell
powershell -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 -Codec h264 -Transport tcp -Bitrate 1100000 -Keyint 15 -FpsHint 30 -NoInputChannel
```

2. UDP 경로
```powershell
powershell -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 -Codec h264 -Transport udp -ControlPort 0 -Bitrate 1100000 -Keyint 15 -FpsHint 30 -NoInputChannel
```

현재 관찰 요약
- UDP 경로 자체는 동작 확인됨.
- 그러나 전체 지연 병목은 여전히 코덱 내부 큐(`c2e/encQueue`)에 남아 있어, `P1-M3/M4`(하드웨어 저지연 인코더/디코더 경로) 구현이 필수다.

### 13) Phase 1 추가 진행 (2026-02-23, 저녁)
반영
1. `P1-M3` 인코더 백엔드 선택 로직 1차
- `mf_h264_codec`에 `MFTEnumEx` 기반 하드웨어 MFT 우선 선택 추가.
- 실패 시 소프트웨어 MFT, 마지막으로 `CLSID_CMSH264EncoderMFT` 폴백.
- host 로그에 `H264 encoder backend=... hw=...` 출력 추가.

2. `P1-M4` 디코더 백엔드 선택 로직 1차
- 디코더도 동일하게 하드웨어 우선/소프트웨어 폴백 구조로 정렬.
- client 로그에 `H264 decoder backend=... hw=...` 출력 추가.

3. `P1-M5` 추가 보강 (지연 누적 방지)
- 클라이언트에서 stale 프레임 드롭(`kStaleCaptureDropUs`) 추가.
- catchup 진입 조건에 `decodeQueueLag`를 포함해 큐 누적 시 더 빨리 keyframe 재동기화.
- catchup 임계치를 3초급 누적 지연 방지 목적에 맞게 보수적으로 조정.

4. 운영 디버깅 보강
- host/client 시작 시 `stdout/stderr` 즉시 flush(`unitbuf`) 적용.
- 강제 종료 상황에서도 로그 유실을 줄여 병목 추적 가능성 개선.

검증 상태
- 빌드 성공:
  - `cmake --build build-native2 --config Debug --target remote60_native_video_host_poc remote60_native_video_client_poc`
- 런타임 스모크:
  - `start_native_video_runtime.ps1`로 host/client 기동 및 세션 파일/상태 확인.
  - 현재 이 실행 환경에서는 WGC 캡처 단계 이후 프레임 진행 로그가 제한적으로 관측되어,
    `p95` 지연 수치 재평가는 실제 인터랙티브 세션(사용자 측 수동 체감 + trace)에서 이어서 수행 필요.

### 14) 지금 시점 완료/미완료 (재개 기준)
완료
1. `P1-M1` 전송 추상화(`tcp/udp`) 반영.
2. `P1-M2` H264 UDP 분기(청크/재조립/핸드셰이크) 반영.
3. `P1-M3` 인코더 하드웨어 우선 선택(소프트/CLSID 폴백 포함) 1차 반영.
4. `P1-M4` 디코더 하드웨어 우선 선택(소프트/CLSID 폴백 포함) 1차 반영.
5. `P1-M5` stale frame drop/catchup 보강 + 저비트레이트 자동 720p 인코딩 폴백.
6. `P1-M6` trace/운영 스크립트 확장(transport/encode-size 가시화).

미완료
1. `P1-M3/M4` 실사용 GPU별(특히 NVENC/QSV/AMF 경로 분리) 최종 고정.
2. `P1-Gate` 수치 달성 검증:
- `1080p60` 기준 `E2E p95 <= 120ms`
- 폴백 `720p60` 기준 `E2E p95 <= 100ms`
3. 장면 변화가 큰 인터랙티브 세션 기준에서 `3s 지연` 재현 로그의 병목 재식별/수치화.

다음 바로 실행(미완료부터)
1. H264 강제 720p로 1차 확인:
```powershell
powershell -ExecutionPolicy Bypass -File automation/start_native_video_runtime.ps1 -Port 42470 -ControlPort 0 -Transport tcp -Codec h264 -Bitrate 1100000 -Keyint 15 -Fps 30 -FpsHint 30 -EncodeWidth 1280 -EncodeHeight 720 -TraceEvery 30 -TraceMax 200 -NoInputChannel -StartClient
```
2. 10~20초 조작 후 로그 분석:
```powershell
powershell -ExecutionPolicy Bypass -File automation/stop_native_video_runtime.ps1
powershell -ExecutionPolicy Bypass -File automation/analyze_native_video_scene_timeline.ps1 -LogDir "<status에서 나온 LOG_DIR>"
```
3. 같은 조건 UDP 비교(transport만 udp) 후 `scene_timeline.csv` 병목 비교.

### 15) 2026-02-23 야간 추가 반영 (이번 턴 실제 적용)
적용 내용
1. 백엔드 선택 안정화
- `mf_h264_codec` 백엔드 모드 파서를 정리:
  - 기본값(`REMOTE60_NATIVE_ENCODER_BACKEND`/`DECODER_BACKEND` 미지정) = `mft_sw` 고정
  - `mft_hw`/`mft_auto`는 명시 지정 시에만 시도
- 목적: 기본 실행에서 하드웨어 경로 초기화 hang으로 무프레임이 되는 회귀 방지.

2. 클라이언트 종료/캐치업 로직 수정
- 프레임이 안 들어올 때도 `--seconds`로 정상 종료되도록 메시지 루프를 `PeekMessage` 기반으로 변경.
- catchup 중 keyframe 복귀 조건을 단순화(키프레임 도달 시 즉시 exit)해 keyframe starvation 방지.
- catchup 임계치 재조정:
  - 과도한 드롭(거의 정지 화면) 방지 위해 SW 경로 기준값으로 상향.

3. 로그 분석 스크립트 내 경로 정규화 보강
- `automation/analyze_native_video_scene_timeline.ps1`에서 터미널 복사 시 섞이는 비가시 문자 제거.

4. 인코더 stale-guard 도입/조정
- host에 stale encoded frame 보호 로직을 추가했으나, 기본 ON 시 무프레임이 되어 기본값은 OFF로 전환.
- 필요 시 `REMOTE60_NATIVE_GUARD_STALE_ENCODED=1`로만 실험적으로 사용.

5. AMF 하드웨어 백엔드 코드 경로 추가(1차)
- `REMOTE60_NATIVE_ENCODER_BACKEND=amf_hw` / `REMOTE60_NATIVE_DECODER_BACKEND=amf_hw` 지원 추가.
- 인코더:
  - AMD H264 MFT CLSID 직접 경로 + 이름 매칭 열거 경로 추가.
- 디코더:
  - AMD D3D11 Hardware Decoder MFT CLSID 직접 경로 + 이름 매칭 열거 경로 추가.
- 디버그 로그:
- `REMOTE60_NATIVE_DEBUG_CODEC=1` 시 코덱 초기화 단계 로그 출력.

6. AMF 백엔드 실제 동작 상태(현재)
- 인코더(`EncoderBackend=amf_hw`)는 초기화/타입 협상까지 성공하고 `hw=1`로 동작 확인.
- 디코더(`DecoderBackend=amf_hw`)는 현재 `configure_input_type` 단계에서 실패(초기화 실패).
- 따라서 현재 실사용 조합은 `encoder=amf_hw`, `decoder=mft_sw`.

실측(로컬, `encoder=amf_hw`, `decoder=mft_sw`)
- 로그: `automation/logs/verify-native-video-20260223-213648`
- `STAGE_TOTALUS_AVG_US=108840.33` (약 109ms)
- `STAGE_C2EUS_AVG_US=66646.5` (기존 SW 인코더 대비 큰 폭 감소)

검증 결과(로컬)
1. SW 기본 경로 정상 송수신:
- 로그: `automation/logs/verify-native-video-20260223-205802`
- `STAGE_TOTALUS_AVG_US=679425.86` (약 0.68s)
- 병목: `STAGE_C2EUS_AVG_US=640423.71` (호스트 인코더 내부 대기)

2. 강제 HW 경로는 현재 실패:
- 로그: `automation/logs/verify-native-video-20260223-205821`
- `EncoderBackend=mft_hw`, `DecoderBackend=mft_hw`에서 프레임 0개(컨트롤 채널만 동작).
- 결론(당시 시점): 해당 빌드 상태에서는 `mft_hw`가 실사용 불가였다.

현재 결론
- 3초급 누적은 현 설정에서 재현 빈도가 낮아졌지만, SW MFT 기준 `~0.6~0.7s` 지연 바닥이 남아 있다.
- Parsec 목표(100~120ms급) 달성을 위해서는 `P1-M3/M4`를 진짜 하드웨어 저지연 경로(NVENC/QSV/AMF)로 고정해야 한다.
- 현재 워크스페이스/기본 경로 기준 `nvEncodeAPI.h`/`nvEncodeAPI64.lib`는 미검출 상태라, NVENC 실백엔드 구현은 SDK 준비가 선행돼야 한다.

### 16) 2026-02-23 심야 후속 (이번 턴)
반영
1. 디코더 지연 계측 정확도/호환성 보강
- `mf_h264_codec` 디코더에 `MF_E_NOTACCEPTING` 처리 추가:
  - 출력 drain 후 input 재시도.
- 디코더 출력 sample-time 누락 시 입력 타임스탬프 fallback 매핑 추가.
- 하드웨어 디코더 출력에서 contiguous buffer 변환 실패 시,
  `IMF2DBuffer` 기반 NV12 2D 복사 fallback 추가.

2. 계측 가시성 추가
- `DecodedFrameNv12`에 `sampleTimeFromOutput` 필드 추가.
- client trace에 `tsSource=mft|input_fallback|header_fallback` 출력.
- `verify_native_video_runtime.ps1`에 다음 지표 추가:
  - `TS_SOURCE_MFT`
  - `TS_SOURCE_INPUT_FALLBACK`
  - `TS_SOURCE_HEADER_FALLBACK`

3. 최신 실측 업데이트 (localhost)
1) `encoder=mft_hw`, `decoder=mft_hw`
- 로그: `automation/logs/verify-native-video-20260223-214859`
- `STAGE_TOTALUS_AVG_US=67123.3` (약 67ms)
- `STAGE_C2EUS_AVG_US=27146.6`
- `TS_SOURCE_HEADER_FALLBACK=0`

2) `encoder=amf_hw`, `decoder=amf_hw` 요청
- 로그: `automation/logs/verify-native-video-20260223-214743`
- 실제 decoder backend는 `mft_enum_hw`로 폴백 동작.
- `STAGE_TOTALUS_AVG_US=74498.54` (약 74ms)
- `TS_SOURCE_HEADER_FALLBACK=0`

3) `encoder=mft_hw`, `decoder=mft_hw`, `bitrate=4Mbps` (1080p)
- 로그: `automation/logs/verify-native-video-20260223-214923`
- `STAGE_TOTALUS_AVG_US=144685.33` (약 145ms)
- `MBPS_AVG=2.2`

현재 해석
- 이전 문서의 "`mft_hw` 강제 시 실사용 불가" 상태는 최신 코드 기준으로 해소되었고,
  현재는 `mft_hw` 송수신이 로컬에서 안정 동작한다.
- 다만 저비트레이트(자동 720p) 구간과 1080p 구간의 지연 차이가 커서,
  다음 우선순위는 `c2eUs`(capture->encode start) 축소를 위한 캡처/인코더 큐 튜닝이다.

### 17) 2026-02-23 심야 후속 2 (P1-Gate 재측정)
적용
1. H264 저지연 실험 플래그를 기본 경로에서 분리
- 기본값은 안정 동작 유지.
- 필요 시에만 아래 env로 실험:
  - `REMOTE60_NATIVE_H264_NO_PACING=1`
  - `REMOTE60_NATIVE_GUARD_STALE_PREENCODE=1`
  - `REMOTE60_NATIVE_CAPTURE_POOL_BUFFERS=1..4`

2. 게이트 수치 재확인
1) `720p60` (`bitrate=1.1Mbps`)
- 로그: `automation/logs/verify-native-video-20260223-220641`
- `STAGE_TOTALUS_AVG_US=75233.71`
- `LAT_P95_US=66580`

2) `1080p60` (`bitrate=4Mbps`)
- 로그: `automation/logs/verify-native-video-20260223-220659`
- `STAGE_TOTALUS_AVG_US=135371.67`
- `LAT_P95_US=137925`
- `PRESENT_GAP_OVER_3S=1`

현재 결론(업데이트)
- `720p60`는 목표권으로 동작.
- `1080p60`는 아직 목표(`p95<=120ms`) 미달.
- 따라서 다음 직접 과제는
  1) 1080p60 전용 코덱/큐 튜닝
  2) 임계치 초과 시 720p로 자동 폴백하는 적응 정책 고도화
이다.

### 18) 2026-02-24 새벽 후속 (UDP 기본화 + 디코드/렌더 병목 완화)
적용
1. 전송 기본값 정책 정리
- `h264`일 때 기본 `transport=udp`, `raw`일 때 기본 `transport=tcp`로 동작하도록 host/client/스크립트 정리.
- TCP fallback(`--transport tcp`)은 유지.

2. UDP 소켓 버퍼 기본 상향
- host UDP send buffer 기본 1MB.
- client UDP recv buffer 기본 1MB, send buffer 기본 256KB.
- 필요 시 JSON/인자로 override 가능.

3. 클라이언트 catch-up 임계치 조정
- multi-second 누적을 더 빨리 자르도록 drop/catch-up 임계치를 하향 조정.

4. JSON 실행 경로 확장
- `run_native_video_with_config.ps1`에서 다음 항목 지원:
  - `udpMtu`
  - `tcpSendBufKb`
  - `tcpRecvBufKb`
- `native_video_profile_720p.json` / `native_video_profile_1080p.json`에 위 항목 반영.

5. 디코드 후 색변환 CPU 경로 최적화
- `nv12_to_bgra()`를 LUT + 2픽셀 처리 루프로 교체해 연산량 감소.

실측
1. 1080p30 UDP (4Mbps, mft_hw/mft_hw) - 개선 전
- 로그: `automation/logs/verify-native-video-20260224-002638`
- `LAT_AVG_US=193513.5`
- `LAT_P95_US=215518`

2. 1080p30 UDP (4Mbps, mft_hw/mft_hw) - 색변환 최적화/버퍼 반영 후
- 로그: `automation/logs/verify-native-video-20260224-003112`
- `LAT_AVG_US=118596.12`
- `LAT_P95_US=125554`

3. 720p60 UDP (2.5Mbps, mft_hw/mft_hw) - 체감 프로필 기준
- 로그: `automation/logs/verify-native-video-20260224-003138`
- `LAT_AVG_US=63906.25`
- `LAT_P95_US=63628`

현재 해석
- UDP 전환 자체는 유효하며, 이번 튜닝으로 1080p 구간 지연이 유의미하게 하락.
- 남은 핵심 병목은 클라이언트 측 decode/render tail이며, Parsec 급 지연을 목표로 하면 다음 단계는 GPU zero-copy 렌더 경로가 우선.

### 19) 2026-02-24 새벽 후속 2 (클라이언트 GPU NV12 렌더 경로 반영)
적용
1. H264 렌더 파이프라인 변경
- 기존: `decode(NV12) -> CPU nv12_to_bgra -> GDI StretchDIBits`
- 변경: `decode(NV12) -> D3D11 NV12 텍스처 업로드 -> 픽셀 셰이더 YUV->RGB -> SwapChain Present`
- raw(BGRA) 경로는 기존 GDI 렌더 유지(회귀 방지).

2. 프레임 저장 구조 개선
- `SharedFrame`을 포맷(`Bgra32/Nv12`) 구분으로 확장.
- 프레임 바이트는 `shared_ptr<vector<uint8_t>>`로 저장해 페인트 시 불필요한 대용량 복사 제거.

3. 안전 폴백 유지
- GPU 렌더 초기화 실패 시 H264 NV12를 기존 CPU 변환 후 GDI로 폴백.

4. 빌드 링크 업데이트
- client target에 `dxgi`, `d3dcompiler` 링크 추가.

실측
1. 1080p30 UDP (4Mbps, mft_hw/mft_hw) - GPU NV12 렌더 반영 후
- 로그: `automation/logs/verify-native-video-20260224-005847`
- `LAT_AVG_US=102131.14`
- `LAT_P95_US=104530`
- client `avgDecodeTailUs` 관찰치가 약 `6~12ms`대로 하락.

2. 720p60 UDP (2.5Mbps, mft_hw/mft_hw)
- 로그: `automation/logs/verify-native-video-20260224-005913`
- `LAT_AVG_US=49335.62`
- `LAT_P95_US=51841`

3. raw 경로 회귀 확인
- 로그: `automation/logs/verify-native-video-20260224-005931`
- `CODEC=raw`, `TRANSPORT=tcp`, `OVERALL_OK=True`

현재 해석
- 이번 변경으로 클라이언트 render/decode tail 병목이 크게 줄었고, 1080p30 기준 `p95 ~105ms` 수준까지 내려옴.
- 다음 단계는 1080p60/외부망 조건에서의 ABR(동적 1080p↔720p)와 인코더 파라미터 자동 제어 고도화.

### 20) 2026-02-24 새벽 후속 3 (ABR 2차 + UDP 복구 + 검증 지표 확장)
적용 (마일스톤별)
1. M1 ABR 2차 (비트레이트 우선 + 해상도 폴백)
- host ABR를 `high -> mid -> low` 3단계로 확장:
  - `high`: 1080p/기본 bitrate
  - `mid`: 1080p/중간 bitrate (해상도 고정, bitrate 우선 조절)
  - `low`: 720p/저 bitrate
- 클라이언트 지표(`avgLatencyUs`, `avgDecodeTailUs`, `decodedFps`)와 host `cb2eAvgUs`를 함께 사용.
- 프로필 전환에 쿨다운/히스테리시스 적용(핑퐁 완화).
- control 채널이 있을 때만 ABR 적용되므로 JSON 프로필 기본 `controlPort`를 `43001`로 변경.

2. M2 UDP 복구 신호 (키프레임 요청)
- control 메시지 추가:
  - `ControlRequestKeyFrame`
- client가 다음 상황에서 요청 전송:
  - catch-up 진입
  - UDP 조립 drop 누적
  - keyframe 대기 장기화
  - decode 실패
- host가 요청 수신 시 즉시 다음 프레임 keyframe 강제.

3. M3 인코더 저지연 고정 강화
- `mf_h264_codec` 저지연 설정에서 VBV 버퍼를 더 짧게 조정:
  - `CODECAPI_AVEncCommonBufferSize`를 `bitrate/80` 기반으로 축소
  - `MaxBitrate`를 `~1.1x`로 설정해 순간 품질 급락 완화
- `reconfigure_bitrate()`에도 동일 정책 반영.

4. M4 검증 자동화/지표 확장
- `verify_native_video_runtime.ps1`에 다음 지표 추가:
  - `ABR_SWITCH_COUNT`, `ABR_TO_HIGH/MID/LOW_COUNT`, `ABR_LAST_PROFILE`
  - `KEYREQ_CLIENT_SENT`, `KEYREQ_HOST_RECV`, `KEYREQ_HOST_CONSUMED`

실측 (localhost, mft_hw/mft_hw)
1. 1080p60 입력(5.5Mbps) + ABR2
- 로그: `automation/logs/verify-native-video-20260224-012951`
- `LAT_AVG_US=57644.78`
- `LAT_P95_US=64577`
- `ABR_SWITCH_COUNT=1`, `ABR_LAST_PROFILE=low`
- 해석: 초기 1080p에서 자동 폴백 후 지연 안정화.

2. 720p60 입력(2.5Mbps) + ABR2
- 로그: `automation/logs/verify-native-video-20260224-013011`
- `LAT_AVG_US=51526.89`
- `LAT_P95_US=54686`
- `ABR_SWITCH_COUNT=1`, `ABR_LAST_PROFILE=low`

3. 저비트레이트 1080p60 입력(1.1Mbps)
- 로그: `automation/logs/verify-native-video-20260224-013033`
- `LAT_AVG_US=48002.57`
- `LAT_P95_US=47056`
- `ABR_SWITCH_COUNT=1`, `ABR_LAST_PROFILE=low`

퍼포먼스 비교 (이번 턴 기준)
- 좋아진 점:
  - 1080p60 시작 조건에서도 수초급 누적 없이 즉시 저지연 프로필로 수렴.
  - ABR 전환/키프레임 복구 동작을 로그 지표로 추적 가능.
- 나빠지거나 주의할 점:
  - 720p60 p95는 이전 기록(`~51841us`) 대비 이번 측정(`54686us`)로 소폭 악화(약 +2.8ms).
  - ABR이 빠르게 low로 내려가므로, 1080p 유지 시간은 짧아질 수 있음(품질보다 지연 우선 정책).

### 21) 2026-02-24 새벽 후속 4 (1080 유지시간 우선 ABR 재튜닝)
적용
1. ABR 강등 판정 완화 (host)
- 초기 워밍업 4초 동안은 강등 판정 억제.
- 강등을 즉시 1080->720으로 내리지 않고 `high->mid` 우선:
  - `high(1080/high bitrate)` -> `mid(1080/mid bitrate)` -> `low(720/low bitrate)`
- `fps` 단독 강등 비중을 낮추고, `latency/decodeTail` 우선으로 판단.
- 연속 압박 초(`abrModSec`, `abrSevSec`) 누적 기반으로만 프로필 전환.

2. ABR 상태 가시성 강화
- host 1초 통계 로그에 아래 필드 추가:
  - `abrProfile`
  - `abrModSec`
  - `abrSevSec`
  - `abrGoodSec`

실측 (localhost, mft_hw/mft_hw)
1. 1080p60 (5.5Mbps), 16초 수신 구간
- 로그: `automation/logs/verify-native-video-20260224-014323`
- `LAT_AVG_US=72705.4`
- `LAT_P95_US=77337`
- `ABR_SWITCH_COUNT=0` (프로필 전환 없음, 1080 유지)
- host 로그에서 전 구간 `abrProfile=high`, `size=1920x1080` 확인.
- 단, `DEC_AVG=15.53`로 프레임은 낮음.

2. 720p60 (2.5Mbps)
- 로그: `automation/logs/verify-native-video-20260224-014359`
- `LAT_AVG_US=46075`
- `LAT_P95_US=48039`
- `ABR_SWITCH_COUNT=0`

퍼포먼스 비교 (20절 대비)
- 좋아진 점:
  - 1080p 유지시간: `짧음 -> 지속 유지`로 개선 (`ABR_SWITCH_COUNT=0`).
  - 720p 지연: `P95 54686 -> 48039`로 개선.
- 나빠지거나 주의할 점:
  - 1080 유지 시 디코드 FPS가 낮아짐(`DEC_AVG 22~23 -> 15~16`).
  - 즉, 현재는 `1080 고정 유지`와 `프레임/지연 최적` 사이 트레이드오프가 존재.

### 22) 2026-02-24 새벽 후속 5 (내부망 타 PC 멈춤 대응: 클럭 오프셋 보정)
증상/원인 정리
- localhost에서는 정상인데, 타 PC(내부망)에서는 수초 지연/멈춤이 반복되는 케이스가 확인됨.
- 원인: 클라이언트의 catch-up/지연 판단에서 `호스트 QPC`와 `클라이언트 로컬 QPC`를 직접 비교하던 경로가 있었음.
- 서로 다른 PC에서는 기준점(offset)이 달라, 실제보다 큰 지연으로 오탐되어 catch-up 진입/키프레임 대기 루프가 유발될 수 있음.

적용
1. 클라이언트 타임라인 정렬(Offset 보정) 추가
- 파일: `apps/native_poc/src/native_video_client_main.cpp`
- 수신 스레드에 `aligned_lag_us(...)` 추가.
- 아래 계산을 보정 기반으로 변경:
  - `streamLagUs`
  - `latencyUs` (`avgLatencyUs`의 기반)
  - `decodeTailUs` (`avgDecodeTailUs`의 기반)
- 효과: 타 PC에서도 클럭 기준 불일치에 의한 가짜 고지연/멈춤 오탐 제거.

2. JSON 백엔드 기본값 호환성 강화
- 파일:
  - `automation/native_video_profile_1080p.json`
  - `automation/native_video_profile_720p.json`
  - `automation/native_video_profile_900p_quality.json`
- 변경: `"encoderBackend"/"decoderBackend"` 값을 `mft_hw` -> `mft_auto`.
- 목적: HW 조건 불일치 PC에서 강제 HW로 인한 실행 실패/정지 완화(HW 우선 + SW 폴백).

검증 (로컬 회귀)
- 빌드: `cmake --build build-native2 --config Debug --target remote60_native_video_client_poc` 성공.
- 로그: `automation/logs/verify-native-video-20260224-021847`
  - `HOST_RC=0`, `CLIENT_RC=0`
  - `LAT_AVG_US=12110.29`
  - `PRESENT_GAP_OVER_3S=0`

주의
- 이번 변경은 "타 PC에서의 멈춤/가짜 지연" 안정화가 핵심.
- Parsec 수준 화질/프레임 격차는 별도 항목(스케일러 품질, 인코더 품질-속도 파라미터, GPU copy 경로)에서 계속 개선 필요.

### 23) 2026-02-24 오전 후속 1 (인코더 효율 1차: RC/QVS/VBV)
목표
- 비트레이트를 강제로 줄이는 방식이 아니라, 동일 목표 bitrate에서 압축 효율을 높여 화질/트래픽 밸런스를 개선.

적용
1. Rate control 모드 개선
- 파일: `apps/native_poc/src/mf_h264_codec.cpp`
- 기존: `CBR` 고정
- 변경: `PeakConstrainedVBR` 우선 적용, 미지원 시 자동 `CBR` 폴백.

2. QualityVsSpeed 조정
- 파일: `apps/native_poc/src/mf_h264_codec.cpp`
- 기존: `100`
- 변경: 기본 `68` (속도 편향 완화, 효율/화질 쪽 가중).

3. VBV/MaxBitrate 튜닝
- 파일: `apps/native_poc/src/mf_h264_codec.cpp`
- VBV:
  - 기존 `bitrate/80` 기반 -> 변경 `bitrate/40` 기반(+min/max 완화)
- MaxBitrate:
  - 기존 `~1.1x` -> 변경 기본 `~1.3x`

4. 런타임 오버라이드(필요 시)
- `REMOTE60_NATIVE_ENCODER_RC_MODE` (`cbr` / `quality` / 기본 VBR 시도)
- `REMOTE60_NATIVE_ENCODER_QVS` (0~100, 기본 68)
- `REMOTE60_NATIVE_ENCODER_MAX_BITRATE_PCT` (기본 130)

검증 상태
- 빌드 성공:
  - `cmake --build build-native2 --config Debug --target remote60_native_video_host_poc remote60_native_video_client_poc`
- 자동 검증은 실행 환경에서 호스트 캡처 아이템 생성 실패(`capture item create failed`)로 수치 무효.
- 실사용 세션에서 `MBPS / decodedFrames / latency` 재확인 필요.

### 24) 2026-02-24 오전 후속 2 (인코딩 전/후/디코딩 후 데이터 크기 계측)
요구
- "인코딩 전 데이터 크기 vs 인코딩 후 전송량 vs 디코딩 후 데이터 크기"를 직접 비교 가능한 지표 추가.

적용
1. 호스트 지표
- 파일: `apps/native_poc/src/native_video_host_main.cpp`
- 1초 통계에 추가:
  - `rawEquivMbps` (인코딩 전 NV12 환산량)
  - `encRatioX100` (`rawEquivalentBytes / encodedBytes * 100`)

2. 클라이언트 지표
- 파일: `apps/native_poc/src/native_video_client_main.cpp`
- 1초 통계에 추가:
  - `decodedRawMbps` (디코딩 후 NV12 데이터량)
  - `decodeRatioX100` (`decodedBytes / recvEncodedBytes * 100`)

3. 검증 스크립트 집계 확장
- 파일: `automation/verify_native_video_runtime.ps1`
- 출력 항목 추가:
  - `DECODED_RAW_MBPS_*`
  - `DECODE_RATIO_X100_*`
  - `ENC_RAW_EQUIV_MBPS_*`
  - `ENC_RATIO_X100_*`

실측 A/B (1080p30 / target 10Mbps / keyint 15)
1. 효율 튜닝 기본값
- 로그: `automation/logs/verify-native-video-20260224-120552`
- `MBPS_AVG=3.43`
- `ENC_RATIO_X100_AVG=15499.62`
- `DECODE_RATIO_X100_AVG=14242.86`
- `LAT_AVG_US=40211.57`

2. 비교군 (강제 CBR + QVS100)
- 로그: `automation/logs/verify-native-video-20260224-120619`
- `MBPS_AVG=3.43`
- `ENC_RATIO_X100_AVG=16872.5`
- `DECODE_RATIO_X100_AVG=15871.57`
- `LAT_AVG_US=46561.71`

해석
- 현재 테스트 장면(정적 비중)에서는 Mbps 차이가 거의 없음.
- 다만 계측 기반 비교 체계는 준비되었고, 실제 변동 장면에서 효과를 수치로 바로 확인 가능.

### 25) 2026-02-24 오전 후속 3 (인코더 효율 기본 튜닝 롤백)
결정
- 사용자 요청에 따라 `mf_h264_codec.cpp`의 효율 튜닝 기본값(RC/QVS/VBV 변경)은 롤백.
- 롤백 사유: 목표(화질 유지 + 트래픽 절감)에 대한 유의미한 실효가 확인되지 않음.

유지한 것
- 계측 체계는 유지:
  - host `rawEquivMbps`, `encRatioX100`
  - client `decodedRawMbps`, `decodeRatioX100`
  - verify 집계 출력(`ENC_RATIO_X100_*`, `DECODE_RATIO_X100_*`)

### 26) 2026-02-24 오후 후속 1 (M0 3장면 자동 측정 스크립트 추가)
적용
1. M0 미완료 항목 구현
- 파일: `automation/verify_native_video_scene_suite.ps1` 신규 추가.
- 목적: `static / scroll / video` 3장면을 동일 파라미터로 연속 측정.
- 동작:
  - 장면별로 `verify_native_video_runtime.ps1`를 동일 파라미터로 1회씩 실행.
  - 1회 실행 시 장면별 로그 디렉터리 3세트 생성.
  - suite 요약 파일 생성:
    - `scene-suite-summary.txt`
    - 각 장면별 `scene-summary.txt`, `source-log-dir.txt`, `*.verify.out.log`

2. 안정성 보강
- 초기 구현에서 로그 디렉터리 이동(`Move-Item`) 시 파일 잠금으로 실패 가능성이 확인됨.
- 수정: 로그 디렉터리는 원위치 유지, scene별 요약에 원본 로그 경로만 참조하도록 변경.

스모크 검증 (로컬)
실행 명령:
```powershell
powershell -ExecutionPolicy Bypass -File automation/verify_native_video_scene_suite.ps1 -Root . -Codec raw -NonInteractive -ScenePrepareSeconds 0 -HostSeconds 4 -ClientSeconds 3 -NoInputChannel
```

결과:
- `SCENE_SUITE_DIR=automation/logs/verify-native-video-scenes-20260224-140628`
- `SCENE_COUNT=3`
- `SCENE_SUCCESS_COUNT=3`
- 장면별 로그:
  - `static`: `automation/logs/verify-native-video-20260224-140628`
  - `scroll`: `automation/logs/verify-native-video-20260224-140630`
  - `video`: `automation/logs/verify-native-video-20260224-140633`

주의
- 이번 스모크는 스크립트 동작/로그 세트 생성 검증 목적이다.
- 현 실행에서는 프레임 통계(`LAT/DEC/MBPS`)가 0으로 관측되어 `SCENE_VERIFY_OK_COUNT=0`이었다.
- 실제 수치 검증은 인터랙티브 장면(정적/스크롤/동영상) + 충분한 측정 시간으로 재실행 필요.

### 27) 2026-02-24 오후 후속 2 (M1 1차: bilinear 스케일러 교체)
적용
1. Host 스케일러 교체
- 파일: `apps/native_poc/src/native_video_host_main.cpp`
- 변경:
  - `resize_bgra_nearest(...)` 구현을 `resize_bgra_bilinear(...)`로 교체.
  - 4채널(BGRA) bilinear 보간으로 다운스케일 경로 품질 개선.
  - H264 경로에서 스케일링 호출부를 bilinear 함수로 전환.

2. 빌드 검증
- 명령:
```powershell
cmake --build build-native2 --config Debug --target remote60_native_video_host_poc
```
- 결과: `remote60_native_video_host_poc.exe` 빌드 성공.

3. 런타임 스모크
- 명령:
```powershell
powershell -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 -Root . -Codec h264 -Transport udp -Bitrate 2500000 -Keyint 15 -Fps 30 -FpsHint 30 -EncodeWidth 1280 -EncodeHeight 720 -HostSeconds 6 -ClientSeconds 4 -NoInputChannel
```
- 로그: `automation/logs/verify-native-video-20260224-144228`
- 주요 수치:
  - `OVERALL_OK=True`
  - `LAT_P95_US=47058`
  - `DEC_AVG=18`
  - host `captureSize=1762x986`, `encodeSize=1280x720` 확인(스케일링 경로 동작).

현재 판단
- M1 1차의 코드 반영(Nearest -> Bilinear) 완료.
- 성능 완료조건(`cb2eAvgUs` 20% 감소)는 장면 고정 A/B(동일 장면/동일 파라미터)로 추가 검증 필요.

유의미성 분석 (M1 1차)
- 판정: `미미(현재 근거 불충분)`
- 근거:
  - 이번 런은 단일 스모크(`automation/logs/verify-native-video-20260224-144228`)만 수행했고, 동일 장면 A/B 비교가 없어 bilinear 교체의 단독 효과를 분리해 확정할 수 없음.
  - 다만 기능 측면에서는 `captureSize=1762x986 -> encodeSize=1280x720` 스케일 경로가 정상 동작했고, 실행 자체는 `OVERALL_OK=True`, `LAT_P95_US=47058`로 회귀는 관측되지 않음.
- 후속 검증 조건:
  - 동일 장면(`static/scroll/video`) 고정 + 동일 파라미터로 `nearest(기준선)` vs `bilinear(변경)` A/B 2회 이상 비교.
  - 비교 지표: `cb2eAvgUs`, `encodedFrames`, `LAT_P95_US`, `DEC_AVG`.

### 28) 2026-02-24 오후 후속 3 (M1 2차: BGRA->NV12 CPU 루프 최적화)
적용
1. BGRA->NV12 변환 루프 최적화
- 파일: `apps/native_poc/src/mf_h264_codec.cpp`
- 변경:
  - 기존 `2-pass`(Y 전체 1회 + UV 전체 1회) 구조를 `2x2 one-pass` 구조로 전환.
  - 2x2 블록에서 Y(4픽셀) + UV(1쌍) 동시 계산으로 입력 픽셀 재읽기 감소.
  - Y 계산에 상수 계수 LUT(`BgraToNv12Tables`)를 도입해 곱셈 오버헤드 완화.

2. 빌드 검증
- 명령:
```powershell
cmake --build build-native2 --config Debug --target remote60_native_video_host_poc
```
- 결과: 성공.

3. 런타임 측정 (동일 조건 A/B)
- 공통 실행 조건:
```powershell
powershell -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 -Root . -Codec h264 -Transport udp -Bitrate 2500000 -Keyint 15 -Fps 30 -FpsHint 30 -EncodeWidth 1280 -EncodeHeight 720 -HostSeconds 6 -ClientSeconds 4 -NoInputChannel
```
- 비교 로그:
  - 변경 전(Baseline): `automation/logs/verify-native-video-20260224-144228`
  - 변경 후: `automation/logs/verify-native-video-20260224-155304`

A/B 핵심 수치
- host `cb2eAvgUs`(1초 통계 4개 평균):
  - 전: `(53435 + 51404 + 51473 + 47380) / 4 = 50923`
  - 후: `(51001 + 46393 + 48064 + 44854) / 4 = 47578`
  - 변화: `-6.6%` (개선)
- `encodedFrames`(1초 통계 평균):
  - 전: `(7 + 27 + 27 + 27) / 4 = 22.0`
  - 후: `(9 + 30 + 29 + 28) / 4 = 24.0`
  - 변화: `+9.1%` (개선)
- verify 요약:
  - `LAT_P95_US`: `47058 -> 49940` (소폭 악화)
  - `DEC_AVG`: `18.00 -> 20.33` (개선)
  - `OVERALL_OK`: 둘 다 `True`

유의미성 분석 (M1 2차)
- 판정: `미미(부분개선)`
- 근거:
  - 병목 지표인 `cb2eAvgUs`는 개선됐지만(`-6.6%`), M1 완료조건인 `20% 이상 감소`에는 미달.
  - `encodedFrames`는 증가했으나, `LAT_P95_US`가 같은 조건에서 소폭 악화되어 체감 개선을 단정하기 어려움.
- 결정: `코드 유지` (회귀 없음 + 일부 지표 개선), 다음 단계에서 추가 최적화 필요.

다음 액션
- M1 잔여 항목인 `스케일링 GPU 이전(D3D11)` 우선 진행.
- 동일 장면 고정 3-scene 반복 측정으로 M1 완료조건(`cb2eAvgUs -20%`) 재검증.

### 29) 2026-02-25 후속 1 (M1 보류 결정 + M2 계측 선행)
결정
- M1은 현재 시점에서 `완료`로 닫지 않고 `보류(Open)`로 유지한다.
- 보류 사유:
  - 완료조건은 `cb2eAvgUs 20% 이상 감소`인데, 최신 A/B 실측은 `-6.6%` 개선에 그침.
  - 일부 지표(`encodedFrames`) 개선은 있으나, 동일 조건에서 `LAT_P95_US` 소폭 악화 구간이 있어 단독 효과를 확정하기 어려움.

작업 순서 재정렬
1. `M2 1차`를 선행:
- client upload/present 구간의 세부 단계 계측 강화
- D3D 실패 시 GDI fallback 비율/원인 로그화
- decode 스레드와 present 스레드 사이 handoff 병목 필드 추가
2. 위 계측 결과를 근거로 M1(특히 GPU 스케일링 이전) A/B 재검증 후 유지/롤백 재판정.

### 30) 2026-02-25 후속 2 (M2 1차 계측 강화 반영)
적용
1. client present 세부 단계 trace 확장
- 파일: `apps/native_poc/src/native_video_client_main.cpp`
- `trace_present`에 아래 필드 추가:
  - `decodeToQueueUs`, `queueWaitUs`, `paintUs`
  - `uploadYUs`, `uploadUVUs`, `drawUs`, `presentBlockUs`
  - `renderPath`, `fallbackReason`

2. D3D 실패/GDI fallback 원인 계측
- 파일: `apps/native_poc/src/native_video_client_main.cpp`
- D3D 경로 실패/성공, GDI fallback 사용량, 실패 원인 카운터를 1초 통계 로그에 추가:
  - `d3dPresentSuccess`, `d3dPresentFail`, `gdiFallbackPresented`, `gdiFallbackRateX1000`
  - `fallbackInitFail`, `fallbackRenderFail`, `fallbackNv12ConvertFail`

3. decode 스레드 <-> present 스레드 handoff 계측
- 파일: `apps/native_poc/src/native_video_client_main.cpp`
- 공유 프레임 구조에 `queueSetUs`, `decodeToQueueUs` 추가.
- present 전에 덮어쓴 프레임과 paint coalescing 카운터 추가:
  - `overwriteBeforePresent`, `paintCoalesced`

4. verify 집계 확장
- 파일: `automation/verify_native_video_runtime.ps1`
- 신규 출력 항목 추가:
  - `D3D_PRESENT_*`, `GDI_FALLBACK_*`, `FALLBACK_*`, `PAINT_COALESCED_TOTAL`, `OVERWRITE_BEFORE_PRESENT_TOTAL`
  - stage 집계 확장(`DECODETOQUEUEUS`, `QUEUEWAITUS`, `PAINTUS`, `UPLOADYUS`, `UPLOADUVUS`, `DRAWUS`, `PRESENTBLOCKUS`)

검증
1. 빌드
- 명령:
```powershell
cmake --build build-native2 --config Debug --target remote60_native_video_client_poc
```
- 결과: 성공

2. 런타임 확인
- 명령:
```powershell
powershell -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 -Root . -Codec h264 -Transport udp -Bitrate 2500000 -Keyint 15 -Fps 30 -FpsHint 30 -EncodeWidth 1280 -EncodeHeight 720 -HostSeconds 6 -ClientSeconds 4 -NoInputChannel -TraceEvery 15 -TraceMax 40
```
- 로그: `automation/logs/verify-native-video-20260225-155459`
- 주요 확인값:
  - `D3D_PRESENT_SUCCESS_TOTAL=44`
  - `GDI_FALLBACK_PRESENTED_TOTAL=0`
  - `PAINT_COALESCED_TOTAL=3`
  - `OVERWRITE_BEFORE_PRESENT_TOTAL=3`
  - `STAGE_QUEUEWAITUS_AVG_US=3586`
  - `STAGE_PRESENTBLOCKUS_AVG_US=1160.5`

판정
- `M2 1차(계측 강화)` 반영 완료.
- `M1`은 여전히 보류(Open)이며, 이번 계측 데이터를 기준으로 M1 잔여 작업(GPU 스케일링 이전) 재검증을 진행한다.

### 31) 2026-02-25 후속 3 (M1 재검증: scaled vs noscale A/B + 3장면 비교)
실행
1. 단일 조건 A/B (각 2회)
- 조건:
  - codec=`h264`, transport=`udp`, bitrate=`2.5Mbps`, fps=`30`
  - host/client=`8s/6s`, no-input-channel
  - 비교군:
    - `scaled`: `--encode-width 1280 --encode-height 720`
    - `noscale`: encode 크기 미지정(캡처 해상도 유지)

2. 3장면 scene-suite 비교
- `scaled` suite:
  - `automation/logs/verify-native-video-scenes-20260225-160359`
- `noscale` suite:
  - `automation/logs/verify-native-video-scenes-20260225-160547`

A/B 결과 (2회 평균)
- `scaled`:
  - `CB2E_AVG_US_AVG=52359.58`
  - `ENCODED_FRAMES_AVG_AVG=20.67`
  - `LAT_P95_US_AVG=42408.5`
- `noscale`:
  - `CB2E_AVG_US_AVG=38956.16`
  - `ENCODED_FRAMES_AVG_AVG=27.33`
  - `LAT_P95_US_AVG=32101.5`
- 비교:
  - `cb2eAvgUs`: `-25.6%` (noscale 개선)
  - `encodedFrames`: `+32.2%` (noscale 개선)

3장면 비교(요약)
- STATIC:
  - scaled `CB2E=50145.67`, `ENC_FR=22.33`
  - noscale `CB2E=34359`, `ENC_FR=27.33`
- SCROLL:
  - scaled `CB2E=49917`, `ENC_FR=21.83`
  - noscale `CB2E=35130.83`, `ENC_FR=27.33`
- VIDEO:
  - scaled `CB2E=49059.83`, `ENC_FR=21.83`
  - noscale `CB2E=38981.5`, `ENC_FR=27.33`

판정
- `M1`은 여전히 `보류(Open)` 유지.
- 다만 재검증 근거상 스케일링 경로가 host 병목에 크게 기여하므로, 다음 구현 우선순위는 `M1 잔여: GPU 스케일링 이전(D3D11)`로 확정.

### 32) 2026-02-25 후속 4 (M1 잔여 구현: Host D3D11 GPU 스케일링 + A/B)
적용
1. Host GPU 스케일러 구현
- 파일: `apps/native_poc/src/native_video_host_main.cpp`
- `GpuBgraScaler` 신규 추가:
  - D3D11 VideoProcessor 기반 BGRA 리사이즈(`in -> out`) 구현
  - 실패 시 즉시 CPU bilinear 폴백
- 활성 조건:
  - 기본 활성(`h264` + 스케일 필요 시)
  - `REMOTE60_NATIVE_DISABLE_GPU_SCALER=1`로 강제 비활성 가능

2. 스레드 안정성 보강
- capture callback과 encode 루프가 동일 D3D immediate context를 공유하므로
  - `d3dContextMu` 뮤텍스로 `CopyResource/Map/Unmap` 및 GPU 스케일링 경로 직렬화.

3. Host 통계 확장
- 1초 통계 로그에 아래 항목 추가:
  - `gpuScaleReq`, `gpuScaleReady`
  - `gpuScaleAttempts`, `gpuScaleSuccess`, `gpuScaleFail`, `gpuScaleCpuFallback`

4. verify 집계 확장
- 파일: `automation/verify_native_video_runtime.ps1`
- host GPU 스케일링 집계 출력 추가:
  - `GPU_SCALE_ATTEMPTS_*`, `GPU_SCALE_SUCCESS_*`, `GPU_SCALE_FAIL_*`, `GPU_SCALE_CPU_FALLBACK_*`

검증
1. 빌드
- 명령:
```powershell
cmake --build build-native2 --config Debug --target remote60_native_video_host_poc
```
- 결과: 성공

2. 단일 스모크(ON)
- 로그: `automation/logs/verify-native-video-20260225-163342`
- host 확인:
  - `gpuScalerRequested=1 gpuScalerReady=1`
  - 초당 `gpuScaleAttempts ~= gpuScaleSuccess`, `gpuScaleFail=0`, `gpuScaleCpuFallback=0`

3. A/B (scaled 1280x720, 각 2회)
- `gpu_on`:
  - 로그: `...163420`, `...163429`
  - 평균: `CB2E_AVG_US=29588.96`, `ENC_FR_AVG=27.06`, `LAT_P95_US=42761.5`
- `gpu_off` (`REMOTE60_NATIVE_DISABLE_GPU_SCALER=1`):
  - 로그: `...163438`, `...163447`
  - 평균: `CB2E_AVG_US=54741.25`, `ENC_FR_AVG=20.08`, `LAT_P95_US=55788.5`
- 비교:
  - `cb2eAvgUs`: `-45.95%` (gpu_on 개선)
  - `encodedFrames`: `+34.8%` (gpu_on 개선)
  - `LAT_P95_US`: 개선

4. 3장면 A/B (scaled 1280x720)
- `gpu_on` suite: `automation/logs/verify-native-video-scenes-20260225-163508`
- `gpu_off` suite: `automation/logs/verify-native-video-scenes-20260225-163546`
- scene 요약:
  - STATIC: `CB2E 30081.67(on) vs 53619.67(off)`, `DEC_AVG 25.5 vs 18.8`
  - SCROLL: `CB2E 33787.17(on) vs 51277.5(off)`, `DEC_AVG 25.4 vs 19.4`
  - VIDEO: `CB2E 29552.5(on) vs 51407(off)`, `DEC_AVG 25.4 vs 19.2`

판정
- `M1` 잔여 항목(Host GPU 스케일링 이전) 구현 완료 + 유의미 개선 확인.
- 단, 현재 테스트 환경 캡처 해상도(약 `1762x986`) 한계로 `1080p30` Gate를 동일 환경에서 직접 검증하지 못해 M1 전체 Gate는 `Open` 유지.

### 33) 2026-02-25 재시도 (원격 환경)
목표
- `원격 접속` 상태에서 동일한 FHD 조건으로 GPU ON/OFF 재비교 검증.

실행
- ON
  - 명령:
```powershell
powershell -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 -Codec h264 -EncodeWidth 1920 -EncodeHeight 1080 -HostSeconds 12 -ClientSeconds 8 -Fps 30 -Bitrate 4000000 -Keyint 30 -NoInputChannel
```
  - 로그: `automation/logs/verify-native-video-20260225-172913`
- OFF (`REMOTE60_NATIVE_DISABLE_GPU_SCALER=1`)
  - 명령:
```powershell
$env:REMOTE60_NATIVE_DISABLE_GPU_SCALER=1
powershell -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 -Codec h264 -EncodeWidth 1920 -EncodeHeight 1080 -HostSeconds 12 -ClientSeconds 8 -Fps 30 -Bitrate 4000000 -Keyint 30 -NoInputChannel
Remove-Item Env:REMOTE60_NATIVE_DISABLE_GPU_SCALER
```
  - 로그: `automation/logs/verify-native-video-20260225-173014`

결과
- `capture item create failed`로 호스트 캡처 초기화가 실패하여 통계 카운트가 모두 `0` 처리.
- 클라이언트는 8개 제어 응답만 기록하고 조기 종료.
- `OVERALL_OK=False`, 병목/latency 비교 지표를 산출하지 못함.

원인(추정)
- 현재 세션에서 기본 모니터 캡처 객체 생성이 불가하여 `GraphicsCaptureItem` 생성 단계에서 실패한 것으로 판단.
- 재시험 필요: 원격 세션 해제(또는 실제 콘솔 화면 있는 환경)에서 동일 조건 ON/OFF 재실행 필요.

### 34) 2026-02-25 재실행 완료 (원격 해제 기대)
목표
- 동일 FHD 조건에서 `gpu_on` / `gpu_off` 결과 재확인(원격 세션 영향 해제 가정).

실행
- ON
  - 명령:
```powershell
powershell -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 -Codec h264 -EncodeWidth 1920 -EncodeHeight 1080 -HostSeconds 12 -ClientSeconds 8 -Fps 30 -Bitrate 4000000 -Keyint 30 -NoInputChannel
```
  - 로그: `automation/logs/verify-native-video-20260225-183850`
- OFF (`REMOTE60_NATIVE_DISABLE_GPU_SCALER=1`)
  - 명령:
```powershell
$env:REMOTE60_NATIVE_DISABLE_GPU_SCALER=1
powershell -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 -Codec h264 -EncodeWidth 1920 -EncodeHeight 1080 -HostSeconds 12 -ClientSeconds 8 -Fps 30 -Bitrate 4000000 -Keyint 30 -NoInputChannel
Remove-Item Env:REMOTE60_NATIVE_DISABLE_GPU_SCALER
```
  - 로그: `automation/logs/verify-native-video-20260225-183911`

결과
- ON:
  - `LAT_AVG_US=22249`, `LAT_P95_US=45553`, `DEC_AVG=15.14`, `DEC_P95=21`
  - `D3D_PRESENT_SUCCESS_TOTAL=102`, `GDI_FALLBACK_PRESENTED_TOTAL=0`, `D3D_PRESENT_FAIL_TOTAL=0`
  - `OVERALL_OK=True`
- OFF:
  - `LAT_AVG_US=59389`, `LAT_P95_US=87552`, `DEC_AVG=14.86`, `DEC_P95=20`
  - `D3D_PRESENT_SUCCESS_TOTAL=100`, `GDI_FALLBACK_PRESENTED_TOTAL=0`, `D3D_PRESENT_FAIL_TOTAL=0`
  - `OVERALL_OK=True`

판정
- 이번 조건에서 FHD 캡처는 정상 동작했고 ON/OFF 모두 수집됨.
- ON이 OFF 대비 P95 지연에서 개선(`87552 -> 45553`)이 있었지만, 스케일러 관련 지표(`gpuScale*`)는 인코딩 크기와 캡처 크기가 동일(`1920x1080`)해 모두 `0`으로, GPU 스케일러 유무 비교가 아닌 일반 FHD 스트림 비교임.

### 35) 2026-02-25 1280x720 스케일 A/B(2회씩)
목표
- `scaled(1280x720)`에서 GPU 스케일링 ON/OFF를 분리 확인.

실행
- ON (2회)
  - 명령:
```powershell
powershell -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 -Codec h264 -EncodeWidth 1280 -EncodeHeight 720 -HostSeconds 12 -ClientSeconds 8 -Fps 30 -Bitrate 2500000 -Keyint 30 -NoInputChannel
```
  - 로그: `automation/logs/verify-native-video-20260225-215016`, `automation/logs/verify-native-video-20260225-215040`
- OFF (`REMOTE60_NATIVE_DISABLE_GPU_SCALER=1`, 2회)
  - 명령:
```powershell
$env:REMOTE60_NATIVE_DISABLE_GPU_SCALER=1
powershell -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 -Codec h264 -EncodeWidth 1280 -EncodeHeight 720 -HostSeconds 12 -ClientSeconds 8 -Fps 30 -Bitrate 2500000 -Keyint 30 -NoInputChannel
Remove-Item Env:REMOTE60_NATIVE_DISABLE_GPU_SCALER
```
  - 로그: `automation/logs/verify-native-video-20260225-215055`, `automation/logs/verify-native-video-20260225-215110`

집계(2회 평균)
- ON:
  - `LAT_AVG_US=81548.77`, `LAT_P95_US=78636`
  - `DEC_AVG=26.46`, `DEC_P95=30`
  - `ENC_RATIO_X100_AVG=26129.44`
  - host `cb2eAvgUs` 요약 평균: `34835.25`
  - `GPU_SCALE_SUCCESS` 계측 active, `GPU_SCALE_CPU_FALLBACK=0`
- OFF:
  - `LAT_AVG_US=62682.93`, `LAT_P95_US=74420`
  - `DEC_AVG=19.36`, `DEC_P95=23`
  - `ENC_RATIO_X100_AVG=22597.44`
  - host `cb2eAvgUs` 요약 평균: `54651.13`
  - `gpuScaleReq=0`, `gpuScaleCpuFallback` 지속(22/23)로 CPU 경로 사용

판정
- `scaled` 경로에서 ON은 client decode/인코딩 대비 지표가 안정적으로 좋음 (`DEC_AVG 26.46 vs 19.36`, `cb2e 34.8ms vs 54.7ms`).
- 다만 `LAT_AVG_US/LAT_P95_US`는 OFF 1회 런에서 큰 편차가 있어 3회 이상 반복으로 최종 판정 권고.

### 36) 2026-02-25 1280x720 스케일 A/B(ON/OFF 3회 재확인)
목표
- `REMOTE60_NATIVE_DISABLE_GPU_SCALER` 상태가 달라졌을 때의 변동성을 줄이기 위해 3회씩 재비교.

실행
- ON (3회)
  - 로그: `automation/logs/verify-native-video-20260225-215801`, `automation/logs/verify-native-video-20260225-215812`, `automation/logs/verify-native-video-20260225-215822`
- OFF (`REMOTE60_NATIVE_DISABLE_GPU_SCALER=1`, 3회)
  - 로그: `automation/logs/verify-native-video-20260225-215838`, `automation/logs/verify-native-video-20260225-215849`, `automation/logs/verify-native-video-20260225-215900`

집계(요약)
- ON:
  - `LAT_AVG_US=80078.09`, `LAT_P95_US=150581`, `DEC_AVG=22.68`, `DEC_P95=30`
  - `ENC_RATIO_X100_AVG=18171.36`
  - host `CB2E_AVG_US_AVG=42355.65` (`count=23`), `CB2E_AVG_P95=46227`
  - `GPU_SCALE_ATTEMPTS_AVG=25.91`, `GPU_SCALE_FAIL_AVG=0`, `GPU_SCALE_CPU_FALLBACK_AVG=0`
- OFF:
  - `LAT_AVG_US=44809.43`, `LAT_P95_US=118820`, `DEC_AVG=12.24`, `DEC_P95=23`
  - `ENC_RATIO_X100_AVG=14991.29`
  - host `CB2E_AVG_US_AVG=54267.25` (`count=24`), `CB2E_AVG_P95=60306`
  - `GPU_SCALE_ATTEMPTS_AVG=0`, `GPU_SCALE_CPU_FALLBACK_AVG=16.2`
  - OFF 2회차에서 `KEYREQ_CLIENT_SENT=84`, `KEYREQ_HOST_RECV=84`, `KEYREQ_HOST_CONSUMED=84` 발생(키프레임 요청 폭증 구간 관측)

판정
- `cb2eAvgUs`/`encoded throughput`는 ON이 OFF 대비 개선(약 `-22%` 수준)을 유지.
- `DEC_AVG` 역시 ON이 OFF 대비 높음(더 많은 frame decode/표시로 해석 가능).
- `LAT`은 ON에서 일부 긴 지연이 누적되어 분산 큼(ON P95/Max 커짐), OFF는 평균은 낮으나 2회차 OFF keyreq 폭주와 함께 저하 구간이 섞임.
- OFF 3회차에서 `키프레임 요청`이 폭증한 RUN이 있어 ON/OFF 성능 비교 시 해당 조건은 변수를 따로 분리해 추가 실험 필요.

추가 액션
- M1 보류(Open) 상태 유지: `cb2eAvgUs` 20% Gate와 안정적 `LAT_P95` 하한은 추가 재현 조건 정리 후 판정.
- 다음으로 `M2 2차(클라 NV12 업로드 경로 최적화)`를 적용하고 재측정.

### 37) 2026-02-25 M2 2차 (클라 NV12 업로드 최적화) 재측정
목표
- `native_video_client_main`의 NV12 업로드에서 `RowPitch==width` 구간 단일 `memcpy` 경로가 동작하는지 확인하고, 업로드 단계 계측 수치 반영.

실행
- ON
  - 명령:
```powershell
powershell -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 -Root . -Codec h264 -Transport udp -Bitrate 2500000 -Keyint 30 -Fps 30 -FpsHint 30 -EncodeWidth 1280 -EncodeHeight 720 -HostSeconds 12 -ClientSeconds 8 -NoInputChannel -TraceEvery 15 -TraceMax 40
```
  - 로그: `automation/logs/verify-native-video-20260225-222406`, `automation/logs/verify-native-video-20260225-222437`
- OFF (`REMOTE60_NATIVE_DISABLE_GPU_SCALER=1`)
  - 명령:
```powershell
$env:REMOTE60_NATIVE_DISABLE_GPU_SCALER='1'
powershell -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 -Root . -Codec h264 -Transport udp -Bitrate 2500000 -Keyint 30 -Fps 30 -FpsHint 30 -EncodeWidth 1280 -EncodeHeight 720 -HostSeconds 12 -ClientSeconds 8 -NoInputChannel -TraceEvery 15 -TraceMax 40
Remove-Item Env:REMOTE60_NATIVE_DISABLE_GPU_SCALER
```
  - 로그: `automation/logs/verify-native-video-20260225-222421`, `automation/logs/verify-native-video-20260225-222452`

집계(2회 평균)
- ON:
  - `LAT_AVG_US=75,204.57`, `LAT_P95_US=104,843.0`
  - `DEC_AVG=26.21`
  - Host `cb2eAvgUs` 추정평균: `35,658.84us` (`count=7`, `count=8` 구간 from per-second summaries)
  - Host `sentFrames` 추정평균: `27.59`
  - `UPLOADY_AVG`=248.79us, `UPLOADUV_AVG`=104.21us
  - `D3D_PRESENT_SUCCESS_TOTAL` 합계=`358`, `D3D_PRESENT_FAIL_TOTAL`=`0`, `GDI_FALLBACK_PRESENTED_TOTAL`=`0`
  - `PAINT_COALESCED_TOTAL` 합계=`10`
- OFF:
  - `LAT_AVG_US=27,067.36`, `LAT_P95_US=30,236.5`
  - `DEC_AVG=19.29`
  - Host `cb2eAvgUs` 추정평균: `54,686.99us` (`count=8`, `count=8`)
  - Host `sentFrames` 추정평균: `20.06`
  - `UPLOADY_AVG`=209.05us, `UPLOADUV_AVG`=85.89us
  - `D3D_PRESENT_SUCCESS_TOTAL` 합계=`264`, `D3D_PRESENT_FAIL_TOTAL`=`0`, `GDI_FALLBACK_PRESENTED_TOTAL`=`0`
  - `PAINT_COALESCED_TOTAL` 합계=`6`

판정
- `UPLOADY/UV` 계측치가 ON/OFF 모두 수치권 안에서 집계되었고, `gdi fallback`은 없었으며 `D3D 경로`는 정상 유지됨.
- 다만 `LAT`/`DEC` 편차가 크고, OFF 1회(첫 번째)에서 측정 이상치가 있어 `M2 2차`의 latency 개선 판정은 `보류`.
- 다음 액션: 동일 조건 3회 이상 반복해 `lat` 편차를 수렴시키고 `M2 완료조건`(1080p30)을 위한 재평가로 이관.

### 38) 2026-02-25 M2 2차 추가 3회 반복 재확인(1280x720)
목표
- 이전 outlier를 줄이기 위해 ON/OFF 3회씩 고정 반복.

실행
- ON (3회)
  - 로그: `automation/logs/verify-native-video-20260225-223204`, `automation/logs/verify-native-video-20260225-223215`, `automation/logs/verify-native-video-20260225-223227`
- OFF (`REMOTE60_NATIVE_DISABLE_GPU_SCALER=1`, 3회)
  - 로그: `automation/logs/verify-native-video-20260225-223239`, `automation/logs/verify-native-video-20260225-223250`, `automation/logs/verify-native-video-20260225-223301`

집계(요약)
- ON:
  - `LAT_AVG_US=82,170.33`, `LAT_P95_US=127,589.33`, `LAT_MAX_US=191,792`
  - `DEC_AVG=23.76`, `DEC_P95=29.33`
  - Host: `CB2E_AVG_US_AVG=40,786` (run별 43,430.14 / 42,572.00 / 36,355.00)
  - Host `sentFrames` 추정평균: `25.10` (run별 23.43 / 24.00 / 27.88)
  - `GPU_SCALE_ATTEMPTS_AVG=27.33`, `GPU_SCALE_CPU_FALLBACK_AVG=0`
  - `UPLOADY_AVG=221.15`, `UPLOADUV_AVG=89.33`
  - `D3D_PRESENT_SUCCESS_TOTAL=~161.3`, `GDI_FALLBACK_PRESENTED_TOTAL=0`
- OFF:
  - `LAT_AVG_US=32,340`, `LAT_P95_US=41,380.33`, `LAT_MAX_US=50,246.33`
  - `DEC_AVG=19.05`, `DEC_P95=22.67`
  - Host: `CB2E_AVG_US_AVG=53,926`
  - Host `sentFrames` 추정평균: `20.08`
  - `GPU_SCALE_ATTEMPTS_AVG=0`, `GPU_SCALE_CPU_FALLBACK_AVG=22.21`
  - `UPLOADY_AVG=184.48`, `UPLOADUV_AVG=78.80`
  - `D3D_PRESENT_SUCCESS_TOTAL=~131.3`, `GDI_FALLBACK_PRESENTED_TOTAL=0`

판정
- `M2 2차`에서 `GPU_SCALE`과 함께 `CB2E`/`sentFrames`는 ON이 유의미하게 우수(`-24%` 수준)하고, `D3D` 경로도 안정적.
- 다만 지연은 ON에서 `정규분포가 아닌 장기 tail`이 커져 `LAT_P95`/`LAT_MAX`가 크게 분산(3회 중 1회는 약 112ms/269ms).
- 이 단계는 `업로드 최적화 동작 확인`은 완료, 그러나 `latency 개선` 판정은 `보류` 유지.

### 39) 2026-02-25 M3 스모크: ABR 품질 유지형 토글 반영
목표
- M3에서 추가한 `REMOTE60_NATIVE_ADAPTIVE_QUALITY_FIRST` 토글이 host 로그/ABR 임계치에 반영되는지 확인.

실행
- 품질우선(quality-first=on)
  - 명령:
```powershell
$env:REMOTE60_NATIVE_ADAPTIVE_QUALITY_FIRST='1'
powershell -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 -Root . -Codec h264 -Transport udp -Bitrate 2500000 -Keyint 30 -Fps 30 -FpsHint 30 -EncodeWidth 1280 -EncodeHeight 720 -HostSeconds 12 -ClientSeconds 8 -NoInputChannel -TraceEvery 15 -TraceMax 40
Remove-Item Env:REMOTE60_NATIVE_ADAPTIVE_QUALITY_FIRST
```
  - 로그: `automation/logs/verify-native-video-20260225-224122`
- 기본값 기준(quality-first=off)
  - 명령:
```powershell
$env:REMOTE60_NATIVE_ADAPTIVE_QUALITY_FIRST='0'
powershell -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 -Root . -Codec h264 -Transport udp -Bitrate 2500000 -Keyint 30 -Fps 30 -FpsHint 30 -EncodeWidth 1280 -EncodeHeight 720 -HostSeconds 12 -ClientSeconds 8 -NoInputChannel -TraceEvery 15 -TraceMax 40
Remove-Item Env:REMOTE60_NATIVE_ADAPTIVE_QUALITY_FIRST
```
  - 로그: `automation/logs/verify-native-video-20260225-224141`

관찰
- host 로그에서 `abrMode=quality-first` / `abrMode=default` 정상 출력 확인.
- 품질우선/기본 모드 모두에서 `ABR_SWITCH_COUNT=0`으로 스위치 없음(안정 구간 스모크).
- 동일 조건에서 `quality-first` + `Fps=60`, `Bitrate=700000`, `HostSeconds=20`으로 부하를 높여도 스위치 없음.
- 품질우선 토글 적용은 완료되었으나, 부하변동 조건에서의 전환 검증은 추가 실험 필요.

판정
- M3 구현은 코드/로그 검증 단계 완료.
- 종료 판정은 스위치 억제/재현성 비교 실험 누적 후 확정(`M3 종료` 보류).

### 40) 2026-02-25 M3 전환 재현(부하 불일치 조건 재시도)
목표
- `quality-first` ON/OFF에서 ABR 스위치 억제/발생 경향을 상대 비교.
- `client decode 성능 저하`를 유발할 수 있는 조건으로 재현성 확보.

실행
- `host fps=120`, `keyint=240`, `bitrate=2500000`, `encode=1280x720`, `transport=udp`, `host/client=20/16`, `NoInput`, `trace every 15` 조건 기준으로 최신 로그 집계.
- quality-first=off (11회)
  - 로그: `225738`, `225830`, `225918`, `225937`, `230038`, `230634`, `230653`, `230713`, `230732`, `230751`, `230811`
- quality-first=on (11회)
  - 로그: `225757`, `225850`, `225956`, `230016`, `230058`, `230830`, `230850`, `230909`, `230928`, `230948`, `231007`

관찰
- off 모드:
  - 동일 조건에서 `ABR_SWITCH_COUNT=1` 이력은 `225830` 1회만 확인됨 (`ABR_TO_MID=1`, `reason=high_to_mid_moderate`).
  - 해당 스위치 상세: `profile=mid bitrate=2000000 clientDecodedFps=37 clientAvgLatUs=76501`.
  - 나머지 off RUN은 스위치 미발생.
- on 모드:
  - 11회 모두 `ABR_SWITCH_COUNT=0`, 동일 클래스에서 스위치 미발생.
  - `LAT_AVG_US`는 오프 대비 일부 구간에서 다소 낮은 편향이 보였으나 run별 편차 큼.

판정
- `quality-first=on`은 동일/유사 조건에서 스위치 억제 경향이 유지됨(현재 샘플 기준).
- 오프 모드의 `high_to_mid`는 현재까지 `1/11` 케이스만 간헐적으로 발생해 재현성 보강 전까지 `M3 종료`는 보류.

### 41) 2026-02-26 h264 지연 병목 분해 재측정 (최종 사용자 확인 반영)
목표
- `~0.5~1초` 체감 지연이 발생한 원인 구간을 정확히 분해.
- `capture`, `capture->callback 선택 대기`, `encoder queue`, `encode` 병목을 user-feedback 로그로 분리 수치화.

변경 반영
- `apps/native_poc/src/native_video_host_main.cpp`
  - `user-feedback` / `trace`에 아래 항목 추가:
    - `captureToCallbackUs`, `selectWaitUs`, `queueGapFrames`
    - `encQueueUs`, `captureToAuUs`, `auCaptureUs`, `cb2eUs`
  - `host` 병목 기준을 `pipeUs`뿐 아니라 보조 지연 항목으로 추적 가능한 형태로 보강.
- `automation/verify_native_video_runtime.ps1`
  - host `user-feedback` 파서에 신규 필드 추가 및 집계/상위 병목 산정 반영.
  - `USER_FEEDBACK_HOST_TOP*`, `USER_FEEDBACK_UF_H_*` 결과에 `captureToCallback/selectWait/encQueue` 반영.
- `build-native2` 정리 후 host/client 재빌드 후 측정 진행.

실행
- 기본: `h264`, `udp`, `-TraceEvery 1`, `-TraceMax 1200`, `HostSeconds 12`, `ClientSeconds 10`, `Bitrate 1100000`, `Keyint 15`
```powershell
powershell -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 -Codec h264 -TraceEvery 1 -TraceMax 1200 -HostSeconds 12 -ClientSeconds 10 -Bitrate 1100000 -Keyint 15
```
- 로그: `automation/logs/verify-native-video-20260226-142520`

추가 확인(Trace 미사용 스모크)
- 동일 조건에서 `-TraceEvery` 미사용 재측정 수행.
- 로그: `automation/logs/verify-native-video-20260226-142543`

결과 요약
- `USER_FEEDBACK_UF_H_PIPEUS_AVG_US=608,840` (`P95=672,989`, `Max=703,217`)
- `USER_FEEDBACK_UF_H_C2EUS_AVG_US=607,944` (`P95=671,821`, `Max=702,523`)
- `USER_FEEDBACK_UF_H_ENCQUEUEUS_AVG_US=607,944` (`P95=671,821`)
- `USER_FEEDBACK_UF_H_SELECTWAITUS_AVG_US=11,738` (`P95=19,225`)
- `USER_FEEDBACK_UF_H_CAPTURETOCALLBACKUS_AVG_US=0`
- `USER_FEEDBACK_UF_H_QUEUEGAPFRAMES_AVG_US=0.1` (`Max=1`)
- `USER_FEEDBACK_HOST_BOTTLENECK_STAGE=c2eUs`
- `USER_FEEDBACK_HOST_TOP0_SEQ=181`, `USER_FEEDBACK_HOST_TOP0_PIPE_US=703,217`
- `presentGap` 0.5~0.7초대 유사 지연보다 작은 구간: `PRESENT_GAP_AVG_US=34,733`, `P95=45,860`, `Max=107,163`

판정
- `capture->callback`와 `capture 큐 갭`은 사실상 0~미미.
- 지연의 실질적 병목은 host에서 `capture 이후 encode 큐 적재/처리` 계열 (`c2eUs`, `encQueueUs`)로 집중됨.
- `-TraceEvery` 유무와 무관하게 값이 유사해, 측정 로그 자체가 병목을 유의미하게 유발했다고 보기 어려움.
- 사용자 체감 1초 지연은 네트워크 RTT가 아닌, host 인코더 경로의 큐 적체/처리 지연 누적에 기인할 가능성이 높음.

다음 액션 (권장)
- `encodePath` 병목 축소 실험(우선순위):
  1) 소프트웨어 인코더 강제/비교
  2) 인코더 큐 정책/파이프라인 단계 제어 실험
  3) 해상도/프레임 설정 조합(720p/60, 720p/30, 1080p/30)에서 `C2E/encQueue` 동시 비교
  4) 동일 시나리오 3회 이상 반복해 `USER_FEEDBACK_UF_H_PIPEUS_P95`를 200ms 이하로 낮추는지 검증

### 42) 2026-02-26 host AU 타임스탬프 편차 추적 반영
목표
- 사용자 체감 지연의 `~0.5~1초` 구간에서 실제 병목이 AU 타임 정합성 문제인지 인코더 큐 적체인지 구분할 수 있도록 trace를 분해.

변경 반영
- `apps/native_poc/src/native_video_host_main.cpp`
  - `trace`/`user-feedback` 로그에 `frameCaptureUs`(큐캡처 시각), `auCaptureUs`(AU sampleTime), `captureToAuSkewUs`를 추가.
- `automation/verify_native_video_runtime.ps1`
  - host 피드백 파서에 `captureToAuSkewUs` 집계 반영.
- 문서:
  - `docs/구현계획.md`에서 host 병목 추적 우선순위에 `CAPTURE_TO_AU_SKEW` 반영.
  - 같은 문서에 `docs/history.md`에 모든 재측정 결과 **반드시 append** 규칙을 명시.

실행
- 동일 변경의 로그 보강 반영은 완료. `USER_FEEDBACK_UF_H_CAPTURETOAUSKEW_*` 계산은 다음 재측정부터 본 채널로 집계 예정.

판정
- 코드/파서 정합성 적용 완료.
- 다음 실행부터 각 run의 결과를 위 형식으로 누적 기록.

### 43) 2026-02-26 host 병목 재측정 10회 반복(재빌드 미반영)
목표
- `pipeUs/c2eUs/encQueueUs` 재현 분포를 재확인하고 병목 위치 재확정.
- 문서 규칙(`history append`)대로 측정 결과 누적.

실행
- 동일 조건 10회 반복:
```powershell
powershell -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 -Codec h264 -Transport udp -Fps 30 -FpsHint 30 -HostSeconds 12 -ClientSeconds 10 -Bitrate 1100000 -Keyint 15 -TraceEvery 0 -NoInputChannel
```
- 로그:
  - `automation/logs/verify-native-video-20260226-224328`
  - `automation/logs/verify-native-video-20260226-224342`
  - `automation/logs/verify-native-video-20260226-224356`
  - `automation/logs/verify-native-video-20260226-224410`
  - `automation/logs/verify-native-video-20260226-224423`
  - `automation/logs/verify-native-video-20260226-224437`
  - `automation/logs/verify-native-video-20260226-224501`
  - `automation/logs/verify-native-video-20260226-224515`
  - `automation/logs/verify-native-video-20260226-224529`
  - `automation/logs/verify-native-video-20260226-224542`

주요 수치
- `USER_FEEDBACK_UF_H_ENCQUEUEUS_AVG_US`: 10회 평균 `~653,531`us, P95 추정 평균 `~705,244`us, 최소~최대 `583,245.5`~`799,343`us
- `USER_FEEDBACK_UF_H_PIPEUS_AVG_US`:
  - 정밀 추출 4회: `603,675.9 ~ 617,098.1`us (평균 `610,050.3`us)
  - 추가 6회: `583,245.5 ~ 750,107.7`us 구간 분포 포함
- `USER_FEEDBACK_UF_H_C2EUS_AVG_US`: 4회 정밀 추출 `601,598.5 ~ 615,353.2`us (평균 `608,410.9`us)
- `USER_FEEDBACK_UF_H_CAPTURETOAUSKEW_*`:
  - 전부 빈값(미수집)
- `HOST_QUEUE_WAIT_TIMEOUT_COUNT`: 전부 `0`
- `HOST_QUEUE_WAIT_NOWORK_COUNT`: 전부 `0`
- `USER_FEEDBACK_HOST_BOTTLENECK_STAGE`: 전부 `pipeUs`

판정
- 병목은 계속 `pipeUs/c2e/encQueue` 구간에 고정되며, `host queue wait` 계열은 기저적.
- `CAPTURETOAUSKEW`는 현재 실행 바이너리에서 미집계되어 원인 분리 불가(재빌드 반영 필요).
- 다음 액션: 재빌드 가능한 환경에서 동일 조건 20회 반복 + `captureToAuSkew` 값을 함께 집계해 `capture timestamp shift` 여부 최종 확정.

### 44) 2026-02-26 host 병목 재측정 20회 반복(재빌드 반영)
목표
- `pipeUs`/`c2eUs` 병목을 `captureToAuSkewUs` 분해로 정밀 판정하고, 큐 대기 계열 타임아웃 여부를 동시에 확인.

실행
- 동일 조건 20회 반복:
```powershell
powershell -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 -BuildDir C:\Users\CHO\AppData\Local\Temp\build-native2-test -Codec h264 -Transport udp -Fps 30 -FpsHint 30 -HostSeconds 12 -ClientSeconds 10 -Bitrate 1100000 -Keyint 15 -TraceEvery 0 -NoInputChannel
```
- 로그:
  - `automation/logs/verify-native-video-20260226-225734`
  - `automation/logs/verify-native-video-20260226-225748`
  - `automation/logs/verify-native-video-20260226-225802`
  - `automation/logs/verify-native-video-20260226-225815`
  - `automation/logs/verify-native-video-20260226-225829`
  - `automation/logs/verify-native-video-20260226-225842`
  - `automation/logs/verify-native-video-20260226-225856`
  - `automation/logs/verify-native-video-20260226-225909`
  - `automation/logs/verify-native-video-20260226-225923`
  - `automation/logs/verify-native-video-20260226-225936`
  - `automation/logs/verify-native-video-20260226-225950`
  - `automation/logs/verify-native-video-20260226-230003`
  - `automation/logs/verify-native-video-20260226-230017`
  - `automation/logs/verify-native-video-20260226-230030`
  - `automation/logs/verify-native-video-20260226-230044`
  - `automation/logs/verify-native-video-20260226-230057`
  - `automation/logs/verify-native-video-20260226-230111`
  - `automation/logs/verify-native-video-20260226-230124`
  - `automation/logs/verify-native-video-20260226-230138`
  - `automation/logs/verify-native-video-20260226-230152`

주요 수치
- `USER_FEEDBACK_UF_H_PIPEUS_AVG_US` 평균: `617,137.6`us (`min=579,515.4`, `max=735,240.1`)
- `USER_FEEDBACK_UF_H_C2EUS_AVG_US` 평균: `615,387.4`us (`min~577,714.5`, `max~732,736.5`)
- `USER_FEEDBACK_UF_H_ENCQUEUEUS_AVG_US` 평균: `615,387.4`us (동일)
- `USER_FEEDBACK_UF_H_QUEUETOENCODEUS_AVG_US` 평균: `26,311.4`us
- `USER_FEEDBACK_UF_H_QUEUETOSENDUS_AVG_US` 평균: `28,061.5`us
- `USER_FEEDBACK_UF_H_CAPTURETOAUSKEW_AVG_US` 평균: `578,147.5`us
- `USER_FEEDBACK_HOST_BOTTLENECK_STAGE` 모두 `pipeUs`
- `HOST_QUEUE_WAIT_TIMEOUT_COUNT`: `0`
- `HOST_QUEUE_WAIT_NOWORK_COUNT`: `0`
- `HOST_QUEUE_SKIPPED_BY_OVERWRITE`: `234`
- `USER_FEEDBACK_UF_H_CAPTURETOAUSKEW_COUNT` 합계: `199`

판정
- 사용자 체감 지연은 큐 대기 타임아웃/무작업(`QUEUE_WAIT_*`)이 아닌 `capture -> AU 타임스탬프 정합성` 경로가 주된 의심으로 확인됨.
- `Q2E`/`Q2S`는 상대적으로 작고 안정적이어서 인코더 제출 자체 지연보다 타임스탬프 오프셋 축적이 핵심인 정황.
- 다음 작업: `captureToAuSkewUs` 축소를 1순위로 하여 AU timestamp 정합성 경로 고정/완화 실험 실행.

### 45) 2026-02-26 host 병목 재측정 20회 반복(캡처 기준 타임스탬프 고정 반영)
목표
- `EncodedFrameHeader.captureQpcUs`를 AU sample 기준이 아닌 캡처 기준으로 고정한 상태에서 병목 위치를 재확인.

실행
- 동일 조건 20회 반복:
```powershell
powershell -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 -BuildDir C:\Users\CHO\AppData\Local\Temp\build-native2-test2 -Codec h264 -Transport udp -Fps 30 -FpsHint 30 -HostSeconds 12 -ClientSeconds 10 -Bitrate 1100000 -Keyint 15 -TraceEvery 0 -NoInputChannel
```
- 로그:
  - `automation/logs/verify-native-video-20260226-231355`
  - `automation/logs/verify-native-video-20260226-231410`
  - `automation/logs/verify-native-video-20260226-231424`
  - `automation/logs/verify-native-video-20260226-231438`
  - `automation/logs/verify-native-video-20260226-231452`
  - `automation/logs/verify-native-video-20260226-231506`
  - `automation/logs/verify-native-video-20260226-231520`
  - `automation/logs/verify-native-video-20260226-231534`
  - `automation/logs/verify-native-video-20260226-231548`
  - `automation/logs/verify-native-video-20260226-231602`
  - `automation/logs/verify-native-video-20260226-231616`
  - `automation/logs/verify-native-video-20260226-231630`
  - `automation/logs/verify-native-video-20260226-231644`
  - `automation/logs/verify-native-video-20260226-231658`
  - `automation/logs/verify-native-video-20260226-231712`
  - `automation/logs/verify-native-video-20260226-231726`
  - `automation/logs/verify-native-video-20260226-231740`
  - `automation/logs/verify-native-video-20260226-231754`
  - `automation/logs/verify-native-video-20260226-231808`
  - `automation/logs/verify-native-video-20260226-231822`

주요 수치
- `USER_FEEDBACK_UF_H_PIPEUS_AVG_US`(user-feedback 존재 구간): 평균 `98,677`us (`count=4`)
- `USER_FEEDBACK_UF_H_C2EUS_AVG_US`(user-feedback 존재 구간): 평균 `95,816`us (`count=4`)
- `USER_FEEDBACK_UF_H_ENCQUEUEUS_AVG_US`: `0`~`1xx`ms 구간 미집계(계측 임계 미달 run 다수로 요약치 0)
- `USER_FEEDBACK_UF_H_CAPTURETOAUSKEW_AVG_US`: 평균 `690,506`us (`count=3`)
- `HOST_QUEUE_WAIT_TIMEOUT_COUNT`: `0`
- `HOST_QUEUE_WAIT_NOWORK_COUNT`: `0`
- `USER_FEEDBACK_UF_H_PIPEUS_COUNT`: `4` (`20` runs 중 16run은 0 집계)

판정
- `captureQpcUs` 정합을 캡처 기준으로 고정하자 `PIPE_US`/`C2E_US`가 기존 600ms대 → 약 `100ms`대(임계 미달 샘플 제외)로 크게 감소.
- `captureToAuSkewUs`는 여전히 수백 ms 대로 관측되며, 현재는 체감 지연의 최우선 병목은 아니라는 신호가 큼.
- 다음 작업:
  - `captureToAuSkewUs`는 AU timestamp source 분기(encoder `sampleTimeHns` 산출) 원인 분해만 추가로 진행.
- 사용자 체감 지연 기준 우선순위를 `PIPE_US`/`C2E_US`/`QUEUE_TO_ENCODE`로 재정렬해 추적.

### 46) 2026-02-26 host 병목 재측정 6회 반복(TraceEvery 1, 큐 내부 분해 강화)
목표
- `user-feedback` 임계치(90ms) 밖을 벗어나는 구간만 수집되는 편향을 줄이기 위해 `trace` 기반으로 `queue->encode/send` 하위 분해를 재측정.
- host 병목이 큐 대기인지 AU timestamp 정합성인지 구간별로 분리.

실행
- 동일 조건 6회 반복:
```powershell
powershell -ExecutionPolicy Bypass -File remote60/automation/verify_native_video_runtime.ps1 -BuildDir C:\Users\CHO\AppData\Local\Temp\build-native2-test2 -Codec h264 -Transport udp -Fps 30 -FpsHint 30 -HostSeconds 12 -ClientSeconds 10 -Bitrate 1100000 -Keyint 15 -TraceEvery 1 -TraceMax 0 -NoInputChannel
```
- 로그:
  - `automation/logs/verify-native-video-20260226-233141`
  - `automation/logs/verify-native-video-20260226-233155`
  - `automation/logs/verify-native-video-20260226-233209`
  - `automation/logs/verify-native-video-20260226-233228`
  - `automation/logs/verify-native-video-20260226-233241`
  - `automation/logs/verify-native-video-20260226-233255`

주요 수치 (host trace 기반 집계)
- `c2eUs` 평균/평활: `35.8ms`
- `queueToEncodeUs` 평균/평활: `25.1ms` (최고 약 `28.5ms`)
- `queueToSendUs` 평균/평활: `26.9ms` (최고 약 `47.3ms`)
- `queueWaitUs` 평균/평활: `12.6ms`
- `selectWaitUs` 평균/평활: `12.6ms`
- `queueWaitTimeoutCount`: `0` (6/6)
- `queueWaitNoWorkCount`: `0` (6/6)
- `queueDepthMax`: `4~5` (대부분 `4`~`5`)
- `captureToAuSkewUs`는 `720,796us ~ 883,313us` 구간
- `encQueueUs`는 `~760,000us ~ 930,000us`(`captureToAu` 기준 계산에서 확대)
- `USER_FEEDBACK` 경보 라인: `PIPE_US`는 임계치 외 5회, 1회만 `~91.8ms` 기록
- `skippedByOverwrite`: run당 누적 `14~24` 내에서 증가

판정
- `QUEUE_WAIT` 계열 지연은 수십 ms로 낮고, `PIPE_US` 임계치 미달로 사용자 샘플링이 제한된 상태였던 것이 통계 상 병목 과대해석 원인 중 하나로 확인됨.
- `captureToAuSkewUs`가 계속 수백 ms대인 점은 남아, 체감 1초대 지연의 주 원인 가능성이 여전히 AU 타임소스 정합성으로 보임.

### 47) 2026-02-26 host 병목 정확도 개선 작업(`captureToAuTimelineSkewUs` 분리)
목표
- AU 기반 타임스탬프와 캡처 기반 타임스탬프를 분리해 병목 후보 계산의 오해를 줄임.
- `encQueueUs` 과대해석 원인을 분리해 실제 큐/인코더 구간 병목만 추적 가능하게 함.

수행
- `apps/native_poc/src/native_video_host_main.cpp` 수정:
  - `captureToAuTimelineSkewUs` 및 `encQueueAlignedUs` 로그 항목 추가.
  - AU/캡처 상대 오차는 `captureTimelineOriginUs`, `auTimelineOriginUs` 기준 앵커로 계산.
  - 기존 `captureToAuSkewUs`는 진단 보조로 유지하고 병목 후보 계산에서 분리.
- `automation/verify_native_video_runtime.ps1` 수정:
  - host 사용자 피드백 집계에 `encQueueAlignedUs`, `captureToAuTimelineSkewUs` 추가.
  - 병목 후보 스테이지에서 raw `captureToAuSkewUs` 대신 `captureToAuTimelineSkewUs` 반영.

판정
- 병목 왜곡 원인(타임라인 스큐 오염) 분리 조치 적용 완료.
- 동일 설정 재측정(TraceEvery 1 권장)에서 `captureToAuTimelineSkewUs`가 낮아지면 큐 병목으로 확정, 유지되면 AU 타임소스 정합 자체 병목으로 추가 분해 예정.

### 48) 2026-02-26 host 병목 재측정 1회(TraceEvery 1, 정합 분리 로깅 반영 후)
목표
- `captureToAuTimelineSkewUs`를 정식 병목 후보로 채택한 뒤 실제 병목을 재확인.

실행
- 동일 조건 1회:
```powershell
powershell -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 -BuildDir C:\Users\CHO\AppData\Local\Temp\build-native2-test2 -Codec h264 -Transport udp -Fps 30 -FpsHint 30 -HostSeconds 12 -ClientSeconds 10 -Bitrate 1100000 -Keyint 15 -TraceEvery 1 -TraceMax 0 -NoInputChannel
```
- 로그:
  - `automation/logs/verify-native-video-20260226-234619`

주요 수치
- `USER_FEEDBACK_UF_H_PIPEUS_AVG_US=35951.87`
- `USER_FEEDBACK_UF_H_C2EUS_AVG_US=34663.71`
- `USER_FEEDBACK_UF_H_QUEUETOENCODEUS_AVG_US=22231.15`
- `USER_FEEDBACK_UF_H_QUEUETOSENDUS_AVG_US=23519.31`
- `USER_FEEDBACK_UF_H_ENCQUEUEALIGNEDUS_AVG_US=34663.71` (실측 큐-인코드 정렬 기준)
- `USER_FEEDBACK_UF_H_ENCQUEUEUS_AVG_US=722149.98` (원시 raw AU 기반)
- `USER_FEEDBACK_UF_H_CAPTURETOAUTIMELINESKEWUS_AVG_US=687486.27` (고정된 AU/캡처 상대 오차)
- `HOST_QUEUE_WAIT_TIMEOUT_COUNT=0`
- `HOST_QUEUE_WAIT_NOWORK_COUNT=0`
- `HOST_QUEUE_SKIPPED_BY_OVERWRITE=19`

판정
- 큐 적체/인코드 제출 병목은 `22~35ms` 대로 비교적 낮고, `encQueueAlignedUs` 기준으로는 병목 후보보다 작음.
- `captureToAuTimelineSkewUs`가 지속적으로 매우 높아(약 687ms) 여전히 **AU 타임라인 정합성 자체**가 사용자 체감 0.5~1초 병목의 주요 원인일 가능성이 높음.

### 49) 2026-02-27 host 병목 우선순위 수정 및 재측정(출력타임스탬프 드리프트 가드)
목표
- 동일 설정에서 `captureToAuTimelineSkewUs`가 0으로 수렴하는지 확인해 host 병목 후보를 정리.
- 출력타임스탬프 오차가 50ms 이상일 때 입력타임스탬프로 강제 fallback하는 수정의 효과 검증.

실행
- 수정된 코드로 동일 조건 1회 재측정:
```powershell
powershell -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 -BuildDir C:\Users\CHO\AppData\Local\Temp\build-remote-fix -Codec h264 -Transport udp -Fps 30 -FpsHint 30 -HostSeconds 12 -ClientSeconds 10 -Bitrate 1100000 -Keyint 15 -TraceEvery 1 -TraceMax 0 -NoInputChannel
```
- 로그:
  - `automation/logs/verify-native-video-20260227-000840`

주요 수치
- `USER_FEEDBACK_UF_H_PIPEUS_AVG_US=37645.05`
- `USER_FEEDBACK_UF_H_C2EUS_AVG_US=36182.43`
- `USER_FEEDBACK_UF_H_QUEUETOENCODEUS_AVG_US=23332.44`
- `USER_FEEDBACK_UF_H_QUEUETOSENDUS_AVG_US=24795.06`
- `USER_FEEDBACK_UF_H_SENDDONEUS_AVG_US=37890.82`
- `USER_FEEDBACK_UF_H_CAPTURE_TO_AU_TIMELINESKEW_US=0`
- `USER_FEEDBACK_UF_H_AU_TS_FROM_OUTPUT=0`
- `HOST_QUEUE_WAIT_TIMEOUT_COUNT=0`
- `HOST_QUEUE_WAIT_NOWORK_COUNT=0`
- `PRESENT_GAP_P95_US=64091`
- `PRESENT_GAP_OVER_1S=0`

판정
- host 병목 후보가 `captureToAuTimelineSkewUs`에서 `sendDoneUs`로 전환되며, 타임스탬프 정합성 병목은 해소됨을 확인.
- 실제 체감 병목은 현재 측면상 `send` 경로(네트워크 송신 지연 + 호스트 스레드 대기/큐 경유 포함)로 정리됨.
- 다음 액션:
  - `sendDoneUs`와 `queueWaitUs`를 상대로한 호스트 전송 경로/스레드 스케줄링 정밀 재분해 (패킷 배치/flush, socket 옵션, frame pacing 조정).
### 50) 2026-02-27 host send 경로 분해 재측정(헤더/페이로드/UDP 청크)
목표
- `sendDoneUs` 과다 체감의 실제 원인을 `send` API 처리시간과 큐/대기시간으로 분리 검증.

실행
- 분해 지표 추가 후 동일 조건 1회 재측정:
```powershell
powershell -ExecutionPolicy Bypass -File remote60/automation/verify_native_video_runtime.ps1 -BuildDir C:\Users\CHO\AppData\Local\Temp\build-remote-sendpath -Codec h264 -Transport udp -Fps 30 -FpsHint 30 -HostSeconds 12 -ClientSeconds 10 -Bitrate 1100000 -Keyint 15 -TraceEvery 1 -TraceMax 0 -NoInputChannel
```
- 로그:
  - `automation/logs/verify-native-video-20260227-001536`

주요 수치
- `USER_FEEDBACK_UF_H_PIPEUS_AVG_US=38967.51`
- `USER_FEEDBACK_UF_H_SENDDONEUS_AVG_US=39154.72`
- `USER_FEEDBACK_UF_H_SENDDURUS_AVG_US=265.65`
- `USER_FEEDBACK_UF_H_SENDPAYLOADUS_AVG_US=244.93`
- `USER_FEEDBACK_UF_H_SENDCHUNKMAXUS_P95_US=244`
- `USER_FEEDBACK_UF_H_SENDCALLCOUNT_P95_US=30` (대체로 UDP 청크 수 증가 구간에서 높아짐)
- `USER_FEEDBACK_UF_H_QUEUETOSENDUS_AVG_US=26247.97`
- `USER_FEEDBACK_UF_H_C2EUS_AVG_US=37466.96`
- `HOST_QUEUE_WAIT_TIMEOUT_COUNT=0`
- `HOST_QUEUE_WAIT_NOWORK_COUNT=0`
- `HOST_QUEUE_SKIPPED_BY_OVERWRITE=23`

판정
- `send` API 자체(`sendHeaderUs`/`sendPayloadUs`/`sendChunk*`)는 평균 수백 μs~ms 미만으로, 송신 호출 자체 병목은 아님.
- `sendDoneUs`/`pipeUs`는 큐 타임 포함값이라 `queueToSendUs`·`queueToEncodeUs` 누적 구간을 함께 판단해야 함.
- 다음 액션:
  - 스케줄러 큐/전송 직전 프레임 간격 제어 및 `sendStart` 기준 지연 분해(예: `sendIntervalUs`, wakeup 타임)로 추가 추적 후 `M2.5` 종료 판단.

### 51) 2026-02-27 host send 스케줄링 지연 추적 항목 적용
목표
- `sendDoneUs`의 구성요소를 더 분해해 `send` API 처리시간 외에 전송 직전 대기/스케줄 지연을 확정.
- 특히 `queueToSendUs`가 큰 시점에서 이전 프레임 간격(`sendIntervalUs`) 증가 여부를 함께 확인해 스레드 스케줄링 병목 후보를 검증.

수행
- `apps/native_poc/src/native_video_host_main.cpp`:
  - host 전송 시점 기준 전 프레임 대비 간격(`sendIntervalUs`) 및 타겟 프레임 간격 대비 편차(`sendIntervalErrUs`) 계산.
  - `raw`/`h264`의 `trace` + `user-feedback` 로그에 두 항목 추가.
  - 정상 전송 완료 시 다음 프레임 비교 기준으로 `lastSendStartUs` 갱신.
- `automation/verify_native_video_runtime.ps1`:
  - `sendIntervalUs`, `sendIntervalErrUs`를 host 사용자 피드백 집계/병목 후보/Top3 항목에 반영.
  - host 병목 후보 후보군에 `sendIntervalUs`, `sendIntervalErrUs` 추가.

판정
- 코드 레벨 분해는 완료되었고, 재빌드+재측정이 필요. 다음은 즉시 재측정 라운드에서 `sendInterval*`와 기존 `sendDoneUs/queueToSendUs`의 상관관계 판정.

### 52) 2026-02-27 host 큐 대기 이유 분류 추적 반영
목표
- `queueWaitUs` 지연이 "timeout/no-work/정상" 중 어떤 경로로 누적되는지 고립.

수행
- `apps/native_poc/src/native_video_host_main.cpp`:
  - `queueWaitReason`을 추가:
    - `0`: 정상 pop(조건 충족으로 wakeup)
    - `1`: `cv.wait_for` timeout으로 wakeup
    - `2`: `frame.version == lastVersionSent` 또는 비어있는 payload로 인한 no-work 경로
  - `raw`/`h264`의 `trace`/`user-feedback` 로그에 `queueWaitReason` 출력 추가.
- `automation/verify_native_video_runtime.ps1`:
  - `queueWaitReason` 개수(`HOST_QUEUE_WAIT_REASON_0/1/2_COUNT`) 집계 출력 추가.
  - Host TOP log에서 `USER_FEEDBACK_HOST_TOP*_QUEUE_WAIT_REASON` 추가.
  - trace/user-feedback 파싱 루프에서 이유 코드를 동시에 집계하도록 확장.

판정
- `queueWaitUs` 병목의 원인 분해를 위한 최소 추적 코드가 완료됨.
- `queueWaitReason` 기반 재측정/상관분석은 다음 런에서 진행 예정.

### 53) 2026-03-02 환경 정비 + FHD/HW 기준선 재검증
목표
- 빌드/의존성 환경을 안정화하고(`vcpkg`, `build-vcpkg-local`) 현재 체감 지연(약 1초)의 주 원인을 실측으로 재확인.
- 자동 종료/프로세스 잔존 여부를 함께 점검.

수행
1. 환경 정비
- `.vcpkg` 로컬 구성 + `vcpkg install --triplet x64-windows` 완료.
- `cmake --preset windows-vs2022-vcpkg` 통과.
- `cmake --build --preset debug-vcpkg --parallel` 통과.

2. 지연 원인 재측정 (SW/HW/해상도 비교)
- H264(기본 수동 실행, 2560x1440):
```powershell
powershell -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 -BuildDir build-vcpkg-local -Codec h264 -Transport udp -Fps 30 -FpsHint 30 -HostSeconds 10 -ClientSeconds 6 -Bitrate 1100000 -Keyint 15 -NoInputChannel -TraceEvery 15 -TraceMax 80
```
  - 로그: `automation/logs/verify-native-video-20260302-202750`
- RAW 기준선:
```powershell
powershell -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 -BuildDir build-vcpkg-local -Codec raw -Transport tcp -Fps 30 -HostSeconds 10 -ClientSeconds 6 -NoInputChannel -TraceEvery 15 -TraceMax 80
```
  - 로그: `automation/logs/verify-native-video-20260302-202824`
- HW 강제 확인:
```powershell
powershell -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 -BuildDir build-vcpkg-local -Codec h264 -Transport udp -Fps 30 -FpsHint 30 -HostSeconds 10 -ClientSeconds 6 -Bitrate 2500000 -Keyint 30 -EncoderBackend mft_hw -DecoderBackend mft_hw -NoInputChannel -TraceEvery 15 -TraceMax 80
```
  - 로그: `automation/logs/verify-native-video-20260302-203223`
  - 백엔드 확인: `encoder backend=mft_enum_hw hw=1`, `decoder backend=mft_enum_hw hw=1`

3. FHD 권장 조건 검증
```powershell
powershell -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 -BuildDir build-vcpkg-local -Codec h264 -Transport udp -EncoderBackend mft_hw -DecoderBackend mft_hw -EncodeWidth 1920 -EncodeHeight 1080 -Bitrate 4000000 -Keyint 30 -Fps 30 -FpsHint 30 -HostSeconds 10 -ClientSeconds 6 -NoInputChannel
```
- 로그: `automation/logs/verify-native-video-20260302-203424`
- 추가 3회 반복:
  - `automation/logs/verify-native-video-20260302-203505`
  - `automation/logs/verify-native-video-20260302-203514`
  - `automation/logs/verify-native-video-20260302-203523`

주요 수치
- H264(초기, 2560x1440): `LAT_AVG_US=51540`, `BOTTLENECK_STAGE=c2eUs`, host 병목 후보 `sendIntervalUs/queue_to_send`.
- RAW 기준선: `LAT_AVG_US=20730`.
- FHD+HW(1회): `LAT_AVG_US=39425`, `LAT_P95_US=49317`, `PRESENT_GAP_OVER_1S=0`.
- FHD+HW(3회 평균):
  - `LAT_AVG_US_AVG=38104.67`
  - `LAT_P95_US_AVG=42497`
  - `LAT_MAX_US` 범위 `59561 ~ 63288`
  - `PRESENT_GAP_OVER_1S_SUM=0`

자동 종료/안정성
- `verify_native_video_runtime.ps1`에서 `--seconds` 기반 host/client 자동 종료 동작 확인.
- 실행 후 `remote60_native_video_host_poc`, `remote60_native_video_client_poc` 프로세스 잔존 없음 확인.

주의/후속
- 3회 반복 출력 말미에 `verify_native_video_runtime.ps1`의 `hostUserFeedbackTopEntries.Count` 접근 예외 1회 확인(요약 지표는 출력됨).
  - 후속 패치 필요(StrictMode에서 배열/단일객체 타입 안전 처리).

판정
- 체감 1초급 지연의 핵심 원인은 `2560x1440 + SW 코덱 경로`에서 발생하는 host-side queue/pacing 누적.
- `FHD(1920x1080) + mft_hw 강제`로 지연이 유의미하게 안정화(약 38~42ms 평균 구간).

### 54) 2026-03-02 색상 반전(blue->orange) 증상 패치
목표
- 클라이언트 렌더 색상 반전(파란색이 주황색으로 보이는 현상) 보정.

수행
- `apps/native_poc/src/native_video_client_main.cpp` 픽셀 셰이더 출력 채널 순서 수정:
  - 변경 전: `float4(saturate(b), saturate(g), saturate(r), 1.0)`
  - 변경 후: `float4(saturate(r), saturate(g), saturate(b), 1.0)`
- 클라이언트 타깃 재빌드:
```powershell
cmake --build --preset debug-vcpkg --target remote60_native_video_client_poc --parallel
```

수동 확인 세션
- 패치 전 FHD/HW 수동 실행:
  - `automation/logs/native-video-manual-fhd-20260302-203703`
- 패치 후 FHD/HW 수동 실행:
  - `automation/logs/native-video-manual-fhd-colorfix-20260302-203903`

판정
- 색상 채널 역전 코드 경로를 직접 수정해 반영 완료.
- 패치 후 수동 세션 재기동 완료(체감 화질/색감 최종 확인 단계).

### 55) 2026-03-02 지연 체감(150~200ms) 대응 조합 재측정 + 해상도 상향 후보 정리
목표
- 체감 지연을 낮추면서 텍스트 선명도를 올릴 수 있는 실행 조합을 실측으로 선별.
- 이전 후속 이슈였던 `verify_native_video_runtime.ps1` StrictMode 예외를 우선 제거.

수행
1. `verify_native_video_runtime.ps1` 안정화 패치
- `host/client top entry` 정렬 결과를 배열로 정규화해 `.Count` 접근이 단일 객체에서도 안전하도록 수정.
- 파일: `automation/verify_native_video_runtime.ps1`

2. 조합 실측 (build: `build-vcpkg-local`, `h264+udp`, `mft_hw/mft_hw`, `NoInputChannel`)
- baseline FHD 30: `1920x1080`, `4Mbps`, `keyint=30`
  - `automation/logs/verify-native-video-20260302-210645`
- quality FHD 30: `1920x1080`, `8Mbps`, `keyint=30`
  - `automation/logs/verify-native-video-20260302-210655`
- quality FHD 30 + async poll(4):
  - `automation/logs/verify-native-video-20260302-210704`
- up-res 1296p 30 + async poll(4): `2304x1296`, `10Mbps`
  - `automation/logs/verify-native-video-20260302-210713`
- up-res 1440p 30 + async poll(4): `2560x1440`, `12Mbps`
  - `automation/logs/verify-native-video-20260302-210722`

주요 수치
- baseline FHD 30 4Mbps:
  - `LAT_AVG_US=34228`, `LAT_P95_US=36953`, `LAT_MAX_US=54899`, `DEC_AVG=13.4`
- quality FHD 30 8Mbps:
  - `LAT_AVG_US=28924.8`, `LAT_P95_US=35132`, `LAT_MAX_US=41782`, `DEC_AVG=14.6`
- quality FHD 30 8Mbps + async4:
  - `LAT_AVG_US=40307`, `LAT_P95_US=43929` (악화)
- up-res 1296p 30 10Mbps + async4:
  - `LAT_AVG_US=39123.6`, `LAT_P95_US=46741`
- up-res 1440p 30 12Mbps + async4:
  - `LAT_AVG_US=57403.2`, `LAT_P95_US=71867`

판정
- 현재 기준 최적 균형점은 `FHD 30 + 8Mbps + mft_hw/mft_hw` (async poll 튜닝 없음).
- `REMOTE60_NATIVE_H264_ASYNC_POLL_MAX=4`는 본 실측 조건에서 지연 개선보다 악화 경향.
- 해상도 상향은 `2304x1296`까지는 수용 가능하나(`~39/47ms`), `2560x1440`는 지연 증가폭이 커 체감 지연 목표와 충돌.

후속 반영
- 신규 프로필 추가:
  - `automation/native_video_profile_1080p_lowlat.json`
  - `automation/native_video_profile_1296p_balanced.json`
- README 프로필 목록 갱신:
  - `apps/native_poc/README.md`

### 56) 2026-03-02 1080p 3장면 스모크 완료 + M3 종료 판정(OFF/ON 20회)
목표
- 구현계획의 미완료 항목 2개를 실측으로 종료:
  - `1080p 3개 장면 스모크 LAT_P95<=70ms` 확인
  - `M3 종료 판정`(quality-first OFF/ON 각 20회 재현성) 완료

수행
1. 스크립트 보강
- `automation/verify_native_video_scene_suite.ps1`
  - `-BuildDir` 인자 추가(기본 `build-native2`) 후 내부 `verify_native_video_runtime.ps1` 호출로 전달.
- `automation/run_native_video_ablation_suite.ps1`
  - `-BuildDir`, `-EncoderBackend`, `-DecoderBackend` 인자 추가 및 verify 호출 전달.

2. 1080p 3장면 스모크 실행
```powershell
powershell -ExecutionPolicy Bypass -File automation/verify_native_video_scene_suite.ps1 -BuildDir build-vcpkg-local -Codec h264 -Transport udp -EncoderBackend mft_hw -DecoderBackend mft_hw -EncodeWidth 1920 -EncodeHeight 1080 -Bitrate 8000000 -Keyint 30 -Fps 30 -FpsHint 30 -NoInputChannel -HostSeconds 10 -ClientSeconds 6 -TraceEvery 15 -TraceMax 80 -NonInteractive -ScenePrepareSeconds 1
```
- 로그:
  - suite: `automation/logs/verify-native-video-scenes-20260302-211915`
  - static: `automation/logs/verify-native-video-20260302-211916`
  - scroll: `automation/logs/verify-native-video-20260302-211926`
  - video: `automation/logs/verify-native-video-20260302-211935`

3. M3 종료 판정 실행(OFF/ON 각 20회)
```powershell
powershell -ExecutionPolicy Bypass -File automation/run_native_video_ablation_suite.ps1 -Mode M3 -Iterations 20 -BuildDir build-vcpkg-local -Codec h264 -Transport udp -Fps 30 -FpsHint 30 -Bitrate 8000000 -Keyint 30 -EncodeWidth 1920 -EncodeHeight 1080 -EncoderBackend mft_hw -DecoderBackend mft_hw -HostSeconds 8 -ClientSeconds 6 -TraceEvery 15 -TraceMax 40
```
- 로그:
  - `automation/logs/native-video-ablation-20260302-212009`
  - summary: `automation/logs/native-video-ablation-20260302-212009/summary.csv`

주요 수치
- 1080p 3장면:
  - static: `LAT_P95_US=62480`
  - scroll: `LAT_P95_US=37784`
  - video: `LAT_P95_US=48708`
  - `SUITE_OVERALL_OK=True`
- M3 OFF/ON 20회:
  - `quality_first_off`: `lat_p95_avg=55022.8`, `ok=20/20`
  - `quality_first_on`: `lat_p95_avg=35499.05`, `ok=20/20`
  - 두 변형 모두 `AbrSwitchCount_Avg=0`

판정
- 구현계획 항목 `1080p 3개 장면 스모크` 완료.
- `M3 종료 판정` 완료, 현재 기본 운영은 `quality-first=on` 유지.
- 비고: 3장면 중 video에서 `PRESENT_GAP_OVER_1S=1` 1회 관측되어 전송 안정화(M6) 항목에서 후속 추적 필요.

### 57) 2026-03-02 ENCODER API 경로별 지연/대기 분기 계측 추가
목표
- 구현계획 M2.5 미완료 항목(`ENCODER API 경로별 지연/대기 분기`)을 코드/파서/실측까지 완료.

수행
1. host 계측 확장
- 파일: `apps/native_poc/src/native_video_host_main.cpp`
- `encoder.encode_frame(...)` 호출 시 `H264EncodeFrameStats` 수집 연결.
- `trace/user-feedback` 로그에 아래 항목 추가:
  - `encApiPathCode`, `encApiHw`
  - `encApiInputUs`, `encApiDrainUs`
  - `encApiNotAcceptingCount`, `encApiNeedMoreInputCount`, `encApiStreamChangeCount`, `encApiOutputErrorCount`
  - `encApiAsyncEnabled`, `encApiAsyncPollCount`, `encApiAsyncNoEventCount`, `encApiAsyncNeedInputCount`, `encApiAsyncHaveOutputCount`
- backend 문자열을 API path code로 맵핑하는 함수 추가:
  - `1=AMF`, `2=NVENC`, `3=QSV`, `4=MFT`, `5=CLSID`, `0/6=unknown/other`

2. verify 파서 확장
- 파일: `automation/verify_native_video_runtime.ps1`
- host user-feedback 통계 키에 `encApi*` 항목 추가.
- path code 분류 집계 출력 추가:
  - `HOST_ENC_API_PATH_CODE_0..6_*`
- top entry 출력에 `ENC_API_*` 상세 항목 반영.

3. 빌드 및 스모크 검증
- 빌드:
```powershell
cmake --build --preset debug-vcpkg --parallel
```
- 검증:
```powershell
powershell -ExecutionPolicy Bypass -File automation/verify_native_video_runtime.ps1 -BuildDir build-vcpkg-local -Codec h264 -Transport udp -EncoderBackend mft_hw -DecoderBackend mft_hw -EncodeWidth 1920 -EncodeHeight 1080 -Bitrate 8000000 -Keyint 30 -Fps 30 -FpsHint 30 -HostSeconds 8 -ClientSeconds 6 -NoInputChannel -TraceEvery 15 -TraceMax 40
```

주요 수치
- `LAT_P95_US=31615`, `OVERALL_OK=True`
- `HOST_ENC_API_PATH_CODE_4_MFT_COUNT=8`
- `USER_FEEDBACK_UF_H_ENCAPIINPUTUS_AVG_US=1859.75`
- `USER_FEEDBACK_UF_H_ENCAPIDRAINUS_AVG_US=752`
- `USER_FEEDBACK_UF_H_ENCAPIASYNCPOLLCOUNT_AVG_US=1`

판정
- ENCODER API path 분기 지표가 host 로그와 verify 요약 모두에서 정상 집계됨.
- 구현계획 M2.5의 해당 미완료 항목을 완료로 전환.

### 58) 2026-03-02 FHD 화질 개선용 비트레이트/Keyint 튜닝 재측정
목표
- 사용자 체감 이슈(FHD인데 화질이 낮아 보임) 대응:
  - 해상도는 FHD 고정
  - 지연 증가폭을 보면서 화질(압축 강도) 개선 가능한 조합 탐색

수행
공통 조건:
- `build-vcpkg-local`, `h264+udp`, `mft_hw/mft_hw`, `1920x1080`, `30fps`, `NoInputChannel`
- `REMOTE60_NATIVE_ABR_DISABLE=1`, `REMOTE60_NATIVE_ADAPTIVE_QUALITY_FIRST=1`

1) 비트레이트 스윕 (`keyint=30`)
- `8/10/12 Mbps`, 각 3회
- 로그:
  - 8Mbps: `verify-native-video-20260302-214208`, `...214217`, `...214226`
  - 10Mbps: `verify-native-video-20260302-214235`, `...214244`, `...214254`
  - 12Mbps: `verify-native-video-20260302-214303`, `...214312`, `...214321`

요약(3회 평균):
- 8Mbps: `LAT_P95_US=31054.67`, `LAT_AVG_US=32292.07`, `MBPS_AVG=4.53`, `ENC_RATIO_X100_AVG=4929.07`
- 10Mbps: `LAT_P95_US=43704.33`, `LAT_AVG_US=42722.06`, `MBPS_AVG=5.24`, `ENC_RATIO_X100_AVG=3976.07`
- 12Mbps: `LAT_P95_US=45763`, `LAT_AVG_US=41776.93`, `MBPS_AVG=6.8`, `ENC_RATIO_X100_AVG=3297.3`

판정:
- 비트레이트를 올리면 `ENC_RATIO_X100`은 유의미하게 하락(압축 강도 완화, 화질 개선 방향)하지만,
  `LAT_P95_US`가 크게 상승.
- 저지연 기준 최적점은 여전히 8Mbps.

2) Keyint 스윕 (`bitrate=8Mbps`)
- `keyint=30/60/90`, 각 2회
- 로그:
  - keyint 30: `verify-native-video-20260302-214407`, `...214416`
  - keyint 60: `verify-native-video-20260302-214425`, `...214435`
  - keyint 90: `verify-native-video-20260302-214444`, `...214453`

요약(2회 평균):
- keyint 30: `LAT_P95_US=30860.5`, `LAT_AVG_US=27827.1`, `DEC_AVG=20.4`, `ENC_RATIO_X100_AVG=4827.07`
- keyint 60: `LAT_P95_US=22431`, `LAT_AVG_US=20474.81`, `DEC_AVG=21.68`, `ENC_RATIO_X100_AVG=4797.1`
- keyint 90: `LAT_P95_US=27594.5`, `LAT_AVG_US=21787.52`, `DEC_AVG=22.44`, `ENC_RATIO_X100_AVG=4778.84`

판정:
- 동일 8Mbps에서 `keyint=60`이 지연 지표가 가장 안정적으로 낮음.
- 화질 관련 프록시(`ENC_RATIO_X100`) 개선폭은 작지만, 지연 악화 없이 운용 가능.

권장
- 저지연 우선 + FHD 유지: `1920x1080 / 30fps / 8Mbps / keyint 60 / mft_hw`
- 화질 우선(지연 증가 허용): `1920x1080 / 30fps / 10Mbps / keyint 60`

### 59) 2026-03-02 외부(WAN) 테스트 준비: 번들 패키징 + JSON 프로필 세트 정리
목표
- 외부망 테스트를 바로 수행할 수 있도록 빌드 산출물/스크립트/프로필을 한 번에 전달 가능한 번들 생성.
- FHD 전용 운영을 위한 현재 권장 설정들을 JSON 프로필로 정리.

수행
1. JSON 프로필 정리
- `automation/native_video_profile_1080p_lowlat.json`
  - `keyint: 60`으로 업데이트(저지연 권장).
- 신규 추가:
  - `automation/native_video_profile_1080p_quality_10m_k60.json`
  - `automation/native_video_profile_1080p_quality_12m_k60.json`
  - `automation/native_video_profile_1080p_external_template.json` (`remoteHost` 템플릿 포함)

2. 외부 배포 번들 스크립트 추가
- `automation/package_native_video_external_bundle.ps1`
  - 입력: `BuildDir`(기본 `build-vcpkg-local`)
  - 출력: `dist/native-video-external-<timestamp>/` + `.zip`
  - 포함:
    - `bin` (host/client exe + dll)
    - `automation` (실행 스크립트 + JSON 프로필)
    - `docs/EXTERNAL_WAN_QUICKSTART.md` (포트포워딩/방화벽/주소입력/실행명령)

3. 문서 업데이트
- `apps/native_poc/README.md`
  - 외부 번들 스크립트/신규 프로필 목록 추가
  - WAN 체크리스트(포트/주소 입력/번들 명령) 추가
- `docs/external_wan_test_guide.md` 신규 추가

4. 번들 생성 검증
```powershell
powershell -ExecutionPolicy Bypass -File automation/package_native_video_external_bundle.ps1 -BuildDir build-vcpkg-local
```
- 출력 확인:
  - `BUNDLE_DIR=D:\remote\remote\dist\native-video-external-20260302-214953`
  - `BUNDLE_ZIP=D:\remote\remote\dist\native-video-external-20260302-214953.zip`

판정
- 외부 테스트용 산출물/설정/가이드가 자동 생성되는 상태로 정리 완료.
- 포트포워딩은 현재 UDP 프로필 기준 `UDP 43000`, 컨트롤 채널 `TCP 43001`만 열면 됨.

### 60) 2026-03-02 Host capture stall watchdog + cross-context handoff update
Goal
- Improve recovery for intermittent capture callback stalls (LDPlayer/page-like freeze patterns).
- Make continuation easy from a fresh context using docs snapshot.

Changes
1. Host capture watchdog/restart
- File: `apps/native_poc/src/native_video_host_main.cpp`
- Added stall detection and auto restart:
  - restart threshold: `kCaptureCallbackStallRestartUs=1200000` (1.2s)
  - restart cooldown: `kCaptureCallbackRestartCooldownUs=3000000` (3s)
- Added lifecycle helpers:
  - `attach_frame_arrived`, `detach_capture_session`, `restart_capture_session`
- Added stats/log fields:
  - `captureRestarts=` in periodic host summary
  - `capture session restarted count=...` on watchdog-triggered recovery
- Cleanup path unified with `detach_capture_session()`.

2. Verification
- Build: `cmake --build --preset debug-vcpkg --parallel`
- Short verify log: `automation/logs/verify-native-video-20260302-233704`
- Longer verify log: `automation/logs/verify-native-video-20260302-233759`
- Result: both `HOST_RC=0`, `CLIENT_RC=0`
- HW encoder confirmed in host log (`mft_enum_hw hw=1`).

3. Bundle sync for real test
- Synced latest runtime files into `D:\remote\build`:
  - `bin` host/client exe (latest)
  - `automation` scripts + `native_video_profile_*.json`
  - `docs/external_wan_test_guide.md`

4. Plan/handoff doc update
- File: `docs/구현계획.md`
- Added `## 7) Context Handoff Snapshot (2026-03-02)` with:
  - current stable status
  - latest patch summary
  - verified log paths
  - immediate next actions (M5 -> M6 -> M7)
  - ready-to-run smoke/real-use commands

Notes
- Freeze class appears reduced in local verify; final confirmation requires user-side repro scenario where callback stall used to occur.
