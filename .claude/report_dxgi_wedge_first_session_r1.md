# report dxgi-wedge-first-session r1 — 작업용2 (remote_작업용2 / sei64muv), 2026-10-21

조사 전용. 제품 수정·버전·게시·사용자 PC/회사 PC 조작 **없음**. NAS 는 gnlink 계정으로 읽기만 했다.
코드 인용 기준: main worktree `.claude/worktrees/main-stability-integration` HEAD **5063cd9** (0.2.161).
D:/remote/remote 의 `refactor/viewer-split`(659f788)에는 WGC 복구 코드가 없다. 이 브랜치가 기준이 아니다.
로그 시계: 뷰어(68f79d01)가 호스트(DEV-SHOTAN-L)보다 **약 +3.3 s** 앞선다. 근거는 호스트 15:17:05.45 punch 와 뷰어 15:17:08.74 connect 의 대응이다.
아래 시각은 각 기기 자기 시계 기준이다.

## 요약 (3줄)
1. **wedge 는 NVIDIA 호스트에서 `d3dMt=off`(장치 멀티스레드 보호 꺼짐) DXGI 캡처에서만 났다.** 회사 PC 는 9/9건이 off 였다. on 은 3.24 h 동안 0건, WGC 는 2.48 h 동안 0건이었다. 다른 NVIDIA 호스트 3be5148e 도 4/4건이 off 였다. AMD 호스트 8ec6ecb1 은 off 로 7.0 h 를 돌았는데 0건이었다. **강한 상관이지 확정 원인은 아니다**(incident_2026-09-12_code_review.md:73 의 경고 유지). 확정은 A/B 로 한다.
2. "호스트 시작 뒤 첫 접속만" 이 일어나는 이유: 호스트를 새로 시작하면 장치가 off 로 만들어진다 → 첫 DXGI 세션에서 10~350 s 안에 wedge → supervisor 가 WGC 격리로 재기동 → 같은 supervisor 수명 동안은 WGC 라 재발하지 않는다. WGC 격리는 **메모리에만** 있어 호스트 재시작·재부팅 시 사라진다.
3. 뷰어는 "재연결 없이 닫힌" 것이 아니다. **닫힌 뒤 1.1 s 만에 shell 의 자동 재연결이 실제로 동작했다**(pid 27888). 하지만 재연결 세션은 **이전 화면이 아니라 picker** 를 보여 줬다. 그 세션은 4 s 뒤 선택 없이 끝났고, 이어 다른 호스트(f6e8e54a) 세션이 시작됐다. 사용자 체감은 "멈춤 약 17 s → 창 꺼짐 → 엉뚱한 창" 이다.

## 1. wedge 발생 이력 (NAS host.log ~ host.log.10, 10-08 ~ 10-21)
회사 PC(51bd6657…, hostId df15b73b). `run` = 마지막 DXGI capture-started 부터 wedge 까지의 시간.

| # | 호스트 시각 | phase | run | d3dMt | 직후 |
|---|---|---|---|---|---|
| 1 | 10-08 17:11:55 | release 5.01 s | 40 s | off | 재기동 뒤 Android `desktop-backend-request dxgi` → DXGI(on) 복귀 |
| 2 | 10-08 17:56:56 | **frame_handler** 5.08 s | 116 s | off | 직전 17:56:56.43 ABR `high_to_low_severe` 1280x720 전환 |
| 3 | 10-11 02:18:19 | release 5.17 s | 30 s | off | Android dxgi 요청으로 DXGI 복귀 |
| 4 | 10-11 02:21:5x | release 5.46 s | 27 s | off | #3 이후 window-select 로 DXGI 재시작(off) → 3 분 만에 재발 |
| 5 | 10-11 16:36:03 | release 5.32 s | 99 s | off | 0.2.156, host-app "WGC recovery" |
| 6 | 10-11 16:40:56 | release 5.34 s (accumulated=2) | 91 s | off | 16:39:21 호스트 0.2.147 로 새로 시작 → WGC 격리 소실 → 재발 |
| 7 | 10-14 14:10:46 | release 5.48 s | 350 s | off | WGC 재기동 → Android dxgi 요청으로 DXGI 복귀 |
| 8 | 10-17 13:46:50 | release 5.18 s | 29 s | off | 0.2.158, WGC |
| 9 | 10-21 15:16:51 | release 5.23 s | 10.5 s | off | 0.2.161, WGC |

집계(같은 awk 를 세 호스트 전 회전본에 적용, 캡처 run 단위):

| 호스트 | GPU(인코더 MFT) | dxgi d3dMt=off | dxgi d3dMt=on | wgc |
|---|---|---|---|---|
| 회사 PC 51bd6657 | NVIDIA | 44 runs / 0.53 h / **9 wedge** | 114 runs / 3.24 h / 0 | 27 / 2.48 h / 0 |
| 3be5148e | NVIDIA | 26 / 0.12 h / **4 wedge** | 19 / 0.29 h / 0 | 6 / 0.01 h / 0 |
| 이 PC 8ec6ecb1 | AMD | 76 / 7.01 h / 0 | 100 / 8.23 h / 0 | 12 / 0.07 h / 0 |

- off 와 on 의 차이: 호스트를 새로 시작하면 장치가 off 로 만들어진다. WGC 로 시작하면 `wgc-started state=on` 이 되고, 그 장치로 DXGI 로 바꿔도 on 이 유지된다(로그의 d3d-mt 줄). on 구간은 사실상 전부 "WGC 로 시작한 뒤 DXGI 로 전환" 경로다. **경로 차이(장치 생성 순서 등) 교란 가능성은 남는다.**
- wedge 직전 공통 신호: 약 5 s 동안 `trailing-edge kick` 만 나가 화면 반복 송출이 1 fps 로 떨어진다. 그 앞에서는 `capture-stamp clamped behind-synthetic readbackWaitUs` 15~30 ms 가 자주 보인다.
- 게임·전체 화면과의 관계: **로그로는 확정할 수 없다.** 호스트는 전경 창·디스플레이 모드 전환·HDR·독점 전체 화면을 기록하지 않는다. 10-21 은 접속 6.5 s 만에 멈췄다. 직후 재연결 세션의 window-list 첫 항목이 `도깨비의세계 - 26.104.005` 라서 게임 창이 떠 있었던 것은 맞다. 그러나 해상도·모드 전환 이벤트는 보이지 않는다. 캡처는 처음부터 끝까지 1920x1080 이었다.
- 코드 경계(5063cd9): `libs/capture/src/capture_backend_dxgi.cpp:527-530` enterPhase(Release) → `duplication->ReleaseFrame()` → markProgress. watchdog 은 `apps/native_poc/src/host_startup_capture.cpp:116-117,131,142`(3 s 경고, 5 s TerminateProcess(44)).
  - ReleaseFrame 호출 중에는 앱 mutex 를 쥐고 있지 않다(`d3dContextMu` 는 onFrame 안에서 이미 풀린다, `d3d_capture_readback.cpp:593-595`).
  - **같은 ID3D11Device 를 MF H.264 인코더 MFT 가 공유한다**(`host_startup_graphics.cpp:153` → `mf_h264_codec.cpp:1245-1248` DXGIDeviceManager).
  - 보호 off 상태에서는 MFT/드라이버 내부 스레드와 immediate context 사용이 직렬화되지 않는다. 이것은 0.2.98 때 Codex 가 "보호 OFF 는 경쟁·hang 위험" 이라고 반대한 바로 그 조건이다(history.md:8542). → **가설: NVIDIA 드라이버에서 보호 off + MFT 동시 사용 → ReleaseFrame 내부 대기.**
  - 확정에는 정체 시점 스레드 스택/덤프 또는 아래 A/B 가 필요하다.

## 2. 뷰어 동작 — 10-21 시간선 (뷰어 시계)
- 15:16:54.89 마지막 프레임 수신. 호스트 기준 15:16:51.6 kill, 15:16:52.5 재등록.
- 15:16:59.04 `link-silent` 기록(로그만 남는 단계).
- 15:17:01.42 control action 이 6.3 s 만에 실패한다(`peer-lost`).
- 15:17:07.30 `session-dead action=close cause=control-lost`.
  - 이는 control 이 끊긴 지 5 s 이상이고 publish 도 5 s 이상 없을 때 나는 판정이다(`viewer_recv_liveness.hpp:104,169`).
  - close 는 `viewer_session_watchdog.cpp:88-105` 에서 exit 43 + WM_CLOSE 로 처리된다. 12 s 상수는 없다. "peer-lost 탐지 약 6.5 s + deadSession 5 s" 의 합이다.
- 15:17:07.46 done → **15:17:08.56 새 뷰어 pid 27888 이 같은 호스트에 접속**.
  - shell 자동 재연결(`client_shell_main.cpp:1268-1272`): exit 43/44/46 이면 `1000<<attempts` ms 뒤 begin_session 을 다시 한다. 첫 회는 1 s 이다.
  - client.log 에는 `15:17:08 session started host=df15b73b` 만 있고 재연결 분기 로그가 없다. 1.1 s 간격으로 자동 재연결이라고 판단했다(**강한 정황, 직접 로그 없음**).
  - 새 호스트 프로세스가 옛 capability 를 거부하는 것은 정상이다. capability 가 1회용·30 s 이고 프로세스 메모리에 있기 때문이다(`directory_client.cpp:1502-1544`). shell 재연결은 새 /api/connect 로 하므로 성공했다(15:17:09 connected, WGC).
- 재연결 세션은 **picker 를 표시**했다(history.md:11773 "자동 재연결은 picker 재표시, 이전 선택 복원 없음"). 그래서 stream inactive 이고 keyframe 재요청 3회는 헛돈다.
- 15:17:13.42 picker 선택 없이 done. 직후 15:17:13 shell 이 **다른 호스트 f6e8e54a** 세션을 시작했다.
  - 27888 이 끝난 이유는 로그에 없다. **사용자가 shell 에서 다른 호스트를 눌렀을 가능성**이 있지만 확정하지 않는다.
- control-resume 은 "같은 호스트 세션 안에서 control 만 끊기고 영상은 살아 있을 때" 쓰는 장치다(`viewer_control_resume.hpp`, `viewer_control_client.cpp:574-585` 의 video_alive 조건). 호스트 프로세스가 재시작된 경우에는 설계상 해당하지 않는다.

## 3. 개선안 (구현 전 — protocol/신뢰 경계 항목은 Codex 확정 필요)
**ⓐ1 (근본 후보, 1순위) DXGI 장치 생성 직후 `ID3D11Multithread::SetMultithreadProtected(TRUE)`**
- 근거: 위 표의 on 0/3.24 h(회사 PC)와 0/0.29 h(3be5148e). 이 설정은 0.2.98 때 Codex 가 안전한 쪽이라고 본 방향이다.
- 위험:
  - 0.2.98 가설인 "보호 on 일 때 AcquireNextFrame 내부 불공정 락이 readback 을 굶긴다(UAC 뒤 지연)". 현재 8 ms timeout + 락 밖 2 ms 양보로 완화돼 있지만 실기에서 확정된 적은 없다.
  - `host_capture_device.hpp:86` 의 "observe only, never Set" 방침을 바꾸는 일이다.
  - `capture_readback_lock_test.cpp:71` 은 "product runs d3d-mt on" 이라고 가정하는데, 실제 출하물은 off 라 주석과 실제가 어긋나 있다.
- 시험: env 토글(예: `REMOTE60_D3D_MT=on|off`)을 둔 test build 로 회사 PC 실기 A/B 를 한다.
  - "호스트 재시작 → 첫 DXGI 세션 + 게임" 을 각 N 회 반복한다(off 는 1~6 분 안에 재발하던 조건).
  - 동시에 readbackWait/stampClamp/입력→화면 지연을 비교한다.
  - AMD(이 PC)에서는 회귀만 확인한다(재현이 없으므로).

**ⓐ2 WGC 격리 영속화 (보조)**
- 장치별 파일에 "DXGI wedge 이력 + GPU 드라이버 버전 + 만료(예: 7일)" 를 남기고, 다음 supervisor 시작 때 WGC 로 시작한다.
- 근거: #6(호스트 재시작으로 격리 소실 → 91 s 뒤 재발), 그리고 사용자의 "매번".
- 위험: WGC 고착(성능·노란 테두리 등 WGC 특성). 드라이버 갱신 뒤에도 격리가 남는다(드라이버 버전 키로 완화).
- 시험: recovery_process_test 확장, fixture child 를 exit 44 시킨 뒤 supervisor 를 재시작했을 때 WGC 로 시작하는지 본다.

**ⓐ3 격리 중 Android `desktop-backend-request dxgi` 무시(stored 만)**
- 근거: #1/#3/#7 에서 Android 요청이 WGC 를 DXGI 로 되돌렸고, #4 는 3 분 만에 재발했다(`host_stage_backend.cpp:120-135` 가 무조건 덮어씀).
- 위험: Android 사용자의 명시 선택이 무시된다 → 뷰어에 "격리 중" 상태 표시가 필요하다(protocol 변경 여부는 Codex 판단).

**ⓑ1 재연결 후 이전 선택 복원 (picker 대신 직전 desktop/window 자동 선택)**
- 근거: 이번 사건에서 재연결은 성공했지만 picker 로 떨어져 사용 흐름이 끊겼다.
- 위험: 창 id 가 새 호스트 프로세스에서 달라진다 → desktop 은 그대로, window 는 프로세스·제목 매칭 실패 시 picker 로 둔다.
- 시험: isolated test build 에서 fixture 호스트를 supervisor 아래 exit 44 시키고 측정한다.
  - 사용자 행동 acceptance: 화면 정지 → "재연결 중" 표시 → 같은 화면 자동 복귀(창이 사라지지 않음 또는 즉시 재표시).
  - 렌더된 창 스크린샷 + 소요 시간을 증거로 남긴다.

**ⓑ2 끊김 탐지 단축과 창 유지**
- 호스트 재기동은 directory 에서 "같은 hostId 의 새 online" 으로 알 수 있다. 뷰어/shell 이 이를 받으면 12 s 를 기다리지 않고 즉시 재연결하고, 창을 닫지 않고 "재연결 중" 오버레이를 둘 수 있다.
- 위험: directory 신호를 새로 쓰는 것은 protocol·신뢰 경계 변경이라 Codex 확정이 필요하다.
- 시험: ⓑ1 과 같은 fixture 로 한다.

**ⓒ 진단 보강 (작고 위험 낮음)**
- shell 재연결 분기에 로그 1줄을 남긴다(현재 `handle_viewer_exit` 가 남기지 않아 이번에도 정황으로만 판단했다).
- watchdog 3 s 경고 시점에 전경 창 프로세스·전체 화면 여부·디스플레이 모드를 기록한다.
- wedge 시 minidump 를 남긴다(`MiniDumpWriteDump` 으로 워커 스택 확보 — 원인 확정용).

## 미검증 / 한계
- 게임 독점 전체 화면·모드 전환과의 관계: 로그에 신호가 없어 미확정이다.
- d3dMt on/off 와 원인의 인과: 상관만 확인했다(교란 = 장치 생성 경로 차이).
- 27888 종료 사유: 로그가 없다.
- viewer.log 회전본은 10-19 이후만 남아 있어, 10-17 이전 사건의 뷰어 동작은 확인하지 못했다.

## 다음 담당
- 검증용(remote_claude): 개선안 채택과 Codex 확정(ⓐ1 env-토글 A/B 실기 계획, ⓑ protocol 여부).
- 작업용2 는 지시를 받으면 구현·test build 로 진행한다.
