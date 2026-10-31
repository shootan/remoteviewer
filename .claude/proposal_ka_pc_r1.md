> **대체됨 → `proposal_ka_pc_r2.md`(2026-10-27, Codex codex_ka_pc_design_r1.md 반영).** 아래는 r1 원문 보존.

# proposal ka-pc-design r1 — PC 클라이언트의 최근 PC 3대 유지 (KA ③) (작업용2, 2026-10-27)

> task ka-pc-design / r1 / kind=design (t-4k5y2g3r). 문서만 — 제품 코드 0, 시험 실행 0.
> **구속 계약**
> - `codex_keepalive_r2.md` §1·§4·§6·§7
> - `codex_keepalive_r2_kept_model.md`(모델 A)
> - `task_keepalive_r2.md`('10-27 ② 판정 취합')
>
> 공용 standby 코드는 `spike/host-admission-r3` 0a5e71d 기준이다. 셸·뷰어 소스는 이 범위에서 main 과 같다(조사로 diff 0 확인). 줄 번호는 0a5e71d 기준이다.
> 결정이 필요한 곳은 **[결정 n]** 으로 표시하고 §9 에 모은다.

## 0) 요약

- **lease 소유자**: 호스트별 **managed Viewer 프로세스**가 자기 StandbyLease 를 갖는다(Codex §4 의 구조, 모델 A 의 kept = StandbyLease).
  - 셸은 목록·정책·자식 수명·IPC 만 맡는다.
  - 셸이 lease 를 갖고 탭 때 소켓을 뷰어로 넘기는 안은 대안으로 비교한다(§1).
- **새 뷰어 모드 `--managed-standby`**: 창·디코더·MF·D3D 없이 lease 만 유지한다.
  - **탭** → 셸이 IPC 로 Activate → 뷰어가 activation handshake 와 창·디코더 초기화를 **병렬로** 시작 → picker 표시.
  - **목록 복귀(X)** → 세션 fence → SessionOver → 같은 소켓으로 다시 standby.
- **자료 경로**
  - 비밀(세션 token 등)은 명령줄·환경변수가 아니라 **상속 익명 pipe** 로 넘긴다.
  - 셸 crash 때는 **Job object(KILL_ON_JOB_CLOSE)** 가 자기 자식만 회수한다.
  - 최근 3대는 `%LOCALAPPDATA%\GNLink\` 에 wide API 로 저장한다(키 = directory origin + account stable ID + hostId).
- **미지원 대상**: 구 Host·prelogin·지원 unknown 은 자동 standby 를 만들지 않는다. 목록 복귀 때 "상시 유지 미지원" 을 알리고 세션을 종료한다.

## 1) lease 를 어느 프로세스가 갖는가

| | O1 **호스트별 managed Viewer 가 lease 소유** (제안) | O2 셸이 lease 3개 소유, 탭 때 소켓을 뷰어로 이관 | O3 셸 단일 프로세스 다중 세션 |
|---|---|---|---|
| Codex 구조 | §4 "셸이 관리하는 호스트별 Viewer, 합계 3" 그대로 | §4 와 다름(구조 변경) | §4 가 **기각** |
| activation | 같은 프로세스 안. `StandbyLease::ActivationArgs` 의 핸들 복제(`standby_lease.hpp:192-200`, `GetCurrentProcessId`)와 handshake 람다(this 캡처, :253-302)를 그대로 쓴다 | 다른 프로세스로 `WSADuplicateSocketW(targetPid)` 이관이 필요하다. handshake 를 셸이 하고 HelloAck 뒤 소켓을 넘기거나, 뷰어로 옮겨야 한다. 세션이 끝나면 소켓을 반납받아야 한다 | — |
| 탭 → 화면 | 프로세스가 이미 떠 있다. 창·디코더 init 과 handshake 를 병렬로 한다 | 탭마다 뷰어 cold start(WSA·창·MF·D3D) 뒤 handshake | — |
| 상시 비용 | standby 뷰어 프로세스 최대 3개(창·디코더 없음, lease 스레드 1개) — **측정 필요** | 셸 안 스레드 3개, 추가 프로세스 0 | — |
| 장애 격리 | 한 자식 crash 는 그 PC 만 잃는다(Codex §7 "한 자식 실패가 다른 2대 보존") | 셸 crash 는 3대 모두 잃는다. 이관 중 소유 경계가 복잡하다 | — |
| 세대 카운터 | 프로세스가 여럿이다 → **[결정 1]** | 셸 하나라 자연스럽다 | — |

→ **O1 을 제안한다.** 근거:
- Codex §4 의 승인 구조와 기존 `gViewerByHost`(`client_shell_main.cpp:78-118`)를 잇는다.
- 공용 lease 의 소유·handshake 계약(같은 프로세스)을 바꾸지 않는다.
- 탭 → 화면 경로가 짧다.

O2 의 유일한 이점은 상시 메모리다. §8 비용 측정에서 standby 뷰어 하나가 셸 대비 무시할 수 없으면 다시 검토한다(기준 제안: 3개 합계 private bytes 가 셸 1개를 넘으면 재검토).

## 2) managed Viewer 의 모드와 수명

**시작**: 셸이 `GNLinkViewer.exe --managed-standby --ipc-pipe <상속 핸들 번호>` 로 띄운다.
- 비밀은 pipe 로만 전달한다: directory url·session token·hostId·observe endpoint·key·child incarnation·owner epoch.
- Codex §4: 새 경로는 token 을 argv/env/로그에 추가하지 않는다. 기존 일반 연결의 `--directory-session` 명령줄(`:1437-1460`)은 이번 범위 밖으로 그대로 둔다.
- 셸은 이미 상속 allowlist 를 쓴다(`:1568-1571`). 여기에 pipe 두 끝을 더한다.

**standby 중**:
- 창·`init_decoder`·MF·D3D·picker·입력 채널을 만들지 않는다. 지금은 `create_window` 가 연결 전에 `ShowWindow` 한다(`viewer_window_proc.cpp:~1775`). Codex §4 는 띄운 뒤 숨기는 방식을 불충분하다고 했다.
- 하는 일은 `StandbyRunner`(키 1개) 실행과 IPC 상태 보고뿐이다.

**활성 (탭)**:
1. 셸이 `Activate{key, incarnation, epoch, opId}` 를 보낸다.
2. 뷰어가 이것을 검증하고 `TakeActivation` 을 호출한다.
3. 동시에 창·디코더 init 을 시작한다(network 와 UI init 이 서로 기다리지 않는다).
4. 세션 연결 경로를 바꾼다. 지금 뷰어는 자체 `udp_hello_handshake` 를 쓴다(`viewer_startup.cpp:665-690`). activation 일 때는 lease 가 준 복제 소켓과 `udpHandshake` 를 쓰고, 그 뒤 control tunnel·receiver·pump 는 기존 경로다. 이것은 `ClientSessionController::ConnectUdpVideo` 가 하는 것과 같은 교체를 뷰어 쪽에 두는 일이다.
5. `--initial-view targets` 와 같이 picker 로 시작한다. 과거 화면을 자동 복원하지 않는다.
6. 세션 범위 상태(UA-2 backend 선택 등)는 Host 기본값부터 다시 시작한다.

**목록 복귀 (managed 창 X)** — 모델 A 의 '목록으로 나가기 1':
1. 입력 release, clipboard·선택 작업 차단, decoder unbind 같은 기존 화면 이탈 fence 를 수행한다.
2. 세션 receive/handshake 가 끝난 것을 확인한다.
3. `SessionOver` 를 호출한다 → 같은 소켓으로 Ping 을 재개한다.
4. 창과 디코더를 **해제**한다(기본값). 숨긴 채 유지하는 안은 재활성 시간과 비용을 측정한 뒤 고른다 **[결정 2]**.
5. Host 가 아직 active 로 보는 구간은 `ReturningToStandby` 로 표시하고, 유한 시간 안에 확인한다.
- standalone 뷰어(셸 밖 실행)의 X = 종료는 그대로 둔다.

**'이 PC 연결 해제'**: Release → 자식 종료. 그 key 의 자동 유지는 셸 수명 동안 억제한다(Codex §1). 명시적으로 다시 접속하면 억제가 풀린다.

**한 프로세스 = 한 host key.** 같은 host 의 중복 뷰어 금지 규칙(`:1360-1420`)은 유지한다.

## 3) 셸 ↔ 뷰어 IPC (새로 만든다)

현재는 명령줄, 상속 cancel event(셸 → 뷰어), stdout pipe(로그·restore target), 종료 코드뿐이다. 양방향 통로가 없다.

**전송**: 상속 익명 pipe 한 쌍. 길이 접두 + 고정 레코드다. HWND 검색·전역 PostMessage 는 쓰지 않는다(Codex §4).

**셸 → 뷰어**
- `Init{secrets…}`
- `Activate{opId}`
- `Park{opId}`
- `Release{opId, reason}`
- `TokenRenewed{token}`
- `Shutdown{deadlineMs}`

**뷰어 → 셸**
- `State{key, incarnation, opId, state, entryKnown, lost, reason}`
- `NeedToken`
- `Presented{opId}` — 첫 정상 프레임
- `PickerReady{opId}` — 대상 UI 사용 가능
- `SessionEnded{reason: user / takeover / network / host}`

**검증**: 모든 메시지에 key + child incarnation + owner epoch + opId 를 싣고, 셸이 현재 값과 다르면 버린다(늦은 Provide·SessionOver fence, 모델 A).

**token 갱신**: 셸이 401 을 받고 `come_back` 으로 token 을 갱신하면(`:1178-1185`) `TokenRenewed` 를 보낸다. lease 의 capability 재획득(`StandbyDirectoryConfig.session` getter, `standby_directory.hpp:23-32`)은 최신 token 을 쓴다.

**observe endpoint**: 지금은 뷰어만 `directory_observe_from_health` 를 부른다(`viewer_startup.cpp:356`). managed 뷰어도 그대로 자기가 구한다. 셸은 넘기지 않아도 된다.

## 4) 상태 모델과 셸 UI

**state**(모델 A): `PreparingStandby / StandbyReady / Activating / Active / ReturningToStandby / Unsupported / Retry(Waiting, 다음 시도 시각)`. 여기에 셸 쪽 상태 `Suppressed`(해제함)를 더한다.

**PC 카드 표시**(지금은 "연결 가능/오프라인/연결하는 중…" 뿐, `shell.html:332-366`):

| 상태 | 표시 |
|---|---|
| StandbyReady | **준비됨** — 실제 entry 확인 + Ping 생존일 때만. Idle·needsCapability·handedOff·무응답만으로는 표시하지 않는다 |
| PreparingStandby·ReturningToStandby | **준비 중** |
| Active(창이 보임) | **연결됨** |
| Retry | **다시 준비 중(n초 뒤)** |
| Unsupported | **상시 유지 미지원 · Host 업데이트 필요** — 탭 = 일반 연결 |
| Suppressed | 표시 없음 — 일반 카드 |

**목록 잠금 해제**: 지금은 뷰어가 하나라도 돌면 `setBusy` 로 목록 전체를 잠근다(`shell.html:256-266, 355-360, 478-482`). 다른 PC 탭이 최신 의도가 되도록(모델 A) **카드별 busy** 로 바꾼다. 여러 가시 창 동작은 유지한다(Codex §4: 전역 active 1 로 줄이지 않는다).

**3대 한도**: active + standby 합계 3.
- 4번째 성공 → 보이지 않는 standby 중 LRU 하나만 정리한다.
- 4번째 실패 → 기존 3대 유지.
- 3개 창이 모두 보이는 중 → 자동으로 닫지 않고 한도와 닫는 방법을 안내한다(Codex §1).

## 5) 최근 3대 저장과 세대 카운터

**최근 3대**: `%LOCALAPPDATA%\GNLink\recent_hosts.v1`(client.txt 옆, `settings_path()` 와 같은 wide 경로 `:245-267`).
- 줄마다 `origin \t accountId \t hostId \t lastUsedUtc`. token·IP·표시명은 키로도 값으로도 저장하지 않는다.
- 쓰기는 tmp → `MoveFileExW(REPLACE_EXISTING)`.
- 순서는 **사용자의 성공한 접속·실제 재사용**만 갱신한다. 자동 준비·Ping·목록 갱신은 갱신하지 않는다.
- 계정별: 같은 파일 안에서 origin+account 로 거른다. 로그아웃해도 기록은 남기고, 그 계정의 lease 만 회수한다 — 다음 로그인 때 다시 준비하기 위해서다.
- 시험 빌드는 기존 `gShellTestDataDir` 격리를 따른다(`:209-260`).

**세대 카운터 [결정 1]**: `GenerationAllocator` 는 설치당 1개 단조 카운터다(`standby_entry_client.hpp:46-97`). O1 에서는 프로세스가 최대 3개라서 다음 셋 중 하나를 골라야 한다.

| 안 | 내용 | 장점 | 단점 |
|---|---|---|---|
| ⓐ host key 별 기기 id + 세대 파일 (제안) | `standby\<sha256(key) 앞 16>.gen`. 그 key 의 뷰어 하나만 연다. `LockFileEx` 배타 잠금 실패면 송신 0 | 프로세스 간 공유가 없다. Host 는 같은 기기 id 안에서만 세대를 비교한다(S2) | Host 가 보는 "기기" 가 PC 별로 다르다. host 당 8 entry 상한(`host_standby_table.hpp:49-52`)은 key 당 1이라 영향 없음 — Codex 확인 필요 |
| ⓑ 셸이 allocator 를 갖고 세대 블록을 IPC 로 대여 | 셸이 먼저 저장한 뒤 넘긴다 | 기기 id 가 설치당 1 로 계약 그대로 | IPC·재시작 경계가 복잡하다 |
| ⓒ 설치당 파일 1개 + 프로세스 간 파일 잠금 | 매 사용마다 잠금 → 읽기 → 증가 → 저장 | 계약 그대로 | 잠금 경합, 실패 경로 |

**비ASCII 경로**: `FileGenerationStore` 는 `std::fopen` + `MoveFileExA` 다(`standby_file_store.hpp:27, 40, 50`). 사용자 이름이 한글인 `%LOCALAPPDATA%` 에서 깨진다. **wide 변형(`_wfopen`/`CreateFileW` + `MoveFileExW`)이 필요하다.** 공용 파일이라 writer 는 작업용1 이다(APK 는 영향 없음). 시험에는 비ASCII 시험 루트를 넣는다.

## 6) 셸 수명: 시작·종료·로그아웃·계정 전환

**로그인 성공(`adopt_session` `:939-983`)**:
1. 최근 3대 중 목록에 online 이고 억제되지 않은 PC 만 standby 뷰어를 띄운다. staging 은 동시 1개, 기존 connect 시한 안에서 순서대로 한다.
2. 지원 사전 확인으로 `directory_standby_support`(`directory_session_client.cpp:421-450`, `/api/hosts` 를 부작용 없이 읽음)를 셸이 1회 호출한다. 미지원 서버면 아무것도 띄우지 않는다.
3. Host 별 지원 여부는 첫 admission 결과(Unsupported = 6 Hello 무응답 ≈3 s)로 그 셸 수명 동안 캐시한다.

**로그아웃·계정 전환**(지금 `:1763-1785` 는 뷰어를 건드리지 않는다):
1. 모든 managed 자식에 `Release{reason=signout}` 를 보낸다. 실행 중인 세션은 fence 뒤 종료한다.
2. Parked 이면 StandbyRelease 1회. Active 이면 Host 가 세션 끝을 본 뒤 Release 한다. 끝을 보기 전에는 Live 로 거부되고, 그러면 lease 가 20 s 뒤 소멸한다(② 진행 3 의 실제 관찰).
3. 유한 대기(예: 3 s) 뒤에도 남은 자식은 Job 으로 종료한다.
4. owner epoch 를 올려 늦은 IPC 를 버린다.
- 일반(비 managed) 뷰어의 기존 동작 — 로그아웃 뒤에도 유지 — 을 바꿀지는 **[결정 3]** 이다.

**셸 종료(`WM_DESTROY` `:1865-1869`)**: 로그아웃과 같은 Release → 유한 대기 → Job 으로 종료한다. 셸 최소화는 종료가 아니다.

**셸 crash**: managed 자식은 셸이 만든 Job(`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`)에 넣는다 → 셸 핸들이 닫히면 OS 가 회수한다. Host entry 는 20 s 뒤 소멸한다.
- 지금 셸은 SYNCHRONIZE 전용 핸들만 갖고 자식을 죽이지 않는다(`~:1626-1631`). Job 은 **자기가 만든 자식**에만 적용하므로 PID 재사용·다른 프로세스를 건드리지 않는다.

**업데이트 handoff**: 셸이 설치기를 넘기기 전에 위 종료 절차를 수행한다.

## 7) 다른 기기 무간섭·구 Host·raw IP·재연결

**무간섭**: standby Ping 은 Host 의 active·held input·영상·선택을 바꾸지 않는다. 이것은 Host 계약이고, spike e2e 에서 "B ping 12 s 동안 A 무간섭" 으로 확인됐다.
- PC 가 추가로 지킬 것: 자동 activation 0. 탭만 activation 한다.
- 다른 기기가 takeover 해서 우리 세션이 끝나면(`SessionEnded{takeover}`) standby 로만 돌아가고 자동으로 되찾지 않는다. 창에는 "다른 기기에서 연결해 이 화면이 닫혔습니다" 를 남기고 → 목록에 표시한다.

**구 Host·prelogin·지원 unknown**:
- 자동 standby 가 Unsupported 면 그 자식은 종료하고 카드에 미지원을 표시한다. legacy Hello 는 보내지 않는다(`StandbyEntryClient` 계약).
- 탭은 기존 일반 연결이다.
- 그 연결에서 목록으로 돌아오면 "상시 유지 미지원" 을 알리고 세션을 종료한다(모델 A 4).

**명시적 일반 연결 → 목록 복귀(모델 A 3, 첫 전환)**:
1. 일반 Hello 로 연 세션의 종료를 직렬화한다.
2. 같은 뷰어 프로세스가 **새 lease 소켓**으로 observe + standby connect + admission 을 한다.
3. 이 첫 전환의 새 directory 요청은 로그와 IPC 에 기록하고, "재사용" 으로 세지 않는다.
4. 도중에 다른 PC 를 탭하면 그것을 최신 의도로 처리하고, 전환은 계속한다(staging ≤1).
5. 처음부터 standby 를 거쳐 activation 하는 안은 미지원 Host 에서 ≈3 s 를 잃는다(Unsupported 판정). 그래서 지원 캐시가 확인된 PC 에만 적용한다 **[결정 4]**.

**raw IP**: 셸은 고정 directory 만 쓴다(`shell_directory_url` `:231-243`). raw IP 는 standalone 뷰어 경로이고, standby 는 없으며 변경도 없다.

**자동 재연결**: 지금은 종료 코드 43/44/46 이면 1/2/4 s 로 최대 3회 다시 띄운다(`:1286-1306`, 창 watchdog 등).
- managed 에서는 재연결을 entry 별로 격리한다. 전역 `gViewerOperation`/`gReconnectRequest` 만으로는 다른 PC 의 복구를 지울 수 있다(Codex §4).
- 활성 세션이 **네트워크** 로 끊기고 lease 가 살아 있으면 같은 lease 로 다시 activation 할지는 **[결정 5]** 다. 제안: 창이 보이는 상태에서 `SessionEnded{network}` 일 때만 1회 시도하고, takeover·host 종료면 하지 않는다.

**NP 와의 관계**: standby 경로는 NAT punch 를 하지 않는다(`standby_directory.hpp:7-8`). relay 가 없으면 lease 는 private 후보 또는 첫 후보를 겨눈다. 회사 → 집 같은 실 NAT 에서 admission 이 Host 의 outbound punch 로만 열리는지는 **미검증**이다(NP r1 측정과 같은 메커니즘). 시험 relay 경로는 ② 진행 2 에서 확인됐다.

## 8) 시험·비용 측정 계획 (격리만)

**환경**:
- 시험 server.js: loopback, relay on, grace 시험값.
- 시험 GNLinkStream: `--bind-address 127.0.0.1`, SLOT_REQUEST CreateNew.
- 셸 시험 빌드: `gShellTestDataDir` 격리 데이터 루트(비ASCII 이름 포함), 시험 계정.
- 화면 증거: 셸·뷰어 창의 UIAutomation + WebView2 `ExecuteScript`/`CapturePreview` + `System.Drawing` 캡처.
- ⚠️ **사용자 화면을 가리지 않는다**(no-covering-user-screen). 창 실기 시험은 별도 데스크톱(CreateDesktop) 또는 화면 밖 배치 seam 이 필요하다 — 방식 확인 **[결정 6]**.
- 사용자 설치본·서비스는 건드리지 않는다.

**필수 행동**(Codex 모델 A 목록을 PC 로 옮긴 것):
1. 최근 A/B/C 저장 → 셸 재시작 → standby 3 → 다음 네 가지가 0 인지 Host 로그로 확인한다: Host session·video, decoder, 창.
2. A 탭 → picker ≤1 s(p95, loopback) → 대상 선택 → 첫 presented(별도 분포) → X → 같은 lease standby(새 directory connect·ordinary Hello 0) → B 탭 → A 탭.
3. 일반 명시 연결 → 목록 → 첫 전환(새 요청 기록). 전환 도중 다른 탭·취소.
4. 구 Host(baseline GNLinkStream) → Unsupported 표시, 세션 0, 탭 = 일반 연결, 목록 복귀 시 미지원 안내 + 종료.
5. 다른 기기(시험 클라이언트) active 중 standby Ping 무간섭 → 탭만 takeover → 상대가 되찾으면 우리는 standby, 자동 재탈취 0.
6. 4번째 성공·실패 eviction, 한 PC offline·재online 이 다른 2대를 유지, 한 자식 crash 가 다른 2대를 유지.
7. 로그아웃·계정 전환·셸 종료·셸 kill(Job 회수): 늦은 IPC·Provide·같은 key 재생성 fence. 자식 0 과 Host entry 소멸을 시각으로 확인한다.
8. 비ASCII 데이터 경로에서 recent·세대 파일을 쓰고, 재시작 뒤 세대가 단조로 이어지는지 확인한다.
9. **부정 대조**: fence 제거, 늦은 opId 수용, 자동 activation, 창을 띄운 뒤 숨김 — 각 mutant 가 해당 시험에서 FAIL 해야 한다.

**비용**(Codex §6): direct 와 시험 relay 각각, standby 0/1/3 · active 1 + standby 2 · 5분 이상 안정 구간.
- 클라이언트: 프로세스별 private bytes·working set·threads·CPU·wakeups(ETW 또는 `typeperf`), GPU 작업 0(standby).
- 네트워크: control bytes·pps 양방향, relay 양 leg 연결 수, directory connect/observe/capability 횟수(서버 로그), 유지 중 directory 요청 0(갱신·복구 제외).
- **O1 판단 자료**: standby 뷰어 1개의 private bytes 를 셸 1개와 비교한다.
- 메모리 증가 추세 0. busy retry 폭주 0(backoff 1~30 s 상한 확인).

## 9) 결정 목록

1. 세대 카운터: ⓐ host key 별 기기 id(제안) / ⓑ 셸 대여 / ⓒ 설치당 파일 잠금 — Host 의 기기 의미와 S2 에 비춘 Codex 확인.
2. 목록 복귀 때 창·디코더를 해제(제안, 비용 최소)할지, 숨긴 채 유지(재활성 빠름)할지 — 측정 뒤.
3. 로그아웃 때 비 managed 일반 뷰어도 닫을지(현재 유지) — 사용자 행동 변화라 제품 결정.
4. 지원이 확인된 PC 의 명시 탭을 standby 경유 activation 으로 바꿀지(제안: 캐시가 확인된 경우만).
5. 네트워크로 끊긴 active 세션의 같은 lease 재activation 1회 허용 여부.
6. 창 실기 시험 방식(별도 데스크톱 vs 화면 밖 seam) — 사용자 화면 금지 규칙과 렌더 증거 요구의 조정.

## 10) 파일과 writer

- **PC 전용** (이번 기능 담당자가 쓴다):
  - `client_shell_main.cpp`, `client_shell_bridge.*`, `ui/shell.html`
  - 뷰어: `viewer_startup.cpp`, `native_video_client_main.cpp`, `viewer_args.cpp`, 새 `viewer_managed_ipc.*`
  - `viewer_startup.cpp` 는 NP 브랜치(feat/np-early-relay 2줄)와 겹친다 → 병합 순서를 맞춘다.
- **공용 native** (writer 작업용1): `standby_file_store.hpp` wide 변형. [결정 1]ⓐ 를 고르면 기기 id 파일 경로를 주입받게 하는 정도다. `StandbyRunner`·`StandbyLease` 는 바꾸지 않는다.
- **서버·Host**: 변경 0. 기존 standby API 를 쓴다.
- **선행 조건**: ② r3 보수(N1~N3)·e 배선의 Codex 확정. 자동 preconnect 출하 OFF 는 독립 검증까지 유지한다.
