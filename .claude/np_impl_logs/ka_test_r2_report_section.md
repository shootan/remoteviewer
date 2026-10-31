

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
