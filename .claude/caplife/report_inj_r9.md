# test-injection-finding r9 — 런타임 시험의 %TEMP% 사용 제거 (b8dcc93 위)

task=test-injection-finding / rev=r9 결과 / 근거=Codex r8 안전 발견

## 고정
- commit **d6659575f9b1b8b4198651bc77ae0113a1c4931f** (ka-r3, 부모 b8dcc93). tracked clean, `git diff --check` 정상.
- 2파일: 새 `e2e_test_root.hpp`, `e2e_guard_runtime_test.cpp`. 제품 코드 0줄.
- **시험은 실행하지 않았다**(보류 지시). 빌드만 했다: 경고 0. exe SHA-256 `09678227B768E27F9BDD2983E4D49A9B596F023810E104CF134E7F6A0506766B`. exe 의 import 문자열에서 `GetTempPathW` 0건을 확인했다.

## 고친 것
- `e2e_test_root.hpp`:
  - `REMOTE60_TEST_TMP` 는 `D:\remote\remote\.claude\test-tmp` **바로 아래**의 일반 ASCII 이름 폴더여야 한다.
  - 드라이브부터 그 폴더까지 모든 기존 구성요소가 link(reparse)가 아니어야 한다.
  - 이 검사는 **무엇이든 만들기 전에** 한다. 어긋나면 거부한다. fallback 은 없다.
- 부모 프로세스: root 를 검증한 뒤, 이 실행 전용의 **새** `guard-runtime-<pid>-<tick>-<n>` 폴더를 만든다(이미 있는 폴더는 채택하지 않는다).
  - root 가 유효하지 않으면 `RESULT: INVALID`, exit 2 로 끝낸다. 이때 아무것도 만들지도 지우지도 않는다.
- 자식 프로세스(파일을 쓰는 empty_file / quiet_file 두 case 만): `REMOTE60_E2E_RUN_DIR` 로 폴더를 넘겨받아 다시 검증한다. root 바로 아래인지, prefix 가 맞는지, 기존 일반 폴더인지 본다. 자식은 폴더를 만들지 않는다.
- 파일과 삭제:
  - 파일은 run 폴더 안에 `CREATE_NEW` 로 만든다(`<tag>_<pid>.log`).
  - 삭제는 `delete_run_file` 만 한다. run 폴더 **바로 안**의 경로만 지우고, 그 파일을 만든 코드만 지운다. run 폴더 자체는 남긴다.
- `GetTempPathW`, `%TEMP%`, `CREATE_ALWAYS` 는 제거했다.
- 같은 패턴을 grep 으로 찾았다.
  - 대상: guard 계열(`e2e_injection_guard.hpp`, `e2e_child_util.hpp`, `e2e_injection_guard_test`, `os_input_record*`, `host_power_policy*`, `standby_legacy_host_test`, fatal)과 guard 를 쓰는 하네스 8개.
  - 결과: `GetTempPath`, `temp_directory_path`, TEMP/TMP 환경 변수 읽기는 **0건**이었다(새 헤더 주석 1건 제외).
  - guard 계열 밖에서는 `GetTempPathW` 를 쓰는 시험이 따로 있다: client_auto_login_ui, client_reconnect_restore_e2e, client_recovery_ui, client_update_ui, host_app_log, host_log_writers_probe, host_thumbnail_lingering, log_upload_shutdown. 그리고 제품 `client_shell_main.cpp`, `client_macro_window.cpp`(userData), 도구 `dump_stacks_tool` 이다.
  - 이번 task 범위 밖이라 **고치지 않고 관찰만 기록**했다. 처리할지는 지시를 받겠다.

## 과거 기록 (확인 가능한 것만)
- **r6·r7 실행에는 파일이 없었다.**
  - `git show <commit>:apps/native_poc/src/e2e_guard_runtime_test.cpp` 로 확인했다.
  - d9a3a91 ~ a8b3407 의 10개 commit 모두 `GetTempPath`/`CreateFileW`/`DeleteFileW` 0건이다. b8dcc93 에서 처음 7건이 생겼다.
  - 따라서 r6 의 10/10·16/16 실행과 r7 의 16/16×3 실행은 %TEMP% 파일을 만들지 않았다.
- **r8 (b8dcc93 내용, 커밋 전 워킹트리, 2026-10-28)** 에서 temp 코드가 든 exe 를 전체 실행한 횟수: **9회**.

  | 회차 | 결과 | 근거 |
  |---|---|---|
  | ① | 35/36 (pipe 이름 버그) | **파일 로그** `ka-r3/build/r8.remote60_e2e_guard_runtime_test.log` ("36 checks, 1 failures") |
  | ② ~ ④ | 36/36 ×3 | ② 는 **파일 로그** `ka-r3/build/r8.runtime.log` ("36 checks, 0 failures"). ③④ 는 이 세션의 도구 출력에만 있다. |
  | ⑤ ~ ⑧ | mutant a·b·c·d 각 1회 | 세션 도구 출력에만 있다(파일로 저장 안 함). |
  | ⑨ | 복원 후 36/36 | 세션 도구 출력에만 있다. |

  - 이 밖에 자식 단독 실행 1회(`--monitor-case pipe_partial`)가 있었다. 이 case 는 파일을 만들지 않는다.
- 전체 실행 1회당 만든 파일 3개(소스 기준):
  - 자식 empty_file: `e2e_guard_runtime_empty_<자식pid>.log` (0 byte)
  - 자식 quiet_file: `e2e_guard_runtime_quiet_<자식pid>.log` (OFF 두 줄)
  - 부모: `e2e_guard_runtime_writeonly_<부모pid>.log` ("abc")
  - 소스상으로는 각 파일을 `GetTempPathW()` 디렉터리에 `CREATE_ALWAYS` 로 만들도록 시도하고, 곧바로 `DeleteFileW` 를 시도한다. 9회 합계는 **생성 시도 27회, 삭제 시도 27회**이며 시도 횟수일 뿐이다. 실제 성공 여부와 TempRoot·PID 는 미확인이다(아래 「과거 9회」 절 참조).
- **실제 TempRoot 경로: 미확인.** 시험이 경로를 출력하지 않았고 로그에도 없다.
  - 참고만: 지금 이 도구 세션의 `TMP` 와 `TEMP` 는 `C:\Users\shota\AppData\Local\Temp`, `GetTempPath` 는 `C:\Users\shota\AppData\Local\Temp\` 이다. 당시 값이라는 근거는 아니다.
- **pid·실제 파일 이름·삭제 성공 여부: 미확인.** 출력에 기록하지 않았다.
- 읽기 전용 확인(2026-10-28, 삭제·정리 없음): 위 Temp 폴더에 `e2e_guard_runtime_*` 는 **0개**다.

## 다음
- 실행 허가가 나면 `REMOTE60_TEST_TMP=D:\remote\remote\.claude\test-tmp\<새 이름>` 으로 1회 실행하고, 결과와 run 폴더 경로를 보고하겠다.
- Codex 의 r8 나머지 판정은 이 r9 에 합쳐 받겠다.

## 추가 — r9 조건부 1회 실행 (Codex d665957 runtime fixture SOURCE OK 뒤, 2026-10-28)
- 고정:
  - ka-r3 HEAD d6659575f9b1b8b4198651bc77ae0113a1c4931f
  - `git diff d665957` (runtime_test.cpp·e2e_test_root.hpp·e2e_injection_guard.hpp·e2e_child_util.hpp) 0줄
  - exe sha256 `09678227B768E27F9BDD2983E4D49A9B596F023810E104CF134E7F6A0506766B` (위 빌드와 같은 값). exe 의 mtime 11:59:34 는 네 소스 파일의 mtime 보다 모두 늦다.
- 사전 확인:
  - `REMOTE60_TEST_TMP=D:\remote\remote\.claude\test-tmp\inj-r9-run1`. 실행 직전에 새로 만들었고, 비어 있었다.
  - D:\remote → \remote → \.claude → \test-tmp → inj-r9-run1 의 각 구성요소: 디렉터리이고 reparse 아님.
- 실행: 이 시험만 실행했다(sleeper·job·event·loopback). Host·Viewer·AVD·GPU 는 0이고, 슬롯 파일은 건드리지 않았다. 300 s 제한을 걸었다.
- 결과:
  - **rc=0**, `^PASS` **36**, 요약 줄 `36 checks, 0 failures` / `RESULT PASS`
  - 로그 `ka-r3/build/r9.runtime.run1.log`, sha256 `adb529e5115dd93045ffead18644855232f206567aa9a1ff8b31bff4f36bde26`
- 새 run 폴더: `D:\remote\remote\.claude\test-tmp\inj-r9-run1\guard-runtime-34008-504495218-0`
  - 실행 뒤 이 폴더는 **비어 있다**(파일 0). 폴더 자체는 남긴다(설계대로).
  - root 안에 다른 항목은 없다.
- 종료 미확인 child: **0**. 실행 뒤 이 exe 경로의 프로세스(부모, sleeper 자식 모두) 조회 결과 0개다.
  - 로그의 "NOT confirmed" 2줄은 의도된 음성 대조다: abandoned launch, 끝을 볼 수 없는 event handle. 둘 다 PASS 항목이다.

## 과거 9회 — 시도 횟수와 실제 성공을 나눠서 (정정)
- **소스상 시도 횟수**: 실행 9회 × 3파일 = 생성 시도 27회(`CREATE_ALWAYS`, `GetTempPathW()` 디렉터리), 삭제 시도 27회(`DeleteFileW`).
- **실제 성공: 미확인.** 생성과 삭제의 반환값, TempRoot, PID 를 출력하지 않았다. 이를 담은 로그는 없다.
- 지금 Temp 폴더에 0개라는 사실은 **현재 상태**일 뿐이다. 과거의 생성·삭제 성공을 판정하는 근거로 쓰지 않는다.
