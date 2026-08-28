# OSLink 구조 분석 — 프로세스 경계와 전송 경로 (실측 2026-08-27~28)

대상: 이 개발 PC에 설치된 **OSLink 1.3.29.3** (`C:\LDPlayer\OSLink\`, XuanZhi Co.,Ltd. — LDPlayer 개발사).
계기: GNLink은 포트포워딩 없이는 붙지 않는데 OSLink은 붙는다는 사용자 관찰. 근거 대장 **E7**(`docs/구현계획.md`)의 상세 근거가 이 문서다.
성격: **경쟁 제품 실측 기록 + 우리 구조와의 대조**. 여기서 아무 것도 결정하지 않는다 — 채택 여부는 `구현계획.md`의 N/H 항목으로 승격할 때 정한다.

---

## 1) 측정 방법 (재현 절차)

| 무엇 | 어떻게 |
|---|---|
| 프로세스 트리·소유자 | `Get-CimInstance Win32_Process` + `GetOwner`, `ParentProcessId` 역추적 |
| 스레드 귀속 | `capture.log`가 찍는 TID를 `(Get-Process -Id N).Threads.Id`와 대조 |
| 소켓 | `Get-NetTCPConnection` / `Get-NetUDPEndpoint`를 PID로 필터 |
| 어느 소켓이 영상을 나르나 | **관리자 권한 `pktmon --capture` 8초** — 위에서 얻은 로컬 포트만 필터로 걸고 `etl2txt` 후 `PktGroupId`로 중복 제거(같은 패킷이 컴포넌트마다 기록됨) |
| IPC | `Get-ChildItem \\.\pipe\` |
| 권한 경계 | 서비스 정의(`Win32_Service`), `log\ldremoteservice.log` |

프로브 스크립트·ETL·임시 해제한 로그는 측정 후 삭제했고 `pktmon` 필터/세션도 원복했다. 이 측정에서 OSLink 파일을 수정한 것은 없다.

---

## 2) 프로세스 구조 — 권한 경계로 4개

```
services.exe
└─ ldremoteservice.exe   PID 4312    LocalSystem   스레드 4    21MB    서비스 "LDRemoteSvc"(표시명 OSLink), Auto
   └─ ldremoteevent.exe  PID 25064   SYSTEM        스레드 4    19MB    콘솔 세션에 재기동되는 이벤트 프로세스
      └─ capture.exe     PID 17956   SYSTEM        스레드 5    24MB    화면 캡처 전담 (DXGI Desktop Duplication)

ldremote.exe             PID 10480   사용자 계정   스레드 84  154MB    네트워크 전부 + RTC 엔진 + UI ("--byself")
```

### 역할이 이렇게 갈린다는 근거

- **소켓은 `ldremote.exe` 하나만 가진다.** `capture.exe`·서비스·이벤트 프로세스는 소켓 0개. 즉 캡처 프로세스는 네트워크를 모른다.
- **캡처는 `capture.exe` 소속이 맞다.** `capture.log`가 찍는 TID(예: 19792)가 `ldremote.exe`의 스레드 목록에는 없고 `capture.exe`에 있다. 로그 심볼도 `DxgiOutputDuplicator::Duplicate`, `MonitorCaptureImpl::loop`, `CapturePipeImpl::send`, `VideoFrameIPC::doInitIfNot`.
- **프레임은 명명 파이프로 건넌다.** `\\.\pipe\ld-winpipe-read-<n>` / `\\.\pipe\ld-winpipe-write-<n>` 쌍. 로그의 `VideoFrameIPC`가 그 계층 이름이다.
- **세션 전환을 서비스가 관리한다.** `ldremoteservice.log`에 `session change eventType:…`, `RunUnderWinLogon: physicalConsoleSessionId:2`, `CreateProcessAsUser success`, `checkSessionID: ldremoteevent session not right 7, 2`, `taskkill ldremoteevent sessionid` — **세션이 바뀌면 SYSTEM 쪽 자식을 죽였다가 새 세션에 다시 띄운다.** 그동안 사용자 세션의 `ldremote.exe`는 살아서 네트워크 연결을 유지한다.

### 엔진 구성

- RTC: **`ZegoExpressEngine.dll`(20.4MB)** — ZEGO 상용 RTC SDK. 로그 `log\zegoEngine.log\zegoavlog*`는 난독화되어 평문 정보가 없다.
- 캡처: `win-capture.dll`, `wjcapture.dll`, `remoteupload\logs\obs-sdk-c.*` — OBS 계열 캡처 코드 기반.
- 가상 디스플레이: 드라이버 `Virtual Display Driver 11.30.4.434` + 모니터 `Generic Monitor (VDD by MTT)`, 파이프 `\\.\pipe\MTTVirtualDisplayPipe`.

---

## 3) 전송 구조 — 리스닝 포트 0개

8초 캡처 창에서 관측된 전부:

| 흐름 | 패킷 / 바이트 | 대역 | 정체 |
|---|---|---|---|
| `UDP 로컬 61354 ↔ 211.218.222.4:55964` | 1,424 / 539KB (**전량 Tx**) | ≈550 kbps | **화면 영상 — 직접 P2P** |
| `TCP 로컬 54185 ↔ 47.86.3.37:7778` | 32 / 4.3KB (Tx 16 / Rx 16) | ≈4 kbps | 상시 시그널링 |
| `TCP 로컬 54850 ↔ 47.237.126.99:443` | 0 | — | 접속 시 API, 이후 유휴 |
| `TCP → 8.222.225.165:443` ×3 | — | — | 접속 초기 API, CloseWait |

- **양쪽 다 임시 포트**(61354 ↔ 55964)다. 서버라면 고정 포트를 쓴다 — 즉 상대는 뷰어 기기이고 **서버 릴레이를 타지 않는다**.
- `211.218.222.4`는 저장소에 **회사 공인 IP**로 기록된 주소(`구현계획.md` N3, history #268)이고 이 PC는 집 개발 PC(`175.209.236.194`)다. 그 구간에 직접 UDP 경로가 실재한다는 것이 E7이다.
- 47.x / 8.222.x는 알리바바 클라우드 대역(대역 기준 추정, 별도 조회는 하지 않음).
- **LISTENING 포트 0개.** 방화벽 규칙도 포트 지정 없이 `ldremote.exe` 프로그램 기준 Any/Any 허용 4쌍뿐이다. 인바운드로 받는 포트가 없으니 포트포워딩할 대상 자체가 없다.

---

## 4) GNLink 현재 구조와 대조

### 4.1 산출물과 실제 상주

| 실행 파일 | 역할 | 권한 | 실측 상주 |
|---|---|---|---|
| `GNLinkHost.exe` | 트레이/셸, 디렉터리 등록, 자식 감시 | 사용자(부모 `explorer.exe`) | **상주** — 스레드 3, 18MB |
| `GNLinkStream.exe` | **캡처 + 인코딩 + ABR + 컨트롤 + 송신 전부** | 사용자(부모 `GNLinkHost.exe`) | **상주** — 스레드 23, 861핸들, 157MB |
| `GNLinkCapture.exe` | GDI 캡처 워커 (`gdi_capture_process.cpp`, `host_capture_session.cpp:447`에서 조건부 기동) | 사용자 | 미기동 |
| `GNLinkInputService.exe` | 보안 데스크톱 입력 (서비스 `GNLinkSecureInput`) | LocalSystem | **Manual / Stopped** — 필요 시에만 |
| `GNLinkViewer.exe` | 클라이언트 | 사용자 | 별도 |

### 4.2 경계가 어디에 그어져 있나

| 축 | OSLink | GNLink |
|---|---|---|
| 캡처 | **항상 별도 프로세스**(SYSTEM 체인 아래) | `GNLinkStream` 내부. 별도 프로세스는 **GDI 폴백 전용** |
| 네트워크 | 사용자 프로세스 1개가 전부 소유 | `GNLinkStream`이 캡처·인코딩과 **같은 프로세스**에서 소유 |
| 세션/보안 데스크톱 | 서비스가 SYSTEM 자식을 죽였다 살리며 따라감 | 입력만 별도 서비스, 캡처는 같은 프로세스가 계속 담당 |
| 인바운드 포트 | 없음 | `UDP 43000` bind + `TCP 43001 LISTEN` |
| 프로세스 간 통신 | 명명 파이프 + `VideoFrameIPC` | (해당 없음 — 한 프로세스라 공유 메모리/락) |

### 4.3 한 프로세스에 몰린 대가 (호스트 코드 기준, 커밋 `ce0f360`)

- 런타임 스레드 **23개** / 핸들 861개.
- 코드상 스레드 생성 지점: supervisor · 자식 stdout reader(`host_app_main`) · encoded sender · DXGI 워치독 · 메인루프 워치독 · control · UDP reader · UDP control.
- 동기화 객체 선언: **mutex류 19개 / atomic 멤버 107개** (뷰어는 mutex 3 / atomic 72).
- 밀집 지점: `host_capture_session.hpp` (mutex 11 라인 / atomic 35), `host_encoded_sender.hpp` (atomic 22), `host_input_router.hpp` (atomic 22), `host_main_loop_mailbox.hpp` (mutex 11 라인).
- Phase 4에서 메일박스/스냅샷으로 상당수를 정리했는데도(`docs/호스트_분할_리팩터_계획.md` §7.2) 이 밀도가 남는다. **원인은 코드 스타일이 아니라 경계다** — 캡처·인코딩·네트워크·입력이 한 주소 공간에 있으면 그 사이는 전부 락 아니면 atomic이 된다.

---

## 5) 프로세스 경계 재설계 — 검토 결과 (코덱스 교차검증 2026-08-28)

**결론부터**: 방향은 타당하지만 **이유가 "스레드·mutex가 많아서"가 아니다.** OSLink의 사용자 프로세스도 스레드 84개다 — 개수는 구조 결함의 증거가 아니다.
분리의 근거는 셋이다: **fault domain**(GPU/MFT가 죽을 때 소켓까지 죽는 것) · **권한/세션**(보안 데스크톱·세션 전환) · **소켓/NAT 매핑 수명**(재시작해도 매핑을 잃지 않는 것).

또한 **프로세스 분리는 포트포워딩/NAT 문제를 고치지 않는다.** OSLink이 포워딩 없이 붙는 것은 ZEGO의 ICE/relay 계층 결과이지 4-프로세스 구조의 결과가 아니다. 그 목표는 N8(릴레이)/N 트랙에서 따로 다룬다. 이 문서의 §3과 §5는 **별개의 주제**다.

### 5.1 경계는 어디에 — 채택 권고: 캡처+인코딩 분리

| 안 | 경계를 넘는 것 | 판정 |
|---|---|---|
| ① 캡처만 분리 (OSLink 방식) | **NV12 텍스처** (D3D11 shared NT handle + keyed mutex/fence) | **보류** — 가장 어려운 seam. 아래 5.3 |
| ② **캡처+인코딩 분리 (권고)** | **H.264 AU + 메타**뿐 (0.07~1.5MB/s) | **채택 권고** |
| ③ 네트워크만 분리 | ②를 반대편에서 본 것 | ②로 흡수. 단 **소켓은 안정 프로세스가 소유**해야 하므로, 현 `GNLinkStream`을 미디어로 두고 네트워크 자식을 붙이는 방향은 소켓 소유자 수명을 오히려 불안정하게 만든다 |

**②의 구조 (권고안)**

- `GNLinkStream` = **broker(안정)**: 디렉터리/릴레이, UDP/TCP 소켓, UdpControl, media epoch/barrier, 입력·컨트롤 라우팅, 클라이언트 메트릭, 워커 감시.
- `GNLinkMediaWorker`(신규, **재시작 가능**): WGC/DXGI/GDI, readback/GPU 스케일, frame gate/kick, MFT 인코더, SPS/PPS/IDR 생성.
- worker→broker IPC: `EncodedFrameHeader` 상당 메타(seq/generation/size/key/capture·encode 타임스탬프) + AU.
- broker→worker IPC: start/stop, 타깃·백엔드 선택, 유효 bitrate/fps/keyint/size, force-IDR, 1Hz 네트워크 피드백 스냅샷.
- `GNLinkInputService`는 보안 입력 전용 그대로.

**②를 고르는 이유**

- **원시 픽셀이 경계를 넘지 않으므로** shared texture/adapter LUID/keyed mutex/드라이버 매트릭스 문제를 통째로 회피한다.
- AU는 0.55~12Mbps(≈0.07~1.5MB/s)라 IPC 복사 비용이 원시 1080p60과 비교 대상이 아니다. 단 **IDR은 수백 KB 버스트**라 인코더 스레드가 blocking write에 묶이지 않게 bounded 큐가 필요하다.
- D3D/DXGI/WGC/MFT의 크래시·행이 worker 재시작으로 격리되고 **broker의 소켓/NAT 매핑은 유지**된다. 지금은 supervisor가 `GNLinkStream` 전체를 죽여 매핑까지 잃는다.
- ABR 왕복은 blocker가 아니다. 현재 ABR도 1초 창의 클라이언트 메트릭/캡처 통계를 쓴다 — worker가 캡처 통계를, broker가 네트워크 통계를 1Hz 스냅샷으로 교환하면 된다. 긴급 keyframe/tune도 소형 IPC 1회.
- 정책 소유: **broker가 대역 목표를 정하고 worker가 적용 후 유효값을 회신**하는 방향(권고).

**②의 함정 (명문화 대상)**

- AU 파이프가 막힐 때 캡처/인코딩을 블록시키면 분리 효과가 사라진다. **bounded latest-wins 큐 + delta drop 시 체인 무효화 + IDR 요청 + key AU 보존**을 현재 `SenderQueueAction`과 동일 정책으로 못박아야 한다.
- 경계는 락을 **없애는 게 아니라 프로토콜/큐/백프레셔로 바꾼다.** correctness가 저절로 쉬워지지 않는다.

### 5.2 기존 `GNLinkCapture.exe` 일반화 — 권고: 하지 않는다

수명 관리 코드는 **추출해서 재사용**할 가치가 있다(`gdi_capture_process.cpp:147~175` Stop/terminate/join/handle 정리, `:183~296` CreateProcess suspended → job kill-on-close → resume → reader 스레드, 프로세스 종료 감시와 폴백 콜백, 3-slot latest-wins 개념) → 공용 `ChildProcessSupervisor` 유틸로 뽑는다.

그러나 **프로토콜과 워커 본체는 재사용 금지**다. 코드에서 확인한 전제:

- `gdi_capture_protocol.hpp:21~50` — 고정 크기 **원시 BGRA** 3-slot 공유 메모리(`SharedHeader{width,height,stride,frameBytes,slots[]}`).
- `gdi_capture_process.cpp:107~117` — 부모가 **매 프레임 새 vector 할당 + `memcpy`**.
- `gdi_capture_worker_main.cpp:97~105`(primary monitor 고정 geometry), `:118~130`(mapping-backed DIB), `:170~186`(`BitBlt`).
- 공유 객체 이름이 `object_name()` 기준 **`Localemote60_gdi_*`** + `SECURITY_ATTRIBUTES` nullptr(기본 ACL) — **세션 로컬**이라 Session 0 서비스 ↔ 콘솔 세션 경계로 그대로 확장되지 않는다.
- 컨트롤 채널이 stop/frame 이벤트뿐이라 select/tune/keyframe/ABR/reconfigure/AU framing/버전 협상이 없다.

→ **신규 `GNLinkMediaWorker.exe` + 버전 있는 미디어 IPC**를 만들고, GDI 백엔드는 그 워커 내부 백엔드로 옮겨 **원시 BGRA IPC 자체를 없앤다.**

### 5.3 ① 캡처만 분리(원시 GPU 공유)는 왜 보류인가

가능은 하다 — consumer 절반은 이미 있다(`mf_h264_codec.cpp:1147~1162` `IMFDXGIDeviceManager`, `:1522~1525` `MFT_MESSAGE_SET_D3D_MANAGER`, `:1703~1745` `MFCreateDXGISurfaceBuffer`, `host_stage_encode_send_h264.cpp:289~300` surface 입력). producer가 `D3D11_RESOURCE_MISC_SHARED_NTHANDLE`(+`SHARED_KEYEDMUTEX`)로 만들고 `IDXGIResource1::CreateSharedHandle` → consumer가 `ID3D11Device1::OpenSharedResource1`로 열면 된다.

그런데 이 경로에 걸린 조건이 많다:

- WGC/DXGI가 돌려준 캡처 텍스처가 shareable로 생성됐다고 **가정할 수 없다.** 앱이 만든 shareable ring으로 `CopyResource`/`VideoProcessorBlt`해야 하므로 "CPU 복사 0"은 되어도 **"GPU 복사 0"은 아니다.**
- shared resource는 **같은 어댑터에서만** 열린다. RDP/가상 디스플레이/하이브리드 GPU에서 두 프로세스가 동일 LUID로 device를 만든다는 계약이 필요하다.
- surface 경로 자체가 드라이버 의존이다 — 코드 주석의 실측: **AMF가 샘플을 받아놓고 프레임당 ~68ms(내부 동기화), 같은 조건 CPU 경로 4.5ms.** 그래서 30프레임 프로브 후 평균 16ms 초과면 세션 단위로 꺼버린다(`host_stage_encode_send_h264.cpp:302~317`). **공유 핸들이 된다는 것과 빠르다는 것은 별개다.**
- **MFT는 `ProcessInput` 반환 후에도 텍스처를 비동기로 보유한다.** 동일 프로세스인 지금도 그래서 `nv12PendingReleases`가 output 수로 소비를 증명할 때까지 슬롯을 반환하지 않는다(`host_stage_encode_send_h264.cpp:296~300`, `:363~367`). 프로세스를 가르면 이 release ack를 **IPC로 되돌려야** 하고 shared ring이 최소 3(실기 4~6 후보) 필요하다. keyed mutex 하나를 MFT output까지 쥔 단일 버퍼 설계는 캡처를 프레임 단위로 막는다.
- 판정 지표: `present gap p95 65ms`는 이 경계를 판단할 수 없다. capture-copy→consumer-acquire, acquire wait, ProcessInput→slot-release, AU IPC enqueue를 각각 찍어 동일 프로세스 대비 delta를 A/B해야 한다.

### 5.4 권한/세션은 별도 게이트

- **"LocalSystem 서비스에서 WGC"가 아니다.** 서비스는 Session 0이고, Microsoft 권고도 서비스가 직접 대화형 데스크톱에 접근하지 말고 `CreateProcessAsUser`로 활성 콘솔 세션에 에이전트를 띄우라는 것이다. OSLink의 `ldremoteservice → ldremoteevent → capture.exe` 체인이 정확히 그 패턴이다(§2 실측).
- 토큰이 SYSTEM이어도 Terminal Services 세션/윈도우 스테이션이 틀리면 실패한다. 반대로 **지금 사용자 세션 워커를 떼어내는 데 SYSTEM은 필요 없다.**
- **보안 데스크톱/UAC 캡처는 별개 문제다.** SYSTEM + 콘솔 세션이라고 WGC가 Winlogon secure desktop을 캡처한다는 보장이 없다. 기존 `E_ACCESSDENIED` 경로와 secure agent 설계를 별도 게이트로 유지한다.

### 5.5 시점 — 지금은 ADR + spike까지만

**0.2.59 실기 안정화 전 production 분리 착수는 반대**(코덱스·클로드 합의). 호스트/뷰어 분할 + Phase 4 직후라 baseline 자체가 아직 실기 확인 전이고, 여기에 프로세스 분리를 얹으면 회귀 귀속이 불가능해진다. 열린 부채(H-24/H-25/H-27, fault-injection)도 0.2.59에서 어떤 실패가 남는지 봐야 seam 우선순위가 정해진다.

권장 단계:

0. **0.2.59 실기**: 게임 로드 / UAC / RDP / 정적 화면 / PC↔모바일 / 재접속, 최소 30~60분 soak. 호스트+뷰어 로그 보존.
1. **ADR 1장**: 소켓 소유자, 워커 권한/세션, IPC 메시지 목록, epoch/generation 소유, 백프레셔·드롭 정책, 크래시 복구, 버전 협상.
2. **encoded-AU IPC spike** (동일 사용자·동일 콘솔 세션, SYSTEM 금지): broker가 UDP 소켓 계속 소유 · 워커 강제 크래시/행 후 broker·세션 유지 → 워커 재시작 → IDR → 화면 회복 · 1080p60 12Mbps + 큰 IDR에서 파이프/백프레셔 측정 · 동일 프로세스 대비 capture→wire/present p50·p95와 CPU/메모리 무회귀.
3. **권한/세션 분리**는 그 다음: 서비스가 활성 콘솔 세션 에이전트 관리, WTS 세션 변경/RDP/빠른 사용자 전환, 명시적 ACL·핸들 복제(공개 named object 금지).
4. **capture-only GPU 공유**는 필요성이 남을 때 별도 spike(어댑터 LUID 일치, shareable 텍스처 생성/열기, WGC/DXGI→shared ring GPU 복사, keyed mutex/fence + MFT deferred release, 드라이버 폴백 매트릭스).
5. feature flag 이중 경로로 실기 A/B 후 in-process 경로 제거 여부 결정.

### 5.6 합의 / 미합의

| 논점 | 판정 |
|---|---|
| "지금 구조가 영구적으로 맞다" | **반대**(양측) — GPU/MFT 실패가 소켓 매핑까지 죽이는 현재 fault domain은 장기적으로 분리 가치가 크다 |
| "지금 바로 OSLink처럼 capture.exe를 확장" | **반대**(양측) |
| "0.2.59 안정화 후 broker + capture/encode 미디어 워커, AU IPC" | **찬성**(양측) |
| "Q1(원시 shared texture)이 안 되면 논의가 무의미" | **클로드의 최초 프레이밍이 틀렸다** — ②의 선결조건이 아니라 ①의 별도 연구 게이트다 |
| "스레드·mutex 개수가 분리 근거" | **틀렸다** — 근거는 fault domain·권한·소켓 수명 |

## 6) ADR 초안 — 미디어 워커 분리 (코덱스 2라운드 합의, 2026-08-28)

**상태: 초안. 승인 전.** 채택은 `구현계획.md`의 N/H 항목 승격으로 결정한다. 아래 6줄이 ADR 본문이고, 6.1~6.3이 그 근거·절차다.

1. **논리 세션 identity(network epoch / stream generation / wire seq / media barrier)는 broker 단독 소유.**
2. **worker identity는 private `workerIncarnation`이며 wire에 노출하지 않는다.**
3. **워커 재시작은 같은 generation의 투명 IDR 복구**다 — 피커를 다시 띄우지 않는다.
4. **frame provenance는 IPC v1 필수 필드**이고, 현재 wire의 SyntheticRefresh 비트가 그보다 선행한다.
5. **성능 게이트는 물리 콘솔 baseline을 뽑은 뒤 사전 동결**한다. RDP는 성능이 아니라 별도 기능 회귀 suite다.
6. **production 착수는 0.2.59 실기 통과 후.**

### 6.1 R1 — 워커 재시작은 generation을 올리지 않는다

근거(코드 확인):

- `viewer_selection_gate.hpp:13~30` — generation은 **"사용자가 승인한 논리 타깃"의 identity**다. 워커 프로세스 수명과 다른 개념이다.
- `:63` `activeStreamGeneration`은 reveal 이후 **영구 필터**다. 워커 재시작만으로 generation을 올리면 새 프레임이 `SelectionAdmit::DropStraggler`(`:54`)로 버려진다. "새 generation" 안은 새 `WindowSelected` ack/선택 트랜잭션 없이는 성립하지 않는다.
- `host_stage_encode_send_h264_au.cpp:235` — `hdr.seq = ++encoder.encodedSeq`. **wire seq가 인코더에서 나오므로** 워커가 죽었다 살아나 seq=1로 돌아오면 같은 generation 안에서 UDP assembly/최신 프레임 판정/telemetry가 이전 seq 공간과 충돌한다. → **wire seq도 broker 소유**로 옮긴다. IPC 프레임은 `{workerIncarnation, localFrameId, brokerGeneration, ...}`으로 올리고 broker가 현재 incarnation인지 검사한 뒤 단조 wire seq를 새로 부여한다.

**투명 재시작 트랜잭션 (broker 주도)**

1. `workerIncarnation++`
2. 송신 AU 큐 비우고, **같은 network epoch에서** media barrier close(`waitingForKey=true`)
3. 옛 incarnation의 AU/청크/IPC completion 전부 drop
4. 현재 타깃·설정·generation + `ForceIDR`를 새 워커에 replay
5. **SPS/PPS 포함 key AU만 첫 프레임으로 accept** — 그 전 delta는 broker에서 drop
6. key enqueue/wire 실패 시 현재 `SenderBarrier` 규칙대로 barrier 재arm
7. key가 도착하면 클라이언트 디코더가 같은 generation에서 IDR로 DPB 재동기화 — **피커는 건드리지 않는다**

**generation을 올려야 하는 경우는 둘뿐**: 사용자가 다른 타깃/모니터/창을 선택했을 때, broker가 논리 타깃 identity를 바꿀 때. 워커 재기동·같은 타깃 재attach·인코더/디바이스 재생성은 generation을 유지한다. 해상도만 바뀌면 지금처럼 같은 generation의 SPS/크기 변경 경로를 쓰되 클라이언트 reconfigure 게이트를 그대로 검증한다.

**H-26/H-27과의 접점**

- `forceKeyNext`/kick/cache는 워커 내부 상태여도 되지만 **"IDR가 아직 필요하다"는 barrier는 broker 권위**다. 워커가 죽으면 broker가 barrier를 닫고 `ForceIDR`를 다시 보내므로 워커 로컬 force 상태의 유실은 안전하다.
- 워커 down 중 뷰어의 keyframe 요청은 broker가 coalesce했다가 새 워커 Ready 뒤 전달한다.
- **IPC 커맨드에는 network/client epoch(또는 broker request generation)을 stamp**해서, H-27의 옛 in-flight 커맨드가 새 워커/세션에 적용되지 않게 한다.
- 선택 진행 중 워커가 죽으면 broker가 **같은 pending selection generation으로 재시도**하고 성공 IDR까지 기존 피커 대기를 유지한다. 임의의 새 generation 금지.
- 워커 재시작 직후에는 옛 raw 캐시가 없다. **새 캡처 백엔드가 현재 데스크톱 seed 프레임을 내는 게이트가 별도로 필요**하다 — "첫 전달 AU는 key" 규칙만으로는 정적 화면에서 워커가 프레임을 영영 못 얻는 경우를 못 고친다.

### 6.2 R2 — SyntheticRefresh는 IPC v1 freeze보다 먼저

근거: `poc_protocol.hpp:130` — `uint32_t flags = 0;  // bit0: keyFrame`. 현재 wire에 **키프레임 비트 하나뿐**이고 `host_stage_encode_send_h264_au.cpp:239`(`hdr.flags = encodedKeyFrame ? 1u : 0u`)도 그것만 싣는다. 분리 후 broker는 AU 바이트/크기만으로 합성 여부를 복원할 수 없으므로, **워커가 origin을 발행하지 않으면 지금의 집계 오염이 프로토콜 계약으로 굳는다.** `HANDOFF.md:45,51~54`가 이미 P2 선행 게이트로 못박아 둔 항목이다(F-10).

순서: **0.2.59 실기 baseline 확인(설치본 불변)** → 독립 커밋으로 현재 wire에 end-to-end SyntheticRefresh 구현·검증 → 그 semantic을 IPC v1 필수 provenance 필드로 넣고 미디어 워커 spike 시작.

스키마 권고:

- IPC는 bool이 아니라 **`FrameOrigin { RealCapture, TrailingKick, StaticRefresh, RecoveryBootstrap }`**.
- wire는 상수를 정의해 `bit0=KeyFrame`, `bit1=SyntheticRefresh`로 매핑.
- `servedBootstrap`은 trailing kick과 static refresh를 함께 뜻하므로 **그대로 bit1의 원천으로 쓰지 않는다**. TickContext/워커 프레임에 origin을 명시한다.
- 클라이언트는 synthetic refresh를 present/디코더 liveness에는 쓰되 **latency·catchup·anomaly·real-fps 근거에서 제외**하고, wire 바이트/디코더 비용 통계에는 포함한다.
- **워커 재시작 첫 IDR은 SyntheticRefresh로 뭉개지 않는다** — `RecoveryBootstrap`으로 구분한다(필요하면 별도 wire 비트).

### 6.3 R3 — 성능 게이트: 두 suite, 물리 콘솔 baseline 후 동결

**선행(fail-closed preflight)**: RDP 접속 해제만으로 부족하다. 물리 콘솔 로그인 후 ① 활성 디스플레이 경로가 실제 GPU 출력인지 ② 호스트 로그가 `desktop_backend=dxgi`인지 ③ `dxgi_select_no_outputs`/WGC 폴백이 없는지 확인하고, OSLink 등 다른 원격 캡처 도구를 종료한 뒤 **동일한 결정적 모션 씬**으로 측정한다.

**A. steady-state** (예: 60초 × 5회, 물리 콘솔)

| 지표 | 임계(시작점 — baseline 분산 확인 후 spike 전 동결) |
|---|---|
| captureQpc→broker wire p50/p95 | baseline + max(10%, 2ms) |
| client present gap p50/p95/max + over1.5x/over2x 건수 | 〃 (max·over2x는 별도로 본다) |
| capture/encoded/sent/decoded fps | baseline의 95% 이상 |
| broker+worker 합산 CPU time (순간값 아닌 delta/wall) | baseline + max(10% 상대, 2%p 절대) |
| 합산 working set/commit | **절대 예산 명시**(새 프로세스 고정비가 있어 상대값만 쓰면 안 됨) |
| AU IPC queue depth/max/drop, key·delta drop, enqueue→dequeue p95 | 정상 부하에서 **drop = 0** |
| bitrate/wire bytes, keyframe 빈도 | 회귀 없음 |

> 10%만 고정하면 baseline이 작은 지표가 노이즈에 지고, 절대값만 쓰면 큰 지표의 회귀를 숨긴다. 그래서 상대·절대를 함께 쓴다.

**B. fault-recovery** (kill/hang 주입 최소 10회)

구간별 시계: 워커 exit/hang 감지 → 새 프로세스 spawn → 워커 Ready(타깃/디바이스/인코더) → 첫 key AU broker accept → 첫 key wire → 클라이언트 첫 디코드/present.

| 게이트 | 값 |
|---|---|
| 첫 present 복귀 | **p95 ≤ 1.0s 목표, max ≤ 1.5s hard** |
| 네트워크 epoch/UDP peer/NAT 매핑 | 유지, control ping 지속 |
| 피커 노출 / active generation | 노출 0 / 불변 |
| 첫 전달 AU | key여야 함. pre-key delta = 0, stale incarnation AU = 0 |
| 디렉터리 재등록 / 릴레이 재협상 / 클라 재접속 | 0건 |
| 자동 복구 | 10/10, 반복 kill 후 핸들·메모리 증가 없음 |

> 1.5s는 `viewer_constants.hpp:37`의 `kCongestionRecoveryTimeoutUsDefault`와 같은 값이다. "그때까지 괜찮다"가 아니라 **그 전에 끝나야** 클라이언트의 추가 key 요청/리셋 churn을 피한다는 뜻이다.
> present gap p95는 1회의 1.5초 blackout을 숨길 수 있으므로 **recovery duration과 max/over2x를 반드시 따로 본다.**

## 7) 한계 / 미확인

- ZEGO 엔진 로그가 난독화되어 있어 **NAT 종류 판정·릴레이 폴백 조건·암호화 방식은 확인하지 못했다.**
- 최초 홀펀칭이 성립한 순간은 캡처 창(연결 이후 8초) 밖이라 관측하지 못했다. 시그널링 채널이 상시 유지된다는 사실만 확인.
- 피어 `211.218.222.4`가 저장소에 기록된 회사 공인 IP와 일치하지만, 그 세션의 물리적 위치를 직접 확인하지는 않았다.
- `ldremote.exe`의 모듈 목록은 비트니스/권한 문제로 열거하지 못했다(파일 시스템의 DLL 구성으로 대체).
- 측정 시점(2026-08-27 21:43)의 세션은 **2026-08-28 10:05:12에 종료**됐다(`capture.log` 마지막 줄 `capture:0 0 0 0`, 미디어 소켓 소멸). 이후 수치는 유휴 상태 기준이다.
- **측정 규칙 재확인**: 이 PC에는 가상 디스플레이가 3개(Parsec / OSLink VDD by MTT / Microsoft Remote Display) 있고 RDP 접속 중에는 `Microsoft Remote Display Adapter 2236x1232`가 활성이다. `구현계획.md` A1이 경고하는 그 상태이므로 **원격 접속 중에 캡처율·fps를 재면 안 된다.**
