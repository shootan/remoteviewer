# Android Direct Client 구현계획
Updated: 2026-04-08 17:32

이 문서는 Android direct client 작업의 단일 기준(Source of Truth)으로 사용한다.

기본 원칙은 `한 번에 전체 구현하지 않기`, `기존 host/protocol 유지`, `Windows native client와 공용 core 최대 재사용`이다.

## 1) 목표
- Android 기기에서 현재 `remote60_native_video_client_poc`와 같은 direct-connect 방식으로 host에 직접 접속한다.
- v1 범위는 `H.264 영상 수신`, `Desktop Mode`, `window list/select`, `single-touch tap/drag`, `committed text 입력`까지로 제한한다.
- signaling, NAT traversal, relay는 이번 범위에 포함하지 않는다.
- host 프로토콜 변경은 원칙적으로 하지 않고, Android는 기존 계약에 맞춘다.

## 2) 기본 정책
- 플랫폼: `Android only`
- 연결 방식: `manual host IP/DNS 입력`
- 전송:
  - video: `UDP`
  - control/input: `TCP`
- codec: `H.264 only`
- 앱 셸: `Kotlin + NDK`
- UI 목표: 현재 Windows native client의 핵심 UX만 축약 재현
  - `Connect/Disconnect`
  - `Refresh`
  - `Desktop Mode`
  - window list
  - selected target 상태 표시

### UI reference
- Android 목표 화면 레퍼런스는 `D:\remote\remote\image\f3c9df0d83454ce1ba78a2fd6cb7e7801735893664.webp`를 기준으로 한다.
- 위 이미지는 구현 방향을 설명하기 위한 예시용 화면으로 취급한다.
- 탭 의미는 아래처럼 고정한다.
  - `LD플레이어` 탭: 각 윈도우별 화면
  - `디바이스` 탭: 각 모니터 화면
- 이후 Android UI 구현은 색/여백/세부 배치는 조정할 수 있어도, 위 레퍼런스의 정보 구조와 탭 의미는 유지한다.

## 3) 재사용 전략
### 재사용 대상
- `poc_protocol.hpp` 기반 바이너리 프로토콜
- UDP media handshake / H.264 chunk 재조립
- TCP control channel
- window list / window select / input ack / keyframe request / metrics 로직

### 분리 대상
- 현재 [native_video_client_main.cpp](D:/remote/remote/apps/native_poc/src/native_video_client_main.cpp)에 섞여 있는 아래 기능을 분리한다.
- 공용 core:
  - session state
  - transport/socket
  - protocol encode/decode
  - UDP assembly
  - control loop
  - input queue
  - window list state
- Windows 전용:
  - Win32 window
  - D3D11 renderer
  - Media Foundation decoder
  - IME/WM_POINTER 처리
- Android 전용:
  - Kotlin UI
  - Surface/MediaCodec
  - Android lifecycle
  - soft keyboard
  - touch event capture

### 공용 core 설계 원칙
- Windows API 타입(`HWND`, `SOCKET` 외 platform UI 타입), D3D11, Media Foundation 의존을 core 밖으로 밀어낸다.
- core는 `세션/프로토콜/상태기계`만 책임진다.
- 디코더와 렌더러는 adapter 인터페이스로 분리한다.
- Windows client도 같은 core를 쓰도록 전환해 Android 전용 분기를 최소화한다.

## 4) 단계별 추진 계획
### Phase A. 공용 core 추출 범위 고정
- 목표:
  - 현재 Windows client에서 Android와 공유 가능한 코드 경계를 먼저 고정한다.
- 작업:
  - `session/transport/protocol/control/input/domain state` 경계를 정의한다.
  - 새 core에 들어갈 책임과 Windows/Android adapter 책임을 확정한다.
  - raw path는 유지하되 Android 범위와 분리한다.
- 산출물:
  - 공용 core 헤더/구현 골격
  - 기존 Windows client에서 옮길 함수 목록
- 완료조건:
  - 구현자가 “어디까지 core로 옮기고 어디를 platform에 남기는지” 추가 판단 없이 착수 가능

### Phase B. Windows client 공용 core 1차 적용
- 목표:
  - 기존 Windows client 동작을 바꾸지 않고 공용 core를 먼저 실제 사용하게 만든다.
- 작업:
  - UDP media connect/handshake
  - TCP control connect
  - H.264 UDP chunk assembly
  - window list/select
  - input queue / input text / ack
  - keyframe request / metrics publish
  - 위 로직을 core로 이동하고 Windows main은 glue만 남긴다.
- 완료조건:
  - Windows client direct-connect 회귀 없음
  - 기존 external guide 기준 localhost/WAN smoke 절차 유지

### Phase C. Android 앱 셸 최소 골격
- 목표:
  - Android 앱이 공용 core를 로드하고 세션을 시작/종료할 수 있게 만든다.
- 작업:
  - Kotlin 앱 프로젝트 추가
  - host/port 입력 UI
  - connect/disconnect
  - JNI bridge
  - session status / error text 표시
- 완료조건:
  - Android 실기기에서 connect/disconnect 반복 가능
  - 오류 이유가 UI 또는 로그로 식별 가능

### Phase D. Android 영상 수신
- 목표:
  - Android에서 direct-connect H.264 영상을 정상 표시한다.
- 작업:
  - core가 access unit을 Android decoder adapter에 전달
  - Android는 `MediaCodec + Surface`로 decode/render
  - keyframe wait/reset/recovery 경로 연결
- 완료조건:
  - Android에서 영상 수신/표시 성공
  - UDP 손실 후 keyframe recovery로 재생 복구 가능

### Phase E. Android 제어 UI
- 목표:
  - Android에서 Desktop Mode 및 window list/select를 사용할 수 있게 한다.
- 작업:
  - `D:\remote\remote\image\f3c9df0d83454ce1ba78a2fd6cb7e7801735893664.webp` 기준으로 탭/카드 구조를 맞춘다.
  - `LD플레이어` 탭은 각 윈도우별 화면 목록/썸네일을 보여준다.
  - `디바이스` 탭은 각 모니터 화면을 보여준다.
  - `Refresh`, `Desktop Mode`, window list, selected target UI 추가
  - control message를 기존 wire format 그대로 사용
- 완료조건:
  - Android에서 window list 조회/선택 성공
  - target 변경 상태가 UI에 반영됨

### Phase F. Android 입력 v1
- 목표:
  - 모바일 기본 입력을 기존 control 채널로 전달한다.
- 작업:
  - tap -> left click
  - drag -> left-button drag
  - touch move -> mouse move
  - committed text -> existing UTF-16 text message
- 제외:
  - multi-touch
  - pinch zoom
  - 제스처 단축동작
  - 고급 하드웨어 키 매핑
- 완료조건:
  - Desktop Mode와 selected window mode에서 tap/drag 동작
  - soft keyboard committed text 전송 성공

### Phase G. 외부 direct-connect 검증
- 목표:
  - 현재 direct-connect 운영 방식 그대로 Android 외부 접속을 검증한다.
- 작업:
  - 포트포워딩/방화벽 조건을 기존 가이드에 맞춰 확인
  - Android 접속 절차를 별도 문서/가이드에 추가
- 완료조건:
  - 외부망 Android direct-connect smoke 성공
  - Windows/Android 혼합 테스트 기준 정리 완료

## 5) 단계 간 Gate
- Gate A: Windows 공용 core 회귀 통과 전 Android 기능 작업 금지
  - direct-connect
  - Desktop Mode
  - window list/select
  - input ack
- Gate B: Android connect/disconnect 안정화 전 영상 디코드 작업 금지
- Gate C: Android 영상 수신 안정화 전 입력 작업 금지
- Gate D: Android 입력 안정화 전 외부 WAN 검증 확대 금지

## 6) 테스트 계획
### Windows 회귀
- localhost direct-connect
- `video=udp`, `control=tcp`, `codec=h264`
- Gate A localhost 기준은 `frame gating off`, `ABR off`, `h264 no pacing` 고정
- Gate A 실패 시 `HOST_CAPTURE_SOURCE_LAST`가 `CreateForWindow(GetShellWindow())`인지 먼저 확인
- Gate A 자동 검증은 `ShellWindow fallback + decoded=0`일 때 monitor capture 확보를 위해 재시도 허용
- Desktop Mode / window list / selected target / input ack

### Android 기능
- LDPlayer는 `Phase C` 앱 셸/JNI/session probe 수준 확인에 사용 가능
- 단, `MediaCodec`, `Surface`, lifecycle, touch/IME, 성능은 실기기 확인 필요
- 실기기 connect/disconnect 반복
- 잘못된 host/port에서 오류 노출
- 영상 수신/표시
- app background/foreground 후 session/surface 재연결
- window list refresh/select
- tap/drag
- soft keyboard committed text

### 외부망 smoke
- 기존 포트포워딩 조건 재사용
- manual IP/DNS 입력 direct-connect
- 장애 시 로그로 실패 지점 분리

## 7) 다음 실행 순서
1. Phase D / Phase E live verify
   - `nativeGetVideoSizePacked`, `TextureView.setDefaultBufferSize`, `window panel JSON`, `refresh/select/Desktop Mode`, `LDPlayer/Devices` 탭 UI 1차, scene split(`connect -> targets -> viewer`) 구현은 끝냈다.
   - LDPlayer screenshot 기준으로 `scene_connect -> scene_targets -> scene_window_viewer -> scene_back_to_list` 전환과 hidden back button 복귀를 확인했다.
   - selected-window path에서는 `CreateForWindow(window-select)` capture source와 full-frame viewer content까지 확인했다.
   - Android 설정 persistence와 diagnostics log file도 붙였고, 첫 선택 후 두 번째 선택 재진입까지 확인했다.
   - 남은 것은 장시간 반복 soak과 desktop path 안정화다.
2. Phase E selection UX 보정
   - 현재는 list scene에서 텍스트 목록을 탭해 viewer scene으로 들어가는 구조이므로, 필요 시 썸네일/카드형 목록 또는 정렬/필터 UX를 추가한다.
   - recursive capture를 막기 위해 helper input window(`textinputhost.exe`)는 제외하고, emulator window는 blanket 제외 대신 현재 Android client instance PID만 좁게 제외한다.
   - static window를 freeze로 오판정하던 `capture-input-stall` low-push restart는 window capture mode에서 비활성화했다.
   - window 선택 시 viewer scene 전환은 selection ack 이후로 미루고, target 변경 시 decoder reset을 명시적으로 호출한다.
   - monitor list는 현 프로토콜 범위 밖이므로, 필요 시 `디바이스` 탭 semantics와 실제 데이터 공급 경계를 다시 정리한다.
3. Phase F 착수 전 Gate 고정
   - 입력 작업에 들어가기 전에 `full-frame video`, `window select`, `viewer -> list 복귀`, `background/foreground 후 surface 재연결` 네 항목을 실기기 기준으로 재확인한다.
   - 위 Gate를 통과한 뒤 `tap/drag`, 마지막으로 `committed text`를 붙인다.

## 8) 리스크와 대응
- 리스크: Windows client에 로직이 과도하게 뭉쳐 있어 core 추출 중 회귀 가능성 높음
  - 대응: Android보다 먼저 Windows core 전환을 끝내고 회귀 확인
- 리스크: Android decode/render와 lifecycle 결합부에서 surface reset 이슈 발생 가능
  - 대응: decoder adapter를 core 밖으로 유지하고 reset 계약을 명시
- 리스크: 입력을 먼저 붙이면 디버깅 경계가 무너짐
  - 대응: 영상 수신과 control UI 안정화 이후 입력 착수

## 9) 이번 문서화 완료 기준
- [x] Android direct client를 별도 계획 문서로 분리
- [x] 공용 client core 재사용 전략 명시
- [x] 단계별 작업 순서와 gate 정의
- [x] v1 범위와 제외 범위 고정
- [x] Phase A 구현 착수
- [x] Phase C 앱 셸 골격 착수
- [x] Phase C JNI stub를 공용 session controller로 교체
- [x] Phase C session controller에 실제 TCP/UDP probe 연결
- [x] Phase C real session lifecycle wiring (`ClientSessionController` async worker/control loop/window list status summary)
- [x] LDPlayer 2에서 real session 상태 변화 검증 (`connecting -> connected/error -> disconnected`)
- [x] Phase D decoder/surface adapter placeholder 고정 (`ClientEncodedFrameSink`, JNI/Kotlin 미연결)
- [x] Phase D UDP H.264 receive + `SurfaceView/MediaCodec` wiring
- [x] real host 순서 호환 수정 (`UDP hello` 후 `TCP control` retry 연결)
- [x] LDPlayer 2에서 real host decode path/log 검증 (`MediaCodec started`, output release, disconnect/error 재확인)
- [x] Phase D TextureView 가시화 디버깅 (`SurfaceView` 대신 `TextureView`, screenshot에 일부 frame 노출)
- [x] decoder debug 상태를 Android UI에 노출 (`surface/codec/csd/in/out`)
- [x] Android 목표 UI 레퍼런스/탭 의미 명시 (`image/...webp`, `LD플레이어`=각 윈도우별 화면, `디바이스`=각 모니터 화면, 예시용 화면)
- [x] Phase D surface buffer 실제 크기 재바인딩 + compact shell layout (`nativeGetVideoSizePacked`, `TextureView.setDefaultBufferSize`, LDPlayer viewport `928x86 -> 936x254`)
- [x] Phase E session/JNI 제어 브리지 1차 (`nativeRequestWindowList`, `nativeSelectWindow`, `nativeSelectDesktopMode`, window panel JSON snapshot)
- [x] Phase E 탭/선택 UI 1차 (`LDPlayer/Devices`, Desktop Mode 버튼, Spinner 기반 window list/select 표시)
- [x] Phase E connected compact mode (`Show Panel` toolbar, connected screenshot 기준 viewport `936x172 -> 936x310`)
- [x] Phase E scene split (`connect scene -> targets scene -> fullscreen viewer scene`, hidden top-left back button)
- [x] Phase E recursive target filter (`textinputhost.exe` 제외 + current Android client LDPlayer PID만 window list에서 제외)
- [x] Phase E window-capture stall false-positive 완화 (`captureInputLowPushStreakSec` 기반 restart를 window capture mode에서 비활성화)
- [x] Android endpoint persistence (`host/videoPort/controlPort` SharedPreferences 저장/복원)
- [x] Android diagnostics log file (`android_direct_client_session.log`, scene/status/video stall 기록)
- [x] Phase E second-selection re-entry guard (selection ack 후 viewer 전환 + target switch 시 decoder reset)
- [x] Phase E polish (`immersive fullscreen`, system back `viewer -> list`, connect/targets exit confirm, orientation recreate 방지)
- [x] Phase E LDPlayer manual verify (`connect` fullscreen screenshot, `targets/connect` exit dialog, `system back -> list`)
