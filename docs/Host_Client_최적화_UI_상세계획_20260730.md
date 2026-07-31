# Host / Client 최적화·UI 상세 구현 계획

- 작성일: 2026-07-30
- 현행화: 2026-07-31 (F1 결함 해소·U2 Android 뷰어 현황·코드 재검증 반영)
- 기준 감사: `docs/Host_Client_최적화_UI_감사_20260730.md`
- 상태: 구현 전 계획
- 적용 대상: Host 관리 앱, 영상 Host, Windows Client, Android GNLink Client, Directory/UDP 보안

## 0. 현행화 메모 (2026-07-31)

이 계획이 참조하는 감사 이후 다음이 이미 반영됐다. 해당 절은 다시 구현하지 않는다.

- 검은 화면 결함은 Host 쪽에서 해소됐다(`bf19eee`). 새 제어 세션이 시작되면 스트림을 기본
  활성으로 복원하고, UDP 리더 스레드가 길이 0 데이터그램·비정상 recv 오류에 죽지 않게 됐다.
  F1은 결함 수정이 아니라 절전용 선택 과제로 축소됐다. 4장 참조.
- Android 뷰어 UI가 사용자 결정으로 크게 바뀌었다: 좌측 존 바(우클릭/태블릿+잠금/마우스),
  태블릿 모드 자연 스크롤, 화면 마우스 클러스터 고정·드래그 배치, 레일 축소(MOUSE/LOG 제거,
  로그는 MENU로), 세로 회전 레이아웃 수정. U2의 Android 뷰어 항목은 15장의 현행 기준을 따른다.
- 매크로는 일시정지/스텝 편집/저장·불러오기와 Windows 매크로 창까지 완료됐고 엔진 테스트는
  46개다(감사 시점 23개).

2026-07-31에 성능 항목(B1, H1~H4, C1~C2, A1~A2) 전부를 코드와 대조 검증했다. 감사 진단은
A2와 A1의 어댑터 부분(이미 변경 게이트 존재)을 빼면 큰 방향이 정확했다. 이어진 2차 검증에서
Windows의 1080→1088 가시 영역 오류, 런타임 bitrate 재설정 시 rate-control/pacing 불일치,
제품 Host의 암묵적 encoder tune 차이도 확인했다. 또한 H2의 구현 수단, H3 surface lifetime,
A1의 scene별 JNI 횟수, A2의 version 의미를 코드에 맞게 정정했다. 각 근거와 조치는 Q1 및
관련 작업 절의 "검증 결과/추가 확인" 소절에 반영돼 있다.

## 1. 목표

이 문서는 감사에서 발견한 항목을 실제 구현 단위로 바꾼다. “최적화한다”가 아니라 어떤 파일과
경계를 바꾸고, 무엇을 측정해 완료로 판정할지를 고정한다.

최종 목표:

- Windows Client의 대상 선택 후 검은 화면 결함 제거
- Host 캡처 callback에서 동기 readback과 전체 프레임 memcpy 제거
- crop/resize/BGRA→NV12를 가능한 범위에서 GPU에 유지
- Media Foundation 인코더에 NV12 D3D surface를 직접 전달
- coded frame과 visible aperture를 분리해 1080p 화면을 1088행으로 렌더하지 않음
- 초기화·런타임 변경·ABR에서 화질 preset/rate-control/UDP pacing을 일관되게 유지
- 1080p30·720p30 성능 Gate를 단독 실행 5회 기준으로 통과
- Host/Windows/Android UI의 브랜드, 상태 표현, 일반/고급 기능 계층 통일
- Android의 고정 250ms 전체 UI 갱신과 세션 동안 낡는 썸네일 제거
- 외부 배포 전에 Directory와 미디어·제어 채널의 평문 전송 제거

## 2. 구현 원칙

### 반드시 지킬 순서

1. 기능 결함을 먼저 수정한다.
2. Release 기준선을 다시 수집한다.
3. Host 병목을 한 단계씩 제거한다.
4. Host가 목표에 근접한 뒤 Client를 최적화한다.
5. UI 구조를 정리한다.
6. 인터넷 공개 전에 보안 Gate를 닫는다.

### 변경 단위

- 작업 ID 하나를 원칙적으로 커밋 하나로 유지한다.
- 성능 작업은 한 커밋에서 한 가지 경로만 바꾼다.
- 공용 `.hpp` 인터페이스를 바꾸면 전체 clean build를 수행한다.
- 각 Host 성능 단계는 CPU fallback을 남기고, fallback 이유를 로그로 출력한다.
- 성능이 개선되지 않거나 안정성이 나빠지면 해당 단계만 되돌릴 수 있어야 한다.
- 대형 파일 분리는 기능 변경을 위한 책임 경계만 추출한다. 선행 대규모 리팩터링은 하지 않는다.

### 하지 않을 작업

- Host 관리 앱 idle loop 미세 최적화
- Android 로그인 화면 idle 렌더 최적화
- Android MediaCodec Surface 출력을 CPU/Bitmap 디코드로 교체
- 측정 근거 없이 bitrate·quality를 낮춰 fps 숫자만 맞추기
- 검증 없이 자체 암호 알고리즘 구현

## 3. 전체 작업 순서

| 순서 | 작업 ID | 내용 | 선행조건 |
|---:|---|---|---|
| 1 | F1 | (결함은 해소됨) Windows stream-state 절전 동기화 | 없음 |
| 2 | U1 | Host signed-in UI 수정 | 없음 |
| 3 | B1 | Release 성능 기준선·격리 실행기 고정 | 없음 |
| 4 | Q1 | visible aperture·rate-control·pacing·제품 tune 정합성 | B1 |
| 5 | H1 | callback copy-only + worker readback ring | Q1 |
| 6 | H2 | GPU-front crop/resize | H1 |
| 7 | H3 | GPU NV12 surface → MF encoder | H2 |
| 8 | H4 | 전송 pacing/feedback 지연 분리 | H3 |
| 9 | C1 | Windows RTV·thumbnail/GDI 비용 제거 | F1, B1 |
| 10 | C2 | Windows decoder surface 렌더 spike | Q1, H3 결과 |
| 11 | A1 | Android dirty snapshot·adaptive poll | F1 |
| 12 | A2 | Android thumbnail TTL·버퍼 재사용 | A1 |
| 13 | U2 | Windows/Android 정보 구조·브랜드 정리 | F1, U1 |
| 14 | S1 | HTTPS 적용 | 기능 개발과 병행 가능 |
| 15 | S2 | 미디어·제어 인증 암호화 | S1 |
| 16 | G1 | 전체 회귀·WAN·soak 최종 Gate | 모든 채택 작업 |

`S1`, `S2`는 구현 순서상 뒤에 있어도 외부 공개 기준으로는 P0이다. 완료 전 인터넷 제품 배포를
허용하지 않는다.

## 4. F1 — Windows stream-state 절전 동기화 (검은 화면 결함은 해소됨)

### 2026-07-31 현행화

감사가 지목한 검은 화면 결함은 Host 쪽 수정으로 해소됐다(`bf19eee`).

- Host는 새 제어 세션이 시작될 때 스트림을 기본 활성으로 복원한다. stream-state 메시지를
  보내지 않는 Windows Client도 재연결 후 영상을 받는다.
- 같은 커밋에서 UDP 리더 스레드가 길이 0 데이터그램(NAT keepalive, 포트 스캐너)이나 알 수
  없는 recv 오류로 조용히 종료하던 문제도 고쳐졌다. 리더가 죽으면 Hello를 읽지 못해 영상
  피어를 새 클라이언트로 넘기지 못했고, 이것이 "제어는 되는데 영상은 검다"의 다른 절반이었다.
- 재현 검증: A 스트리밍 → 길이 0 데이터그램 주입 → A 강제 종료 → B 접속만으로 영상 수신.

따라서 이 절의 남은 범위는 결함 수정이 아니라 **절전 동기화**다: Windows Client가 대상 목록에
있는 동안 Host가 인코딩을 계속하는 것을 멈추게 한다. 우선순위는 낮으며 C1과 묶어 진행해도 된다.

### 남은 원인 (절전 관점)

`apps/native_poc/src/native_video_client_main.cpp`의 `gStreamStateControl`은
`ClientControlScheduler`에 연결돼 있지만 Windows UI에서 `Request()`를 호출하지 않는다.
Host가 세션 시작 시 스트림을 켜 주므로 영상은 나오지만, 목록 화면에서도 인코딩과 전송이
계속된다. Android의 `ClientSessionController::RequestStreamActive()`가 비교 기준이다.

### 수정 파일

- `apps/native_poc/src/native_video_client_main.cpp`
- `apps/native_poc/src/native_video_client_shared_core_test.cpp`
- 필요할 때만 `apps/native_poc/src/native_video_client_shared_core.hpp/.cpp`

### 구현 방법

1. picker 상태 직접 쓰기를 두 함수로 모은다.
   - `enter_target_picker(reason)`
   - `enter_viewer(targetId, alreadySelected, reason)`
2. `enter_target_picker()`는 다음 순서로 처리한다.
   - `gStreamStateControl.Request(false)`
   - picker 표시
   - window list 요청
   - 입력 capture/button 상태 초기화
   - scene 전환 로그 기록
3. `enter_viewer()`는 다음 순서로 처리한다.
   - `gStreamStateControl.Request(true)`
   - 현재 선택과 다를 때만 `queue_window_select_request(targetId)`
   - 이미 선택된 대상이면 picker를 즉시 닫음
   - 새 대상이면 selected 응답 또는 첫 frame 전까지 전환 overlay 유지
4. mouse와 `WM_POINTER`의 Desktop/card/toggle 분기가 모두 위 함수를 호출하게 한다.
5. `apply_window_selected_result()`는 성공 응답일 때만 viewer 전환을 확정한다.
6. 연결 종료·control 오류에서는 pending stream state와 picker transition을 초기화한다.
7. 로그에 다음을 남긴다.
   - `scene=targets streamActive=0`
   - `scene=viewer streamActive=1 selectedId=... alreadySelected=...`
   - stream-state 전송 seq와 window-select seq

`ClientControlScheduler::NextAction()`은 stream state를 window select보다 먼저 소비한다. 이 순서를
회귀 테스트로 고정하고 scheduler 우선순위는 바꾸지 않는다.

### 테스트

- shared core:
  - active와 select를 함께 queue하면 첫 action이 `StreamState`, 다음이 `WindowSelect`
  - `Request(true)` 반복 시 마지막 상태가 정상 전송
  - targets 복귀 시 `active=false`
- Windows 실제 연결:
  - 최초 목록 → Desktop → 영상
  - viewer → Targets → 이미 선택된 Desktop → 영상
  - viewer → Targets → 다른 window → selected ack → 영상
  - mouse와 touch/pointer 경로 각각 1회
  - 빠르게 Targets/viewer를 10회 왕복해 검은 화면·입력 누수 없음

### 완료 기준

- 위 5개 실제 시나리오에서 첫 영상이 2초 안에 표시
- Host 로그에서 viewer 진입 전 `stream-state active=1` 확인
- targets 상태에서 Host encoded frame이 계속 증가하지 않음
- 기존 Android stream-state 순서 무회귀

## 5. U1 — Host signed-in UI 수정

### 수정 파일

- `apps/native_poc/src/host_app_main.cpp`
- 신규 `apps/native_poc/res/gnlink.ico`
- 신규 `apps/native_poc/host_app.rc`
- `apps/native_poc/CMakeLists.txt`

### 구현 방법

1. `AppState`에 로그인 라벨 HWND를 모두 저장한다.
   - server/account/password/PC name/signup key label
2. control 생성 시 ID 0의 임시 라벨을 사용하지 않고 상태별 control 집합을 명확히 만든다.
3. 레이아웃 함수를 분리한다.
   - `layout_signed_out(dpi)`
   - `layout_signed_in(dpi)`
4. signed-out 화면:
   - 제목/설명
   - 계정, 비밀번호
   - PC 이름
   - Directory URL은 기본 접힘 상태의 `고급 설정`
   - 계정 생성 체크와 signup key
   - 명확한 primary `Sign in`
5. signed-in 화면:
   - 계정
   - 이 PC 이름
   - 상태 badge: 시작 중 / 연결 가능 / 재로그인 필요 / 오류
   - 자동 시작
   - 계정 변경, 로그아웃, 로그 열기
6. signed-in 전환 시 로그인 control과 라벨을 모두 숨기고 창 높이를 상태 카드 크기로 줄인다.
7. `statusLabel` 고정 40px을 없애고 3줄 이상 표시 가능한 DPI 기반 높이로 배치한다.
8. `WM_DPICHANGED`에서 새 DPI로 font와 모든 control 위치를 다시 계산한다.
9. 창 제목, tray tooltip, Run value 표시는 사용자 노출 부분부터 `GNLink Host`로 통일한다.
   기존 cache/실행 파일 호환용 내부 식별자 `remote60`은 별도 migration 없이 유지한다.
10. `.rc`에 앱/트레이 아이콘을 연결하고 기본 `IDI_APPLICATION` 사용을 제거한다.

### UI 검증

- DPI 100%, 150%, 200%
- signed-out, 계정 생성 펼침, 로그인 진행 중, signed-in, token rejected, child restart 상태
- 키보드 Tab 순서와 Enter 기본 버튼
- 긴 계정·PC 이름 ellipsis 또는 줄바꿈
- Windows 고대비 모드에서 상태를 색상만으로 구분하지 않음

### 완료 기준

- signed-in 화면에 로그인 라벨이 하나도 남지 않음
- reachable 또는 오류 문구가 잘리지 않음
- 모든 DPI에서 control 겹침·창 밖 배치 없음
- 비밀번호가 cache/log에 기록되지 않는 기존 보안 테스트 PASS

## 6. B1 — Release 성능 기준선과 자동화 수정

### 현재 문제 (2026-07-31 코드 검증 완료)

`automation/verify_native_video_runtime.ps1:46-49`는 실행 파일 경로의 `Debug` 구성이 리터럴로
박혀 있고 `-Configuration` 인자가 없다. `:51-52`는 시작 전에
`remote60_native_video_host_poc`/`remote60_native_video_client_poc` 이름의 프로세스를 PID 구분
없이 전부 강제 종료한다(와일드카드는 아니고 정확히 이 두 이름). 종료 시 정리는 이미 PID 기준으로
올바르다. scene suite는 이 스크립트에 BuildDir만 넘기므로 같은 제약을 그대로 상속한다.

이 문제는 이미 실제 사고를 냈다: 2026-07-30 감사 실행이 사용자가 쓰던 Host를 종료시켰고, 그
직후 좀비 클라이언트/스트림 상태가 겹쳐 "제어는 되는데 영상이 검은" 장애로 이어졌다
(`bf19eee`로 호스트 내성은 확보했지만, 스크립트가 남의 프로세스를 죽이는 것 자체를 없애야 한다).
최적화 비교는 Release여야 하며, 사용자가 실행 중인 Host를 건드리지 않는 격리 실행이 필요하다.

### 수정 파일

- `automation/verify_native_video_runtime.ps1`
- `automation/verify_native_video_scene_suite.ps1`
- 필요 시 신규 `automation/compare_optimization_runs.ps1`

### 구현 방법

1. `-Configuration Debug|Release` 인자를 추가하고 실행 파일 경로에 반영한다.
2. `Stop-Process remote60_*` 일괄 종료를 제거한다.
   - 지정 포트가 사용 중이면 소유 PID를 출력하고 실패
   - 스크립트가 시작한 PID만 `finally`에서 종료
3. 기본 최적화 포트를 제품 포트와 다른 격리 포트로 지정할 수 있게 한다.
4. run metadata를 JSON에 기록한다.
   - commit
   - configuration
   - CPU/GPU/OS
   - codec/backend/resolution/fps/bitrate/keyint
   - scene/반복 번호
5. host/client log에서 다음을 JSON으로 집계한다.
   - decoded fps 평균·최소
   - latency p50/p95/max
   - 1초 이상 gap
   - Host callback/readback/crop-scale/NV12/encode/send 시간
   - CPU time, peak working set
   - queue depth/drop/fallback 횟수
6. `compare_optimization_runs.ps1`은 before/after 5회 중앙값과 최악값을 표로 출력한다.
7. Debug 빌드는 기능 테스트, Release 빌드는 성능 판정으로 역할을 고정한다.

### 기준선 매트릭스

| 해상도 | 장면 | 반복 | 목적 |
|---|---|---:|---|
| 1920×1080 30fps | static | 5 | frame gating·idle 비용 |
| 1920×1080 30fps | scroll | 5 | 텍스트·상호작용 |
| 1920×1080 30fps | video | 5 | 최대 지속 처리량 |
| 1280×720 30fps | scroll | 5 | 저부하 회귀 |

성능 작업의 주 비교 장면은 1080p30 scroll이다. 장면 준비가 다르면 수치를 비교하지 않는다.

### 완료 기준

- 기존 사용자 Host를 종료하지 않고 격리 실행 성공
- Release 결과 JSON 생성
- 동일 commit 5회 결과 편차와 중앙값 출력
- 실패 시 Host/Client raw log와 PID/포트 정보 보존

## 6A. Q1 — 화질·동적 설정 정합성 quick fixes

H1~H3 구조 변경 전에, 현재 화질을 불필요하게 흐리거나 실행 경로별 결과를 다르게 만드는
정합성 문제를 먼저 고친다. 이 항목은 bitrate를 올리는 튜닝이 아니라 geometry와 기존 설정을
의도대로 적용하는 correctness 작업이다.

### 수정 파일

- `apps/native_poc/src/mf_h264_codec.hpp/.cpp`
- `apps/native_poc/src/native_video_client_main.cpp`
- `apps/native_poc/src/native_video_host_main.cpp`
- `apps/native_poc/src/host_app_main.cpp`
- 관련 codec/client shared-core 단위 테스트

### Q1-1 Windows visible aperture와 coded size 분리

#### 검증 결과 (2026-07-31, 코드·로그 확인)

- 동일 실행에서 Host는 `size=1920x1080`을 보내지만 Windows Client의 decode 통계는 계속
  `size=1920x1088`이다. H.264/MFT의 16행 정렬 coded height가 그대로 노출된 증거다.
- `H264Decoder::query_output_size()`는 `MF_MT_FRAME_SIZE`만 읽고
  `MF_MT_MINIMUM_DISPLAY_APERTURE`/`MF_MT_GEOMETRIC_APERTURE`를 읽지 않는다.
- `DecodedFrameNv12`에는 width/height/bytes만 있고, `resolve_active_video_content_size()`는
  선택 대상 크기보다 decode frame 크기를 우선한다. 따라서 aspect-fit, 입력 좌표 변환,
  D3D/GDI 렌더가 1920×1088을 콘텐츠 크기로 취급해 약 0.74%의 종횡비 왜곡과 재샘플링을 만든다.
- Android decoder는 이미 MediaCodec의 `crop-left/right/top/bottom`을 읽어 visible size를
  분리하므로 이 문제는 Windows 경로에 국한된다.

#### 구현 방법

1. `DecodedFrameNv12`에 coded width/height와 visible rect를 구분해 보관한다.
2. MFT output type에서 minimum display aperture를 우선 읽고 geometric aperture, 유효한
   packet/header 화면 크기 순으로 fallback한다. 모든 rect는 coded plane 경계 안인지 검증한다.
3. NV12 plane offset/stride와 CPU 복사는 coded size를 유지하고, aspect-fit·입력 좌표·GDI fallback은
   visible rect만 사용한다.
4. C2 direct-surface 경로도 전체 coded texture가 아니라 같은 visible source rect를 샘플링한다.

완료 기준:

- Host 1920×1080 입력에서 Windows Client의 coded height는 1088일 수 있어도 visible size와
  aspect/input domain은 1920×1080
- 화면 하단 8행을 잘못 포함하거나 실제 UV plane을 잘라내는 회귀 없음
- 16:9 격자·원·1px 텍스트 screenshot에서 종횡비/선명도 회귀 없음

### Q1-2 runtime bitrate의 rate-control과 UDP pacing 동기화

#### 검증 결과 (2026-07-31, 코드 확인)

- encoder 초기화는 `PeakConstrainedVBR`과 기본 peak 300%(CBR은 110%), tune별 VBV를 적용한다.
  반면 `H264Encoder::reconfigure_bitrate()`는 환경 설정을 다시 쓰지 않고 max bitrate를
  `low_latency=110%`, `stable_text=130%`로 고정한다. 런타임 설정/ABR로 bitrate만 바뀌면
  같은 세션의 scene-change bit 여유가 갑자기 줄어 작은 글자가 다시 뭉개질 수 있다.
- `gUdpPacePeakBitrateBps`는 시작 시 `args.bitrate`로 한 번만 저장된다.
  `apply_encoder_target()`이 runtime config/ABR/M9 bitrate를 바꿔도 pacing budget은 갱신되지
  않아, 하향 시 과도한 burst, 상향 시 불필요한 전송 지연을 만들 수 있다.

#### 구현 방법

1. rate-control mode, mean/max bitrate, peak percent, VBV, MaxQP를 계산·적용·로그하는 공용
   helper를 만들고 초기화와 `reconfigure_bitrate()`가 같은 정책을 사용하게 한다.
2. `apply_encoder_target()`이 성공한 직후 active bitrate와 기존 `udpPacePeakPercent`로
   `gUdpPacePeakBitrateBps`도 다시 계산한다. no-pacing 모드는 기존처럼 실제 대기를 건너뛴다.
3. 요청값뿐 아니라 MFT가 받아들인 mode/mean/max/VBV와 현재 pacing bps를 변경 전후 로그에 남긴다.

완료 기준:

- 시작 8Mbps → runtime 4Mbps → 10Mbps와 ABR down/up 후에도 peak percent·VBV 정책이 동일
- 각 변경 후 `udpPacePeakBps`가 active bitrate와 일치
- 텍스트 scene change에서 변경 직후만 QP/화질이 급락하거나 queue-to-send가 튀지 않음

### Q1-3 제품 Host encoder tune 명시

`remote60_host_app`은 child 실행 시 encoded experiment만 환경에 넣고 encoder tune은 지정하지
않는다. 그래서 native Host 기본값 `low_latency`(`QualityVsSpeed=100`)를 쓰는 반면, Android LAN과
external 검증 profile은 `stable_text`(`QualityVsSpeed=68`)다. 제품과 검증 경로의 화질 결론이
현재 같지 않다.

- Host 앱이 이름 있는 제품 preset을 명시적으로 전달하고 시작 로그/상태에 노출한다.
- `low_latency`와 `stable_text`를 동일 1080p30 scroll/text 장면에서 A/B해 decoded fps,
  latency p95, QP/bitrate, 작은 글자 screenshot으로 선택한다.
- H3 전에는 성능 저하 가능성이 있으므로 측정 없이 `stable_text`를 제품 기본값으로 강제하지 않는다.
- 선택한 제품 preset과 B1/G1 검증 preset을 동일하게 고정한다.

## 7. H1 — callback copy-only + worker readback ring

### 목표

캡처 callback에서 `Map`, 전체 프레임 memcpy, CPU crop, heap allocation을 제거한다.

### 검증 결과 (2026-07-31, 코드 확인)

- `publish_captured_texture`가 callback 스레드에서 `CopyResource` 직후 flags 0(블로킹)
  `Map`과 전행 memcpy를 인라인 수행한다. 실측 CopyMap 평균 ~1.0ms, memcpy 평균 ~0.8ms.
- **DXGI 경로는 더 나쁘다**: frame handler가 `AcquireNextFrame`과 `ReleaseFrame` 사이에서
  호출되므로, duplication 프레임을 쥔 채 GPU 동기 readback을 기다린다. H1 구현 시
  `CopyResource`까지만 하고 프레임을 먼저 release하는 순서를 명시해야 한다.
- staging 텍스처 ring(3개 이상, busy CAS)은 이미 있으나 Map/memcpy가 동기라 의미가 없다.
  readback worker 스레드는 현재 존재하지 않는다(스레드는 제어용 3개뿐).
- `captureD3DWaitUs`는 GPU 대기가 아니라 `d3dContextMu` 뮤텍스 경합 시간이다. 지표 해석과
  이름을 정리한다.

### 추가 범위 — frame gating 재설계 (필수 선행)

현재 gating은 원본 해상도 CPU BGRA payload를 4KB 블록 memcmp로 비교하고
(`estimate_bgra_change_permille`), 이전 프레임 payload의 `shared_ptr` 참조를 프레임 사이에
계속 쥔다. H1은 이 비교 입력을 없애므로 gating을 함께 옮기지 않으면 정지 화면 절전이 조용히
죽는다.

- H1 단독 단계에서는 readback 이후 encode 크기 CPU 버퍼 비교를 임시 경로로 쓸 수 있다.
  다만 H3의 최종 surface 경로가 CPU 입력을 다시 없애므로, 최종 설계는 GPU downsample/hash
  또는 별도 소형 readback처럼 surface 경로에서도 유지되는 비교 입력을 사용한다. H1에서
  임시 CPU 전용 인터페이스를 공용 계약으로 굳히지 않는다.
- 버퍼 풀 재사용은 gating이 쥔 이전 프레임 참조와 충돌하지 않게 lease 수명을 설계한다.
- `gatingMotionPm`(`frameGatingMotionThresholdPermille`)은 시작 로그 외에는 읽히지 않는
  죽은 설정이다(실판정은 `changePermille > 0`). 삭제하거나 실제 판정에 연결한다.

### 수정 파일

- 신규 `apps/native_poc/src/d3d_capture_readback.hpp`
- 신규 `apps/native_poc/src/d3d_capture_readback.cpp`
- `apps/native_poc/src/native_video_host_main.cpp`
- `apps/native_poc/CMakeLists.txt`
- 신규 단위 테스트가 가능한 순수 ring state는 별도 test target에 포함

### 제안 경계

`D3dCaptureReadbackPipeline`이 다음을 소유한다.

- 3개 GPU slot
  - staging texture
  - completion query
  - `Free / GpuPending / Readback` 상태
  - frame 크기·timestamp·stream generation·crop metadata
- worker thread와 bounded pending queue
- 3개 재사용 CPU frame buffer
- drop/시간 통계

### callback 경로

1. stream inactive 또는 크기 불일치면 즉시 반환
2. `Free` slot 하나를 획득
3. `CopyResource(slot.texture, src)`
4. completion query `End`
5. metadata 기록 후 slot index를 worker에 통지
6. 반환

callback에서는 다음을 금지한다.

- `Map`/`Unmap`
- `std::vector` 전체 프레임 생성
- row memcpy
- crop
- GPU 완료 대기

### worker 경로

1. condition variable로 pending slot 대기
2. query 또는 `Map(D3D11_MAP_FLAG_DO_NOT_WAIT)`로 GPU 완료 확인
3. 아직 준비되지 않았으면 1ms 이내 재대기하고 다른 최신 slot을 우선 확인
4. 준비된 최신 frame만 reusable CPU buffer로 복사
5. 오래된 pending frame은 latency 우선 정책으로 폐기
6. 기존 encode queue에 frame metadata와 buffer lease 전달
7. 소비가 끝나면 buffer와 slot을 pool에 반환

`ID3D11DeviceContext` 접근은 기존 `d3dContextMu` 정책을 유지한다. callback과 worker가 같은 immediate
context를 동시에 호출하지 않게 한다.

### reconfigure/종료 처리

- capture 크기 또는 device generation 변경:
  - 새 submit 중단
  - worker pending drain/drop
  - query/texture 재생성
  - stream generation 증가 후 재개
- 종료:
  - callback detach
  - worker stop
  - slot release
- 이전 generation frame은 encode queue 진입 전에 폐기

### 추가 지표

- `captureSubmitUs`
- `captureGpuPendingUs`
- `captureWorkerMapUs`
- `captureWorkerMemcpyUs`
- `captureRingBusyDrop`
- `captureSupersededDrop`
- `captureCpuBufferReuse`
- callback 내 `MapCount`, `MemcpyBytes`는 항상 0이어야 함

### 완료 기준

- callback 코드에 `Map`, row memcpy, full-frame allocation 없음
- callback p95 CPU 시간이 기준선 대비 50% 이상 감소
- callback→encode p95가 악화되지 않음
- 10분 resize/window switch에서 device error·deadlock 없음
- 1080p30 scroll 5회 중 decoded fps 중앙값 개선

### 롤백 조건

- GPU busy drop 때문에 decoded fps가 기준선보다 5% 이상 감소
- capture generation 전환 후 이전 화면이 다시 나타남
- 10분 내 deadlock/device removal 발생

## 8. H2 — GPU-front crop/resize

### 목표

원본 해상도 전체를 CPU로 읽은 뒤 crop/resize하는 경로를 제거하고, encode 크기의 BGRA만 한 번
readback한다.

### 검증 결과 (2026-07-31, 코드 확인)

- 창 crop은 callback 스레드에서 전체 크기 버퍼를 새로 할당해 행 단위로 재복사한다(확인).
- `GpuBgraScaler::scale()`은 입력이 `const uint8_t*`뿐이고(`ID3D11Texture2D*` 오버로드 없음),
  `UpdateSubresource`로 업로드한 뒤 결과를 staging `Map`으로 다시 CPU에 읽는다. 즉 GPU
  스케일을 써도 프레임당 full-frame transfer leg가 GPU→CPU capture, CPU→GPU upload,
  GPU→CPU scaler output의 3개다. "GPU↔CPU 왕복 3회"라는 기존 표현은 과장이라 정정한다.
- 이름과 달리 구현은 pixel shader가 아니라 `ID3D11VideoProcessor`/`VideoProcessorBlt` 기반이다.
- H1의 gating 재설계와 입력/출력 버퍼 소유권을 공유하므로 H1과 인터페이스를 같이 설계한다.

### 수정 파일

- 신규 `apps/native_poc/src/d3d_frame_preprocessor.hpp/.cpp`
- `apps/native_poc/src/d3d_capture_readback.*`
- `apps/native_poc/src/native_video_host_main.cpp`
- `apps/native_poc/CMakeLists.txt`

### 구현 방법

1. 기존 `GpuBgraScaler`의 video processor 설정·resource 생성·색공간 정책을 재사용하되 입력을
   CPU byte가 아니라 `ID3D11Texture2D*`로 받게 한다.
2. `ProcessTexture(source, sourceRect, outputWidth, outputHeight)` 인터페이스를 제공한다.
3. window client crop rect는 짝수 좌표·짝수 크기로 정렬한다.
4. crop과 aspect-fit resize를 한 draw pass에서 수행한다.
5. output texture ring은 encode 크기별로 재사용한다.
6. H1 worker는 전처리 output을 staging에 복사해 encode 크기만 readback한다.
7. GPU 전처리 실패 시 기존 CPU crop/resize fallback을 사용하고 이유와 횟수를 기록한다.

### 화질 검증

- Desktop 전체 화면
- 창 client-only crop
- 홀수 위치/크기의 창
- 16:9, 4:3, 세로형 창
- 텍스트 100%/150% DPI
- 빠른 창 resize 중 aspect 왜곡·검은 가장자리·이전 frame 없음

### 완료 기준

- GPU 정상 장치에서 CPU crop·CPU resize 호출 0회
- readback bytes가 `encodeWidth × encodeHeight × 4` 이하
- 같은 encode 크기에서 기준선 대비 readback+scale 합계 감소
- pixel/aspect 회귀 screenshot 승인

## 9. H3 — GPU NV12 surface를 MF encoder에 직접 전달

### 목표

CPU `bgra_to_nv12`와 `MFCreateMemoryBuffer` memcpy를 제거한다.

### 검증 결과 (2026-07-31, 코드 확인)

- `bgra_to_nv12`는 SIMD 없는 스칼라 per-pixel 루프이고 출력 vector를 매 호출 재할당한다.
  감사 로그 재계산 결과 프레임당 평균 8.1ms, 최대 11.5ms — 1080p30 예산 33.3ms의 약 4분의 1로,
  단일 항목 중 기대 이득이 가장 크다.
- `create_input_sample`이 매 프레임 `MFCreateMemoryBuffer`를 새로 만들고 NV12 전체를 다시
  memcpy한다(확인).
- 전제 성립 확인: `IMFDXGIDeviceManager`가 초기화 시(`MFT_MESSAGE_SET_D3D_MANAGER`)와 device
  재생성 후 모두 하드웨어 MFT에 이미 연결돼 있고, 감사 실행도 hardware backend
  (`amf_mft_h264enc`, hw=1)였다. `MFCreateDXGISurfaceBuffer`는 코드 어디에도 없으며 texture를
  받는 encode 변형도 없다 — 계획대로 신규 추가가 맞다.

### 수정 파일

- 신규 `apps/native_poc/src/d3d_nv12_processor.hpp/.cpp`
- `apps/native_poc/src/mf_h264_codec.hpp/.cpp`
- `apps/native_poc/src/native_video_host_main.cpp`
- `apps/native_poc/CMakeLists.txt`

### 구현 방법

1. `ID3D11VideoDevice/ID3D11VideoContext` 기반 video processor 가능 여부를 탐지한다.
2. `DXGI_FORMAT_NV12` texture 3개를 encode 크기로 만든다.
3. BGRA input → NV12 output에 다음을 명시한다.
   - BT.709
   - limited range
   - progressive
   - source/destination rect
4. 장치가 지원하면 crop/resize/color conversion을 한 `VideoProcessorBlt`로 합칠 수 있다.
   첫 구현은 H2 output을 입력으로 사용하고, 결과가 검증된 뒤 pass 통합 여부를 A/B한다.
5. `H264Encoder`에 surface 입력 함수를 추가한다.
   - `encode_frame_surface(ID3D11Texture2D*, subresource, forceKeyFrame, timestamp, ...)`
6. `MFCreateDXGISurfaceBuffer`로 `IMFMediaBuffer`를 만들고 기존 sample timestamp/duration을 설정한다.
7. 기존 D3D manager가 설정된 hardware MFT에 sample을 전달한다.
8. MFT가 DXGI sample을 거부하면 해당 backend 세션에서는 CPU NV12 경로로 fallback한다.
9. fallback 이유를 backend명과 함께 한 번만 명확히 출력한다.

### 인터페이스 안전

- 기존 `encode_frame(vector<uint8_t>)`는 software encoder와 fallback을 위해 유지
- surface pool slot은 `ProcessInput` 반환 시 바로 재사용하지 않는다. async MFT가 sample/texture
  참조를 해제할 때까지 수명을 유지하고, tracked sample allocator 또는 명시적 COM 보유/완료
  신호로 반환 시점을 결정한다.
- encoder 재초기화와 device reset 시 NV12 texture generation도 함께 갱신
- output H.264 timestamp/keyframe/SPS 처리 코드는 공유

### 테스트

- CPU NV12와 GPU NV12의 색상 patch 오차 비교
- 검정/흰색/회색/빨강/초록/파랑과 작은 글자
- 720p/1080p, Desktop/window
- hardware backend별 surface accept/fallback
- encoder restart, bitrate 변경, source resize

### 완료 기준

- 지원 장치에서 `bgra_to_nv12` 호출 0회
- `sampleCreateUs`의 full-frame memcpy 제거
- GPU 경로와 CPU 기준의 색상 오차가 승인 범위 내
- 1080p30 scroll에서 decoded fps 중앙값 27 이상
- surface 미지원 장치에서 자동 CPU fallback 후 영상 정상

## 10. H4 — pacing/feedback 지연 분리

H1~H3 이후에도 latency p95 또는 decoded fps가 목표에 미달할 때만 착수한다.

### 검증 결과 (2026-07-31, 코드 확인)

- 인코드→패킷화→pacing 대기→`sendto`가 전부 main 스레드 인라인이다. pacing 대기는
  `send_udp_chunks_timed` 내부의 sleep+스핀 루프(`udp_pace_wait_until`, 2ms 미만은 yield 스핀)
  라서 한 프레임의 전송 예산이 다음 프레임 인코드 시작을 직접 늦춘다(확인).
- 인코드와 전송 사이에 큐는 없고, 영상 경로 전체의 유일한 생산자/소비자 핸드오프는 캡처
  callback→인코드 루프의 최신 프레임 1칸(`FrameState`, latest-wins)뿐이다. 감사의
  queue-to-send 평균 16.8ms는 이 구조와 부합한다.

### 수정 파일

- `apps/native_poc/src/native_video_host_main.cpp`
- 필요 시 전송 책임을 신규 `encoded_frame_sender.hpp/.cpp`로 추출
- `automation/verify_native_video_runtime.ps1`

### 구현 방법

- encode 완료→packetize→pacing wait→send syscall→feedback 처리 시간을 각각 기록
- Q1에서 runtime bitrate와 pacing budget의 즉시 동기화는 먼저 끝낸다. H4는 그 뒤에도 남는
  inline wait/send 구조를 sender queue로 분리하는 작업이다.
- encode thread에서 send가 장시간 block되면 bounded encoded-frame queue로 분리
- queue는 최신 frame 우선, keyframe은 보존
- queue depth 2를 기본으로 시작하고 수치 없이 늘리지 않음
- feedback는 송신 hot path에서 전체 pipe 작업을 동기 수행하지 않게 함

### 완료 기준

- queue-to-send p95가 1080p30 frame budget 33.3ms 이하
- 100ms 이상 spike 원인이 단계별 지표로 설명됨
- queue 분리 시 메모리 무한 증가와 오래된 frame 전송 없음

## 11. C1 — Windows Client 저위험 최적화

### 검증 결과 (2026-07-31, 코드 확인)

- `ensure_rtv()`는 매 프레임 무조건 `GetBuffer`+`CreateRenderTargetView`를 호출한다. 크기
  검사는 `ResizeBuffers`만 막고 RTV 재생성은 못 막으며, `GetClientRect`+`GetDesc`도 매
  프레임 실행된다(확인).
- `draw_target_card`가 `gThumbMu`를 쥔 채 HALFTONE `StretchDIBits`를 그리고, 썸네일 수신
  스레드는 같은 락을 쥔 채 `InvalidateRect`까지 호출한다(확인). C1-2에 "락 안
  `InvalidateRect` 제거"를 포함한다.
- 브러시는 카드당 3~4개, 버튼당 1개, 오버레이 알파 사각형은 DC/비트맵/브러시 3개를 매
  페인트 생성/파괴하고, 제목 폰트도 매 페인트 재생성한다(확인). 본문 폰트 `gUiFont`만 DPI
  기준으로 캐시돼 있다.
- swapchain은 레거시 `DXGI_SWAP_EFFECT_DISCARD` + 레거시 `CreateSwapChain`이며 waitable
  frame-latency object는 없다. flip-discard 전환은 감사의 후보 그대로 C1 이후 A/B 항목으로
  유지한다.

### C1-1 RTV 캐시

수정: `apps/native_poc/src/native_video_client_main.cpp`

- `ensure_rtv()`에서 크기가 같고 `rtv`가 있으면 즉시 반환
- `ResizeBuffers`, device reset, shutdown 때만 RTV reset/recreate
- `rtvCreateCount`, `resizeCount`를 진단 로그에 추가

완료 기준:

- 정상 렌더 중 프레임당 RTV 생성 0회
- resize 100회 후 leak/device error 없음

### C1-2 thumbnail lock 축소

- `gThumbMu` 안에서는 대상 thumbnail의 immutable snapshot 또는 shared buffer만 가져온다.
- `StretchDIBits`는 lock 밖에서 수행한다.
- fetch thread가 map entry를 교체하고 paint가 snapshot을 참조하는 동안 lifetime을 보장한다.

완료 기준:

- `gThumbMu` 구간에 GDI 호출 없음
- thumbnail 수신 중 picker scroll/paint stall 없음

### C1-3 GDI object 캐시

- card별 `CreateSolidBrush/DeleteObject` 반복을 palette 단위 cache로 교체
- font는 DPI 변경 때만 재생성
- `WM_DESTROY`에서 GDI object 정리

완료 기준:

- picker를 10분 열고 갱신해 GDI handle 증가 없음

## 12. C2 — Windows decoder surface 렌더 spike

H1~H3과 C1 후에도 Windows Client CPU가 단일 코어 환산 15% 이상이거나 1080p30 목표를 막을 때만
착수한다.

### 검증 결과 (2026-07-31, 코드 확인)

- 현재 디코더 출력은 CPU bytes(`DecodedFrameNv12.bytes`)이고, 매 프레임 Y/UV dynamic texture
  2장에 `Map(WRITE_DISCARD)`+행별 memcpy로 업로드된다(확인). 디코더에 DXGI 디바이스 매니저는
  설정돼 있어 내부 DXVA는 쓰지만 출력 샘플은 즉시 시스템 메모리로 복사된다.
- **감사가 놓친 선행 조건**: 디코더의 D3D 디바이스와 렌더러의 디바이스가 서로 다른
  `ID3D11Device`다. surface 직접 렌더는 두 디바이스를 통합하거나 shared handle로 텍스처를
  넘기는 작업이 먼저다. spike 견적에 이 비용을 포함한다.
- Q1에서 확인한 coded 1088행/visible 1080행 분리를 surface 경로에서도 유지해야 한다.
  그렇지 않으면 CPU 복사를 없애도 종횡비 왜곡과 불필요한 보간은 남는다.

### spike 내용

- 디코더와 렌더러의 D3D 디바이스 통합(또는 shared handle) 설계
- `H264Decoder`가 `DecodedFrameNv12.bytes` 대신 DXGI surface output을 선택할 수 있는지 확인
- `IMFDXGIBuffer`에서 texture/subresource 획득
- renderer가 NV12 plane SRV 또는 video processor로 swapchain에 직접 출력
- GDI fallback을 위해 필요할 때만 CPU readback

### 채택 기준

- 1080p30 Client CPU가 C1 기준보다 20% 이상 감소
- present latency/jank 악화 없음
- 지원하지 않는 decoder에서는 현재 CPU NV12 경로 유지

효과가 기준 미만이면 spike code는 제품 경로에 합치지 않는다.

## 13. A1 — Android dirty snapshot과 adaptive poll

### 검증 결과 (2026-07-31, 코드 확인)

- 250ms 폴은 씬과 무관하게 돈다. 직접 호출은 기본 6회이고 stable VIEWER에서는
  data usage·중복 presentation timestamp·video size 조회가 더해져 9회다. pending config,
  thumbnail 변화 등 상태에 따라 더 늘 수 있다(상태/오류 각각 스냅샷 전체 딥카피,
  디코더 뮤텍스 아래 ~28필드 디버그 문자열 생성, panel JSON 생성→`NewStringUTF`→Kotlin
  `org.json` 재파싱·객체 재할당 포함). 기존의 모든 scene "8~9회" 표현을 정정한다.
- 씬 visibility 6개와 targets 위젯 ~18개 속성은 변경 여부와 무관하게 매 틱 재설정된다. 확인.
- **감사 정정**: 대상 목록 어댑터는 이미 변경 게이트가 있다(라벨/id/selectedId 비교 후에만
  `notifyDataSetChanged`). 다만 비교용 리스트는 매 틱 새로 할당된다.
- native에 세션/panel 수준 version 카운터는 없다. 재사용 가능한 것: per-thumbnail
  `WindowThumbnailVersion`, atomic `sessionBytesReceived_`, 디코더의
  `ReadySelectionGeneration`/`LastOutputPresentationUs`.
- 구현 순서 권장: snapshot JSON보다 **초경량 version getter를 먼저** 추가하고(단일 atomic
  read), version이 변했을 때만 snapshot JSON을 가져온다. JSON 직렬화 자체도 틱 비용이기
  때문이다.

### 수정 파일

- `apps/native_poc/src/native_video_client_session.hpp/.cpp`
- `apps/android_direct_client/app/src/main/cpp/native_bridge.cpp`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/NativeSessionBridge.kt`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt`

### 구현 방법

1. native에 UI용 단일 snapshot을 추가한다.
   - monotonic `version`
   - status/error
   - window panel + panel version
   - video debug + video version
   - selection generation/ready generation
   - last output presentation timestamp
2. 상태가 실제로 바뀔 때 version을 증가시킨다.
3. JNI 여러 getter 대신 `nativeGetUiSnapshotJson()` 한 번으로 일관된 snapshot을 가져온다.
4. Kotlin은 마지막 version과 같으면 scene 전체를 다시 그리지 않는다.
5. poll 간격을 scene별로 조정한다.
   - LOGIN/HOSTS: 1000ms
   - TARGETS stable: 750~1000ms
   - CONNECTING/SWITCHING/stall recovery: 250ms
   - VIEWER stable: 500ms
6. selection timeout과 stall 판단은 시간이 지나야 하므로 version이 같아도 timer 판단은 실행한다.
7. macro playback 16ms runnable은 status poll과 분리된 현재 구조를 유지한다.

### 완료 기준

- JNI 상태 조회가 poll당 1회
- 동일 version에서 adapter notify, scene visibility 재적용, Surface rebind 없음
- 연결/선택 timeout과 stall recovery 동작 무회귀
- 로그인·목록·viewer 각각 10분 실행 시 CPU/jank가 기준선 이하

## 14. A2 — Android thumbnail 수명과 버퍼 재사용 (범위 축소, P2)

### 현재 원인 (2026-07-31 검증으로 정정)

감사의 "다음 list roundtrip에서도 세션 cache가 유지된다"는 진단은 **틀렸다**.
`RequestWindowList()`가 요청 시마다 `thumbs_`와 fetch queue를 비우므로 수동 새로 고침
리프레시는 이미 동작하고, Kotlin 쪽도 같은 native cache version 사이에서는 JNI 호출을 생략한다.

다만 2차 검증에서 `thumbVersion`의 주석/의미도 정정했다.

- Host는 성공한 fetch마다 `rsp.version = qpc_now_us()`를 넣는다. 콘텐츠가 바뀔 때만 증가하는
  version이 아니라 **캡처 시각**이다.
- `ControlWindowThumbnailRequestMessage`에는 이전 version/hash가 없고 list entry에도 version이
  없다. response의 "unchanged" flag 주석은 현재 protocol로 구현할 수 없으며 Host도 쓰지 않는다.
- 따라서 TTL fetch를 단순 추가하면 화면이 같아도 payload 전체 전송, BGRA→RGBA, version 변경,
  JNI/Bitmap 재생성이 매번 발생한다.

실제 남는 문제만 다룬다:

- TARGETS 화면을 계속 열어둔 동안에는 같은 list 세대 안에서 갱신이 없다(TTL 부재).
- 썸네일이 갱신될 때 native vector → `NewByteArray` → direct ByteBuffer → Bitmap의 3중
  복사와 신규 할당이 일어나고 교체된 Bitmap을 recycle하지 않는다.
- BGRA→RGBA 스위즐이 control 스레드의 스칼라 per-pixel 루프다.

우선순위는 P2로 낮춘다.

### 수정 파일

- `apps/native_poc/src/native_video_client_session.hpp/.cpp`
- `apps/native_poc/src/native_video_client_shared_core_test.cpp`
- `apps/android_direct_client/app/src/main/java/com/remote60/androiddirect/MainActivity.kt`

### 구현 방법

- native thumbnail cache에 `fetchedUs`, wire BGRA의 64-bit `contentHash`, client-local monotonic
  `contentVersion`을 저장한다. 기존 Host timestamp는 진단용 `captureVersion`으로만 분리한다.
- 기본 TTL 10초
- TARGETS 진입/refresh 때 다음 조건이면 queue:
  - cache 없음
  - TTL 만료
- TTL fetch payload는 BGRA→RGBA 전에 hash를 계산한다. 크기와 `contentHash`가 같으면
  `fetchedUs`만 갱신하고 swizzle을 건너뛰며 `contentVersion`/RGBA buffer를 유지한다.
  실제 변경 때만 buffer 교체와 version 증가를 한다.
- Host capture/network까지 줄이려면 이전 content hash/version을 요청에 싣고 unchanged 응답을
  주는 별도 negotiated capability를 추가한다. 구형 peer와 메시지 크기를 깨는 무협상 확장은
  A2 기본 범위에 넣지 않는다.
- queue 중복 방지 유지
- 현재 보이는 card를 먼저 요청하고 한 번에 1개씩 가져옴
- viewer에서는 주기 refresh 중단
- Kotlin은 같은 width/height의 Bitmap과 direct ByteBuffer를 재사용
- 사라진 target의 Bitmap은 즉시 recycle 대상으로 제거

### 완료 기준

- 같은 세션에서 대상 화면 변경 후 10초 안에 preview 갱신
- 동일 콘텐츠의 TTL fetch에서는 JNI byte array/Bitmap 재생성 없음
- 30분 목록 반복에서 native/Java heap 지속 증가 없음

## 15. U2 — Windows/Android UI 정보 구조 정리

### Windows Client

수정: `apps/native_poc/src/native_video_client_main.cpp`, 앱 resource

- 창 제목/아이콘을 `GNLink`로 통일
- picker header에 연결된 PC 이름, 연결 상태, Disconnect, Settings 표시
- footer의 작은 상태 문구를 상태 badge + 입력 허용 문구로 분리
- viewer에 auto-hide toolbar:
  - Targets
  - Keyboard/Input 상태
  - 품질 preset
  - Disconnect
- reconnect/loading/error overlay를 같은 위치와 상태 용어로 통일
- 개발 통계는 기본 숨김, `Diagnostics`에서만 표시

### Android 로그인

수정:

- `apps/android_direct_client/app/src/main/res/layout/activity_main.xml`
- `apps/android_direct_client/app/src/main/res/values/strings.xml`
- `apps/android_direct_client/app/src/main/res/values-ko/strings.xml`
- 관련 drawable/color

변경:

- GNLink 제목과 짧은 설명 추가
- 계정/비밀번호를 첫 정보 계층으로 배치
- Directory URL은 접힌 `고급 설정`으로 이동
- 직접 IP 연결은 full-width primary button에서 text/secondary action으로 변경
- 비밀번호 표시 toggle과 연결 도움말 추가

### Android targets/settings

- `[Windows]` 문자열을 제거하고 selected-state drawable로 표현
- 일반 설정은 `Mobile / Balanced / Sharp` preset
- DXGI/WGC와 raw bitrate/fps는 `고급 설정`에 배치
- 적용 결과는 성공/실패/현재값으로 구분

### Android viewer (2026-07-31 현행 기준)

아래는 이미 구현된 현행 구조이며, 계획 초안의 rail/More 재편안은 사용자 결정으로 폐기됐다.

- 좌측 존 바: 우클릭(홀드) / 태블릿(홀드, 🔒 잠금 버튼으로 고정 가능) / 마우스(탭)
  - 세로 모드에서는 상단 가로 띠로 재배치된다
- 태블릿 모드 스크롤은 자연 방향(내용이 손가락을 따라옴)
- 화면 마우스: 화살표만 포인터를 따라가고, 버튼 클러스터는 고정·드래그 배치
- rail: 목록 / 키보드 / 매크로 / KEYS / ROTATE / MENU
  - 진단 로그는 MENU(빠른 설정) 안으로 이동, SCROLL·MOUSE·LOG 버튼은 제거됨
- 남은 개선 항목:
  - 최소 48dp touch target과 TalkBack content description 점검
  - 품질 preset의 MENU 노출 정리 (현재 MENU에 preset 3종 존재)

### UI 완료 기준

- 360dp 높이 landscape에서 primary control이 모두 화면 안에 존재
- 일반 사용 흐름에 server URL, DXGI/WGC, raw bitrate가 기본 노출되지 않음
- Host/Windows/Android가 GNLink와 동일 상태 용어 사용
- 한국어/영어, font scale 1.0/1.3, light/dark 또는 현재 지원 theme에서 잘림 없음

## 16. S1 — Directory HTTPS

### 수정 파일

- `apps/native_poc/src/directory_client.hpp/.cpp`
- `apps/android_direct_client/app/src/main/res/xml/network_security_config.xml`
- Android Directory HTTP 호출부
- `apps/directory/` 배포 설정
- `automation/deploy_directory.ps1`

### 구현 방법

1. Directory 서버는 reverse proxy 또는 load balancer에서 TLS를 종료한다.
2. 유효한 hostname과 인증서를 사용하고 HTTP→HTTPS redirect를 설정한다.
3. Windows의 직접 socket HTTP 구현을 WinHTTP 기반 HTTPS client로 교체한다.
4. `parse_directory_url()`이 HTTPS를 정상 처리하고, 운영 모드에서 HTTP를 거부한다.
5. Android cleartext 허용 설정을 제거한다.
6. 개발 LAN HTTP가 꼭 필요하면 debug build/명시적 localhost에만 제한한다.
7. password, host token, session token이 URL query나 로그에 들어가지 않는지 테스트한다.

### 완료 기준

- Host/Android 로그인·등록·목록·connect가 HTTPS에서 PASS
- 운영 build가 `http://` 외부 주소를 거부
- 인증서 오류·hostname 불일치에서 연결 실패
- redirect 과정에 Authorization/token 유출 없음

## 17. S2 — 미디어·제어 인증 암호화

직접 암호 알고리즘을 작성하지 않는다. 구현 전 짧은 protocol decision record를 만들고, 검증된
라이브러리와 packet format을 확정한다.

### 권장 구조

- X25519 기반 session key 합의
- HKDF로 media/control 방향별 key 분리
- AEAD는 검증된 라이브러리의 ChaCha20-Poly1305 또는 AES-GCM
- packet sequence를 nonce와 replay protection에 사용
- 암호화되지 않는 header는 associated data로 인증
- video와 UDP control 모두 같은 session identity를 사용하되 key는 분리
- key와 원문 token은 로그에 출력하지 않음

### protocol 변경

- capability negotiation에 encrypted media/control 지원 bit 추가
- handshake에 ephemeral public key, session id, key confirmation 추가
- packet에 key epoch와 sequence 추가
- 64~128 packet 범위 replay window 유지
- 재연결·sequence wrap 전에 key rotation
- 구버전 peer는 개발 LAN에서만 명시적 fallback, 운영에서는 연결 거부

### 테스트

- 정상 암복호화
- bit tamper, 잘못된 key, replay, 순서 변경, 중복, 손실
- 400KB UDP control fragmentation 5% 손실
- key rotation과 reconnect
- 암호화 on/off CPU와 latency A/B
- packet capture에서 계정/token/화면 원문이 보이지 않음

### 완료 기준

- 외부 peer 연결에서 모든 video/control payload가 인증 암호화됨
- tamper/replay packet이 적용되지 않고 카운터만 증가
- 1080p30 latency p95 목표 유지

## 18. G1 — 최종 검증 Gate

### Build/Test

```powershell
cmake --build build-local --config Debug --clean-first --target `
  remote60_host_app `
  remote60_native_video_host_poc `
  remote60_native_video_client_poc `
  remote60_native_video_client_shared_core_test `
  remote60_udp_control_channel_test `
  remote60_udp_control_e2e_test `
  remote60_input_macro_test --parallel 2
```

- `remote60_native_video_client_shared_core_test.exe`
- `remote60_udp_control_channel_test.exe`
- `remote60_udp_control_e2e_test.exe`
- `remote60_input_macro_test.exe`
- `node apps/directory/test/run.js`
- Android `:app:assembleDebug`

### Performance

- Release, 단독 Host/Client, 격리 포트
- B1 기준선과 동일 장면
- 각 조건 5회

| Gate | 1080p30 | 720p30 |
|---|---:|---:|
| decoded fps | 평균 27 이상 | 평균 28 이상 |
| latency p95 | 70ms 이하 | 55ms 이하 |
| frame gap | 1초 초과 0회 | 1초 초과 0회 |
| fallback | 의도하지 않은 fallback 0회 | 의도하지 않은 fallback 0회 |

### Quality/config correctness

- Host visible 1920×1080 → Windows coded 1920×1088 허용, visible/aspect/input 1920×1080 고정
- runtime bitrate·ABR 전환 전후 rate-control peak/VBV 정책과 UDP pacing target 일치
- 제품 Host와 B1/G1 실행의 encoder tune preset 일치 및 로그 확인
- 100%/150% DPI 작은 글자, 원형/격자 screenshot에서 종횡비·색공간·선명도 회귀 없음

### Soak/WAN

- localhost reconnect 30회
- window/Desktop 전환 100회
- 2시간 Host/Windows soak
- 2시간 Host/Android soak
- 외부 2PC WAN 1080p30/720p30
- packet loss/reorder 환경에서 UDP control과 keyframe recovery
- device reset, 화면 잠금/해제, 해상도 변경

### UI

- Host 100/150/200% DPI
- Windows mouse/touch
- Android phone/tablet landscape
- 영어/한국어, 큰 font
- 로그인→PC 목록→target→viewer→목록→disconnect 전체 흐름

## 19. 작업별 완료 보고 형식

각 작업 ID 종료 시 다음을 기록한다.

```text
작업 ID:
변경 파일:
변경 전 문제:
구현 내용:
실행한 build/test:
Before/After 지표:
fallback/부작용:
미완료:
다음 작업:
```

성능 수치가 없는 최적화 작업은 완료로 체크하지 않는다. 코드 변경 후 이전 측정값을 재사용하지 않는다.
