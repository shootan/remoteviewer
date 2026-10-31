

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
| N4 | **owner fence**: 연결 뒤 cancel 이벤트(전에는 "no effect")를 받으면 UI thread 가 입력·클립보드를 즉시 차단한다. 눌린 키·버튼 해제, 클립보드 listener 해제, paste 취소, 이후 입력 메시지 drop 을 한 뒤 창 자신의 WM_CLOSE teardown 으로 닫는다. 감시 thread 는 join 한다(detach 아님). 창이 생기기 전의 취소는 창을 만들지 않고, connect 완료 직후의 취소는 세션을 시작하지 않는다. managed Release/Shutdown 도 같은 fence 를 쓴다. 강제 종료 fallback 은 3 s → **8 s** 로 늘렸다(뷰어 teardown 의 join 한도가 3 s+3 s 이기 때문, ordinary·managed 둘 다). PID·창명 검색이나 외부 뷰어 종료는 하지 않는다. |
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
| `ka_pc_ipc_close_race_test`(새, 실제 header·pipe, 순서 고정) | **9/0 ×6**. writer/reader 가 첫 cancel 을 놓쳐도 1.06 s / 47 ms 에 끝난다. 정상 drain 은 Bye 까지 온전하다. 반쪽 frame 을 받은 peer 는 아무것도 전달하지 않고 닫힌다. | fdb4232 header: **1번 FAIL(3000 ms 초과)**, 나머지 PASS. 구 코드는 reader 의 handle 을 thread 아래에서 닫아 hang 하지 않지만, handle 재사용 위험은 있다. |
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

## 남은 것 · 위험

- **교체 동작 변화**: 이미 연결된 ordinary 뷰어가 있는 PC 를 다시 탭하면(begin_session 교체), cancel 이벤트가 이제 fence 이므로 **기존 창이 닫힌다**. 이전에는 기존 창이 남아 두 세션이 경합했다.
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
