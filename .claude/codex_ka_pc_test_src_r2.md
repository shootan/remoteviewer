# KA PC 시험 SOURCE r2 — 3146c14 + macro 후속 aecbdfd

2026-10-28 · Codex 독립 검토. **SOURCE OK — 실행 후보는 macro 보수를 포함한 aecbdfd7d1712b66a4073f7645ff0129c4a94aeb로 고정한다. 결정은 (a) 수용이다.** 실제 두 PC e2e의 EVIDENCE 판정이나 출하 승인이 아니다.

## 고정 범위와 근거

- 요청 후보 3146c1478ff50b732c27b72e473263c77b8daa8e: ce0a672 위 5파일 시험 변경. 이전 시험 SOURCE r1의 N1/N2를 검토했다.
- 검토 도중 같은 보고서에 추가된 aecbdfd: 3146c14 위 macro UDF 보수 4파일도 함께 읽고 아래 범위로 수용한다. 요청 이후 HEAD 이동을 3146c14의 내용으로 혼동하지 않았다.
- cd9937a..aecbdfd의 client_shell_main.cpp / viewer_* / ui diff는 0이고, c0271a7..aecbdfd의 공용 e2e_injection_guard.hpp / e2e_child_util.hpp diff도 0임을 직접 확인했다. ce0a672..aecbdfd diff --check 통과.
- report_ka_pc_impl_r1.md는 후속 절이 추가된 현재 SHA-256 9014f02477f76ad500e89ba26ef7745cd667bf2e9c88af92ebf4984c0b7ea555를 읽었다. 요청 당시 93671b04…와 같은 snapshot이라고 주장하지 않는다. 소스는 각각 위 git object로 고정했다.

## 결정 (a) — 로컬 페이지 Navigate 수용

새 제품 시험 seam은 요구하지 않는다. 이 고정 하네스의 **사전 검사 뒤 로컬 shell.html Navigate, 이어서 effective UDF 검사, 그 뒤 명시적 로그인/client 시작** 순서를 수용한다.

근거는 단순히 file URL이라는 사실만이 아니다. 다음 초기화 경로도 함께 확인했다.

1. 환경 생성 전에 자기 새 run TEMP를 만들고 Win32 GetTempPathW 반환을 대조한다. 제품의 MAX_PATH + GNLinkClient 계산과 명시 UDF 변수가 같은 새 run 경로여야 한다. 다른 WEBVIEW2_* 환경 변수, HKLM/HKCU 64/32 WebView2 policy의 존재·조회 불명은 거부한다. 환경 생성 직전에도 다시 검사한다.
2. 제품 callback은 controller를 만든 뒤 로컬 shell.html을 Navigate한다. 고정 shell.html에 외부 script/resource URL, fetch, 자동 download/new-window 경로는 없다. 시작 시 ready는 보낸다.
3. **ready가 아무 동작도 하지 않는 것은 아니다.** native handler는 load_settings와 begin_auto_login을 호출한다. 다만 이 시험은 그 전에 gShellTestDataDir를 새 run의 shelldata, gShellTestDirectoryUrl을 fixture loopback 주소로 지정한다. settings_path와 sign_in_store는 같은 시험 저장소를 사용하며, seam 설정이 없으면 사용자 설정/제품 서버로 fallback하지 않고 거부한다. 따라서 여기서 사용자 로그인 저장소나 운영 서버로 넘어가는 근거는 없다. 이 자동 복귀 시도를 숨기고 “getter 전에는 모든 로그인 코드 0”이라고 기록하면 안 된다.
4. gWebView 확보 직후 Environment7.get_UserDataFolder를 확인하고 실제 경로가 기대한 새 run 내부 폴더로 해석돼야 진행한다. 실패하면 중단하고 profile/log를 보존한다. download/new-window 거부 handler 설치가 성공해야 명시적 로그인·viewer 조작으로 넘어간다.

이 결정은 **검수한 로컬 페이지와 시험 저장소/서버 배선**에 한정한다. 실행 자료에는 로드한 shell.html 위치·해시도 바이너리 해시와 함께 남긴다. getter 불일치는 이미 환경 생성이 있었던 것이므로 “밖의 UDF에 접근하지 않았다”는 증거로 바꾸지 않는다. 위 preflight가 그보다 앞선 방어다.

Microsoft 계약상 환경 변수/정책은 생성 인자를 덮을 수 있고, Environment7 getter는 실제 사용 중인 절대 UDF를 반환한다. 이번 사전 검사와 사후 getter의 역할 구분은 그 계약에 맞는다. [환경 생성 계약](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/webview2-idl?view=webview2-1.0.4129.50#createcorewebview2environmentwithoptions), [실제 UDF getter](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2environment7?view=webview2-1.0.4129.50#get_userdatafolder).

## N2 — 공통 Child 보수 수용

ka_pc_e2e_child.hpp의 Start는 pipe/NUL/비상속 설정/attribute 초기화/HANDLE_LIST 설치를 확인하고 실패 시 CreateProcess로 진행하지 않는다. suspended child는 기존 공용 job 편입·limit 확인 뒤에만 재개한다. 자기 log를 CREATE_NEW로 열고 읽은 출력은 즉시 WriteFile로 기록하며 쓰기 오류도 판정으로 전달한다.

End는 미시작/이미 끝난 child와 구분한다. job 종료 확인과 reader의 EOF/Error/Cut을 판정하고, **Host Finalize보다 먼저 자기 log의 trailer**를 쓴다. monitor 없는 Node/Viewer도 종료·출력·로그 오류가 child_failures에 들어간다. 두 하네스의 결과 직전 children_end_all 반환을 check하고 공용 final_check_all도 호출하는 것을 확인했다. Host 문구 조건을 비Host에 억지로 요구하지 않는다.

직접 실행은 **aecbdfd에서 동결한 child test와 헤더 4개만** 별도 폴더로 추출·컴파일해 수행했다. Host/Viewer/WebView/AVD/GPU/실 입력은 실행하지 않았다.

- 위치: .claude/codex_ka_pc_review/test_r2_aec/child_test_aec.exe
- exe SHA-256: 4d596e73097a18730a80b61c840a22c02f89584c5e238a2b06e95421de73c169
- run root: .claude/test-tmp/codex-ka-child-r2-1793159951934363100/run-32592-505768671
- 결과: rc 0, ALL PASS. 자신의 sleeper child만 job 안에서 실행했다. 시작 단계 6개 거부 대조, generic end 판정, 실행 중 log 보존, 중복 End, Finalize INVALID 이전 log/trailer 보존, WebView 생성 없는 preflight 대조를 직접 확인했다.
- build.log/run.log와 자기 새 run 파일은 보존했다. 사용자 TEMP/UDF 읽기·정리, 다른 프로세스 종료, slot 파일 조작은 하지 않았다. 현재 환경/정책의 사전 검사를 읽었을 뿐 과거 상태로 일반화하지 않는다.

## Macro UDF 영향과 aecbdfd 수용 범위

제품의 진입은 viewer_startup의 toolbar onMacro, viewer_window_proc의 macro button mouse/pointer 처리 → toggle_macro_window → client_macro_window.cpp의 GetTempPathW + GNLinkMacro다. 두 고정 시험은 macro 버튼/toolbar를 조작하지 않는다. managed의 실제 mouse 메시지는 로그에 나온 desktopCard의 중앙을 누르는 경로이며 macro 조작 시험이 아니다. macro 창을 자동으로 여는 시작 호출은 확인되지 않았다.

그러나 3146c14의 환경을 자식 Viewer가 상속하면, 예상 밖 macro 창은 shell의 WEBVIEW2_USER_DATA_FOLDER를 따라 shell UDF를 공유할 수 있다. managed 시험은 부모 WebView가 없다는 설명만으로 자식 macro의 환경 override를 다룬 것이 아니다. **aecbdfd의 같은 task 보수를 최종 실행 후보로 쓰는 이유다.**

aecbdfd는 공통 preflight를 shell/macro 이름별로 분리한다. shell environment와 getter가 확인된 뒤, 명시적 로그인 전에 자식이 상속할 UDF 변수를 새 GNLinkMacro 경로로 바꾼다. managed도 child 생성 전에 macro 환경을 고정·검사한다. 이후 shell WebView 재생성 메시지는 소비하지 않고 중단한다. 기존 environment의 UDF를 바꿨다고 주장하지 않는다.

두 시험의 macro 창 관찰과 마지막 macro UDF 미생성 검사는 **예상 밖 경로의 실패 검사**로 수용한다. 창 감시는 build 폴더의 image 경로를 제한 조회하며 사용자 Viewer를 닫거나 profile을 읽지 않는다. macro getter는 검사하지 못하므로 이번 승인은 macro 창 열기·사용 성공을 포함하지 않는다. 예상 밖 macro 창이 발견되면 shell은 중단하고 managed는 실패로 기록한다. macro 기능 자체 시험이 필요하면 별도 격리/getter 경로를 갖춘 범위로 검토한다.

## 과거 영향과 다음 실행

보고서의 5분류를 수용한다. 로그에 남은 WebView 생성 12회는 **보존 로그로 확인된 실행 범위**이고, 실제 UDF·당시 환경/정책·process 공유·cookie/storage/계정 변경은 미확인이다. 새 보수로 과거 격리를 소급 증명하지 않는다. 사용자 profile 조사·삭제·복구는 하지 않는다. 기존 실제 Host EVIDENCE의 입력/전원·profile 격리 미확인 보류는 그대로이며, PC 제품 SOURCE 승인과 순수 시험까지 무효로 만들지 않는다.

검증용은 **aecbdfd의 fixed artifact hash를 기록하고 기존 슬롯 해제를 확인한 뒤, 두 지정 PC e2e의 기존 loopback/화면 밖 시나리오를 실행할 수 있다.** 이 SOURCE OK 이후 별도 실행 go를 다시 받을 필요는 없다. 자기 새 run 경로, 공용 input/power gate, 종료/EOF/final 판정, 실제 UDF getter와 UI 해시를 남긴다. old-host 게시본은 NOT RUN·미검증으로 유지한다. 금지된 viewer_prelogin_login_e2e 및 실제 OS 입력/재부팅/설치·사용자 profile 조작은 포함하지 않는다.

이번에는 검토 기록과 자기 probe 산출물만 만들었다. 제품/정책 변경·commit/push·배포는 없다. history/계획/커밋은 기존 KA 구현·안전 기록에 검증용이 취합한다. 사용자 현재 조치 없음.
