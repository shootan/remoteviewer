# dxgi-oslink-q9 r1 — OSLink DXGI 사용 방식, GNLink 대조, 수정 목표 후보 (작업용2, 2026-10-28)

정본 지시: `.claude/task_dxgi_oslink_q9_r1.md`. 결정은 Codex 와 사용자가 한다. 이 문서는 근거와 후보만 담는다.

- **A 정본**: `C:\work\oslink-analysis\q9_dxgi_usage.md`
  - 근거: `evidence/q9_screencapture_excerpts.txt`, `evidence/q9_sdk_vtables_iids.txt`
  - 스크립트: `work/q9_query.py`, `work/q9_sdk_vtables.py`
- **B 기준**: `C:\work\gnlink-wt\ka-r3` HEAD **af49c26ff8fff967003433d07126f58384f93cc3** (spike/host-admission-r3). 추적 파일 변경 없음.
- **하지 않은 것**: OSLink 실행, 제품 수정, Host·GPU 실행, 삭제. 저장소 밖에는 새 파일만 만들었다.

## A 요약 (판정은 A 정본 참조)

1. **장치**: `D3D11CreateDevice(adapter, UNKNOWN, Flags=0x21 = SINGLETHREADED|BGRA, FL 기본, …)`. 어댑터마다 하나씩 만들고, 생성 경로가 둘이라 **같은 어댑터에 두 벌**이 생긴다. — 확인
2. **멀티스레드 보호 없음**: MT IID 가 0개이고 SINGLETHREADED 를 쓴다. — 확인
3. **프레임 경로**: Acquire(timeout 0) → Texture2D QI → GetDesc → (mutex 아래) 필요할 때 SHARED 텍스처 생성 → **CopyResource 한 번** → SetEvent → **ReleaseFrame**(같은 호출 안). 그 앞에 Map·Flush·셰이더는 없다. — 확인
4. **스레드**
   - 캡처 스레드는 장치 D1 만 쓴다.
   - 소비 스레드는 이벤트를 기다렸다가 **장치 D2** 로 OpenSharedResource → CopyResource → NV12 셰이더 → staging → 블로킹 Map 을 한다.
   - 각 장치를 만지는 스레드는 하나다. — 확인(구조)
   - 나머지 `_beginthreadex` 3개의 역할은 미확인이다.
5. **재생성**: ACCESS_LOST 이면 DuplicateOutput 을 다시 만들 수 있다. 장치까지 다시 만드는지는 미확인이다. 큰 복구는 capture.exe 프로세스를 다시 띄우는 것이다(q1).
6. **libwj-d3d11.dll**: OBS libobs-d3d11 의 개명본이다. screencapture 가 `wjcapture.dll`(libobs)을 LoadLibrary 한다. 쓰는 조건은 미확인이다.
7. **인코더**: ldremote.exe(Zego, 별도 프로세스)에 있다. 캡처 장치와 공유하지 않는다. — 확인

## B 대조표 — GNLink (af49c26)

| 질문 | OSLink | GNLink (file:line) |
|---|---|---|
| 장치 생성 플래그 | SINGLETHREADED\|BGRA (0x21) | `D3D11CreateDevice(adapter, UNKNOWN/HARDWARE, …, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, …)` — `apps/native_poc/src/host_capture_device.cpp:419-428`(플래그 :422). SINGLETHREADED·VIDEO_SUPPORT 없음. 호출 `host_startup_graphics.cpp:148` |
| 장치 수 / 공유 | 캡처 D1, 처리 D2, 인코더는 다른 프로세스 | **장치 하나**(res.d3d/res.ctx)를 다음이 함께 쓴다. ① DXGI duplication(`libs/capture/src/capture_backend_dxgi.cpp:343` DuplicateOutput) ② readback(`d3d_capture_readback.cpp`) ③ GPU 스케일러(`host_capture_session.cpp:133`, `host_startup_graphics.cpp:167`) ④ MF H.264 MFT(`host_startup_graphics.cpp:163` → `mf_h264_codec.cpp:1238-1255` MFCreateDXGIDeviceManager/ResetDevice, `:1868-1871` MFT_MESSAGE_SET_D3D_MANAGER, `:1867` MF_TRANSFORM_ASYNC_UNLOCK) ⑤ WGC 경로의 같은 장치 래핑(`host_capture_session.cpp:99` CreateDirect3D11DeviceFromDXGIDevice, `:934` CreateFreeThreaded, `:961` StartCapture) |
| 멀티스레드 보호 | 없음(SINGLETHREADED) | 제품은 SetMultithreadProtected 를 **부르지 않는다**. 상태 로그만 있다(`host_capture_device.cpp:341-346`, 로그 :356-376, `host_startup_graphics.cpp:158/164`, `host_capture_session.cpp:122/125/535/933/962`). 시험 seam 만 켠다(`mt_ab_test_seam.hpp:13,52`, `host_startup_graphics.cpp:151-156` `#ifdef REMOTE60_MT_AB_TEST_SEAM`). **WGC 시작 뒤 state=on**: 제품 코드 중 켜는 곳이 없다. WGC 런타임(프레임 풀 생성이나 StartCapture, :934-961 사이)이 켜는 것으로 **추정**한다. 로그상 `wgc-pool-before`=off → `wgc-started`=on (`report_dxgi_wedge_first_session_r1.md:37`). |
| Acquire timeout | 0 + 루프 대기 | `acquireTimeoutMs` 기본 100(`capture_backend_dxgi.hpp:27`), Host 는 env 기본 **8**(`host_capture_session.cpp:709`). idle 때 2 ms sleep(:710, `capture_backend_dxgi.cpp:496-505`) |
| Acquire→Release 사이 | CopyResource 1회 (D1) | `onFrame(texture)`(`capture_backend_dxgi.cpp:645`) → PublishCapturedTexture → readback Submit: **d3dContextMu 아래에서** CopyResource(`d3d_capture_readback.cpp:595`, :602/:659) + End(query) + **Flush**(:666) → 돌아와 ReleaseFrame(`capture_backend_dxgi.cpp:594-604`, :653). Map 은 하지 않는다(OSLink 와 같다). hold 시간은 stats.holdMaxUs 로 잰다. |
| DXGI 호출과 context 잠금 | 같은 스레드가 독점(경합 없음) | **Acquire/ReleaseFrame 은 잠금 없이 캡처 스레드에서 부른다**(`capture_backend_dxgi.cpp:462`, :485, :596; 파일 안에 mutex 가 없다). 같은 immediate context 를 **readback worker 가 동시에** 쓴다: GetData(`d3d_capture_readback.cpp:828-837`), Map/Unmap(:934, :964), d3dContextMu 아래. 스케일러도 같은 mutex 를 쓴다. **MFT 내부 스레드가 장치나 context 를 쓰는 것은 앱 mutex 밖이다**(기본은 CPU 샘플 경로지만 SET_D3D_MANAGER 는 보낸다. NV12 surface 경로는 opt-in: `host_capture_session.cpp:226-227`, `host_app_main.cpp:401`). |
| 소비 처리 | 다른 장치 D2, 블로킹 Map | 같은 장치. worker 스레드가 GetData 로 폴링(DONOTFLUSH) → Map/Unmap(`d3d_capture_readback.cpp:105` 스레드, :828-967) |
| 포인터·dirty rect | 호출 미확인 | 포인터 위치는 frameInfo 로 전달한다(`capture_backend_dxgi.cpp:586`). GetFrameDirtyRects·MoveRects·PointerShape 는 쓰지 않는다(파일 grep 0) |
| ACCESS_LOST | DuplicateOutput 재생성 + 프로세스 재기동 | DuplicateOutput 재생성(`capture_backend_dxgi.cpp:536-556`). 실패하면 fallback(WGC) |
| 멎었을 때 감시 | 미확인 | worker heartbeat 감시: 3 s 경고, 5 s 에 `TerminateProcess(44)` → supervisor 가 재기동(`host_startup_capture.cpp:112-134`, :362). 격리 영속화(0.2.162 DW)는 그 뒤 DXGI 를 피한다. |

### Microsoft 공식 문서 (2026-10-28 확인)

- [Introduction to Multithreading in Direct3D 11](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-intro), "Multithreading and DXGI":
  - "Only one thread at a time should use the immediate context. However, your application should also use that same thread for … DXGI operations."
  - "**It is invalid to use an immediate context concurrently with most of the DXGI interface functions.** … the only DXGI functions that are safe are AddRef, Release, and QueryInterface."
  - → GNLink 에서는 Acquire/ReleaseFrame(캡처 스레드, 잠금 없음)과 readback 의 GetData/Map(worker)이 같은 immediate context 에서 겹칠 수 있다. 이 구조는 문서가 말하는 "invalid" 에 해당한다.
- [ID3D11Multithread](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_4/nn-d3d11_4-id3d11multithread):
  - "By default, multithread protection is turned off."
  - "By default in D3D11, applications can only use one thread with the immediate context at a time … The interface can turn on threading protection for the immediate context, which will increase the overhead of each immediate context call."
- [Supporting Direct3D 11 Video Decoding in Media Foundation](https://learn.microsoft.com/en-us/windows/win32/medfound/supporting-direct3d-11-video-decoding-in-media-foundation), Open a Device Handle 9단계:
  - DXGI Device Manager 로 공유한 장치의 context 에 대해 "**It is recommended that you use multi-thread protection on the device context to prevent deadlock issues** …(GetDecoderBuffer/ReleaseDecoderBuffer)", 그리고 `ID3D10Multithread::SetMultithreadProtected(true)`.
  - 이 문장은 **디코더** 문서다. **인코더 MFT 에 같은 요구가 있다는 공식 문장은 이번에 찾지 못했다.** 유추일 뿐이다.

## C 수정 목표 후보 (결정은 Codex·사용자)

| 후보 | 근거 | 기대 효과 | 위험 | 비용 | acceptance (측정 가능) |
|---|---|---|---|---|---|
| **G-f (새로 나온 것) DXGI 호출을 앱 context 잠금 안으로** — Acquire/ReleaseFrame(그리고 DuplicateOutput)을 d3dContextMu 로 감싸거나, readback·스케일러와 같은 직렬화 아래에 둔다 | MS "concurrently invalid" 문장. GNLink 는 잠금이 없다(위 표). OSLink 는 단일 스레드가 독점한다 | 문서상 정의되지 않은 동시 사용을 없앤다. 보호를 켜지 않아도 앱 안의 경합은 사라진다 | Acquire(timeout 8 ms)가 잠금을 잡고 있는 동안 worker 가 기다린다 → readback 지연(0.2.98 굶주림과 같은 계열). timeout 을 0 으로 하고 폴링하면 완화된다. **MFT 내부 사용은 이 잠금으로 막지 못한다** | 작음(라이브러리 config 에 mutex 포인터 추가) | 사전 회귀(지연): ctxWait p99 증가 ≤ X ms. 실기: NVIDIA·Unity 전체 화면에서 N 시간 동안 DXGI wedge 0(exit 44 0회) |
| **G-a 공유 장치에 보호 켜기** (SetMultithreadProtected(TRUE), 시작 시) | MF 디코더 문서의 권고. 현재 on 구간은 WGC 경로에서만 우연히 켜진다. A/B seam 이 이미 있다 | DXGI·MFT·앱 스레드를 런타임이 직렬화한다. 최소 변경이다 | 기존 기록: on 상태에서 Acquire 가 내부 잠금을 잡아 readback 이 100~500 ms 굶었다(0.2.98, `capture_backend_dxgi.cpp:496-502` 주석). **wedge 13건은 off 와의 상관일 뿐 확정 원인이 아니다** | 작음 | A/B: 같은 기기·같은 게임에서 off vs on 의 wedge 횟수와 지연(holdMax, ctxWait) 비교. on 에서 wedge 0, 지연 p99 ≤ 기준 |
| **G-b 장치 분리** (캡처 D1, 인코드·readback D2. 공유 텍스처(+keyed mutex 또는 fence) 또는 CPU 경유) | OSLink 와 같은 구조(D1/D2 + shared handle, 인코더는 다른 프로세스) | DXGI duplication 장치를 다른 사용자와 아예 공유하지 않는다. MFT 영향이 끊긴다 | 장치 사이 동기화(OSLink 는 Flush 없이 이벤트만 쓴다 — 그대로 따라 하면 안 된다). 텍스처 복사가 1회 늘어난다. 장치 재생성 경로가 두 배로 늘어난다 | 중간 | wedge 재현 조건에서 wedge 0. 프레임 지연 증가 ≤ 1 ms(p50) |
| **G-c 즉시 ReleaseFrame** | GNLink 는 이미 Acquire→Copy(+Flush)→Release 이고 Map 은 Release 뒤다(위 표). OSLink 도 같다 | 추가로 얻을 것이 거의 없다. 남은 것은 Flush 와 mutex 대기를 Release 뒤로 미루는 정도다 | 낮음 | 작음 | holdMaxUs 분포가 줄어드는지 |
| **G-d 캡처 프로세스 분리** (작업목록 CS) | OSLink 는 capture.exe 를 따로 띄우고 kill/reboot 한다(q1) | wedge 가 나도 연결과 세션이 유지된다. 캡처만 다시 띄운다 | 프로세스 간 텍스처 공유와 IPC, 보안 데스크톱/세션 처리 | 큼 | wedge 를 강제로 일으켰을 때 viewer 연결 유지, X 초 안에 영상 복귀 |
| **G-e 복구 시간 단축** | 13:27:47 Host 재기동 → 13:27:58 viewer 가 끊김을 감지(11 s) | 체감 정지가 21 s 에서 감시 5 s + 재연결 수 초로 줄어든다 | 너무 이른 판정은 오탐(정상적인 긴 프레임) | 작음~중간(감시 임계값, viewer 의 끊김 감지, 즉시 재연결 알림) | wedge 주입(시험 seam)부터 영상 복귀까지 ≤ X s. 오탐 0(정적 화면 1시간) |

**우선순위 제안**(결정 아님): G-f 와 G-a 를 같은 A/B 틀에서 비교하는 것이 가장 싸고 근거도 직접적이다. 근본 차단은 G-b, 사용자 체감 단축은 G-e 가 맡는다. G-d 는 장기 과제다.

**실기 측정 계획** (사용자 조율 필요, 절차만 적는다)
1. 회사 PC 와 홈 PC 의 NVIDIA 기기에서 dokkaebi(Unity) 전체 화면을 켜 둔다.
2. 각 arm 의 Host 를 같은 시간 동안 접속과 재접속을 반복하며 운용한다.
3. 다음을 수집한다: exit 44 횟수, `[capture] … hold`·ctxWait, d3d-mt 줄, wedge 직전 phase(Release/Acquire).
4. arm 순서를 섞는다.
5. 끊김 감지 시각을 NAS 로그로 대조한다.

## 남은 미확인

- OSLink 의 나머지 스레드 3개의 역할.
- ACCESS_LOST 때 장치까지 다시 만드는지.
- 콜백 끝이 공유 메모리인지(q2 와 연결하는 일).
- libwj 를 쓰는 조건.
- GNLink 에서 MFT 내부 스레드가 장치나 context 를 실제로 어떻게 쓰는지(NVIDIA MFT 내부라 정적으로는 알 수 없다).
- 인코더 MFT 에 대한 MS 의 멀티스레드 공식 문장.
