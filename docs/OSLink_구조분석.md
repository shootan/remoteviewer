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

## 5) 프로세스 분리로 실제로 얻는 것 / 잃는 것 (검토 후보, 결정 아님)

**얻는 쪽**

1. **동기화 객체가 IPC 경계로 대체된다.** 캡처↔인코딩을 프로세스로 가르면 그 사이 공유 상태는 프레임 큐 하나로 줄고, 지금 그 경계에 있는 mutex/atomic이 통째로 사라진다.
2. **크래시 격리.** 캡처 백엔드(WGC/DXGI/GDI)는 드라이버·세션 상태에 물려 있어 가장 잘 죽는 부분인데, 지금은 죽으면 네트워크 연결까지 같이 죽는다. OSLink은 `capture.exe`만 죽었다 살아난다.
3. **세션/보안 데스크톱 대응이 구조로 풀린다.** 잠금화면·UAC·세션 전환은 "SYSTEM 자식을 새 세션에 다시 띄우는" 문제인데, 사용자 프로세스가 캡처를 들고 있으면 그 프로세스 자체를 옮겨야 한다.
4. **권한 최소화.** 네트워크 프로세스는 SYSTEM일 필요가 없고, 캡처 프로세스는 소켓이 필요 없다.

**잃는 쪽 / 비용**

1. **프레임 IPC 비용.** 1080p60이면 초당 수백 MB — 명명 파이프로 픽셀을 복사하면 답이 없다. 공유 메모리(또는 D3D11 shared handle/NT handle 전달)로 **0-copy**를 유지해야 하고, 그렇지 않으면 지금의 지연 예산이 무너진다. **이게 채택 여부를 가르는 핵심 질문이다.**
2. **수명 관리가 늘어난다.** 자식 크래시 감지·재기동·중복 기동 방지·좀비 정리. 호스트 원장 H-01(프로세스 핸들 소유권)·H-06(detach 워치독)이 이미 이 종류의 버그였다.
3. **e2e·측정 복잡도.** 로그가 프로세스별로 갈리고, 지연 계측이 프로세스 경계를 넘는다.
4. **리팩터 직후다.** Phase 4까지 막 끝낸 구조를 다시 가르는 것이라, 지금 하면 그 회귀 원인 분리가 어려워진다.

**따라서 답해야 할 질문 (코덱스 상의 대상)**

- Q1. 프레임 전달을 0-copy로 유지할 수 있나? D3D11 텍스처를 프로세스 간에 넘길 때(shared NT handle) 지금의 readback/인코더 경로가 그대로 성립하나?
- Q2. 가른다면 경계를 어디에 두나 — ①캡처만 분리 ②캡처+인코딩 분리 ③네트워크만 분리?
- Q3. 이미 있는 `GNLinkCapture.exe`(GDI 워커)를 일반화하는 길이 가장 싼가?
- Q4. 지금 해야 하나, 아니면 실기 안정화 뒤인가? 선행 조건은?

---

## 6) 한계 / 미확인

- ZEGO 엔진 로그가 난독화되어 있어 **NAT 종류 판정·릴레이 폴백 조건·암호화 방식은 확인하지 못했다.**
- 최초 홀펀칭이 성립한 순간은 캡처 창(연결 이후 8초) 밖이라 관측하지 못했다. 시그널링 채널이 상시 유지된다는 사실만 확인.
- 피어 `211.218.222.4`가 저장소에 기록된 회사 공인 IP와 일치하지만, 그 세션의 물리적 위치를 직접 확인하지는 않았다.
- `ldremote.exe`의 모듈 목록은 비트니스/권한 문제로 열거하지 못했다(파일 시스템의 DLL 구성으로 대체).
- 측정 시점(2026-08-27 21:43)의 세션은 **2026-08-28 10:05:12에 종료**됐다(`capture.log` 마지막 줄 `capture:0 0 0 0`, 미디어 소켓 소멸). 이후 수치는 유휴 상태 기준이다.
- **측정 규칙 재확인**: 이 PC에는 가상 디스플레이가 3개(Parsec / OSLink VDD by MTT / Microsoft Remote Display) 있고 RDP 접속 중에는 `Microsoft Remote Display Adapter 2236x1232`가 활성이다. `구현계획.md` A1이 경고하는 그 상태이므로 **원격 접속 중에 캡처율·fps를 재면 안 된다.**
