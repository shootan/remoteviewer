# task dxgi-oslink-q9 r1 — OSLink DXGI 사용 방식 추가 분석 + GNLink 대조 → 수정 목표 후보 (검증용 remote#7fsfy22z, 2026-10-28) — 담당 작업용2

**이 파일이 정본이다.** 끝까지 읽고 CLAUDE.md·AGENTS.md 대로 진행한다.

## 왜
- 사용자 지시(10-28): "추가 분석 진행해서 gnlink 를 어떻게 수정할지 목표를 세우자."
- 발단: 10-28 13:27 홈 PC(3be5148e, 0.2.163) 접속 직후 약 21 s 동안 화면이 멈췄다.
  - DXGI `ReleaseFrame` 단계에서 정지했다. 전경은 dokkaebi.exe(Unity) 전체 화면이었고, 장치 상태는 `d3dMt=off` 였다.
  - Host 가 재기동한 뒤, 재접속 때 DXGI 를 격리하고 WGC 로 회복했다.
- 기존 조사(`.claude/report_dxgi_wedge_first_session_r1.md`): wedge 13건이 모두 NVIDIA + `d3dMt=off` + DXGI 조건이었다. 이것은 상관이지 확정 원인이 아니다.
- 검증용 사전 확인(10-28, 바이트 존재 검사만 했고 호출 경로는 보지 않음):
  - OSLink `screencapture.dll` 은 `D3D11CreateDevice`·`DuplicateOutput`·`ReleaseFrame` 문자열이 있다. ID3D10/11Multithread IID 바이트는 **없다**.
  - 인코더(`ZegoExpressEngine.dll`, NvEncodeAPI 와 MT IID 있음)는 다른 프로세스(ldremote.exe)에 있다.
  - 화면은 `Global\leidian_fb_rgb_mem_*` 공유 메모리로 넘긴다(`docs/OSLink_정적분석_2026-10.md` §3).
  - GNLink 는 한 D3D11 장치를 DXGI 캡처와 MF H.264 MFT(DXGIDeviceManager)가 함께 쓴다. DXGI 경로는 SetMultithreadProtected 를 부르지 않는다.

## 할 일
### A. OSLink 정적 분석 — `screencapture.dll`(+ `capture.exe` 의 캡처 루프)
기존 환경을 쓴다: `C:\work\oslink-analysis`(REA·Ghidra 12.1.4, `tools.md`). 새 파일만 만든다(`q9_dxgi_usage.md`, `evidence/q9_*`, `work/q9_*`). 기존 근거 파일을 덮어쓰지 않는다.
판정은 **확인 / 추정 / 미확인** 으로 나누고, 근거는 주소·Evidence ID 를 단다. 디컴파일 코드 원문은 옮기지 않는다(필요한 최소 발췌만).
1. **장치 생성**
   - `D3D11CreateDevice` 인자: adapter, driver type, flags(BGRA/VIDEO_SUPPORT/**SINGLETHREADED**/DEBUG 등), feature level 배열.
   - 장치 개수, 어느 스레드에서 만드는지, 출력(모니터)별로 장치를 만드는지.
2. **멀티스레드 보호**: ID3D10/11Multithread QI 나 SetMultithreadProtected 호출이 있는지, SINGLETHREADED 플래그를 쓰는지.
3. **프레임 경로 순서**
   - AcquireNextFrame(timeout 0) 다음에 무엇이 오는지: Texture2D QI → CopyResource/CopySubresourceRegion(staging, CPU_ACCESS_READ) → Map/Unmap → 공유 메모리 복사 → **ReleaseFrame** 의 순서.
   - ReleaseFrame 이 Map 앞인지 뒤인지.
   - 획득한 텍스처에 다른 GPU 작업(셰이더·VideoProcessor·커서 합성)이 있는지.
   - 프레임을 다음 Acquire 까지 들고 있는지, 바로 놓는지.
4. **스레드 모델**: 장치·immediate context 를 만지는 스레드가 몇 개인지, 캡처 루프 외에 같은 장치를 쓰는 구성요소(커서·스케일·썸네일)가 있는지.
5. **포인터·dirty rect**: GetFramePointerShape, GetFrameDirtyRects/MoveRects 를 쓰는지.
6. **재생성**: ACCESS_LOST 때 DuplicateOutput 만 다시 만드는지, 장치까지 다시 만드는지. 해상도·모드 전환은 어떻게 처리하는지.
7. **동기화·대기**: Flush, ID3D11Query(EVENT) 대기, Sleep/프레임 간격 정책.
8. **`libwj-d3d11.dll`**: 정체(게임 hook 캡처인지 등), capture.exe 가 로드하는지, 어떤 조건에서 쓰는지.
9. 인코더 장치가 캡처 장치와 별개라는 것을 정적 근거로 재확인한다(다른 프로세스·다른 D3D11CreateDevice).

### B. GNLink 대조표 (제품 소스, 고정 commit 을 적는다 — 권장: ka-r3 HEAD)
A 의 1~7 과 같은 질문에 GNLink 의 file:line 으로 답한다. 시작점은 아래와 같다:
- 장치 생성: `apps/native_poc/src/host_capture_device.cpp:419`. 상태 로그는 `:343-376`(d3d-mt). WGC 시작 뒤 state=on 이 되는 이유도 찾는다(누가 켜는지).
- 인코더 공유: `apps/native_poc/src/mf_h264_codec.cpp:1245-1248`(MFCreateDXGIDeviceManager/ResetDevice), `host_startup_graphics.cpp:153`.
- 프레임 경로: `libs/capture/src/capture_backend_dxgi.cpp:527-530`(Release phase), `d3d_capture_readback.cpp`(readback·d3dContextMu), acquireTimeoutMs=8.
- 장치를 만지는 모든 스레드: 캡처, MFT 비동기, GPU 스케일러, 커서, 썸네일 등.
- watchdog: `host_startup_capture.cpp`(3 s 경고, 5 s TerminateProcess 44). 격리 영속화(0.2.162 DW).
- 시험 seam `mt_ab_test_seam.hpp`(MT A/B arm)는 현재 상태만 적는다.
- **Microsoft 공식 문서**: IMFDXGIDeviceManager 또는 D3D11 장치를 MF 와 공유할 때 멀티스레드 보호에 대한 요구·권고를 찾아 URL 과 해당 문장 요지를 적는다. 없으면 없다고 적는다.

### C. 수정 목표 후보 (결정은 Codex·사용자; 여기서는 후보와 근거만)
각 후보를 근거, 기대 효과, 위험, 비용, 검증 방법(측정 가능한 acceptance)으로 정리한다:
- G-a 캡처 장치가 MFT 와 공유될 때 멀티스레드 보호를 켠다(최소 변경, A/B seam 있음).
- G-b 캡처 장치와 인코더 장치를 분리한다(공유 텍스처 또는 CPU 경유; OSLink 와 같은 분리).
- G-c 획득한 프레임을 복사한 뒤 즉시 ReleaseFrame 하고, 인코드는 복사본으로 한다(hold 시간 최소화).
- G-d 캡처 프로세스를 분리한다(작업목록 CS; wedge 가 나도 연결 유지).
- G-e 복구 시간 단축(현재 Host 재기동 13:27:47 → viewer 끊김 감지 13:27:58, 11 s 지연).
- 그 밖에 분석에서 나온 것.
- acceptance 예시: NVIDIA·Unity 전체 화면에서 DXGI N 시간 wedge 0, 또는 wedge 시 연결 유지·X 초 내 영상 복귀. 실기 측정 계획(회사 PC/홈 PC, 사용자 조율 필요)은 절차만 적는다.

## 규칙
- **OSLink 는 읽기 전용이다.** 대상 실행·주입·디버거·서비스/프로세스 조작은 하지 않는다. 정적 분석과 기존 로그만 쓴다. 코드 복제 목적이 아니라 설계 비교다.
- 저장소 밖에서는 **새 파일 생성만** 한다. 삭제·덮어쓰기는 하지 않는다. 분석 자식 프로세스의 TEMP/APPDATA 는 기존처럼 `work/` 아래로 한정하고, %TEMP% 에서 만들고 지우는 일이 없게 한다.
- 제품 코드는 수정하지 않는다. Host·GPU·AVD 실행도 하지 않는다. 측정 슬롯은 필요 없다.
- 새 도구 설치는 하지 않는다. 필요하면 이유를 적고 멈춘다.
- 산출: `C:\work\oslink-analysis\q9_dxgi_usage.md`(A) 와 `D:\remote\remote\.claude\report_dxgi_oslink_q9_r1.md`(A 요약 + B 대조표 + C 후보). 회신에는 두 파일의 sha256 과 핵심 5줄을 담는다.
- ka-pc 실 Host e2e(슬롯 해제 뒤 실행 예정)는 이 분석보다 우선한다. 슬롯이 풀렸다는 알림이 오면 그것을 먼저 한다.
