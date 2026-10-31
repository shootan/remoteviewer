# task integ-0162 r1 — PC 0.2.162 통합 후보 (LK + DW, 이후 hello) (검증용 remote#wwml7h91, 2026-10-22) — 담당 작업용2

**이 파일이 정본이다. 끝까지 읽고 CLAUDE.md 대로 진행한다.**

## 입력 (Codex SOURCE/EVIDENCE OK 로 고정된 후보)
- **LK** `fix/lockscreen-cred` @ **e728485** — `.claude/codex_lockcred_impl_r4.md`
- **DW** `fix/dxgi-quarantine` @ **fc4cd57** — `.claude/codex_dxgi_wedge_fix_r4.md`
- **hello** `fix/hello-delay`(작업용1 t-q4eq9aiz 진행 중) — Codex OK 뒤 r2 로 추가 merge. 지금은 넣지 않는다.
- **SH**(Android) 는 이 PC 통합에 넣지 않는다(APK 범위 사용자 미결).
- 기준: 로컬 `main` @ **83cbd78**(= 5063cd9 + docs c04eb47·83cbd78, 미push).

## 할 일
1. 새 worktree·브랜치 `C:/work/gnlink-wt/integ-0162`, `integ/0.2.162` = main 83cbd78. webview2 junction(제거 시 rmdir 먼저).
2. `git merge --no-ff fix/lockscreen-cred`(e728485) → `git merge --no-ff fix/dxgi-quarantine`(fc4cd57). merge commit 은 git CLI(두 번째 parent 보존). 충돌은 **양쪽 의미를 모두 보존**:
   - `automation/gnlink_check_fixed_server.py` TEST_MARKERS = 합집합(LK `GNLINK_SIS_TEST_`·`sis-test-seam`… + DW `mt-ab`·`GNLINK_CHAIN_`·`viewer-present-fail-seam`·`GNLINK_VIEWER_TEST_`…). self-test 도 둘 다.
   - `docs/history.md`·작업목록 등 문서 충돌은 두 항목 모두 유지.
   - 제품 코드 충돌이 나면 **임의 해석 금지** — 충돌 파일·양쪽 의도를 검증용에 보고하고 대기(protocol/공유 헤더 `poc_protocol.hpp`·`viewer_control_client.cpp`·`host_control_session.cpp` 가 후보).
3. 버전은 올리지 않는다(검증용이 rel 단계에서). 
4. 통합 영향 검증(전량 반복 아님): 빌드 rc0; 두 브랜치의 핵심 시험 — LK: lockcred·channel·relay·logon_ui·lock_input_policy·prelogin_input_gate + UI e2e `--lock-screen`·prelogin·not-prelogin; DW: store·quarantine·restore_target·os_snapshot + `client_recovery_ui` + 사슬 desktop·monitor(출하 뷰어)·nomonitors; gate `--dir` + self-test(합집합 표식 둘 다 부정 대조); 출하 exe seam 0. 공유 경로(뷰어 control·툴바·호스트 control session)에서 두 기능이 서로를 깨지 않는지 — 예: 잠금 화면 상태에서 화면 목록 요청·툴바 버튼, DW 재연결 뒤 LockCredV1 pong.
5. 측정 슬롯 CreateNew, 사용자 설치본·서비스·세션 무조작, 게시·push 금지.

## 보고 (1회)
`.claude/report_integ_0162_r1.md` + `remote#wwml7h91` pin: 브랜치·merge commit·충돌과 해소 방식, 산출물 해시(Stream/Host/Viewer/Client/InputService/Updater + shell.html), 시험 rc+요약, gate, 남은 위험.

## r2 (2026-10-22) — hello-delay merge
- 입력 추가: **hello** `fix/hello-delay` @ **d0fe74e**(= 6e89962 + docs 정정, Codex SOURCE/EVIDENCE OK `.claude/codex_hello_delay_fix_r2.md`).
- `integ/0.2.162`(현재 71d6834) 에 `git merge --no-ff fix/hello-delay` (git CLI). 충돌 규칙 r1 과 같음(gate 표식 합집합 — hello 의 시험 매크로는 시험 타깃 전용, 문서 양쪽 보존, 제품 충돌은 보고 후 대기).
- 영향 시험: 빌드 rc0; hello_backlog·punch_any + 두 mutant(FAIL 기대); LK lock-screen e2e 1회; DW 사슬 presentfail·desktop 각 1회(첫 재연결 hello 시간 기록); gate --dir + self-test; **출하 target 에 시험 define 부재 확인(생성 vcxproj/CMake 에서 GNLINK_*_TEST/MUTANT 매크로가 시험 타깃에만 있음 — exe 문자열 grep 만으로 부재 주장 금지)**.
- 보고: `.claude/report_integ_0162_r1.md` 에 'r2' 절 + 검증용 pin. directory_retry 조사(t-azc166pv)는 이 merge 뒤 이어서.
