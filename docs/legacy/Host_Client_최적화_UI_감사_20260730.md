# Host / Client 최적화·UI 감사 보고서

- 작성일: 2026-07-30
- 대상: Host 관리 앱, 영상 Host, Windows Client, Android GNLink Client
- 목적: 현재 병목과 UI 문제를 Host/Client별로 분리하고, 다음 최적화 순서를 결정한다.

> **현행화 (2026-07-31)** — 이 문서는 감사 시점의 기록이며 측정치는 그대로 둔다. 이후 해결된
> 항목만 여기 표시한다.
>
> - 5장 P0 "이미 선택된 대상으로 진입할 때 검은 화면": **해소됨**(`bf19eee`). Host가 새 제어
>   세션 시작 시 스트림을 기본 활성으로 복원하고, UDP 리더 스레드가 길이 0 데이터그램 등으로
>   조용히 죽던 문제(영상 피어를 새 클라이언트로 못 넘기던 또 하나의 원인)도 함께 수정됐다.
>   클라이언트 쪽 stream-state 호출은 절전용 선택 과제로만 남는다.
> - 6장 UI 판정의 viewer rail 재편 제안은 사용자 결정으로 다른 형태로 구현됐다: 좌측 존 바
>   (우클릭/태블릿+잠금/마우스), rail 축소(목록/키보드/매크로/KEYS/ROTATE/MENU), 로그는 MENU로.
> - 매크로 엔진 테스트는 일시정지/편집/직렬화 추가로 23개 → 46개가 됐다.
> - 세로 회전 시 뷰어 레이아웃이 무너지던 문제(`2f47497`)와 태블릿 스크롤 방향, 화면 마우스
>   클러스터 이동 문제도 이후 수정됐다.

## 1. 결론

현재 제품은 기능 골격과 전송 안정성은 갖췄지만, 1080p30 성능 목표를 안정적으로 만족하는 단계는 아니다.
가장 큰 병목은 Host 영상 준비 경로의 반복적인 GPU↔CPU 복사와 프레임별 메모리 할당이다.
Windows Client에는 이미 선택된 Desktop 카드로 진입할 때 스트림 활성화 요청을 보내지 않아 검은 화면이
될 수 있는 기능 결함도 있다.

UI는 Android 로그인 화면과 Windows 대상 선택 카드가 기본적으로 읽기 쉽지만, 제품 전체의 GNLink
브랜드와 상태 표현이 일관되지 않는다. Host 로그인 후 화면은 입력 필드만 숨기고 라벨을 남겨 두며,
핵심 상태 문구가 잘리는 문제가 있어 우선 수정 대상이다.

우선순위는 다음과 같다.

1. Windows Client 검은 화면과 Host 상태 UI처럼 기능 사용을 막는 결함 수정
2. M1.6 Host 저복사 파이프라인 구현 및 동일 조건 A/B 측정
3. Host 전송 큐 지연과 Windows 렌더 경로 최적화
4. Android 상태 갱신·썸네일 수명 및 모바일 UI 밀도 개선
5. 외부 배포 전에 인증·미디어 전송 암호화 완료

## 2. 감사 범위와 방법

### 확인한 실행 경로

- Host 관리 앱: `apps/native_poc/src/host_app_main.cpp`
- 영상 Host: `apps/native_poc/src/native_video_host_main.cpp`
- H.264 인코더: `apps/native_poc/src/mf_h264_codec.cpp`
- Windows Client: `apps/native_poc/src/native_video_client_main.cpp`
- Android Client: `apps/android_direct_client/`
- 공용 제어/세션: Windows·Android가 사용하는 shared core 및 UDP control

### 확인 방법

- Host, Windows Client, Android Client 정적 코드 감사
- Windows Host/Client 실제 실행 및 UI 확인
- Android 에뮬레이터 실행, UI·메모리·프레임 통계 확인
- 격리 포트에서 1080p30 H.264 localhost 진단
- CMake Debug 빌드, shared core/UDP control/input macro/Directory API 테스트
- Android Debug APK 전체 빌드

실행 화면에는 계정, 서버, 바탕화면 정보가 포함될 수 있어 캡처 이미지는 저장소에 커밋하지 않았다.

## 3. 성능 측정 결과

### 1080p30 localhost 진단

| 항목 | 결과 | 판정 |
|---|---:|---|
| Client 평균 디코드 | 19.42fps | 목표 27fps 미달 |
| Client 1초 구간 | 최소 14 / 최대 23fps | 변동 폭 큼 |
| Host CPU | 단일 코어 환산 약 49.65% | 최적화 필요 |
| Windows Client CPU | 단일 코어 환산 약 23.73% | 후속 최적화 대상 |
| Host working set | 약 130.8MB | 스트리밍 중 참고 기준 |
| Client working set | 약 83.3MB | 스트리밍 중 참고 기준 |
| Host BGRA→NV12 | 평균 8.38ms, 최대 11.52ms | 프레임 예산의 큰 비중 |
| Host queue-to-send | 평균 16.81ms, 최대 24.84ms | 추가 분석 필요 |
| 사용자 피드백 pipe 경고 | 약 100~106ms 3회 | 간헐 지연 존재 |

동시에 기본 Host가 별도로 실행 중이어서 이 수치는 최종 벤치마크가 아니라 병목 진단값이다.
다만 기존 2026-07-28 검증 로그도 대체로 14~18fps와 높은 NV12 시간을 보여, 목표 미달 방향은
일관된다. 최종 성능 판정은 단독 실행, 동일 장면, 동일 빌드에서 5회 반복해야 한다.

### Idle 상태

- Host 관리 앱: 5초 표본에서 CPU 0%, working set 약 17MB
- 대기 중 영상 Host: 5초 표본에서 CPU 0%, working set 약 11.7MB
- Android 로그인 화면: CPU 0%, total PSS 약 57.6MB
- Android 로그인 화면 렌더: 초기화 후 20프레임, jank 0%, p95 5ms

따라서 Host 관리 앱 idle 루프와 Android 로그인 화면은 현재 최적화 우선순위가 아니다.

## 4. Host 감사

### P1: 영상 준비 경로의 다중 복사

현재 한 프레임은 대략 다음 경로를 지난다.

`GPU capture → CPU BGRA → CPU crop → GPU scale → CPU BGRA → CPU NV12 → MF buffer copy → encoder`

주요 문제는 다음과 같다.

- 캡처 콜백에서 `CopyResource` 직후 동기 `Map`과 전체 프레임 memcpy를 수행한다.
- crop이 필요하면 별도 전체 버퍼를 할당하고 다시 복사한다.
- GPU scaler를 사용해도 CPU BGRA를 업로드한 뒤 결과를 다시 CPU로 읽는다.
- BGRA→NV12가 CPU에서 매 프레임 수행된다.
- 프레임별 `vector`와 access unit 할당이 반복된다.
- Media Foundation 입력 샘플에 NV12를 다시 복사한다.

이는 `docs/구현계획.md`의 미완료 M1.6과 정확히 같은 병목이다. 현재 최적화가 불가능한 것이 아니라,
완료되지 않은 저복사 파이프라인이 가장 큰 남은 개선점이다.

권장 구현 순서:

1. 캡처 콜백은 `CopyResource`만 수행하고 readback ring을 worker가 소비
2. crop과 resize를 GPU에서 먼저 수행해 readback 바이트 수 축소
3. 재사용 가능한 frame/NV12/access-unit ring으로 프레임별 할당 제거
4. D3D11 video processor 또는 동등 경로로 NV12 texture 생성
5. `MFCreateDXGISurfaceBuffer` 기반으로 하드웨어 인코더에 surface 직접 전달

각 단계는 한 번에 하나씩 적용하고 같은 장면에서 A/B 로그를 남겨야 한다.

### P1: 전송 큐 지연

일반 구간의 queue-to-send보다 사용자 피드백 경고 구간에서 약 83~92ms까지 커지는 표본이 있었다.
다만 영상 준비 단계가 프레임 공급 자체를 흔들고 있으므로, M1.6-1 적용 후 다음 항목을 분리 측정한다.

- capture callback 대기
- worker readback 대기
- encode submit/result 대기
- packet pacing 대기
- feedback 처리 대기

### P0: Host 관리 UI

실행 화면과 코드를 대조했을 때 다음 문제가 확인됐다.

- 로그인 성공 후 edit control만 숨기고 `Server`, `ID`, `Password`, PC 이름 라벨은 남는다.
- 3줄 상태 문자열에 비해 상태 control 높이가 작아 핵심 reachable 상태가 잘린다.
- 로그인 후 큰 빈 공간이 남아 현재 상태를 빠르게 파악하기 어렵다.
- 고정 pixel 배치라 per-monitor DPI 선언에 비해 실제 control 재배치가 부족하다.
- 창 제목과 아이콘이 `remote60` 기본값이라 Android의 GNLink와 불일치한다.

권장 UI:

- 로그인 후 작은 상태 카드로 전환하거나 창 높이를 자동 축소
- 계정, PC 이름, 연결 가능 여부, 자동 시작, 로그 열기를 한 화면에 표시
- 모든 로그인 라벨과 입력 필드를 함께 숨기거나 scene 자체를 교체
- 상태 control을 내용 기준으로 자동 높이 조절
- GNLink 제목·아이콘·용어 통일
- DPI별 margin, font, control 크기를 한 레이아웃 단위로 계산

## 5. Windows Client 감사

### P0: 이미 선택된 대상으로 진입할 때 검은 화면

Windows Client에는 공용 `StreamStateControl`이 연결되어 있지만 실제 `Request(active)` 호출이 없다.
대상 목록을 보는 동안 Host 스트림은 정지하며, 이미 선택된 Desktop 카드를 다시 누르면 picker만
숨기고 스트림 활성화 요청을 보내지 않는다. Android Client는 같은 상황에서 활성화 요청을 보낸다.

권장 수정:

- viewer 진입 전에 `stream-state active=true` 요청
- 목록 복귀 전에 `active=false` 요청
- 대상 선택 요청보다 stream-state 요청을 먼저 보내도록 공용 순서 보장
- “이미 선택된 대상”과 “새 대상” 양쪽을 shared-core test로 고정
- 실제 Host 연결에서 `목록 → 기존 대상 → 영상`, `viewer → 목록 → 다른 대상 → 영상` 검증

### P1: 프레임마다 RenderTargetView 재생성

렌더 함수가 매 프레임 swapchain back buffer를 가져와 RTV를 새로 만든다. RTV는 resize나 device
재생성 때만 갱신하고 평상시에는 캐시해야 한다.

그 다음 단계로 고려할 항목:

- decoder CPU NV12 → dynamic texture 두 장 복사 경로의 D3D surface 직접 전달
- thumbnail mutex를 잡은 채 GDI `StretchDIBits`를 수행하지 않도록 snapshot 분리
- paint마다 생성하는 font/brush 캐시
- 필요하면 flip-discard와 frame-latency waitable object A/B

현재 present 자체는 큰 병목으로 확인되지 않았으므로 RTV 캐시 후 다시 측정한다.

### UI 판정

카드형 대상 선택 화면은 현재 UI 중 가장 유지할 가치가 높다. 미리보기, Desktop 고정 카드, 넓은
hit target이 좋다. 다만 다음 정보 구조가 부족하다.

- GNLink 브랜드·아이콘 통일
- 연결 해제, 계정, 설정 진입점
- 연결/입력 허용 상태를 눈에 띄게 표시
- 로딩·재연결·오류 overlay
- viewer의 작은 auto-hide toolbar와 Targets 복귀 버튼
- 키보드 단축키와 입력 모드 도움말

## 6. Android Client 감사

### 유지할 성능 구조

MediaCodec 출력 buffer를 Surface로 release하는 현재 경로는 모바일 Client에서 적절한 zero-copy
방식이다. 이를 CPU 디코드나 Bitmap 렌더 방식으로 바꾸면 안 된다.

### P1: 250ms 전체 상태 polling

`renderStatus()`가 250ms마다 여러 JNI 문자열과 JSON을 가져오고, scene·adapter·Surface 상태를
반복 갱신한다. 로그인 idle에서는 문제가 없었지만 active session과 저사양 단말에서 불필요한 UI/JNI
비용이 될 수 있다.

권장 수정:

- native 상태 version 또는 event callback으로 dirty 상태만 전달
- 최소 변경안은 로그인/목록 1초, 전환·stall 구간만 250ms
- 값이 바뀐 view와 adapter만 갱신
- 썸네일 ByteArray/ByteBuffer/Bitmap을 같은 크기에서 재사용

### P1: 썸네일이 세션 동안 갱신되지 않음

공용 session은 같은 target ID의 썸네일이 이미 있으면 다시 fetch하지 않는다. 주석의 “다음 목록
왕복에서 refresh”와 실제 구현이 다르며, 장시간 접속 시 미리보기가 낡는다.

TTL, host thumbnail version 또는 목록 재진입 시 제한된 refresh 중 하나를 적용해야 한다.

### UI 판정

로그인 화면은 간격과 가독성이 양호하다. 개선할 항목은 다음과 같다.

- 일반 사용자는 계정/비밀번호가 먼저 보이고 directory server URL은 `고급 설정`으로 이동
- `IP로 직접 연결`을 로그인과 같은 주 버튼이 아닌 보조 링크로 변경
- GNLink 브랜드, 비밀번호 표시, 도움말/계정 생성 안내 추가
- 대상 탭의 `[Windows]` 문자열 대신 실제 segmented selected state 사용
- DXGI/WGC, raw bitrate/FPS는 고급 설정으로 이동하고 일반 화면은 품질 preset 중심으로 구성
- landscape viewer rail은 `뒤로`, `키보드`, `마우스`, `더보기`만 남기고
  Scroll/Keys/Macro/Log/Rotate/품질은 bottom sheet로 이동

## 7. 보안·제품화 차단 항목

현재 directory endpoint가 평문 HTTP이고 미디어·제어 전송도 종단간 암호화되지 않았다. 인터넷
제품으로 배포할 때는 성능과 별개로 P0 차단 항목이다.

- directory API를 유효한 인증서의 HTTPS로 전환
- 클라이언트의 인증서 검증 정책 확정
- punch token에서 세션 키를 파생하거나 별도 키 교환 도입
- 영상·제어 packet에 인증된 암호화와 replay counter 적용
- 로그와 오류 메시지에서 token/계정/주소 노출 재점검

## 8. 우선순위 목록

| 우선순위 | 항목 | 완료 기준 |
|---|---|---|
| P0 | Windows stream-state 누락 | 기존/신규 대상 모두 viewer 영상 출력 |
| P0 | Host 로그인 후 상태 UI | 잔여 라벨·잘림 없음, reachable 상태 즉시 확인 |
| P0 | 외부 배포 전 HTTPS·미디어 암호화 | 평문 자격증명·세션·영상 전송 없음 |
| P1 | M1.6 callback copy-only + worker ring | callback Map/memcpy 제거, 동일 장면 A/B 개선 |
| P1 | GPU crop/resize/NV12 | CPU full-frame 왕복과 NV12 변환 시간 대폭 감소 |
| P1 | Host pacing/feedback 지연 | queue spike 원인 분리, p95 목표 통과 |
| P1 | Windows RTV 캐시 | 평상시 프레임당 RTV 생성 0회 |
| P1 | Android dirty-state 갱신 | idle/active 불필요 JNI·adapter 갱신 감소 |
| P1 | Android 썸네일 refresh | TTL 또는 목록 재진입 시 최신 미리보기 |
| P2 | GNLink UI/브랜드 통일 | Host/Windows/Android 동일 용어·아이콘·상태 체계 |
| P2 | 대형 main 파일 책임 분리 | 기능 변경 단위의 작은 module과 회귀 테스트 확보 |

## 9. 다음 성능 Gate

M1.6 각 단계 후 단독 실행으로 5회 반복한다.

- 1080p30: `decodedFrames >= 27`, `LAT_P95_US <= 70,000`, 1초 초과 frame gap 없음
- 720p30: `decodedFrames >= 28`, `LAT_P95_US <= 55,000`, 1초 초과 frame gap 없음
- CPU: Host/Client 각각 동일 장면 전후 비교
- 메모리: 10분 실행 후 working set과 frame buffer allocation 횟수 비교
- Host 세부 시간: callback, readback, crop/scale, NV12, encode, queue-to-send
- UI: connect/list/viewer 전환 중 1초 이상 UI stall 없음

## 10. 실행한 검증

- CMake Debug:
  - `remote60_host_app`
  - `remote60_native_video_host_poc`
  - `remote60_native_video_client_poc`
  - `remote60_native_video_client_shared_core_test`
  - `remote60_udp_control_channel_test`
  - `remote60_udp_control_e2e_test`
  - `remote60_input_macro_test`
- `remote60_native_video_client_shared_core_test.exe`: PASS
- `remote60_udp_control_channel_test.exe`: 5/5 PASS
- `remote60_input_macro_test.exe`: 23개 검사 PASS
- `node apps/directory/test/run.js`: 전체 PASS
- Android `:app:assembleDebug`: PASS, 4 ABI native build 포함
- 1080p30 격리 포트 실제 Host/Windows Client 진단: 실행 및 지표 수집 완료

이번 작업은 감사와 우선순위 문서화 범위이며, 위 코드 결함은 아직 수정하지 않았다.
