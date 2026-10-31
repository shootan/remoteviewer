# report ka-pc-impl r1 — PC 클라이언트 최근 PC 3대 유지 (KA ③) (작업용2, 2026-10-28)

task ka-pc-impl / r1 / kind=implement (t-lcit94to).
- 정본: `proposal_ka_pc_r2.md`(§11), `task_keepalive_r2.md` '10-27 ③ PC 설계 판정', Codex `codex_ka_pc_design_r1.md`.
- 운영 서버·NAS·사용자 설치본은 건드리지 않았다. 버전·서명·게시·push 없음.

## 고정 후보

- branch `feat/ka-pc`, **7144e1c**(= fdb4232 + 시험 1 commit: 첫 전환·상대 present 측정. 제품 코드는 fdb4232 와 같다). worktree `C:/work/gnlink-wt/ka-pc` clean.
- 커밋 이력:
  - `3d86268` = **e16ad0d(NP) + merge 82679b2(ka-r3 공용 KA, SOURCE+EVIDENCE OK)**. 충돌 0. 병합 커밋만 git CLI 로 만들었다(Git MCP 에 merge 가 없음).
  - `8936b0b` 구현(WIP, 빌드만) → `fdb4232` 실행 검증과 보수.
  - 둘 다 Git MCP 커밋이다.
- 공용 `standby_*`(작업용1 소유)는 **바꾸지 않았다.**
- 빌드 `C:/work/gnlink-wt/ka-pc-build`(Release). sha256 앞 16:
  - GNLinkViewer.exe 404ccb28f638cd4a
  - GNLinkClient.exe 1907299659c9c13a
  - GNLinkViewerKaTest.exe 423d9b8675409285
- 출하 GNLinkViewer 에는 시험 seam 문자열(`[viewer][ka-test]`·`REMOTE60_VIEWER_TEST_*`)이 없다. 시험 빌드에만 있다.

## 1) 구현 (Codex O1)

**뷰어**
- `run_session`: 한 세션을 그 세션 전용 context 에서 돌린다. 한 프로세스가 세션을 여러 번 돌릴 수 있다.
  - 창 클래스 재등록을 허용한다.
  - 창 프로시저의 정적 상태(IME·붙여넣기)와 세션 툴바를 세션마다 초기화한다.
  - **모든 종료 경로에서 창을 닫고 메시지를 정리한다**(실행 중 발견 — 아래 §3).
- `--managed-standby`(`viewer_managed_standby.cpp`): 한 PC 의 StandbyLease 를 갖는다.
  - StandbyRunner 를 쓴다. capability 는 lease 소켓으로 observe 와 standby connect 를 해서 얻는다.
  - 세대 저장소: PC key 의 **전체 SHA-256** 을 파일 이름으로, wide 경로로 둔다. 공용 FileGenerationStore 의 별도 `.lock` 을 쓰고 단일 writer 다.
  - **Activate 전에는 창·디코더·MF·D3D 를 만들지 않는다**(e2e 에서 확인).
  - activation = lease 의 복제 소켓으로 한 세션을 돌린다.
    - `open_media_socket` 은 directory 를 건너뛰고 lease 소켓을 받는다.
    - `connect_media_socket` 은 Hello 대신 lease handshake 를 한다.
    - 재시도 대화상자가 없다. 공유 소켓에 `shutdown()` 을 걸지 않는다.
    - 세션 스레드가 join 된 뒤 정확한 op 로 `SessionOver` 를 부르고, 소켓을 받지 않았으면 `Abandon` 한다.
  - **자동 재activation 은 0** 이다(Codex ⑤).
  - 세션이 끝난 뒤 **12 s 동안 "returning"**(§3) 을 보고한 뒤 "ready" 로 바꾼다.
  - 첫 화면 시점은 `PickerReady`(picker 첫 그림)와 `Presented`(선택 뒤 첫 present)로 따로 기록한다.
- `managed_viewer_ipc.hpp`: 상속 익명 pipe 2개를 쓴다.
  - 프레임 = 길이·버전·종류. 최대 크기와 escape 를 검증한다.
  - 수신·송신 각각 전용 스레드와 상한 있는 큐. Close 는 시한 안에 끝난다(상대가 읽지 않으면 1 s 뒤 취소).
  - 모든 메시지에 key + incarnation + owner epoch 를 싣고, activation 계열 메시지에는 opId 까지 싣는다.
  - token 은 Init·TokenRenewed 프레임으로만 전달한다(tokenVersion 으로 늦은 갱신 차단). argv·env·로그에는 넣지 않는다.

**셸**
- `client_managed_viewers.hpp`
  - **suspended → 자식별 Job(KILL_ON_JOB_CLOSE) → resume.** Job 편입에 실패하면 실행 전에 종료한다.
  - 자식 쪽 pipe 끝·stdout·NUL 만 상속하고, Job handle 은 상속하지 않는다.
  - 종료 = Release/Shutdown 송신 → reaper 스레드에서 3 s 대기 → 자기 Job 종료.
- `client_recent_hosts.hpp`
  - key = origin + account + hostId. seq 로 정렬하고, 필드는 escape, 계정별 3개, wide 경로, 원자 교체.
- `client_shell_main.cpp`
  - **스위치 2개**: 로그인 때 자동 준비 = **OFF(출하)**, 목록 복귀 때 유지 = ON(모델 A).
    - ⚠️ 이렇게 읽었다: OFF 여도, 연결된 directory 세션을 사용자가 닫으면 standby 뷰어가 남는다. 사용자에게 보이는 변화다.
  - 연결됐던 세션을 닫으면 첫 전환을 한다. 로그에 "new observe + standby connect" 를 남긴다.
  - ready 또는 returning 인 PC 를 탭하면 activation 한다. 준비 중이면 명시 실패 문구를 보이고, 일반 연결을 함께 시도하지 않는다.
  - 최대 3대: 사용 중이 아닌 PC 만 LRU 로 정리한다. 3대 모두 보이는 중이면 안내만 한다.
  - owner epoch 를 먼저 올린 뒤 `end_owner_children` 를 부른다(로그인 시작·자동 로그인·세션 만료·로그아웃·창 종료 5곳).
    - 관리 뷰어는 정리한다.
    - **그 셸이 띄운 일반 뷰어**는 취소 이벤트를 먼저 보내고, 3 s 뒤에도 살아 있으면 launch 핸들로 종료한다(Codex ③). 이 종료는 `TerminateProcess` 라서 **graceful close 가 없다** — Host 는 제어 채널이 10 s 조용해진 뒤(peer-lost) 세션을 끝낸다.
  - token 갱신은 버전과 함께 전달한다.
  - "확인되지 않음"(미지원·실패)은 **60 s 뒤 1회 재확인**하고, 그 뒤에는 그 owner 동안 다시 하지 않는다.
- `shell.html`: 카드별 busy(다른 PC 탭 가능). 유지 상태 표시는 뷰어 보고로만 한다(준비됨/돌아오는 중/준비 중/미지원).

## 2) 시험 (격리)

환경: loopback, 시험 server.js(relay·host-facing 127.0.0.1), 시험 Host = 이 빌드의 GNLinkStream(`--bind-address 127.0.0.1`), 구 Host = **공개 0.2.163 GNLinkStream**(sha 825feb97…).
- 뷰어 창은 가상 화면 오른쪽 밖(x=2120 > 1920)에 두고, 포커스 없이 표시했다.
- 모든 파일은 `REMOTE60_TEST_TMP`(`D:/remote/remote/.claude/test-tmp/ka-*`) 아래에 두었다.
- GPU 슬롯을 CreateNew 로 잡고 반납했다. 콘솔 세션이다.
- 화면·GPU 의존(display-capture/gpu)과 network(loopback) 시험이다.

| 시험 | 결과 |
|---|---|
| `ka_pc_managed_e2e_test` | **53/0, rc 0, ×3** (8·9·10회차) |
| `ka_pc_managed_e2e_test` @7144e1c(+첫 전환·present 계수 5 check) | **58/0, rc 0, ×2** (11·12회차). 시험 exe sha 764f1cd9…, 소스 sha 348538ca… |
| `ka_pc_reentry_probe.ps1` (한 프로세스 2세션) | 2세션 모두 connected·present(32/41, 32/39, 36/46줄) **×3** |
| `ka_pc_units_test` (IPC·recent, 비ASCII 폴더) | 20/20 |
| punch_any · directory_session_client · connect_candidates · connect_op_gate · standby_lease · standby_entry_client(72) | PASS |

**e2e 가 확인한 행동** (실측)
- standby: ready, 창·디코더 없음.
- Activate → **제품 picker**(WGC 캡처 `A-round1-picker.bmp`, variety 9). PickerReady 64 ms / 재activation 25 ms(Activate 기준, 같은 프로세스).
- picker 의 desktop 카드를 마우스 메시지로 누름(뷰어 로그의 제품 레이아웃 좌표) → **첫 화면 present**(WGC 캡처, variety 65). Activate 기준 1090 ms / 905 ms(누를 때까지의 시험 대기 포함).
- Park → 세션 끝(connected, park) → 창 사라짐 → **"returning"** → 12.0 s 뒤 **같은 lease 로 ready** → 재activation. 두 activation 동안 **새 directory capability 0**.
- Host 의 세션은 모두 activation 에서 나왔고 ordinary Hello 0. 뷰어 로그는 "activation ok".
- **다른 기기 사용 중**: 일반 클라이언트가 Host 를 쓰는 동안 standby 를 12 s 유지 → Host 에 새 세션·takeover 없음. 상대 뷰어 로그의 `[present]` 줄이 그 12 s 동안 **0 → 39 / 0 → 33**(e2e 11·12)으로 늘었다 — 상대 세션이 계속 그렸다. **탭만 인수**한다.
- **첫 전환(출하 경로)**: 일반 세션이 연결됐다가 끝난 **직후** 그 PC 의 managed 뷰어(처음 유지하는 key)를 시작 → **ready 680 ms**(×2). FINDING 은 이 경로에서 나지 않았다. 아래 §4 참고.
- 다른 owner epoch 의 Activate 는 무시된다.
- Release: Host 가 받아들였다(Live 거부 없음).
- **셸 제품 모듈 `ManagedViewers`**: launch → State 전달 → 이전 owner epoch 이벤트 차단 → `EndAll` 은 즉시 반환 → 자식은 시한 안에 종료 → 남은 것 0.
- Job 마지막 handle 을 닫으면 자식이 종료된다(셸 crash 동등).
- **구 Host(공개 0.2.163)**: unsupported, ordinary Hello 0, 구 Host 세션 0, Release 로 종료.

**증거 구분**
- 렌더는 **제품 뷰어 창의 WGC 캡처**다(`display-capture`). 다만 화면 밖 배치라 실제 사용자 모니터의 표시는 아니다. throttle 은 관측되지 않았고 present 줄이 나왔다.
- **셸 WebView UI(카드 탭 → activation)는 실행하지 않았다.** 셸 쪽 검증은 제품 모듈(`ManagedViewers`) 수준이다. 페이지 상태 표시와 카드별 busy 는 **렌더 증거가 없다**.

## 3) 실행 중 발견·보수

1. **세션 2 가 곧바로 끝남**(재진입 probe 첫 실행): `--seconds`·host 끊김 등 X 이외의 세션 종료는 창을 남겼다. 지금 제품은 프로세스가 끝나서 무관하지만, 같은 프로세스의 다음 세션이 그 창과 quit 을 물려받았다(해제된 context 를 가리키는 창 = use-after-free 위험). → `run_session` 의 모든 종료 경로에서 창을 닫고 메시지를 정리한다.
2. **park 직후 "ready" 보고**(모델 A 위반): Host 는 제어 채널이 10 s 조용해야 세션을 끝낸다(`UdpControlLink(…, 10000)`). 뷰어는 떠난다고 알리지 않는다. 그 사이 Release 는 Live 로 거부되고 같은 기기 재입장은 Busy 다. Pong 에는 grant 반환 표시가 없다. → 12 s "returning" 을 둔다(그 동안의 탭은 activation 이 된다 — 실측).
   ⚠️ **이것은 Host 확인이 아니라 잠정 시한이다.** returning → ready 는 Host 의 10 s 상수에 2 s 를 더한 시각으로 정했다. Pong 에 grant 상태가 없어서 Host 가 grant 를 돌려받았는지 확인할 수단이 없다. 공용 wire 에 grant 상태를 넣을지는 **Codex 가 정할 일**이다(§6).
3. **activation 로그가 "hello ok" 로 찍힘** → "activation ok" 로 고쳤다.

## 4) FINDING — 공용 Host/relay (작업용1 소유, 고치지 않음)

**현상**: 같은 기기의 새 standby lease 를 직전 입장 직후(약 30 s 안)에 시작하면, relay 가 Hello 를 Host 로 전달하는데도(`c2h=6`) **Host 가 아무 답도, 로그도 없다**(`no StandbyAck`, `h2c=0`). 그 결과 클라이언트는 6 Hello 뒤 **"unsupported" 로 잘못 분류**한다.
- 재현: crash 직후 재준비에서 ×3(e2e 8·9·10). 모듈 EndAll 직후 C(e2e-7)도 같은 모습이다.
- 다음 입장은 직전 입장 약 30.3 s 뒤에 되었다. Host 의 `kStandbyHelloReplayMs=30000` 과 시각이 맞지만 **원인으로 확정하지 않았다.**
- **시험에서의 처리**: 같은 PC 를 다시 준비하는 단계는 직전 입장 31 s 뒤에 시작한다(주석·이 보고에 명시). 즉시 재준비는 `FINDING` 줄로 측정만 하고 판정에는 넣지 않았다.
- **셸의 완화**: "확인되지 않음" 은 60 s 뒤 1회 재확인한다(영구 미지원으로 고정하지 않음).
- **출하 첫 전환에서는 나지 않음(실측)**: Host 가 보는 standby 신원은 일반 뷰어의 것이 아니라 key 별 generation store 에서 나온다(`viewer_managed_standby.cpp` `FileGenerationStore`/`GenerationAllocator`). 그래서 일반 세션 직후 처음 유지하는 PC 는 ready 680 ms 였다(e2e 11·12). FINDING 이 나는 것은 **같은 key 를 30 s 안에 다시 lease** 할 때다: crash 뒤 재준비, EndAll 뒤 재준비, 축출·해제 직후 다시 유지.
- **영향**: 셸 재시작·재로그인을 30 s 안에 하면, 그 PC 는 이번 owner 동안 첫 시도에서 "미지원" 으로 보이고 60 s 뒤 사용자 동작으로 다시 시도한다.

## 5) §11 조건 대응

| # | 조건 | 상태 |
|---|---|---|
| 1 | fence 먼저 → 종료 대기는 worker | **구현**(epoch 를 올린 5곳 직후). 셸 UI 경로 자체는 미실행이고, 모듈 EndAll 만 실행 |
| 2 | pipe 2개, 자식 끝만 상속, Job 비상속, 프레임 검증·bounded·deadline | **구현 + 단위/e2e** |
| 3 | tokenVersion, 401 ≠ network, 로그아웃 뒤 NeedToken 무시 | tokenVersion 은 **구현**. NeedToken 은 뷰어가 보내지 않음(셸이 갱신을 push). 401 경로는 기존 come_back 이다 — **e2e 미검증** |
| 4 | suspended → Job → resume, 실패면 미실행, 자식별 handle | **구현 + e2e**(모듈 launch, job close) |
| 5 | key 별 영속 id, 송신 전 내구 예약, stable lock, full key | 공용 store + 전체 SHA-256 파일명. **e2e**: 같은 key 재사용 ready. 두 프로세스 동시 잠금은 단위 시험 없음(공용 store 의 owns() 사용) |
| 6 | init·handshake 의 op 결합, PickerReady≠Presented | **구현 + e2e**(op 별 메시지·시각) |
| 7 | park: fence → reader 정지 → 정확한 op SessionOver → Ping | **구현 + e2e** |
| 8 | 자동 재activation 0 | **구현**(재시도 대화상자·43/44/46 재시작 없음). takeover 를 network 처럼 보이게 하는 반례는 **미실행** |
| 9 | 확인되지 않음 ≠ 영구 미지원 | **구현**(60 s 1회 재확인). 시험 없음 |
| 10 | 착수 순서 | 공용 82679b2 + NP e16ad0d 위에서 진행 |

## 6) 미검증 · 남은 일

- **셸 WebView UI 실기**: 카드 탭 → activation, 상태 표시, 카드별 busy, 3대 LRU, 첫 전환(`keep_after_close`), 로그아웃·계정 전환·셸 종료 때 일반 뷰어 종료. 셸 시험 빌드(WebView2)와 화면 밖 seam 이 필요하다 — 다음 단계.
- 실 NAT·회사 → 집, 비용(§6: private bytes·스레드·패킷 0/1/3), 실제 다중 모니터 표시.
- relay 아닌 직접 경로의 standby(이 시험은 relay 예약 경로). 실 0.2.163 외의 구 Host 변형.
- 위 FINDING(공용).
- **Codex 결정 대기(잠정 정책 2개)**: ① returning 12 s 는 Host 확인이 아니라 시한이다(공용 wire 에 grant 상태를 추가할지). ② "확인되지 않음" 은 60 s 뒤 1회 재확인한다(FINDING 완화).
- **degraded 경로**: managed 모드에서도 뷰어의 `abandon_thread_and_exit`(exit 44)는 프로세스를 끝낸다 → lease 를 잃는다 → 셸은 종료를 보고 상태를 "none" 으로 둔다. 자동 재준비는 없다.
- 셸이 일반 뷰어를 끝낼 때는 `TerminateProcess` 이다(graceful close 없음). Host 는 peer-lost 10 s 뒤 세션을 끝낸다.
- retry 시험 등 이 브랜치의 옛 판 시험(실행 파일 옆에 쓰는 것)은 실행하지 않았다.

## 7) 정리·삭제 후보 (추가만)

- `C:/work/gnlink-wt/ka-pc`(worktree)·`ka-pc-build`·`ka-pc-build.*.log`
- `D:/remote/remote/.claude/test-tmp/ka-*`(e2e 증거: 로그·캡처 BMP. 보존 중)
- 삭제는 하지 않았다.


---

# r2 (2026-10-28, 작업용2) — Codex `codex_ka_pc_src_r1.md` N1~N6 · D1/D2 보수 + 공용 a7773e6 merge

## 고정 후보

- `feat/ka-pc` **a864105**. worktree `C:/work/gnlink-wt/ka-pc`, tracked clean, `git diff --check` rc 0.
- 7144e1c 뒤 커밋(first-parent):
  - `2425df8` 보수 본체(N1~N6, D1/D2)
  - `79257f2` 셸 e2e 등 시험
  - `06e7f14` **merge ka-r3 a7773e6**(공용 standby/Host, 30 s FINDING 보수 포함)
  - `a864105` 시험 보정
- merge 커밋만 git CLI 로 만들었다(Git MCP 에 merge 가 없음). 충돌 0, 공용 파일은 merge 만 했다. 나머지 커밋은 Git MCP 다.
- 제품 변경 파일(7144e1c..79257f2):
  - 셸: `client_shell_main.cpp`, `client_managed_viewers.hpp`, `ui/shell.html`
  - IPC: `managed_viewer_ipc.hpp`
  - 뷰어: `native_video_client_main.cpp`, `viewer_managed_standby.cpp`, `viewer_window_proc.cpp`, `viewer_constants.hpp`, `viewer_context.hpp`, `viewer_session_state.hpp`, `viewer_startup.cpp`(1줄)
  - 공용 `standby_*` 직접 수정은 0이다.
- 빌드 `C:/work/gnlink-wt/ka-pc-build` Release, sha256 앞 16자리:
  - GNLinkViewer.exe 6a2738b3745a8e38
  - GNLinkClient.exe a514abaa628258bd
  - GNLinkViewerKaTest.exe f1f4980a0205b0e1
  - GNLinkStream.exe 9f6e6806d0ff5af1
  - ui/shell.html 7ea0316ada461863
  - remote60_ka_pc_shell_e2e_test.exe 5898f6b93faf39f6
  - remote60_ka_pc_managed_e2e_test.exe 580cdf669951af5e(r2-4/5 빌드)
- 출하 exe 에 시험 문자열이 0건이다: GNLinkViewer 의 `REMOTE60_VIEWER_TEST`/`ka-test]`, GNLinkClient 의 `shell-test-seam`.

## 보수

| # | 내용 |
|---|---|
| N1 | 목록 복귀 유지는 **PC 별로** 판단한다: 그 PC 의 현재 슬롯(같은 PC 의 교체된 옛 세션 제외) ∧ connected ∧ 정상 종료(exit 0). 전역 최신 op 인지는 상태줄을 누가 쓰는지만 정한다. `gConnectedOps` 는 모든 종료 경로(owner 변경·stale 포함)에서 지운다. |
| N2 | **논리 PC 장부 하나**로 센다: ordinary 활성 ∪ managed, host 별, 최대 3, staging 1. 새 ordinary PC 는 3대가 모두 가시(ordinary 또는 managed 활성)면 **거부**한다(가시 창은 축출하지 않음). 대기 PC 가 있으면 실행하고, **connect 성공 시점에만** 가장 오래 *사용하지 않은* standby 를 축출한다. 실패하면 3대를 보존한다. managed 후보가 3대 옆이면 staged 로 두고 ready 에서만 commit 한다(실패하면 보존). 최근 순서는 **실제 connect·재사용(PickerReady)** 에서만 갱신하고, 창 닫는 시점에는 갱신하지 않는다. |
| N3 | 카드마다 별도 **"연결 해제"** 버튼을 둔다: bridge `releaseKept` → `Release` → 자식 종료 → 자동 유지 억제(목록 복귀·로그인 준비). 억제는 사용자의 명시 connect 만 푼다. 늦은 IPC 는 `Current()` 가 버리고, staging 도 해제한다. |
| N4 | **owner fence**: 연결 뒤 cancel 이벤트(전에는 "no effect")를 받으면 UI thread 가 입력·클립보드를 즉시 차단한다. 눌린 키·버튼 해제, 클립보드 listener 해제, paste 취소, 이후 입력 메시지와 내부 클립보드 post(`kMsgPushClipboardNow`, `kMsgApplyClipboard`) drop 을 한 뒤 창 자신의 WM_CLOSE teardown 으로 닫는다. 감시 thread 는 join 한다(detach 아님). 창이 생기기 전의 취소는 창을 만들지 않고, connect 완료 직후의 취소는 세션을 시작하지 않는다. managed Release/Shutdown 도 같은 fence 를 쓴다. 강제 종료 fallback 은 3 s → **8 s** 로 늘렸다(뷰어 teardown 의 join 한도가 3 s+3 s 이기 때문, ordinary·managed 둘 다). PID·창명 검색이나 외부 뷰어 종료는 하지 않는다. |
| N5 | `Channel::Close`: 마감 뒤 `abort_` 를 세우고, 모든 Write/Read 전에 확인한다. 각 thread 가 끝났다고 할 때까지 `CancelSynchronousIo` 를 **반복**한다. handle 은 thread 가 멈춘 뒤에만 닫는다. reaper 는 완료 플래그를 보고 다음 종료 때 join 한다(앱 종료까지 쌓이지 않음). |
| N6 | `SessionEnded.connected` 는 **실제 connect**("connected host=" 지점의 `sessionConnected`)다. `taken`(소켓 인수)은 따로 보낸다. 소켓 인수 뒤 handshake 실패는 `reason=connect-failed rc=6` 이고, 셸은 오류로 표시한다. PickerReady/Presented/SessionEnded 는 현재 `activeOp` 와 같은 op 만 받는다. Activate 를 보내지 못하면 activeOp 를 남기지 않고 오류를 표시한다. 카드 표시: Activate 송신 → "여는 중", PickerReady → "연결됨". |
| D1 | `kReturningMs` 주석과 카드 문구를 바꿨다. returning 은 "대기 중 · 이전 연결 정리 중일 수 있음"이고, **grant 반환 증명이 아니다**. ready 는 "대기 중"(로컬 반환 완료 + entry 생존)이다. returning 중에도 탭·해제를 허용한다. 새 wire 는 없다. |
| D2 | 60 s·추가 1회는 **자동 유지 예산에만** 적용한다. 타이머가 아니라, 60 s 이후의 다음 자동 요청만 허용한다. 명시 connect 는 예산을 지운다(새 유한 시도). "PC 업데이트 필요" 진단 문구를 없애고 "대기 확인 안 됨 (다시 연결하면 재시도)"로 바꿨다. |

부수 변경:
- `make_managed_viewers()` 로 분리했다. wWinMain 과 셸 e2e 가 같은 생성 코드를 쓴다.
- managed 자식의 출력 pump 는 op 0 이라 "connected" 표시·최근 갱신을 하지 않는다(의도).

## 시험

환경: 콘솔 세션. GPU 슬롯은 CreateNew 로 잡고 반납했다. 파일은 `D:/remote/remote/.claude/test-tmp/ka-*` 아래에만 썼다.

| 시험 | 결과 | 부정 대조 |
|---|---|---|
| `ka_pc_ipc_close_race_test`(새, 실제 header·pipe, 순서 고정) | **9/0 ×7**. writer/reader 가 첫 cancel 을 놓쳐도 1.06 s / 47 ms 에 끝난다. 정상 drain 은 Bye 까지 온전하다. 반쪽 frame 을 받은 peer 는 아무것도 전달하지 않고 닫힌다. | fdb4232 header: **1번 FAIL(3000 ms 초과)**, 나머지 PASS. 구 코드는 reader 의 handle 을 thread 아래에서 닫아 hang 하지 않지만, handle 재사용 위험은 있다. |
| `ka_pc_units_test` | ALL PASS. reaper: launch/release 20회에 동시 보유 최대 1. | 구 코드에는 ReaperCount 가 없어 대조할 수 없다(구조상 20개 누적). |
| `standby_lease_test`(merge 후 공용) | 66/0 | — |
| `ka_pc_managed_e2e_test` @a864105 | **61/0 ×2**(r2-4/5). r2-2/3 은 보정 전이라 60/1(아래). | — |
| `ka_pc_shell_e2e_test`(새) | **84/0 ×2**(r2-2/3) | 셸 시나리오는 구 코드로 컴파일되지 않아(새 심볼) 구 코드 대조가 **없다**. N1/N2/N3 의 근거는 새 단정뿐이다. |

### 셸 e2e (`ka_pc_shell_e2e_test.cpp`)

구성:
- 제품 `client_shell_main.cpp` 를 entry 이름만 바꿔 쓴다(`REMOTE60_SHELL_TEST_SEAM`).
- **실제 WebView 페이지**를 DOM `.click()` 으로 조작한다. 사람이 읽는 것(카드 `.state` 문구, "연결 해제" 버튼, 상태줄)을 DOM 에서 다시 읽는다.
- 뷰어는 GNLinkViewerKaTest 다. 셸 창과 뷰어 창은 모두 **가상 화면 오른쪽 밖**에 둔다.
- 실 server.js 와 Host 4대(KA-A~D)를 쓴다. 한 PC 의 Host 는 machine id 가 같아 기록 하나로 합쳐지므로, **시험이 4개 기록을 직접 등록**하고 각 Host 의 `--directory-cache` 로 넘겼다. Host 는 제품 경로로 캐시를 읽고 다시 등록하지 않는다.
- CapturePreview PNG 6장: `test-tmp/ka-shell-r2-*/shots/`.

확인한 행동(r2-2):
- **N1**: A·B·C 를 연결한 뒤 **A(최신 아님) → C → B 순으로 X** 를 눌렀다. 셋 다 "대기 중"과 "연결 해제"가 나왔고, 첫 전환 로그는 3건이다. 최근 순서는 connect 순서(C, B, A)이고, 창을 닫아도 그대로다.
- **N2**:
  - D 를 연결하면 **D 가 connected 된 뒤에만** "making room after host=D succeeded, releasing host=A" 가 나온다. 축출은 정확히 1건, B·C 는 유지된다. D 를 닫으면 D 가 유지된다(B, C, D).
  - B·C·D 를 모두 탭해 활성으로 만든 뒤 A 를 탭하면 **"PC 3대가 모두 열려 있어 …"로 거부**된다. 뷰어는 뜨지 않고 축출도 0이다.
  - 실패한 4번째: Host 가 사라진 C 를 탭해도 A·B·D 가 모두 유지되고 축출은 0이다.
- **N6/D1**:
  - 탭 → "연결하는 중…"/"여는 중" → "연결됨"(그 op 의 picker-ready).
  - X → "대기 중 · 이전 연결 정리 중일 수 있음" → "대기 중".
  - Host 를 끈 직후 D 를 탭하면 `op=2 rc=6 reason=connect-failed connected=0 taken=1` 이고, 상태줄은 "연결에 실패했습니다"다.
- **N3**: C 의 "연결 해제" → "released by the user" → 카드가 "연결 가능"(버튼 없음)으로 바뀌고 관리 뷰어 프로세스가 끝났다. 목록을 새로 고쳐도 다시 유지되지 않았고, B·D 는 영향이 없었다.
- **FINDING 보수(a7773e6)**: 축출 직후 A 를 **대기 없이** 다시 유지하면 "대기 중"이 된다.
- **N4 로그아웃**:
  - 셸이 띄운 ordinary B 뷰어 로그에 "owner fence: input and clipboard cut"이 남고 **exit 0** 이다. "did not close in time" 줄이 없으니 handle 로 종료하지 않았다.
  - 관리 뷰어도 모두 끝났다.
  - **셸이 띄우지 않은 외부 뷰어**(KA-A, 별도 프로세스)는 present 줄이 1 → 11 로 늘어 계속 그렸다.
  - 페이지는 로그인 화면으로 돌아갔다.

### managed e2e 추가분

- **Activate 와 Release 를 연달아 보냄**: 창 0, connected 0, picker 0, `reason=release connected=0 taken=1`.
  - 20 ms 늦게 보내면 따뜻한 lease 의 handshake 가 이미 끝나 있어, 활성 세션을 fence 로 닫게 된다(실측). 그건 다른 경우이고 셸 e2e 에서 다룬다.
  - 이때 Host 는 이미 그 activation 에 세션을 내줬다. 그래서 Release 를 "the entry holds the session"으로 **거부**하고, 약 10 s 뒤 control silence 로 그 세션을 스스로 끝냈다(05:49:08.97 거부 → 05:49:18.95 ended). 모델 A·D1(새 wire 없음)대로이고, 시험은 이를 그렇게 판정한다.
- **crash 직후 같은 PC 재유지 → ready**: r1 의 FINDING 줄을 판정으로 바꿨고, a7773e6 로 PASS 한다. EndAll 직후 재유지도 31 s 대기를 없앴고 PASS 한다.

### 시험 보정(a864105) — 모두 시험 쪽 오류였다

- 최근 순서: UI 가 post 를 처리한 뒤에 읽도록 했다.
- 실패 activation: 탭 이후의 로그에서 읽도록 했다. 전에는 이전 세션의 종료 줄을 읽었다.
- 강제 종료 fallback 확인: 고유 줄 "did not close in time"으로 찾는다. 전에는 셸의 예고 문구("…handle only if still there")를 오매칭했다.
- 카드 상세: 대기 **뒤에** 읽는다. 함수 인자 평가 순서가 정해져 있지 않아, r2-1 의 상세 문자열은 대기 전 값이었다(판정은 맞았다).

## Codex 결정 요청 (이번에 생긴 동작 변화)

1. **교체 시 기존 창이 닫힌다**: 출하 페이지에서 일어날 수 있는 일이다. B 를 탭하면 다시 그려질 때 A 의 connectingId 가 풀리고, A 카드가 다시 눌린다. 그 상태에서 A 를 다시 탭하면(begin_session 교체) cancel 이벤트가 이제 fence 이므로 **열려 있던 A 창이 닫히고 새로 연결된다**. 이전에는 기존 창이 남아 두 세션이 경합했다. 요청받지 않은 사용자 가시 변화다.
2. **자동 재연결도 3대 규칙을 탄다**: `begin_session(automatic=true)`(43/44/46 재연결)도 `room_for_new_pc` 를 거친다. 가시 PC 3대 상태에서는 사용자가 누르지 않은 경로인데도 "PC 3대가 모두 열려 있어 …"로 거부된다.

## 남은 것 · 위험

- **N1 닫는 순서**: 실행한 것은 A→C→B 한 순서다. 이 순서에서 A·B 는 최신이 아닌 op 로, C 는 최신 op 로 끝나서 두 분기를 모두 탔다.
- **N2 취소된 D**: D 를 탭한 뒤 연결 전에 다른 곳을 탭하는 경우는 실행하지 않았다(실패 D 만 실행했다).
- **N6 부정 3종은 코드만 있고 시험 없음**: 옛 op 의 PickerReady/SessionEnded drop, 다른 PC 연결 중 늦게 온 이벤트, Activate 송신 실패.
- **`client_recovery_ui_test`(기존 셸 회귀)는 실행하지 않았다.** 창을 화면 위(20,20)에 띄우고, runner 가 저장소 밖 worktree `.claude` 에 쓰기 때문이다. 이 시험에 서로 다른 PC 4대 이상을 동시에 여는 단계가 있다면 새 3대 규칙으로 거부될 수 있다(**위험**).
- 미검증:
  - 실 NAT, 직접 admission, 비용(standby 0/1/3 자원, 반복 비용)
  - takeover 를 network 처럼 보이게 한 반례
  - 두 프로세스의 동시 잠금
  - 401·권한 철회
  - 셸 종료(WM_DESTROY)·계정 전환 경로의 실측(로그아웃만 실측했다)
  - 다중 모니터 실제 표시
- `apps/shared/update_manifest/files/ui_shell.html` 은 `ui/shell.html` 과 원래 내용이 다른 파일이라(r1 이전부터) 손대지 않았다.
- staging(관리 후보가 3대 옆에 오는 경우)은 출하 설정(자동 준비 OFF)에서 사실상 생기지 않는다. ordinary connect 가 먼저 축출하기 때문이다. 코드 경로는 있지만 e2e 로 돌리지 않았다.

## 삭제 후보(추가만)

- `D:/remote/remote/.claude/test-tmp/ka-shell-r2-*`, `ka-managed-r2-*`, `ka-units-r2`
- `.claude/np_impl_logs/ipc_new.hpp`(대조용 사본)


---

# r2b · r3 (2026-10-28, 작업용2) — 534242b merge, N6·N2 반례, Codex `codex_ka_pc_src_r2.md` R1~R4 + 결정 2건

## 고정 후보

- `feat/ka-pc` **5a1fb45** (tracked clean, `git diff --check` rc 0).
- a864105 이후 커밋:
  - `c4e0df0` merge ka-r3 **534242b**. 공용 punch 보수(6c3cb5a 큐 수명)와 시험이 들어온다. git CLI 로 merge 했고 충돌 0, 공용 파일은 merge 만 했다.
  - `705ed23` r3 본체 + r2b·r3 시험
  - `d9a75f1`, `5a1fb45` 시험 보정
- 제품 변경 (a864105..705ed23):
  - `client_shell_main.cpp` (+264), 4파일 합계 +303/-89
  - `native_video_client_main.cpp`, `viewer_window_proc.cpp`, `ui/shell.html`
  - 705ed23 이후 제품 변경 0 (시험 파일만 바뀜)
- 빌드 sha256 앞 16자리:
  - GNLinkViewer.exe 62118c9d4b45bca9
  - GNLinkClient.exe c0617443f6165639
  - GNLinkViewerKaTest.exe 123b0fa88611e5d3
  - GNLinkStream.exe 623bd5fca4efa5f7
  - ui/shell.html 12085292b1851ec4
  - remote60_ka_pc_shell_e2e_test.exe cfaa2dc2a90fe3d5 (r3-4/5)
  - remote60_ka_pc_managed_e2e_test.exe 1e4ad8ed1932bbf1 (r3-1/2)
- 출하 exe 의 시험 문자열은 0건이다.

## r2b (검증용 지시: 534242b merge + 반례)

- merge 뒤 공용 `host_agent_punch_test` 9/0.
- 즉시 재유지 (crash 직후 / EndAll 직후 / 축출 직후 A) 는 모두 계수 대상이고 PASS 했다.
- c4e0df0 + r2b 시험 (시험 exe 3cab51eafa3d2ff0): managed 61/0 ×2, shell **98/0 ×2**.
- 반례 4종 (모두 PASS):
  - **N6 옛 op**: B 활성 중 다른 op 의 SessionEnded 2건과 PickerReady 1건을 넣었다. 3건 모두 drop 되고 로그가 남았으며, B 는 '연결됨' 그대로였고 상태줄도 그대로였다.
  - **N6 늦은 이벤트**: B 를 여는 동안 해제된 C 의 옛 incarnation 으로 SessionEnded, State ready, exit 를 넣었다. B 는 정상으로 '연결됨' 이 됐고, C 카드와 상태줄은 바뀌지 않았다.
  - **N6 송신 실패**: A 의 channel 을 닫고 바로 connect 했다. "could not be sent" 로그가 남고, activeOp 0, 상태줄 "여는 요청을 보내지 못했습니다", 일반 뷰어 0.
  - **N2 취소된 4번째**: 연결 중인 C 를 다시 connect 하자 첫 시도가 "connect cancelled" 로 끝났다. 축출 0, C 유지 없음.
- 주입 방식:
  - 늦은/옛 메시지는 셸 forwarder 가 UI 로 넘기는 형태 그대로 (`ManagedEvent` → `on_managed_event`) 넣었다. 처리는 셸 자신의 핸들러가 한다.
  - "다시 connect" 와 "송신 실패" 는 화면을 거치지 않고 bridge 의 native 입구 (`handle_page_message`) 를 직접 불렀다. 페이지는 연결 중인 카드를 누를 수 없게 막기 때문이다.

## r3 보수

**R1 — 예약을 하나로**

- 3대 옆에서 시작하는 모든 경우가 **같은 예약 하나** (`gStagingHost`) 를 시작 전에 잡는다: 손으로 하는 ordinary connect, 자동 재연결, 대기 준비.
- 예약이 이미 있으면 두 번째 후보는 거부한다 ("다른 PC 를 여는 중").
- 예약 중에 그 후보를 위해 비울 마지막 standby 를 여는 것도 거부한다.
- 예약 반납 시점: 실패, 취소, 연결 없이 종료, 사용자 해제, owner 변경.
- 성공 (ordinary connect, managed ready) 할 때만 commit 하고, 그 시점에 다시 센다 (`settle_pc_count`).
- `logical_pcs` 는 예약 후보를 ordinary·managed 양쪽에서 모두 뺀다.

**R2 — 연결 통지와 종료 순서**

- 연결 통지에 owner epoch 를 묶었다. `on_ordinary_connected(op, host, epoch)` 는 다른 owner 의 통지를 버린다.
- 로그아웃하면 옛 owner 의 ordinary 슬롯을 registry 에서 **즉시 분리**한다. 그 뒤 종료 (fence, fallback) 는 worker 가 자기 handle 로 맡는다.
- exit 판정은 stdout pump 의 끝을 worker 에서 **최대 2 s 기다린다**. 그보다 늦은 connected 줄은 세지 않는다 (`gEndedBeforePump`, pump 가 끝나면 비운다).

**R3 — fence 를 gate 로**

- `kMsgOwnerFence` 순서:
  1. 입력이 켜진 상태에서 눌린 입력을 **먼저 해제**한다. control-resume 과 같은 집합이다: modifier 전부, 이 창이 보낸 키, 마우스 버튼, scan 키, capture, picker press, macro 정지.
  2. `inputEnabled=false` 와 `ownerFenced` 를 켠다.
  3. clipboard listener 를 해제하고 WM_CLOSE 로 닫는다.
- fenced 동안 drop 하는 것: IME 메시지 (`WM_IME_*`), deferred·restore·unsendable pick.
- payload 를 가진 post 3종 (`kMsgApplyClipboard`·`kMsgApplyWindowList`·`kMsgDecideRemoteFiles`) 은 **적용하지 않고 해제만** 한다.
- IME 결과 경로 (`enqueue_input_text_units`) 는 `inputEnabled` 로 막힌다 (소스로 확인).
- **cancel 이 미리 signal 된** ordinary 는 창을 만들지 않는다.
- cancel watcher 는 창보다 먼저 시작하고, 창·decoder 를 만든 뒤에 다시 확인해서 connect 전에 끝낸다.

**R4 — 상태줄과 카드 표시 분리**

- PC 에 관한 상태는 `hostId` 를 붙인다 (`post_host_status`).
- 페이지는 다른 PC 의 idle/error 로 지금 연결 중인 카드의 표시를 풀지 않는다.
- managed 이벤트, 해제, 거부, 송신 실패의 상태가 모두 hostId 를 갖는다.

**결정 ①** — 연결된 ordinary PC 를 단순히 다시 탭하면 **그 창을 앞으로** 가져오고 (`SetForegroundWindow`) "이미 열려 있습니다" 를 표시한다. 교체는 아직 연결 중인 시도에만 한다.
- 남은 경계: 연결은 됐지만 "connected" 줄이 아직 셸에 도착하지 않은 짧은 순간의 재탭은 연결 중으로 보고 교체한다. R2 의 join 은 이 창을 좁힐 뿐 없애지는 않는다.

**결정 ②** — 자동 재연결 (43/44/46) 도 같은 예약·한도를 따른다. 거부되면 재시도하지 않는다 (timer 해제, 시도 횟수 0). 상태줄은 "PC 3대가 열려 있어 다시 연결하지 않았습니다. 다른 창을 닫은 뒤 이 PC 를 다시 눌러 주세요" 이고 로그는 `reconnect=refused reason=three-pcs`.

## r3 시험

| 시험 | 결과 |
|---|---|
| shell e2e @5a1fb45 | **123/0 ×2** (r3-4/5). r3-1 (@705ed23) 123/0, r3-3 (@d9a75f1) 123/0 |
| managed e2e @705ed23 | **62/0 ×2** (r3-1/2) |
| units | ALL PASS |
| R4 부정 대조 | 페이지만 a864105 판 (8172a343…) 으로 바꿨다: **R4 FAIL** — A 의 SessionEnded 뒤 C 카드가 '연결 가능' 으로 풀렸다. 대조 뒤 12085292… 로 복원한 것을 해시로 확인했다 |

- 같은 대조에서 '…exactly one' 도 FAIL 했는데, 이것은 시험 결함이었다. 셸은 07:40:06 에 정확히 한 번 축출했다. log_has 가 R1 단계의 같은 문구를 먼저 잡았다. 5a1fb45 에서 동작 이후 로그만 읽도록 고쳤다.
- r3-2 shell 은 시험 결함으로 9/27 이었다. 페이지의 restore 메시지가 채워 둔 아이디 칸을 비워 로그인이 전송되지 않았다 (client.log 에 로그인 시도 0). 내 시험 프로세스를 경로로 확인해 중단했고 슬롯은 반납됐다. d9a75f1 에서 로그인을 재시도하도록 고쳤다.

**R1 실측 (호스트 5대):** A 대기, B·C 열림 상태에서 페이지에서 D, A, E 를 한 번에 눌렀다.
- D 만 예약됐다 ("reserved beside three PCs").
- A 는 "the last standby" 로 거부됐다.
- E 는 "another PC is being opened" 로 거부됐다.
- D 가 연결된 뒤 "committed" → "releasing host=A" 가 정확히 1건 나왔다.
- 그 뒤 E 는 "three PCs are in use" 로 거부됐다.
- 호스트가 없는 C 는 연결 없이 끝났고 "reservation … released (ended without connecting)" 로 예약을 돌려줬다.

**R4 실측:** A 를 연 상태에서 호스트가 없는 C 를 탭했다 (연결 중). A 의 창을 닫자 A 의 SessionEnded 가 셸에 도착했지만, C 카드는 '연결하는 중…' 을 유지했다.

**R2 실측:**
- 로그아웃 → 다시 로그인 (새 owner) 한 뒤, 옛 epoch 로 연결 통지 2건을 넣었다. recent 파일 바이트, "committed", "making room" 이 모두 그대로였다.
- (exit 와 pump 의 역순은 정상 실행의 '연결 직후 X' 로만 확인했고, 역순을 강제로 만든 시험은 없다.)

**R3 실측:**
- B 뷰어 로그: "owner fence: held input released (modifierUps=11), input and clipboard cut, closing the session" → exit 0, handle 종료 없음.
- cancel 이 미리 signal 된 GNLinkViewerKaTest: 창 0, connect 0, "called off before the session began", exit 0.

## 남은 것 · 미검증

- **R3 의 IME / deferred pick / 클립보드 payload drop 은 소스로만 확인했다.** 실제 IME 조합이나 원격 클립보드 도착 중에 fence 를 거는 시험은 없다.
- **R1 의 경합:** staged managed 가 대기 중일 때의 다른 탭과 복구 timer 경합, D/E 완료 역순은 시험하지 않았다. 실측한 것은 한 턴에 세 번 누른 경우와 순차 성공·실패·취소다.
- **R2 의 역순:** exit 가 pump 보다 먼저 처리되는 순서를 강제한 시험은 없다.
- `client_recovery_ui_test` (기존 셸 회귀) 는 여전히 실행하지 않았다 (화면 위 창, 저장소 밖 쓰기). Codex 가 이 판단을 수용했다. 영향이 있는 단정은 내부 root 의 화면 밖 seam 으로 옮기는 것이 후속이다.
- 실 NAT, 비용, takeover 반례, 동시 잠금, 401, 셸 종료·계정 전환 실측.

## 삭제 후보 (추가만)

- `D:/remote/remote/.claude/test-tmp/ka-shell-r2b-*`·`ka-managed-r2b-*`·`ka-shell-r3*`·`ka-managed-r3-*`·`ka-units-r3`·`ka-punch-r2b`
- `.claude/np_impl_logs/shell_html_*.html` (대조용 사본)


---

# r4 (2026-10-28, 작업용2) — Codex `codex_ka_pc_src_r3.md` R1~R4 + 결정 ①, 그리고 시험 Host 입력 주입 사건

## 상태

- **제품 보수와 시험 코드는 완료. 실 Host e2e 는 미실행.**
- 검증용 지시(test-injection-finding, 정책 main 14cdde1)에 따라, 시험 Host 의 공용 guard 가 들어오기 전까지 실 Host e2e 를 돌리지 않는다.
  - 공용 guard: 입력 none + 전원·화면 효과 OFF/record-only, 작업용1 ka-r3.
  - 그 commit 을 병합한 뒤 그대로 쓴다. 자체 변형은 만들지 않는다.
- 후보: `feat/ka-pc` **c8f78c3**
  - d81148e: 제품 + 시험
  - c8f78c3: 시험 Host 를 `--input-injection-mode none` 으로
- 5a1fb45..d81148e 의 제품 변경: 11파일 +349/-68. 공용 `standby_*` 변경은 0.

## 시험 Host 입력 주입 사건 (보고·조치 완료)

- **원인**: GNLinkStream 의 `enableInputInjection` 기본값은 **true** 다(`host_args.hpp:47`). 나는 `--enable-input-injection` 플래그가 있다는 이유로 기본이 OFF 라고 잘못 판단했다.
- **영향**:
  - 오늘 ka-pc e2e(r1~r4-1, `test-tmp/ka-*`)의 시험 Host 는 `injectionEnabled=1 mode=background_message` 였다.
  - 뷰어 세션이 시작될 때마다 modifier key-up 11개가 이 PC 의 입력 경로로 갔다. 합계 **2490건**이며 전부 eventKind=6 이다.
  - key-down·mouse·text 는 0건이다(host 로그 집계). 화면에 실제로 반영됐는지는 확인할 수 없다.
- **조치**:
  1. 검증용에 즉시 알렸다.
  2. 모든 시험 Host 를 **`--input-injection-mode none`** 으로 띄우고(Codex 표준), 이번 launch 가 "input injection disabled" 를 말하기 전에는 **뷰어를 연결하지 않는다**(fail-closed).
  3. 기억 메모를 남겼다.
- **중간에 시도했다 버린 방식(`--config`)**: config 파일은 Host 의 env override 를 지운다(`json_profile.hpp clear_runtime_env_overrides`). 그래서 h264 스위치가 사라져 Host 가 "unsupported codec: h264" 로 바로 꺼졌다. r4-2 에서 확인했고, fail-closed 로 뷰어 연결은 0이었다.
- **증거 정정**: Codex 정정(`codex_ka_pc_src_r3.md` 머리)대로, r1~r3 및 r4-1 의 e2e 수치는 기능 관측으로만 보존하고 완료 관문에는 쓰지 않는다.

## r4 보수 (d81148e)

**R1 — 예약이 자리를 지킨다**
- 예약은 성공하거나 반납될 때까지 **PC 하나로 센다**: `occupied = logical_pcs(except) + (예약 있음 ? 1 : 0)`.
- D 를 예약한 뒤 A 를 해제해도 그 빈자리는 E 에게 가지 않는다.
- commit 시점에도 놓아 보낼 standby 가 없어 4번째가 되면 확정하지 않는다. owner-end 신호로 닫고 사용자에게 알린다.

**R2 — pump 종료와 exit 판정을 같은 lock 으로**
- `note_pump_end` / `note_exit_decided` 를 같은 `gConnectedMu` 아래에서 처리한다. 어느 쪽이 먼저 와도 표식이 남지 않는다.
- exit 판정이 먼저면 늦게 온 connected 줄은 세지 않는다.

**R3 — fence 가 release 의 Host 확인을 기다린다**
- fence 순서: held release 를 enqueue → 새 입력을 즉시 gate → 닫기.
- 닫는 조건은 **queue 비었음 ∧ 진행 중인 입력 교환 없음 ∧ control 채널에 미확인 송신 없음** 이다.
  - 입력 교환은 request/response 다. Host 가 각 이벤트에 ack 를 보내며, `inputInFlight` 는 control thread 가 dequeue **전에** 올린다.
  - 위 조건이 2 s 안에 맞지 않으면 그대로 닫는다(죽은 링크에서도 유한하게 끝남).
- 시험 빌드 전용: `REMOTE60_VIEWER_TEST_HOLD_SEND_AFTER_FENCE_MS` 가 fence 뒤 송신을 N ms 붙잡는다.

**R4 — ordinary 상태도 hostId 로**
- ordinary exit·재연결·실패 상태가 모두 hostId 를 갖는다.
- ordinary 가 연결되면 그 카드의 '연결하는 중…' 표시를 끝낸다("<PC> 연결됨"). r4-1 에서 발견했다: 이게 없으면 연결된 카드가 잠긴 채로 남아 결정 ① 의 재탭이 페이지에서 불가능했다.

**결정 ① — 신호 두 개**
- `--cancel-event` 는 **connect 만** 부른다. 이미 성립한 세션에는 "no effect" 다.
- `--owner-end-event` 는 owner 의 종료(로그아웃·계정 전환·셸 종료)이며 fence 를 건다.
- 교체됐지만 이미 연결돼 있던 뷰어는 계속 이 owner 의 것이다(`gReplacedViewers`). 그 뷰어의 종료나 owner 종료 때 회수한다.
- 셸이 connected 로 본 PC 를 재탭하면 창을 앞으로 가져온다(r3 그대로).

## 시험

| 시험 | 결과 |
|---|---|
| units @d81148e | ALL PASS |
| IPC race @d81148e | ALL PASS |
| managed / shell e2e (주입 OFF) | **미실행** — 공용 guard commit 병합 뒤 실행 |
| r4-1 (주입 ON, 무효) | managed 62/0, shell 133/4 — 기능 관측으로만 기록 |
| r4-2 (`--config`) | Host 시작 실패. fail-closed 로 연결 0 |

r4-1 의 기능 관측(근거 아님):
- R3: 송신을 800 ms 붙잡은 상태에서 fence → "releases acknowledged by the host after **844 ms**" → 닫힘. Host 로그에 key-up 11건.
- 결정 ①: connect cancel 신호에 "no effect (session established)", 뷰어 생존.
- R1·R4 혼합 반례와 R2 양순서는 PASS.
- 실패 4건:
  - 최근 순서 판정 시점 오류(시험)
  - R3 Host 확인을 세션 종료 기록 전에 읽음(시험)
  - 결정 ① 재탭 2건: 카드가 잠긴 제품 결함. d81148e 에서 보수했다.

r4 e2e 에 들어간 반례(실행 대기):
- R1: C 예약 중 A 해제 → E 거부
- R4 혼합: ordinary E 종료 중 managed D 를 여는 카드 유지
- R3: Host 로그에서 key-up 이 세션 종료보다 먼저 기록되고 `injectionEnabled=1` 이 0건
- 결정 ①: 재탭 → 이미 열림, connect cancel → no effect
- R2: 양순서

## 남은 것

- 공용 guard commit(작업용1 ka-r3) 병합 → 주입 OFF·전원 효과 OFF e2e 실행 → 이 절에 결과 추가.
- 이전 미검증(IME·clipboard 실조작, 일부 예약 경합, `client_recovery_ui_test`, 실 NAT·비용·takeover·동시 잠금·401·셸 종료/계정 전환)은 그대로 남는다.

### r4 추가 (09:5x) — 공용 test-host guard 병합

- `f3ec7e8` 에 ka-r3 **6b9cf79** 를 merge 했다(git CLI, 충돌 0, 공용 파일은 그대로 둠).
- `42782c2` 에서 두 e2e 가 guard 를 **수정 없이** 쓰도록 바꿨다.
  - 실행 인자: `e2e_injection_off_arg` (`--input-injection-mode none --host-power-management off`)
  - 연결 전 확인: `e2e_wait_injection_off`. 이번 launch 의 두 줄(입력 OFF, 전원 OFF)이 모두 나와야 하고, 아니면 `e2e_injection_setup_failed`(INVALID, exit 2)로 끝낸다.
  - 실행 후 확인: `e2e_injection_contradicted`(rule 4)에 걸리면 INVALID 로 판정한다.
- 공개판 0.2.163 Host 경우는 **NOT RUN** 으로 바꿨다. 옛 binary 는 이 PC 에서 띄우지 않는다는 guard 규칙 때문이다. 이 경우는 미검증으로 남는다.
- 실 Host 없이 도는 시험 결과(@42782c2): guard 20/0, units ALL PASS, IPC race ALL PASS, standby_lease 75/0.
- 실 Host e2e 는 아직 돌리지 않았다. 검증용이 Codex SOURCE OK 와 슬롯 해제를 알려 주면 바로 실행한다. 후보는 **42782c2** 다.


---

# r5 (2026-10-28, 작업용2) — Codex `codex_ka_pc_src_r4.md` P1 3건 (R1-a · R3-a · R3-b)

## 고정 후보

- `feat/ka-pc` **1c321d1** (42782c2 위 1 commit, tracked clean, `git diff --check` rc 0).
- r5 제품 변경 (42782c2..1c321d1, 9파일 +442/-23 중 제품):
  - `client_shell_main.cpp`
  - `viewer_control_client.cpp`
  - `viewer_control_state.hpp`
  - `viewer_session_state.hpp`
  - `viewer_window_proc.cpp`
  - `viewer_owner_fence.hpp` (새 header)
- 나머지는 시험 2개와 CMake 다. 공용 `standby_*`·guard 파일 변경은 0이다.
- 빌드 sha256 앞 16자리:
  - GNLinkViewer.exe cc5e426245d6c5cb
  - GNLinkClient.exe f14c5942e7f2bf8d
  - GNLinkViewerKaTest.exe a99bf4f751245ff0
  - remote60_ka_pc_fence_test.exe 45ff6844b29d368f
  - remote60_ka_pc_shell_logic_test.exe 166b6e91320d8a60
- 출하 GNLinkViewer 에서 `REMOTE60_VIEWER_TEST` 는 0건이다.
- 실 Host 실행은 0이다(검증용 지시).

## 보수

**R1-a — 교체로 남은 viewer 를 그 PC 로 센다**
- `ViewerCancelSlot` 에 `hostId` 를 넣었다. struct 끝에 두어 기존 brace 초기화의 의미가 바뀌지 않는다.
- `logical_pcs` 는 `gReplacedViewers` 를 PC 별 합집합에 넣는다.
- `pc_visible` 도 이 viewer 를 화면에 있는 것으로 본다. 그래서 standby 를 축출할 때도 이 PC 를 보이지 않는 것으로 취급하지 않는다.
- 한 PC 의 viewer 두 개(남은 옛 것과 새 교체)는 한 PC 로 센다.

**R3-a — fence 확인은 InputAck 로만**
- control thread 가 실행한 입력 교환을 센다: `taken` / `acked`(Host 의 InputAck) / `failed`.
- fence 는 release 를 넣기 **전에** 이 카운터를 기록한다.
- `fence_release_verdict` 판정:
  - Confirmed: 그 뒤 시작된 교환이 모두 acked 됐을 때만.
  - Failed: 하나라도 실패하면 즉시 "NOT confirmed (an exchange failed)" 로 닫는다.
  - 2 s budget 초과는 "NOT confirmed within the budget".
- queue 가 비었거나 channel 이 비었다는 것은 보조 조건일 뿐이고, ack 로 치지 않는다.

**R3-b — fence 뒤에는 release 만 간다**
- fence 때 UI thread 가 즉시 하는 일 (`owner_fence_cancel_pending`):
  - 큐에 남은 release 가 아닌 입력(down·text·move·wheel)을 버린다.
  - pending clipboard text 를 zero 하고, clipboard sync 를 끈다(`enabled=false` 이므로 `pump_clipboard_sync` 가 보내지도 poll 하지도 않는다).
  - 봉인 대기 중인 비밀번호를 zero 한다.
  - 이미지·파일 offer 를 `Stop` 한다.
- 그 뒤 control thread 는 **release 만** 큐에서 바로 꺼내 보낸다(`take_release_action`).
  - ping 예약 작업, pick, stream state, clipboard, credential, IME align 은 보내지 않는다.
  - release 교환이 실패하면 resume 하지 않는다.
- fence 상태는 atomic `ownerFenceAtMs` 로 읽는다. UI bool 을 무동기로 읽지 않는다.

## 회귀 시험 — 보수 전 FAIL → 보수 후 PASS (실 Host·창·네트워크·clipboard API·실 비밀번호 없음)

| 건 | 시험 | 보수 전 | 보수 후 |
|---|---|---|---|
| R1-a | `ka_pc_shell_logic_test`: 셸의 **실제** `logical_pcs`/`pc_visible`/`reserve_for_new_pc` 를 메모리 registry 로 실행한다. replaced A 는 살아 있고 map 에는 B·C 만 있다 | r5 두 줄을 뺀 mutant: **FAIL 3** — logical=2, A 가 보이지 않음, "D admitted (four PCs open)". 로그 `np_impl_logs/ka_r5_logic_neg.log` | **ALL PASS** — 3대로 세고 D 거부, 같은 PC 의 두 viewer 는 1대 |
| R3-a | `ka_pc_fence_test`: 마지막 release 교환이 실패(ack 1, fail 1)한 뒤 queue 가 비고 inFlight 도 없는 상황 | d81148e 의 판정식(원문 복사) → **delivered=1** (결함 재현) | `fence_release_verdict` → **Failed**. 정상 경로(모두 ack)는 Confirmed, 교환 중이면 Pending, fence 이전에 진행 중이던 교환은 그대로 Confirmed |
| R3-b | 같은 시험: pending clipboard 와 비밀번호, 큐에 down·text·up | r4 fence 는 이 상태를 건드리지 않음 → pump gate(`enabled && hostSupports`)가 열려 있고 pending 이 남음 → **보낸다** | clipboard 꺼짐 + pending 없음, 비밀번호 zero, down·text 는 버리고 **release 1건만** 나간다 |

- mutant 는 소스를 임시로 바꿔 만들었다. 대조 뒤 원본을 복원했고 sha256 db57b771… 로 일치를 확인했다. 재빌드 뒤 다시 PASS 했다.
- 그 밖에 실 Host 가 필요 없는 시험: units, IPC race, 공용 injection guard 시험 — 모두 PASS.

## 실행하지 않은 것

- 실 Host e2e (managed·shell). 공용 guard 의 Codex SOURCE OK 와 슬롯 해제를 기다리는 중이다.
- 시험 빌드 전용 `REMOTE60_VIEWER_TEST_FAIL_RELEASE_AFTER_FENCE=1` (fence 뒤 첫 release 교환을 실패시킴)은 넣어 두었다. 이것을 쓰는 실 viewer 경로 시험(실패 → "NOT confirmed")은 그 실행 때 함께 붙인다.
- 실제 IME 조합·클립보드 조작·자격증명을 쓴 확인은 하지 않는다(사용자 PC). 기록 fixture 로만 확인했다.


---

# r6 (2026-10-28, 작업용2) — Codex `codex_ka_pc_src_r5.md` F1 · F2 · T1

## 고정 후보

- `feat/ka-pc` **e1acbb5** (1c321d1 위 1 commit, tracked clean, `git diff --check` rc 0, 8파일 +514/-235)
- 제품 파일
  - `viewer_owner_fence.hpp` (다시 씀)
  - `viewer_control_state.hpp`
  - `viewer_session_state.hpp`
  - `viewer_window_proc.cpp`
  - `viewer_control_client.cpp`
  - `viewer_clip_image_wiring.cpp`
- 시험: `ka_pc_fence_test.cpp`, `ka_pc_shell_logic_test.cpp`
- 셸(`client_shell_main.cpp`) 변경 0, 공용 standby·guard 파일 변경 0
- 빌드 sha256 앞 16자리
  - GNLinkViewer.exe f3349791928c09c7
  - GNLinkClient.exe f14c5942e7f2bf8d (r5 와 같음)
  - GNLinkViewerKaTest.exe ae1b6d8c1c878f90
  - remote60_ka_pc_fence_test.exe 6ceaaaf9a25e513d
  - remote60_ka_pc_shell_logic_test.exe 9aaeb557832087fd
- 출하 GNLinkViewer 에 두 시험 env marker(`…HOLD_SEND_AFTER_FENCE_MS`, `…REFUSE_RELEASE_WRITE_AFTER_FENCE`)가 ASCII·UTF-16 모두 0건이다.
- 실 Host 실행 0건.

## 보수 — fence 를 하나의 lock 아래 하나의 상태로

`OwnerFenceGate` 는 다음을 모두 한 mutex 아래에 둔다: fenced, releaseExchange, inFlight, taken/acked/failed/skipped, 그리고 fence 때의 baseline. 이 필드를 건드리는 코드는 `viewer_owner_fence.hpp` 의 함수뿐이다.

**F1 — 일관된 관측**
- UI 의 fence 는 `fence_raise` 를 **가장 먼저** 부르고, 같은 critical section 안에서 baseline 을 잡는다.
- control thread 는 큐에서 입력을 꺼내는 일과 그것을 세는 일(taken·inFlight·release 여부)을 **한 critical section** 에서 한다.
  - 일반 branch: scheduler 의 `NextAction` 자체를 `gate_begin_action` 안에서 실행한다.
  - fenced branch: `gate_take_release` 를 쓴다.
- 판정 `fence_verdict` 는 queue 비었는지·inFlight·카운터·baseline 을 **같은 lock 안에서** 한 번에 읽는다. 따라서 "꺼냈지만 아직 안 센" 순간이 어떤 판정에도 보이지 않는다.
- Confirmed 조건: baseline 이후 모든 입력 교환이 InputAck 를 받았거나, 로그온 화면 gate 때문에 일부러 보내지 않은 것(skipped)뿐일 때.
- 하나라도 실패하면 즉시 Failed 다. 2 s budget 은 그대로다.

**F2 — fence 와 송신 시작의 중재**
- 루프의 **모든** 제어 쓰기는 `FenceGatedLink` 를 거친다.
  - fence 뒤에는 **집계된 release 교환만** 쓰기를 시작할 수 있다.
  - 쓰기의 "시작"은 gate 가 허락한 순간이다. fence 전에 허락된 쓰기는 fence 전에 시작된 것으로 보고 자르지 않는다. 거부된 쓰기는 아예 시작되지 않는다.
  - 이 시작 판정과 fence 는 같은 lock 아래에서 정해진다. 둘 사이에 무동기 check→send 틈이 없다.
- sender 가 공유 pending 에서 이미 가져간 clipboard 사본은 보냈든 거부됐든 즉시 zero 한다.
- 이미지·파일 bulk datagram 도 gate 가 거부한다.
  - fence 가 worker join(`Stop`)을 기다리지 않게 했다. join 은 세션 teardown(`shutdown_viewer`)이 한다.
- 거부 때문에 실패한 교환은 release-only branch 로 돌아간다. resume 하거나 루프를 끝내지 않는다.
  - 대상: 일반 action, clipboard, 썸네일, IME align.
- Resuming:
  - 이미 진행 중인 resume 은 fence 에서 멈춘다.
  - fence 뒤에는 `begin_control_resume` 이 거부한다.
  - resume probe 도 gate 를 거쳐 쓴다.
- release-only take 의 조건
  - **release 규칙**(공용 core 의 `release_identity` 와 같은 규칙)을 helper 안에서 직접 검사한다. down·move·wheel·text 는 버리고 보내지 않는다.
  - 기존 **로그온 화면 gate** 도 그대로 거친다: r7 H6 `ViewerMaySendAction`, r9 prelogin allowlist, 보내지 않은 down 의 up.
  - 그 gate 에 걸린 것은 skipped 로 센다.

**T1 — 시험 root**
- `REMOTE60_TEST_TMP` 는 다음을 모두 만족해야 한다.
  - `GetFullPathNameW` 로 정규화한 경로의 부모가 정확히 `D:\remote\remote\.claude\test-tmp` 다.
  - 그 anchor 와 root 둘 다 reparse 가 아닌 실제 폴더다.
  - 이번 실행이 그 안에 **새** `run-<pid>-<tick>` 폴더를 만들 수 있어야 한다. 이미 있으면 거부한다.
- 앞의 shelldata 를 다시 쓰지 않는다.
- 실측
  - 저장소 밖 `C:\work\elsewhere-test-tmp`: 거부, 폴더 만들지 않음.
  - `…\test-tmp\..\x2`: 거부, 만들지 않음.
  - anchor 자체: 거부.
  - 정상 경로: 새 run 폴더 1개.

## 시험 hook 의 의미 (Codex 지적 그대로 기록)

- r5 의 `REMOTE60_VIEWER_TEST_FAIL_RELEASE_AFTER_FENCE` 는 `execute_control_action` 을 **실제로 실행한 뒤** 결과만 false 로 바꿨다.
  - 그래서 그것은 write 거부나 Host ACK 부재를 만든 시험이 **아니었다**. 실패 결과 처리의 대조로만 쓸 수 있었다.
- r6 는 이 hook 을 지우고 `REMOTE60_VIEWER_TEST_REFUSE_RELEASE_WRITE_AFTER_FENCE` 로 바꿨다.
  - gate 가 fence 뒤 첫 release **쓰기 자체를 거부**한다. 바이트가 하나도 나가지 않으므로 Host 는 ACK 할 수 없다.
  - 이 hook 은 시험 빌드(`GNLinkViewerKaTest`)에만 있다.
  - 이 hook 을 쓰는 실 viewer 시험은 아직 실행하지 않았다(실 Host 대기).

## 회귀 시험 — 보수 전 FAIL → 보수 후 PASS (실 Host·창·네트워크·clipboard API·실 비밀번호 없음)

| 건 | 시험 | 보수 전 (r5, 1c321d1 원문 복사) | 보수 후 (r6 실제 함수) |
|---|---|---|---|
| F1 | Codex 의 스케줄을 실제 두 번째 thread 로 만든다. timer 가 inFlight=false 를 읽은 뒤, sender 가 마지막 release 를 dequeue 하고 taken++ 전에 멈춘다(300 ms). | **Confirmed** — ACK 0, release 는 sender 손에 있음 | timer 의 판정이 handoff 가 끝날 때까지 기다린 뒤(297 ms) **Pending**. 이어서 ACK 없이 실패 → **Failed** |
| F1 | 정상 경로: fence 전 진행 중 교환 + fence 뒤 release | — | Pending → (release 가져감) Pending → ack → **Confirmed** |
| F2 | sender 가 pending clipboard 를 로컬로 옮긴 뒤 UI fence(공유 zero·sync off), 그다음 sender 송신. "link" 는 기록기다. | **writes=1** — fence 뒤 clipboard 송신 | gate 가 거부해 **writes=0** |
| F2 | fence 뒤 일반 branch: pick(WindowSelect)·key down / key up | — | pick·down 은 **Refused**(쓰지 않음), up 은 Admitted 되어 쓰기가 나감. 교환이 끝나면 다시 거부. bulk 거부 |
| F2 | fenced take: 큐에 down + up | — | down 은 버리고 **up 만** 가져감 |
| T1 | root 경계 | r5 의 substring 검사는 `…\test-tmp\..\x` 를 받아들여 **`D:/remote/remote/.claude/x`(빈 폴더)를 만들었다**. 내 실수로 빌드가 실패한 상태에서 옛 exe 가 돌아 우연히 재현됐다. 지우지 않고 삭제 후보에만 올렸다. | 위 4가지 경로 모두 기대대로 |

- units, IPC race, 공용 injection guard 시험: 모두 PASS
- `ka_pc_shell_logic_test` (R1-a): ALL PASS

## 남은 것

- 실 Host e2e 와 그 안의 실 viewer 경로 시험(REFUSE_RELEASE_WRITE hook 사용)은 공용 guard SOURCE OK 와 슬롯 해제를 기다린다.
- 실 IME·clipboard·자격증명 조작은 하지 않는다(사용자 PC). 기록 대역으로만 확인했다.


---

# r7 (2026-10-28, 작업용2) — Codex `codex_ka_pc_src_r6.md` N1 · N2 · 완료 문구

## 고정 후보

- `feat/ka-pc` **cd9937a**
  - e1acbb5 위 1 commit, 8파일 +257/-37
  - tracked clean, `git diff --check` rc 0
- 제품 변경
  - `native_video_client_shared_core.cpp/.hpp`: 공유 release 규칙. 셸·뷰어 클라이언트 공용 core 이며, 공용 `standby_*` 는 아니다.
  - `viewer_owner_fence.hpp`
  - `viewer_control_state.hpp`
  - `viewer_control_client.cpp`
  - `viewer_window_proc.cpp`
- 시험 변경: `ka_pc_fence_test.cpp`, `ka_pc_shell_e2e_test.cpp`(로그 문구 1곳)
- 닫힌 항목은 다시 손대지 않았다: F1 lock/handoff, F2 사본, T1, join 제거, resume/role, R1/R2/R4, 두 신호.
- 빌드 sha256 앞 16자리
  - GNLinkViewer.exe ddda8e8c9423cc6b
  - GNLinkClient.exe f14c5942e7f2bf8d (변경 없음)
  - GNLinkViewerKaTest.exe bdff93631077fe96
  - remote60_ka_pc_fence_test.exe 473149823dbc3256
- 출하 Viewer 안의 두 시험 env marker: ASCII·UTF-16 모두 0건
- 실 Host 실행: 0건
- `.claude/x` 는 보존했다(삭제하지 않음).

## 보수

**N1 — X1/X2 up 도 release 다**
- 공유 core 의 release 규칙(`release_identity`)이 mouse up 을 VK 1·2·4 뿐 아니라 **5·6(X1·X2)** 까지 release 로 센다.
- 같은 규칙을 `control_input_release_identity` 로 공개했다. gate 의 `action_is_release` 는 사본 없이 이 하나의 규칙을 호출한다.
- 결과
  - fence 전에 큐에 있던 X up 이 `DropAllButReleases` 에서 살아남는다.
  - fence 가 새로 만든 X up(`enqueue_release_for_pressed_mouse_buttons`)도 drain 이 가져간다.
- X edge 를 Host 에 보내도 되는지는 기존 viewer capability fence(`enqueue_input_event`/`fence_mouse_for_host`)가 그대로 정한다. 이번 변경은 규칙만 바꿨다.

**N2 — fence 가 첫 byte 전에 거부한 쓰기는 '취소'다**
- gated link 가 거부 이유를 gate 에 기록한다.
  - fence 가 메시지의 **첫 byte 전에** 거부
  - 메시지 **중간에** 거부
  - 시험 hook 거부
- 실패한 교환을 다루는 caller 는 모두 공통 결과 타입 `gate_take_failure` 로 그 이유를 한 번 받는다. 대상: 일반 action, input, clipboard, IME, thumbnail.
- 분류
  - **NotStarted(정책상 미송신)** → `InputOutcome::Cancelled`. 남은 release drain 을 계속한다.
  - **Broken(메시지가 잘림)**, **HookRefused(release 거부)**, **Transport(거부 없는 실제 실패)** → Failed. fence 뒤에는 resume 하지 않으므로 loop 가 끝난다.
- 손상된 교환을 fence 취소로 착각해 계속 쓰는 일은 없다. 반대로 모든 실패를 Skipped 로 돌리지도 않았다.
- release 교환은 gate 가 허가하므로 취소될 수 없다. release 의 실패는 언제나 Failed 다.

**완료 문구**
- verdict 의 tally 가 다음을 따로 센다: ack, 로그온 gate skip, fence 취소, failed, taken.
- 로그 예: `owner fence: releases confirmed after N ms -- since the fence: A acknowledged by the host (InputAck), S not sent by the logon-screen gate, C non-release(s) cancelled by the fence before a byte, F failed, of T taken`
- 보내지 않은 것을 InputAck 로 부르지 않는다.

## 회귀 시험 — 보수 전 FAIL → 보수 후 PASS (실 Host·창·네트워크·실제 입력 없음)

| 건 | 시험 | 보수 전 | 보수 후 |
|---|---|---|---|
| N1 | 공유 규칙에 X1·X2 up / left up / X down 을 넣는다. fence 전에 queued 된 X1 up 을 cancel 한 뒤 drain 한다. 제품 함수 `enqueue_release_for_pressed_mouse_buttons` 로 X1·X2·left 를 held 로 두고 fence 한다. | 공유 규칙의 r7 줄을 뺀 **mutant: FAIL 3**. X up 이 버려지고 drain 이 X 를 하나도 가져가지 않는다(x=0). 그런데도 verdict 는 **Confirmed** 다. Codex 가 지적한 결함 그대로다. 로그 `np_impl_logs/ka_r7_fence_neg.log` | X 를 받는 Host: X up 2 + left 1 을 가져간 뒤 Confirmed. **X 미지원 Host: X edge 0**, left 1 |
| N2 | down 이 fence 전에 Admitted → fence → held Ctrl up 이 queued → down 의 Write 가 0 byte 로 거부된다(실제 gate/link). | r6 의 결과 매핑(!ok → Failed): 즉시 **Failed**, Ctrl up 은 아직 큐에 있음, wire 0 | **Pending** → Ctrl up 송신·ack → **Confirmed** (ack 1, cancelled 1, failed 0, wire 1) |
| N2 | 메시지 중간 거부 / hook 거부 / ack 없는 release | — | 각각 Broken / HookRefused / Transport 로 분류되고 모두 **Failed**. 이전에 쓰기를 시작한 메시지가 fence 뒤 중간에서 거부되면 Broken 으로 기록된다. |

- mutant 는 소스를 임시로 바꿨다가 복원했다. 복원 뒤 sha256 703a407d… 로 일치를 확인했고, 재빌드 후 ALL PASS 다.
- units / IPC race / 공용 injection guard / shell logic: 모두 PASS.

## 남은 것

- 실 Host e2e 와 그 안의 실 viewer 경로 시험(hook 을 이용한 release 쓰기 거부 → "NOT confirmed")은 공용 guard SOURCE OK 와 슬롯 해제를 기다린다.
- 실 X 버튼 주입은 하지 않는다(사용자 PC). 기록 queue·gate 로만 확인했다.


---

# 시험 하네스 — 공용 guard 적용 (2026-10-28, 작업용2; 제품 후보 cd9937a 는 고정)

## commit

- **c0271a7**: cd9937a + merge ka-r3 **b8dcc93**(공용 guard 최종 안전 SOURCE OK)
  - git CLI 로 merge 했고 충돌은 0이다.
  - PC 제품 파일(client_shell_main·viewer_*·ui)은 cd9937a 와 같다.
- **ce0a672**: ka_pc_managed_e2e_test.cpp 와 ka_pc_shell_e2e_test.cpp 를 공용 guard 그대로 쓰게 바꿨다(자체 변형 없음).

## 바꾼 것

- **child 시작·출력 읽기 (모든 child)**
  - `e2e_adopt_or_invalid` 로 시작한다. kill-on-close 가 확인된 job 안에서만 resume 하고, 확인이 안 되면 INVALID 다.
  - 출력은 `e2e_read_to_end` 로 끝까지 읽는다. 끝은 확인된 EOF 만 인정한다.
- **시험 Host 판정 (모든 시험 Host)**
  - 인자는 `e2e_injection_off_arg` 다: `--input-injection-mode none --host-power-management off`.
  - `Guard()`:
    - client 를 만들기 전에 그 launch 가 직접 출력한 OFF 두 줄을 `e2e_wait_injection_off` 로 확인한다. 아니면 `e2e_injection_setup_failed`(INVALID, exit 2).
    - 확인 뒤에는 `E2eInjectionMonitor` 가 계속 감시한다.
  - 종료(`End()`)는 최종 판정이다: `e2e_end_job_confirmed` → reader EOF → `e2e_final_pipe` → `Finalize`.
  - 결과를 내기 전에 `e2e_injection_final_check_all` 을 부른다.
- **fixture 와 임시 폴더**
  - 검사를 통과한 `REMOTE60_TEST_TMP` 안에, 이번 실행이 **새로** 만든 `run-<pid>-<tick>` 폴더만 쓴다. 이미 있으면 거부한다.
  - TEMP/TMP(시험 자신과 모든 child)도 그 run 폴더 안으로 돌린다.
- **%TEMP% 점검 결과**
  - ka-pc 시험 자체에는 GetTempPath/%TEMP% 생성·삭제가 없었다.
  - 다만 shell e2e 가 부르는 제품 `create_shell_webview` 는 `GetTempPath + "GNLinkClient"`(실제 GNLinkClient 와 같은 WebView 프로필)를 쓴다. 이번에 TEMP 를 run 폴더로 돌려서 그 경로도 repo 안으로 들어간다. 제품 코드는 바꾸지 않았다.
  - ⚠️ 이전 실 Host 실행(r2~r4)에서 shell e2e 가 사용자 `%TEMP%\GNLinkClient` 를 썼을 수 있다. 확인·삭제는 하지 않았다.
- 공개판 0.2.163 Host 경우는 NOT RUN 이다(guard 규칙).

## 실행

- 0. 빌드만 확인했다. 시험 SOURCE 와 슬롯 조건을 기다린다.


---

# 시험 r2 (2026-10-28, 작업용2) — Codex `codex_ka_pc_test_src_r1.md` N1 · N2

## 고정 후보

- `feat/ka-pc` **3146c14**: ce0a672 위의 commit 1개.
  - 변경 파일
    - `ka_pc_managed_e2e_test.cpp`
    - `ka_pc_shell_e2e_test.cpp`
    - CMakeLists(시험 target 1개 추가)
  - 신규 파일
    - `ka_pc_e2e_child.hpp`: 두 e2e 공용 Child
    - `ka_pc_e2e_child_test.cpp`
  - `git diff --check` rc 0, tracked clean.
- 제품 파일: cd9937a 대비 **0줄**. 확인 대상은 다음과 같다.
  - `client_shell_main.cpp`
  - `viewer_*`
  - `native_video_client_*`
  - `client_managed_viewers.hpp`, `managed_viewer_ipc.hpp`
  - `ui/`
- 공용 guard `e2e_injection_guard.hpp`: c0271a7 대비 **0줄**(변형 없음).
- 빌드 sha256 앞 16자리
  - GNLinkClient.exe f14c5942e7f2bf8d (변경 없음)
  - GNLinkViewerKaTest.exe bdff93631077fe96 (변경 없음)
  - GNLinkStream.exe f35143b25c37df9d
  - remote60_ka_pc_managed_e2e_test.exe 2b382c01c76adbf2
  - remote60_ka_pc_shell_e2e_test.exe 40ab396291fb4e60
  - remote60_ka_pc_e2e_child_test.exe c569bf30f32fe747
- **실 Host·WebView e2e 실행: 0건.** 사용자 profile·registry 의 열람·변경·삭제: 0건.

## N1 — 셸 WebView 프로필

모든 확인은 **child 를 하나도 띄우기 전, WebView environment 를 만들기 전에** 한다.

1. **TEMP/TMP**
   - run 폴더가 자기 자신으로 resolve 되는지 `GetFinalPathNameByHandleW` 로 확인한다.
   - `run\temp` 를 새로 만든다.
   - TEMP/TMP setter 의 성공을 확인한다.
   - 제품과 같은 `GetTempPathW(MAX_PATH)` 의 실제 반환값을 run temp 와 대조한다.
   - managed 시험에도 같은 함수를 적용했다.
2. **UDF**
   - `run\temp\GNLinkClient` 로 정한다. 새 경로여야 하고, run 안이어야 하고, MAX_PATH 안에 들어가야 한다. 제품의 `wcscat_s` 가 넘치지 않는지도 함께 확인한다.
   - `WEBVIEW2_USER_DATA_FOLDER` 를 이 경로로 설정하고 다시 읽어 확인한다.
3. **override 거부**
   - UDF 외의 `WEBVIEW2_*` 환경변수가 하나라도 있으면 거부한다.
   - `SOFTWARE\Policies\Microsoft\Edge\WebView2` 의 key 와 value 를 HKLM/HKCU 양쪽, 64/32 view 모두에서 **읽기만** 해서 확인한다. 하나라도 있거나 읽을 수 없으면 거부한다.
   - env 와 registry 중 어느 쪽이 우선하는지 몰라도 되게 했다. 둘 다 없어야 통과한다.
4. **추가 browser args 의 경계: 없음.** 제품은 options 로 nullptr 를 넘기고, 해당 변수와 policy 도 없어야 통과한다. runtime 버전은 기록만 한다.
5. **실패 시**: `FAIL SETUP … RESULT: INVALID`, exit 2. **CreateCoreWebView2Environment 는 한 번도 호출하지 않는다.** 제품이 environment 를 만들기 직전에 같은 preflight 를 한 번 더 한다.
6. **거부 대조 3건** (environment 없이, 같은 함수로)
   - 소유한 다른 경로 `run\other-root\GNLinkClient`
   - 추가 변수 `WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS`
   - 다른 경로로 옮긴 UDF 변수
   - 셋 다 거부돼야 하고, 원복 뒤에는 다시 통과해야 한다. 하나라도 어긋나면 INVALID 다(거부하지 못하는 preflight 는 근거가 아니다).
7. **실제 사용 경로 확인**
   - 제품이 environment 를 만든 뒤, `ICoreWebView2_2::get_Environment` → `ICoreWebView2Environment7::get_UserDataFolder` 로 실제 경로를 얻는다.
   - 이 경로가 run UDF 와 같고, final path 도 run 안이어야 한다.
   - 이 확인 전에는 **sign-in 도, 어떤 client 도** 진행하지 않는다.
   - 불일치하면 run 을 멈춘다(INVALID). "다른 폴더였다면 이미 닿았을 수 있다" 고 기록하고, 아무것도 지우지 않는다.
8. **download·새 창·OS dialog**
   - `DownloadStarting` 은 Cancel + Handled 로, `NewWindowRequested` 는 Handled 로 즉시 거부한다.
   - 이 시험 프로세스의 `#32770` 창, 또는 이 프로세스 창이 owner 인 `#32770` 창(파일 dialog 포함)은 감시 thread 가 닫는다.
   - 위 셋 중 하나라도 생기면 run 을 멈추고 실패로 처리한다. 멈출 때의 순서는 다음과 같다.
     1. 셸 창을 닫는다(제품 viewer 들에 종료를 알림).
     2. child 전부를 End 로 판정한다. Host 는 Finalize 를 거친다.
     3. guard 의 final check 를 한다.
     4. FAILED 로 끝낸다.
9. **보존**: profile 과 로그는 남긴다. UDF·browser 는 삭제하지 않는다.

**남은 틈(이름 붙여 둔다):** 제품 callback(`create_shell_webview`, 동결된 cd9937a)이 controller 를 만든 직후 같은 callback 안에서 로컬 `file:///…/ui/shell.html` 로 **Navigate 한다.** 그래서 이 Navigate 는 getter 확인보다 먼저다.
- getter 확인은 sign-in 과 모든 client 보다 앞선다.
- 그 전에 preflight 가 env·policy override 를 막고, 같은 UDF 를 env 변수로도 고정한다.
- Navigate 자체까지 getter 뒤로 미루려면 제품 파일에 `#ifdef REMOTE60_SHELL_TEST_SEAM` hook 을 새로 넣어야 한다. 이번에는 제품 파일을 건드리지 않았다. 그 hook 을 넣을지는 Codex 가 정한다.

**한계**
- **registry 거부 대조는 하지 않았다.** registry 를 쓰지 않기 위해서다. 현재 policy key 가 없다는 것은 이 PC 의 **지금** 상태이고, 과거 상태는 아니다.
- **파일 dialog 감지는 owner 창 기준이다.** WebView 가 owner 없이 띄우는 창은 잡지 못할 수 있다.
- **script dialog(alert)는 따로 막지 않았다.** 제품 settings 를 바꾸지 않았기 때문이다.

## N2 — 모든 child 의 종료 판정 (공용 `ka_pc_e2e_child.hpp`)

- **Start**: 다음 각 단계의 결과를 확인한다.
  - CreatePipe
  - read 끝 비상속
  - NUL
  - attribute list 의 크기 산정과 Initialize
  - `UpdateProcThreadAttribute(HANDLE_LIST)`
  - CreateProcess

  실패하면 자기 handle 만 닫고 **process 를 만들지 않으며**(Resume 도 없음) 실패로 센다. managed 의 IPC pipe 4개도 같은 방식으로 확인한다.
- **로그**
  - child 마다 **자기 로그를 새로 만든다**(CREATE_NEW). 이미 있으면 거부하고 덮어쓰지 않는다.
  - 출력은 **읽는 즉시 Win32 WriteFile 로** 쓴다. 이후 어디서 `_Exit` 가 나도 이미 읽은 부분은 남는다. 쓰기 실패도 실패로 센다.
  - `add-account` child 에도 로그를 붙였다.
- **End (한 번만)**
  - 시작 안 된 child(NOT RUN 인 hostOld 등)와 이미 끝난 child 는 건너뛴다.
  - 순서
    1. job 을 끝내고 그 종료가 확인됐는지 판정한다.
    2. reader 를 기다린다(5 s). 그래도 막혀 있으면 `CancelSynchronousIo` 로 끊는다(2 s). 끝내 못 끝내면 trailer 를 쓰고 INVALID 로 종료한다. hang 도 pass 도 없다.
    3. join 한다.
    4. **trailer `<log>.end`**(producer end / read end / error / bytes / log kept)를 쓴다. Host 의 Finalize 가 run 을 끝낼 수 있으므로 반드시 그 **전에** 쓴다.
    5. Host 는 Finalize 한다.
    6. **모든 child** 에 generic 판정을 한다: 종료 확인 + EOF + 로그 보존. 하나라도 아니면 실패로 센다.
  - Host OFF 문구는 Host 에만 요구한다(monitor).
- **결과 직전**: `children_end_all()` 이 살아 있는 child 를 모두 End·판정한다. 한 줄 판정(`check`)과 guard final check 를 거친 뒤에 결과를 낸다. 그래서 소멸자에서 결과 뒤에 판정되는 child 는 없다.

## 시험 — `ka_pc_e2e_child_test` (Host·viewer·창·network·WebView 없음; child 는 이 exe 자신)

| 건 | 내용 | 결과 |
|---|---|---|
| 판정 | 종료 미확인 / read Error / Cut / Running / 로그 미보존 → 각각 실패. 확인 + EOF + 보존 → 통과 | PASS 6 |
| 설정 실패 | pipe·noinherit·NUL·attrsize·attrinit·**HANDLE_LIST** 각 단계를 시험 seam 으로 실패시킴 → Start 거부, process 0, 실패 1건씩 셈 | PASS 6 |
| 실제 child | job 안에서 시작 → **실행 중에 이미 로그에 기록됨** → End: 확인 + EOF + 보존, trailer 일치, 두 번째 End 무변화, 시작 안 된 child 의 End 는 실패 아님 | PASS 6 |
| Finalize INVALID | 시나리오 프로세스(자신도 Child)가 OFF 문구 없는 child 를 Host monitor 로 Finalize → exit 2, `RESULT: INVALID`. **그 전에 읽은 줄이 로그에 있고, trailer 도 Finalize 전에 기록됨** | PASS 3 |

- 전체 **ALL PASS**, exit 0.
- **mutant**(로그를 즉시 쓰지 않음 + trailer 를 Finalize 뒤로 옮김): **FAIL 3** — 실행 중 로그, Finalize 전 로그, Finalize 전 trailer. 정확히 이번 결함이다.
- 원복 sha256 30795f3f… 일치를 확인했고, 재빌드 후 ALL PASS 다.
- 원본은 `np_impl_logs/ka_pc_e2e_child.hpp.orig` 에 보관했다.
- **직접 유발하지 않은 것**
  - 실제 OS 의 attribute API 실패. seam 으로 그 단계만 실패시켰다.
  - 실제로 종료가 확인되지 않는 child. 판정 함수 단위로만 확인했다.

## 과거 영향 기록 (profile 격리 finding)

| 분류 | 내용 |
|---|---|
| source 사실 | 제품 `create_shell_webview` 는 `GetTempPathW + "GNLinkClient"` 를 UDF 로 요청한다. ce0a672 이전 셸 e2e 는 TEMP/TMP·`WEBVIEW2_USER_DATA_FOLDER` 를 지정하지 않았다. 실행 스크립트(`ka_r2_slot_run.ps1`)도 `REMOTE60_TEST_TMP` 만 설정했다. 남은 run root 어디에도 `temp` 폴더가 없다. 즉 UDF 는 run 밖, **상속된 TEMP** 기준으로 요청됐다. |
| 조건부 가능성 | 당시 상속 TEMP 가 사용자 TEMP 였고 override 도 없었다면, UDF 가 사용자 GNLinkClient 의 UDF 와 **같았을 수 있다.** 그렇다면 cookie·storage 등 profile 데이터를 사용·변경했을 수 있고, 실행 중이던 GNLinkClient 와 browser process·session 을 공유했을 수 있다. |
| 실행 범위 | 남은 로그로 확인되는 셸 e2e 의 WebView 생성("the product shell's WebView starts" PASS)은 **12회**다: `ka_shell_r2-1/2/3`, `r2b-1/2`, `r3-1..5`, `r3neg-page`, `r4-1`. 로그 mtime 은 10-28 05:34–08:17 이다. `r4-2` 는 Host OFF gate 에서 멈춰 WebView 가 없다. managed e2e(`ka-e2e-*`, `ka-managed-*`)는 WebView 를 만들지 않는다. **로그가 남지 않은 실행이 있었는지는 모른다.** ce0a672·3146c14 이후 실행은 0이다. |
| 미확인 | 실제로 선택된 UDF. 당시 TEMP 값과 env/policy 상태(지금 policy 가 없다는 것은 현재 상태일 뿐이다). browser process 공유 여부. 읽거나 쓴 파일·cookie·storage·계정의 변화와 손상 여부. 사용자 `%TEMP%\GNLinkClient` 는 **열람하지 않았고**, 그 존재나 mtime 으로 영향 0 이나 손상을 주장하지 않는다. |
| 보수·새 근거 | 3146c14 의 preflight 와 effective getter. 새 독립 run 에서 UDF 경로·getter 결과·artifact hash 를 남긴다. **이전 12회를 소급해 "격리된 성공" 으로 바꾸지 않는다.** |

참고(이번 task 밖, 읽기만 함): `caplife/r3reg/dxgi_wedge.txt` 와 `caplife/rel_smoke/dxgi_wedge.txt` 에도 같은 "WebView starts" 줄이 있다. 다른 시험도 같은 제품 함수를 쓴 것으로 보인다. 그 시험의 TEMP 처리는 확인하지 않았다.

## 남은 것

- 위 Navigate 틈: Codex 결정 대기.
- Codex 시험 SOURCE 와 슬롯 해제 뒤 실 Host e2e 실행. preflight 줄, getter 경로, 모든 child 판정 줄, artifact hash 를 남긴다.

## 시험 r2 추가분 — macro WebView UDF (aecbdfd, 3146c14 위)

- **결함**: 제품 viewer 는 shell 환경을 그대로 물려받는다(`client_shell_main.cpp` 의 viewer 실행 CreateProcess, `client_managed_viewers.hpp` 의 CreateProcess 모두 env=nullptr). 그래서 3146c14 대로라면 viewer 의 macro 창 WebView(`client_macro_window.cpp`, `GetTempPathW + "GNLinkMacro"`)가 상속된 `WEBVIEW2_USER_DATA_FOLDER`(=shell UDF)를 따라 **shell 과 같은 폴더를 썼을 것이다.**
- **보수**
  - 공용 `ka_pc_e2e_webview.hpp` 로 preflight 를 WebView 이름별로 나눴다. 대상은 shell `GNLinkClient` 와 macro `GNLinkMacro` 이다.
  - **shell e2e**
    - 시작할 때 두 UDF 의 preflight 를 모두 확인한다.
    - shell environment 를 만들고 getter 로 확인한 뒤, sign-in 전에 변수를 macro UDF 로 바꾸고 preflight 를 다시 한다. 이후에 시작되는 viewer 는 macro UDF 를 물려받는다.
    - 제품이 shell WebView 를 다시 만들려 하면(WM_APP+3) 그 메시지를 처리하지 않고 run 을 멈춘다. 지금 환경에는 macro UDF 가 들어 있어 shell 용 preflight 가 성립하지 않기 때문이다.
  - **managed e2e**: 모든 child 환경에 macro UDF 를 명시한다(env_with 가 현재 환경을 복사한다). 시작할 때 preflight 를 확인한다.
  - **effective 확인**: macro WebView 는 viewer 프로세스 안에 있어 시험 쪽에서 getter 를 호출할 수 없다(제품 seam 없음). 그래서 다음을 실패로 처리한다.
    - 이 빌드 폴더에서 실행된 프로세스의 `GNLinkMacroWindow` 가 나타나면 실패다(shell 은 run 을 멈추고, managed 는 끝에서 실패 처리).
    - 끝에서 run 의 macro UDF 폴더가 존재해도 실패다.
  - 제품 profile 기본값은 바꾸지 않았다. 제품 파일 diff 는 cd9937a 대비 0이다.
- **시험**: `ka_pc_e2e_child_test` 에 preflight 절을 추가했다. 전체 ALL PASS, exe b1a06fd6e0e96960.
  - shell·macro preflight 가 이 PC 의 **현재** 상태(다른 WEBVIEW2_* 변수 없음, policy 없음)에서 통과한다.
  - 다음 4건은 거부된다: 다른 소유 경로, 이름 교차, 추가 변수(`WEBVIEW2_BROWSER_EXECUTABLE_FOLDER`), 변수에 다른 UDF. 원복하면 다시 통과한다.
  - 빌드: managed e2e 8e33d14a98269bfa, shell e2e 9d0baa9698ae80ad. GNLinkClient 와 GNLinkViewerKaTest 는 바뀌지 않았다.
- **그 밖의 TEMP 사용처 (읽기 분류만, 변경 없음)**
  - 제품 `client_shell_main.cpp`: shell UDF. 위에서 다뤘다.
  - 제품 `client_macro_window.cpp`: macro UDF. 위에서 다뤘다.
  - `dump_stacks_tool.cpp`: `getenv("TEMP")\symcache`. 진단 도구라 ka-pc e2e 가 실행하지 않는다.
  - `updater_main.cpp`, `updater_options.hpp`: 주석에서 %TEMP% 를 쓰지 않는다고 적은 것뿐이다.
  - 그 밖의 GetTempPath 는 ka-pc 가 아닌 시험이나 probe 의 것이다. 예: `e2e_guard_runtime_test` 는 작업용1 이 보수 중이다.
- **과거 영향 추가**: ce0a672 이전 셸 e2e 12회에 대해 macro 창이 열렸는지는 남은 로그로 확인하지 않았다(미확인). 시험 코드에 macro 조작은 없다.

## 실행 후보 고정 (2026-10-28, Codex test SOURCE OK: codex_ka_pc_test_src_r2.md)

- 후보: `feat/ka-pc` **aecbdfd7d1712b66a4073f7645ff0129c4a94aeb**. tracked clean, 더 고치지 않는다.
- sha256 (전체 값)
  - remote60_ka_pc_managed_e2e_test.exe 8e33d14a98269bfa13182bae0c9d914276940ed102aab9801a1259b50af5a10a
  - remote60_ka_pc_shell_e2e_test.exe 9d0baa9698ae80ad9ffe1554b0a33482bd948e24092ff096bdce54896c5a6b1a
  - GNLinkClient.exe f14c5942e7f2bf8d68aa8bd2b32a8a1075537336d5b90d6dd150a6ff1858a6ac
  - GNLinkViewerKaTest.exe bdff93631077fe96a07d747c6aa37d25e8276062823c79971ce03e3cfab70509
  - GNLinkStream.exe f35143b25c37df9d52b9bb5465b49732e895eb1e2841e1f5ca1521129d808e6b
  - ui/shell.html 12085292b1851ec42834eaca8121389b5f5079f66100962533ce445a994a0991
  - apps/directory/server.js 5a22f37280fe412f32f94b34b155e9091500b3694eadd020e9c15c67486e67db
- 실행 순서
  1. 사용자가 SLOT_REQUEST.txt 를 지운다.
  2. 검증용 batch(record + 일반 8 + fatal)를 돌린다.
  3. 검증용의 완료 통보를 받는다.
  4. `ka_r2_slot_run.ps1` 로 managed 와 shell 을 실행한다(새 run 폴더, 슬롯은 CreateNew 로 잡고 자기 파일만 지운다, old-host NOT RUN).
- 문구 한계
  - macro 에 대해 말할 수 있는 것은 "기존 조작 경로에 없음, 예상 밖 창이 뜨면 실패" 까지다. macro 기능이 성공한다는 뜻이 아니다.
  - "로그인 코드 0" 이라고 쓰지 않는다. ready→auto-login 호출은 시험 저장소와 loopback 배선이다.
  - 이 결과는 실제 PC EVIDENCE 후보이고, 출하 승인이 아니다.
- macro 결과 문구(검증용 보충): macro 창 감시는 polling 이고, 폴더가 없다는 것도 접근이 없었다는 완전한 증명이 아니다. 따라서 결과에는 **"macro 를 열지 않는 지정 시나리오에서 예상 밖 macro 경로가 검출되지 않음(사전 UDF 고정·override/policy 검사 통과)"** 으로 적는다. "macro 접근 0" 이나 "macro UDF 격리 확인" 이라고 쓰지 않는다.

## 실 Host e2e 결과 (2026-10-29, 후보 aecbdfd, 실제 PC EVIDENCE 후보 — 출하 승인 아님)

- **고정 확인**: HEAD aecbdfd7d1712b66a4073f7645ff0129c4a94aeb, 추적 파일 변경 없음. artifact sha256 은 위 "실행 후보 고정" 절과 앞 16자리까지 전부 일치한다.
  - managed 8e33d14a98269bfa
  - shell 9d0baa9698ae80ad
  - GNLinkClient f14c5942e7f2bf8d
  - GNLinkViewerKaTest bdff93631077fe96
  - GNLinkStream f35143b25c37df9d
  - ui/shell.html 12085292b1851ec4
  - server.js 5a22f37280fe412f
- **환경**: 사용자의 설치된 GNLinkHost(pid 28932)와 그 자식 GNLinkStream(pid 27044, 10-27 시작)이 실행 중이었다. 건드리지 않았다. 시험은 자기 loopback fixture directory 만 썼다.
- **슬롯**: CreateNew 로 잡고 자기 파일만 지웠다.
  - 1차: 17:27:51 잡음 → 17:29:05 해제
  - 2차: 17:33:22 잡음 → 17:35:39 해제
  - 끝난 뒤 `SLOT_REQUEST.txt` 가 없음을 확인했다.
- **old-host(0.2.163)**: NOT RUN. `REMOTE60_KA_OLD_STREAM` 를 설정하지 않았다.
- **실행 실수(작업용2)**: 1차에서 `powershell -File … -Runs 'managed:fin-1','shell:fin-1'` 로 넘긴 인자가 문자열 하나로 합쳐졌다. 그래서 managed 만 tag `fin-1,shell fin-1` 로 실행됐다. shell 은 2차에 따로 실행했다(같은 후보, 새 run 폴더).

| 시험 | rc | ^PASS | ^FAIL | 마지막 요약 줄 | 로그 (sha256 앞 16자리) |
|---|---|---|---|---|---|
| managed | **1** | 58 | 3 | `ka_pc_managed_e2e_test: 1 FAILED` | `np_impl_logs/ka_managed_fin-1,shell fin-1.log` (UTF-16) 58a6e1251a72b456. run `test-tmp/ka-managed-fin-1,shell fin-1/run-43420-608290562` |
| shell | **0** | 148 | 0 | `ka_pc_shell_e2e_test: ALL PASS (0 failed)` | `np_impl_logs/ka_shell_fin-1.log` 0bc53af52c4b2688. run `test-tmp/ka-shell-fin-1/run-29120-608621468` |

- **OFF 최종 판정 줄**
  - managed: `PASS  input / power state: no contradiction during the run (1 launch(es): 1 judged on their complete output -- end, EOF and OFF lines confirmed, 162639 bytes --, 0 still running judged now)`
  - shell: `PASS  input / power state: no contradiction during the run (5 launch(es): 5 judged on their complete output -- end, EOF and OFF lines confirmed, 278864 bytes --, 0 still running judged now)`
- **shell WebView**
  - preflight 를 통과했다. UDF 는 `run\temp\GNLinkClient` 이고, 다른 WEBVIEW2_* 변수와 policy 는 없었다. 거부 대조 3건도 통과했다.
  - `get_UserDataFolder` 가 run UDF 와 일치했다(3.4 s).
  - download·새 창·dialog·macro 창·재생성: 0.
  - child 8개 모두 종료 확인, EOF, 로그 보존.
- **macro 문구 기준**: macro 를 열지 않는 지정 시나리오에서 예상 밖 macro 경로가 검출되지 않았다(사전 UDF 고정과 override/policy 검사 통과). macro 기능이 동작한다는 뜻이 아니다.
- **managed FAIL 3줄의 원인**: 결함은 1건이고, 이번 N2 시험 하네스의 결함이다(작업용2, aecbdfd). 3줄은 child 판정 1 + 그 요약 1 + check 1 이다.
  - 시험 "shell crash" 단계(`ka_pc_managed_e2e_test.cpp:789-790`)가 viewer-C 의 job handle 을 직접 닫고 `job = nullptr` 로 둔다.
  - 그 child 는 실제로 끝났다. 직전 check `closing the job's last handle ends the child (KILL_ON_JOB_CLOSE)` 가 PASS 이고, trailer 에 `read: end of output … log: kept` 가 있다.
  - 그런데 공용 Child::End 는 job 이 null 이면 `e2e_end_job_confirmed` 가 false 를 돌려주므로 "producer end NOT confirmed" 로 셌다.
  - 제품 동작의 FAIL 은 0 이다(나머지 58 PASS, Host OFF 최종 판정 PASS).
  - 후보 고정 지시에 따라 고치지 않았다. 보수안: 시험이 job 을 넘겨받아 닫은 child 는 "프로세스 종료 확인 + EOF" 로 판정한다. 결정은 Codex 가 한다.
- **로그인 문구**: "로그인 코드 0" 이라고 주장하지 않는다. ready→auto-login 호출은 시험 저장소와 loopback 배선이다.

## 시험 r3 (2026-10-29) — managed 의 crash 단계 판정 보수 (5f0e7a3, aecbdfd 위, 하네스만)

- **보수**
  - managed 의 shell-crash 단계가 `CloseHandle(job); job = nullptr` 대신 `Child::CrashCloseJob` 를 부른다.
  - close **전에** 다음을 기록한다.
    - kill-on-close: job 에서 다시 읽어 확인
    - root 의 job 소속: `IsProcessInJob`
    - job 구성원 목록: `JobObjectBasicProcessIdList` 상 root 와 **OS console host(`%SystemRoot%\System32\conhost.exe`)만** 있음
    - root 가 아직 살아 있음
  - close 의 성공과 root 의 종료 신호도 기록한다.
  - End 는 이 기록이 **모두** 성립할 때만 종료 확인으로 친다. 여기에 EOF 와 로그 보존이 함께 있어야 한다.
  - trailer 에 `producer end evidence:` 를 적는다(crash-close 기록 / job terminate 의 active 0 조회 / 기록 없음을 구분).
  - "이미 닫힌 job 의 active 0 조회" 라고 주장하지 않는다.
- **active 1 대신 구성원 목록을 쓴 이유**: 콘솔 child 에는 CREATE_NO_WINDOW 라도 conhost.exe 가 붙어 job 의 active 가 2 다. 첫 시도에서 이 때문에 정상 child 가 거부됐다. conhost 외의 descendant 는 여전히 거부한다(여러 descendant 로 넓히지 않았다).
- **거부하는 경우**
  - 기록 없는 null job(예전 단계)
  - close 실패: seam 이 close 전에 거부하고, job 은 cleanup 용으로 남지만 증거로 치지 않는다
  - descendant 가 있음
  - root 가 끝나지 않음
  - host(monitor 가 붙음): 이 단계 자체를 거부한다
  - read 오류: 기존 generic 판정
- **정상 경로**: 일반 child 의 job terminate 판정은 바뀌지 않았다(trailer 에 evidence 줄 하나만 추가). shell·제품·UI 의 종료 의미는 그대로다. 그래서 shell 148 은 다시 돌리지 않는다. 예전 shell 로그는 예전 binary(9d0baa96…)의 결과로 남긴다.
- **`ka_pc_e2e_child_test` 대조 (ALL PASS, exe 3cc31e64ec0a6645)**

  | 경우 | 결과 |
  |---|---|
  | crash 기록 7항목 | 하나라도 빠지면 거부 |
  | 예전 단계(손으로 close + null) | child 는 끝났지만 **NOT confirmed**. 10-29 FAIL 의 재현이며, 보수 뒤에도 거부한다 |
  | 새 단계 | **confirmed**, EOF, 로그 보존. evidence 는 "root + 1 console host(s) only" |
  | descendant | 거부 |
  | close 실패 | 거부, job 은 유지 |
  | host | 단계 거부 |
- **binary sha256** (aecbdfd 대비 바뀐 것은 시험 exe 뿐이다)
  - managed 7bb6b7902c5b4874…
  - shell f4b67f281700f3de… (header 가 바뀌어 재빌드됨. 실행하지 않음)
  - GNLinkClient f14c5942… (변경 없음)
  - GNLinkViewerKaTest bdff9363… (변경 없음)
  - GNLinkStream f35143b2… (변경 없음)
  - 제품, UI, 공용 guard 의 diff 는 aecbdfd 대비 0이다.
- **재실행**: managed 만, 새 run 에서, 단일 호출로 1회 돌린다. 검증용이 Codex 확인 경로와 슬롯 순서를 정한 뒤에 한다. 10-29 의 1차 실수 실행은 managed 1건으로 보존한다.

## managed 재실행 결과 (2026-10-29, 후보 5f0e7a3, Codex test SOURCE OK r3 — 실제 PC EVIDENCE 후보, 출하 승인 아님)

- **고정**: HEAD 5f0e7a3dddc84eeaecba8a46227320d2961ce6f4, 추적 파일 변경 없음.
  - managed exe 7bb6b7902c5b4874
  - GNLinkClient f14c5942e7f2bf8d
  - GNLinkViewerKaTest bdff93631077fe96
  - GNLinkStream f35143b25c37df9d
- **실행**: `ka_r2_slot_run.ps1 -Runs @('managed:r3-1')`. 단일 원소 배열로 in-process 호출 1회.
  - 슬롯: CreateNew 로 18:21:55 에 잡고 18:23:09 에 자기 파일을 지웠다. 끝난 뒤 `SLOT_REQUEST.txt` 가 없음을 확인했다.
  - old-host: NOT RUN.
  - run 폴더: `test-tmp/ka-managed-r3-1/run-33672-611534937`(새로 만든 폴더).
  - 같은 root 이름 아래에는 10-28 의 옛 r3 실행 잔여물이 따로 있다. 건드리지 않았고 섞이지 않았다.
- **결과**: rc **0**, ^PASS **60**, ^FAIL **0**. 마지막 요약 줄 `ka_pc_managed_e2e_test: ALL PASS`. 로그 `np_impl_logs/ka_managed_r3-1.log` sha256 bdfae981ffc0010d.
- **OFF 최종 판정**: `PASS  input / power state: no contradiction during the run (1 launch(es): 1 judged on their complete output -- end, EOF and OFF lines confirmed, 163490 bytes --, 0 still running judged now)`
- **crash 단계**: `PASS  closing the job's last handle ends the child (KILL_ON_JOB_CLOSE)  4 ms`
- **viewer-C trailer**
  - `producer end: confirmed`
  - `producer end evidence: crash-close by this test, single root: kill-on-close read back, the root in the job, its listed members the root + 1 console host(s) only before the close, CloseHandle succeeded, the root's handle signalled after it`
  - `read: end of output (error 0) after 243 bytes` / `log: kept`
- **증거 유형의 한계**: 확인한 것은 close 전의 구성원 목록과 close 뒤 root 의 종료 신호다. 닫힌 job 의 active 0 조회나 conhost 의 개별 종료는 보지 않았다.
- **child 판정**: `PASS  every child this test started (10): its end confirmed, its output read to EOF and kept in its own log`
- **macro**: 지정 시나리오(macro 를 열지 않음)에서 예상 밖 macro 경로가 검출되지 않았다(사전 UDF 고정과 override/policy 검사 통과).
- **shell**: 148/0 결과는 옛 binary 9d0baa96 의 것으로 보존한다. 새 f4b67f28 로는 실행하지 않았다.
- **10-29 1차 실수 실행**(managed, FAIL 1, 로그 58a6e125…)은 그대로 보존한다.

## V1 (2026-10-29) — 흰 picker 캡처: 원인과 판정 보수 (320bc36, 5f0e7a3 위, 시험만)

**원인 구분**

1. **남은 자료**
   - 남아 있는 picker 캡처는 모든 run(10-28 r3, 10-29 fin-1, 10-29 r3-1)에서 client 안쪽이 **RGB 255 한 가지 색**이다. sha 는 모두 f62e2dba… 다.
   - presented 캡처는 안쪽 색이 4,884~7,546 개다.
   - viewer 로그에는 두 round 모두 `[picker] first paint after show … desktopCard=…` 가 있고, `present FAILED` 는 0건이다.
2. **소스**
   - 새 activation 창은 아직 swapchain present 가 없다(`gdi_reaches_the_screen()`). 그래서 picker 를 GDI 로 창 DC 에 그린다(`viewer_present.cpp:541-549` else 가지). first paint 줄도 거기서 찍힌다.
   - park 하면 창이 없어지므로 round 2 도 새 창, 같은 GDI 경로다.
3. **최소 실행 `remote60_ka_pc_wgc_offscreen_probe`** (exe ea4b523545684784; Host·viewer·입력 없음, 창은 모든 모니터 밖 left=2120 > 1920)

   | 경우 | 결과 |
   |---|---|
   | A. flip swapchain present 1회 후 WGC 첫 frame | 그린 색(0d0f14) |
   | B. 캡처 중 다시 present | 그린 색 |
   | **C. GDI WM_PAINT 창의 WGC 첫 frame** | **흰색 1색** |
   | **C2. 캡처 중 다시 그림** | **흰색 1색** |
   | D. PrintWindow(CLIENTONLY\|RENDERFULLCONTENT), PrintWindow(CLIENTONLY) | 둘 다 그린 내용(배경 0d0f14, 카드 343a46) |

   → **흰색은 캡처 방식의 결과다.** 모니터 밖 창의 GDI 내용을 WGC 가 흰 면으로 돌려준다.

4. **판단의 한계**
   - picker 가 실제로 그려졌는지는 흰 캡처로 보면 증거가 **없다**(그려졌다는 증거도, 안 그려졌다는 증거도 아니다).
   - 물리 모니터 위의 모습은 보지 않았다(창을 사용자 화면에 두지 않는다).
   - 제품의 결함 가능성은 이 실험만으로는 0 이 아니다. 판정은 아래 새 증거로 한다.

**판정 보수** (`ka_pc_e2e_picker_judge.hpp`)
- **picker**
  - PrintWindow 로 **client 영역**을 캡처한다.
  - **이 activation 의 Activate 이후 로그**에 있는 desktopCard 를 기준으로 판정한다. 통과 조건은 다음과 같다.
    - 카드 바깥 표본의 절반 이상이 picker 배경 RGB(13,15,20)±6 이다.
    - 카드 둘레 표본의 절반 이상이 카드 테두리색 RGB(52,58,70) 또는 활성색이다.
    - 카드 안쪽 색이 3가지 이상이다.
  - 통과할 때만 카드를 누른다.
- **선택 뒤 영상**: WGC 로 캡처하고 안쪽을 본다. 거친 색 16가지 이상이고 picker 배경이 절반 미만이어야 한다.
- 예전 `variety()`(창 전체 색 개수)는 지웠다.
- 제품, UI, render seam, 창 위치, 공용 `e2e_window_capture.hpp` 는 바꾸지 않았다.

**대조 — `ka_pc_e2e_child_test`** (exe 9271430d1b496877, `REMOTE60_KA_V1_FIXTURES` = r3-1 run 폴더) **ALL PASS**

| 경우 | 결과 |
|---|---|
| 합성: 배경 + 카드 | visible |
| 합성: 흰색 | 거부 |
| 합성: 카드 없음 | 거부 |
| 합성: picker 이미지를 영상 판정에 | 거부 |
| **실제 흰 캡처 A-round1/2-picker.bmp** | 옛 판정은 **통과**(variety 9), 새 판정은 **거부**("background covers 0 of 81144/82395") |
| 실제 presented | fresh picture 로 인정, picker 로는 거부 |

- **binary**
  - managed 163ee8f6cf6e7274
  - GNLinkClient f14c5942… (변경 없음)
  - GNLinkViewerKaTest bdff9363… (변경 없음)
  - GNLinkStream f35143b2… (변경 없음)
- **남은 것**: 실제 picker 가 보인 뒤 선택 → fresh frame 으로 이어지는 2 activation 근거. managed 실행 1회가 필요하다(Host 사용, 슬롯 필요). 검증용이 순서를 정한 뒤 실행한다. 그 실행에서는 V1 판정 줄만 새 근거로 쓰고, managed 60 전체는 다시 주장하지 않는다.

## V1 한정 실행 결과 (2026-10-29, 320bc36, Codex V1 SOURCE OK — 실제 PC EVIDENCE 후보, 출하 승인 아님)

- **실행 외피**
  - `e2e_job_run.exe` sha256 DC4C5049E8E9D32A… (검토판과 일치를 실행 전에 확인).
  - 인자: `--stop-file <run root>\v1.stop`(시작 전에 만들어 둠) `--grace-sec 600` `--cmd "<managed exe>"`. native 하네스만 이 안에서 돌렸다.
  - 실행 스크립트 `np_impl_logs/ka_v1_job_run.ps1` 가 managed exe hash(163ee8f6cf6e7274)를 확인하고, root 와 로그가 새것인지 확인한다.
  - 슬롯은 이 PowerShell 이 job 밖에서 갖는다. CreateNew 로 19:48:46 에 잡고, finally 에서 자기 파일을 19:50:00 에 지웠다. 끝난 뒤 `SLOT_REQUEST.txt` 가 없음을 확인했다.
  - old-host: NOT RUN.
- **job 결과**: `job_run: child_exit=0 residual=0 forced=0 ended_confirmed=1`, 74 s (상한 600 s).
  - 실행 뒤 이 run 시각에 시작된 GNLinkStream, node, viewer, 하네스 프로세스는 없었다.
  - 사용자의 GNLinkStream pid 27044(10-27 시작)과 기존 node 들은 건드리지 않았다.
- **managed**: rc **0**, ^PASS **60**, ^FAIL **0**. 마지막 요약 줄 `ka_pc_managed_e2e_test: ALL PASS`. 로그 `np_impl_logs/ka_managed_v1-1.log` sha256 8ef3167fbaff9fdc. run 폴더 `test-tmp/ka-managed-v1-1/run-9872-616745562`.
- **V1 판정 줄**
  - `PASS  [1] ...the picker is visible in the client area (PrintWindow): its background and the desktop card this activation logged  1584x860 card=desktopCard=24,78,320,293`
  - `PASS  [1] desktop pressed -> the first picture presented (Presented)  ms(since Activate)=968`
  - `PASS  [1] ...after the selection a fresh picture is presented (WGC, interior: a picture, not the picker)`
  - `[2]` 도 같은 세 줄이 PASS 다(Presented ms=892, card=24,78,320,293).
- **OFF 최종**: `PASS  input / power state: no contradiction during the run (1 launch(es): 1 judged on their complete output -- end, EOF and OFF lines confirmed, 132082 bytes --, 0 still running judged now)`
- **기타**: child 10개 판정 PASS, crash 단계 PASS(4 ms).
- **이미지 4장**: 모두 실제 파일로 있고 디코드된다. 직접 열어 봤다(PNG 사본 `test-tmp/ka-managed-v1-1/png/`).

  | 파일 | 크기 | sha256 | 확인한 내용 |
  |---|---|---|---|
  | A-round1-picker-client.bmp | 1584x860 | 4f936b3bce6c2619b3f779cfbe9b551e637b47af0ff95d4e1fd20318720891b7 | picker: 제목 "공유 화면 선택", "공유할 수 있는 창 10개", 카드 11장. "전체 화면" 카드가 선택 테두리와 체크 표시를 가짐. 하단에 "제어 연결됨 · 입력 켜짐"(10-29 정정: 처음에 "입력 꺼짐" 으로 잘못 읽었다. 이 문구는 viewer 의 상태 표시이고, Host OS 입력 주입이 켜졌다는 증거가 아니다. Host 는 OFF 최종 판정을 통과했다) |
  | A-round2-picker-client.bmp | 1584x860 | 4f936b3b… (round 1 과 **바이트까지 같다**) | 같은 화면이다. 두 activation 사이에 창 목록과 카드 배치가 바뀌지 않았다. 판정은 각 op 가 자기 로그에 남긴 card 를 썼다. |
  | A-round1-presented.bmp | 1586x893 | cd995248b46e04e8e04f5c7616b06e7649ad16d71b4924a0202d6dc0360f5c17 | 이 PC 데스크톱의 실시간 영상, 화면 시계 19:48:51 |
  | A-round2-presented.bmp | 1586x893 | becb04539634fdb939a426c9572a73ff9e01bd074a5bf1d4ab1f8d7a220b523d | 같은 데스크톱, 시계 19:49:05. round 1 과 다른 새 frame 이다 |

- **표현의 한계**
  - PrintWindow 는 "실제 activation HWND 가 picker 를 client 에 렌더함" 의 근거다.
  - 물리 모니터 표시, 자동 paint 완료, 실 사용자 클릭의 근거가 아니다. 클릭은 PostMessage 로 보냈다.
  - presented 이미지에는 시험 Host 가 캡처한 이 PC 의 실제 화면 내용이 들어 있다. 로컬 시험 폴더에만 있다.
