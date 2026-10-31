
### 안전 injection — 고정 패킷 (task test-injection-finding r7, 2026-10-28)

근거: 검증용 지시(urgent-check → decision → redesign-scope → r3 fix-packet), Codex `codex_test_injection_finding.md`. 규칙 1–6, Finding-2, "mode=none 채택", 진행 보고 추가 판정 :242–275.
**이번 라운드는 실행하지 않았다**(Source 수정·읽기 조사·빌드·순수 시험만). Host·Viewer·real-input 4개·fatal·prelogin 은 실행하지 않았다.

**고정점**
- ka-r3 **a8b3407**(r7: N1·N2) ← 85f9383(r6: G1·F-a·F-b·F-c) ← f4689df(r5 최종: 7c37cc2 N1·N2·T1 + 7205206 fatal F1–F4 + ba2eaf2 P2-1·P2-2 + f4689df ④ fixture) ← 6b9cf79(r4) ← 92f33ca(r3)(branch spike/host-admission-r3, tracked clean, 미추적 파일 제외). 이 라운드 커밋 세 개:
  - **d9a3a91**: 시험 guard 초판(config 방식), OS 효과 경계 `os_input_effects.hpp`, 시험 build `GNLinkStreamInputRecordTest`, 출하 gate marker.
  - **198a63a**: 표준을 `--input-injection-mode none` + 한 줄 전체 확인으로 바꿈. working helper 의 config 경로를 제거해 커밋으로 고정 — 미커밋 config 경로는 남아 있지 않다.
  - **92f33ca**: fatal_exception 의 Hello 전 gate 와 run root, opt-in 문구.
- `viewer_prelogin_login_e2e_test.cpp` 는 HEAD 그대로다(무변경). 실행 금지.
- 제품 build 동작: 변경 없음. OS 입력 호출이 `os_input::` 래퍼를 지나지만, 제품에서는 래퍼가 Win32 호출 그 자체다. 여기서 빌드한 제품 GNLinkStream.exe 에는 `os-input-record`·`stream-test-seam` 문자열이 없다(확인).





**r7 — Codex "85f9383" N1·N2(ka-r3 a8b3407, 85f9383 위; 실행 0)**
- 닫힌 것(재작업 없음): full-state 사후 power 판독, runtime monitor 실제 연결, 포트 0 기동 전 INVALID, 종료 확인·미확인 출력, prelogin log 보존, runtime test(Codex 직접 10/10).
- **N1 최종 판정 순서**:
  - `E2eInjectionMonitor` 를 세 단계로 나눴다. polling → `StopPolling()`(Host 를 끝내기 전) → `Finalize(outputComplete)`.
  - Finalize 는 **producer 종료**(job 의 모든 프로세스, 확인) **와 reader EOF 뒤** 최종 snapshot 을 판정한다. 출력 완료를 모르면 INVALID 다.
  - 모든 launch 는 test 수명 동안 등록돼 있다. `e2e_injection_final_check_all` 은 각각을 판정한다: finalized 는 그 판정, 아직 실행 중이면 지금의 출력, Finalize 없이 사라졌으면(abandoned) INVALID. "live monitor 0" 을 "모두 판정" 의 근거로 쓰지 않는다.
  - 구조체형 4개(standby·punch·migration·reboot_delegate): 이전에는 EOF 전에 `reading=false`·read end 닫기를 했고, 그래서 마지막 출력이 잘렸다. 이제는 job 종료 확인 → reader 가 EOF 까지 읽음 → join → Finalize 다. 종료가 미확인이면 읽기를 끊고 Finalize(false) 로 INVALID 다.
  - 파일 로그 4개(android·bwe·control_deadline·two_dispatcher): StopPolling → job 종료 확인 → Finalize → job close.
- **N2 fatal**:
  - std·winrt·none 의 상충, 또는 run_with_client 의 INVALID 는 **시험 전체를 그 자리에서 끝낸다**(`e2e_case_invalid`): 다음 launch 0, 삭제 0, RESULT INVALID exit 2, 그 child 의 종료 확인/미확인을 말한다.
  - run·run_with_client 의 미확인 종료는 반환되고, 그 case 의 log 는 보존된다(끝 정리에서 지우지 않음).
- **부정 대조** `e2e_guard_runtime_test` **16/16 ×3**:
  - polling 이 power ON / 다른 mode / injectionEnabled=1 을 잡는다.
  - **final 단독**(poll 하지 않는 monitor, phase 출력으로 분리)이 잡는다.
  - polling 정지 뒤의 tail 을 Finalize 가 잡는다. Finalize 하지 않으면 final check 가 잡는다.
  - abandoned·incomplete → INVALID.
  - 조용한 finalized run → 통과.
  - case 상충 → 다음 case 전에 INVALID, log 보존, 미확인 종료를 말한다.
  - F-a·F-c 는 이전 그대로다.
- 제품 소스 무변경. 영향 시험 빌드 OK. 순수: guard 20/20, power 27/27, match 9/9, legacy 13/13.

**r6 — Codex "ba2eaf2: 제품/기록 경계 SOURCE OK, 일반 하네스 NEEDS_CHANGES" G1·F(ka-r3 85f9383, f4689df 위; 실행 0)**
- 닫힌 것(재작업 없음): 제품 wrapper, power 단일 parser, os_input_record_e2e 기록 경계, IME/Attach/UIA, 10 launch job, Unicode·ctx 매칭, fatal 비재귀·loopback·private AppData·none 재분류.
- **G1 실행 중·종료 재판정**:
  - `e2e_injection_contradicted` 를 전체 상태 재판정으로 바꿨다. 명시 OFF 가 아닌 power/input 상태 줄이 앞뒤 어디든 있으면 모순이다(뒤에 나온 power ON 포함). 주입 ON 상태의 입력 수신(`injectionEnabled=1`)도 모순이다. 이전 판은 power 상충을 놓쳤다(Codex late_power_probe 반례).
  - `E2eInjectionMonitor`: 초기 gate 통과 뒤 launch 의 **전체 출력**을 다시 판정한다 — 200 ms 마다, Host Stop 때 한 번, 결과 출력 직전 한 번(`e2e_injection_final_check_all`).
  - 모순이면 그 job 을 종료한다(활성 프로세스 0 확인 여부도 말함). 출력은 `FAIL RUN …`·`RESULT: INVALID`, exit 2 다. 이것은 초기 gate 의 PASS 와 따로 보고한다.
  - 연결한 곳:
    - Host 구조체에 monitor(Stop 에서 먼저 정지): standby_admission·punch_reply·migration·reboot_delegate.
    - gate 뒤 monitor 하나(job 을 닫기 전에 정지): android_session_reboot·bwe_observe·host_control_deadline·host_two_dispatcher.
    - fatal: case 끝마다 전체 출력 재판정. run_with_client 는 INVALID, run() 은 check.
  - 8개 하네스의 결과 줄 앞에는 모두 final check 가 있다.
- **F-a 포트 확보**(`e2e_child_util.hpp`): socket·bind·getsockname 을 각각 확인하고, 실패하면 0 이다. `e2e_port_or_invalid` 는 0 이면 child 가 생기기 전에 INVALID 로 끝낸다(0 이면 parse_args 가 제품 43000 으로 돌아감 — Codex fatal_port_probe 반례). fatal 의 모든 포트(run·run_with_client·worker seam)가 이것을 쓴다.
- **F-b prelogin timeout**: 자기 handle 로 terminate 하고 bounded wait 로 종료를 **본 경우만** 확인으로 친다. 못 보면 log root 와 log 를 보존하고 정리를 보류한다고 출력한다.
- **F-c 회수 결과**: fatal 의 terminate 4곳과 공용 adoption 실패 회수가 확인한 것만 말한다. 요청만으로 "ended" 라고 하지 않는다. 미확인 종료는 FAIL 이다.
- **부정 대조** `e2e_guard_runtime_test` **10/10 ×3**. 실제 호출 경로를 쓰고 Host 는 없다: 고정 문자열과, kill-on-close job 안에서 잠자는 자기 실행 파일. case 마다 별도 프로세스다.
  - gate 통과 뒤 power ON / 다른 mode / injectionEnabled=1 → INVALID, child 종료 확인.
  - 모순 없는 run → 멈추지 않음.
  - 결과 직전 check 만으로도 잡힘.
  - port 0 과 socket() 실패 → child 전에 INVALID.
  - 잠자는 child 종료 확인. 종료를 볼 수 없는 handle → "NOT confirmed" 라고 말함.
- 제품 소스 무변경. 영향 시험 모두 빌드. 순수: guard 20/20, power 27/27, match 9/9, legacy 13/13.
- 참고: os_input_record_e2e 는 검증용이 ba2eaf2 소스로 따로 독립 실행한다(슬롯 해제 뒤). 이 r6 은 그 시험의 기록 경계를 바꾸지 않았고, 공용 guard 의 monitor 는 그 시험에 연결하지 않았다(record-only 는 injection "enabled" 가 논리 상태라 일반 OFF 판정을 적용하지 않음 — Codex 지침).

**r5 — Codex "d9a3a91 SOURCE: NEEDS_CHANGES" N1·N2·T1 + 결정 5(ka-r3 7c37cc2, 6b9cf79 위; 실행 없음)**
- **N1 record build 무효과 범위**:
  - `os_input_effects.hpp` 경계에 추가로 넣은 것: AttachThreadInput 6곳·ImmSetOpenStatus 1곳·ImmSetConversionStatus 2곳(`host_input_inject.cpp`), 자격 증명 필드의 UIA `SetValue` clear(`logon_ui_target.cpp`).
  - 제품에서는 그대로 Win32/UIA 호출이고, record-only 에서는 기록만 한다.
  - IME 의 읽기(ImmGetContext·ImmGetConversionStatus)와 foreground 조회는 실제로 남는다(관측, 효과 없음).
  - SetThreadExecutionState 는 r4 에서 이미 래핑했다(확인: `host_log.hpp` 에 직접 호출 0).
  - record-only startup 줄이 이제 다루는 API 목록과 **clipboard 쓰기는 미포함**을 명시한다. clipboard 를 건드리면 안 되는 시험은 `REMOTE60_CLIPBOARD_SYNC=0` 이다.
- **N2 자식 회수**: `e2e_adopt_or_invalid(job, pi)`.
  - job 존재 → limit 을 **다시 읽어** KILL_ON_JOB_CLOSE 확인 → suspended child 편입 → 그 뒤에만 Resume.
  - 하나라도 실패하면 자기 handle 로 child 를 종료·대기하고 handle 을 회수한 뒤 INVALID(exit 2). PID·이름 kill 은 없다.
  - 적용 10곳: os_input_record, android_session_reboot, bwe_observe, host_control_deadline, host_two_dispatcher, host_standby_admission(+ 그 directory 자식), host_punch_reply, host_migration, host_reboot_delegate.
  - fatal_exception 은 job·suspend 없이 직접 띄우고 각 case 가 자기 handle 로 종료·대기한다. 이것은 기존 그대로다.
- **T1 기록의 의미**:
  - 각 기록에 그 효과를 낳은 dispatch 를 붙인다: `ctx what=input|text|release seq=N epoch=E frag=K`. 제품의 dispatch 세 곳(`host_control_session.cpp`)에 `os_input::DispatchContext` 를 두었고, 제품 build 에서는 아무 일도 하지 않는다. dispatch 없는 효과(wake)는 `ctx none` 이다.
  - `os_input_record_match.hpp`: `[input]/[input-text] injected seq=N` 각각을 이 세션 epoch 의 그 입력 기록과 짝짓는다. 조건: fragment 순서, 필수 효과의 순서. 그 밖에는 IME 준비(AttachThreadInput·ImmSet*)만 허용한다 — 이것은 실제 foreground IME 를 읽은 결과에 따라 달라진다.
  - **부정 대조**(`os_input_record_match_test`, 순수, **9/9**): 다른 세션 epoch, 누락된 unit, 뒤바뀐 순서, 다른 입력에 붙은 효과, 예상 밖 효과, fragment 공백, dispatch 줄 없는 기록, wake 가 입력 대신 쓰임 — 모두 FAIL 이어야 하고 FAIL 이다.
  - Unicode: 텍스트는 UTF-16 unit 마다 SendInput down/up 이다(`apply_input_text_message`). 그래서 기대를 `units=1 fnv(A)`, `units=1 fnv(b)` 순서로 고쳤다(units=2 한 줄이 아님). 제품 batching 은 바꾸지 않았다.
  - **삭제**: os_input_record_e2e 의 "전후 cursor·foreground 동일 = 이 PC 무효과" 판정과 그 주석. 무효과의 근거는 검토된 효과 경계(호출 대신 기록)다. 모의 기록의 "성공" 반환은 OS 수락·UIPI·실제 입력 성공을 증명하지 않는다.
- **fatal F1–F4**(Codex "92f33ca SOURCE: NEEDS_CHANGES", ka-r3 7205206):
  - F1: **재귀 삭제를 모두 없앴다.** 이 시험이 쓴 파일을 이름으로 하나씩 지우고(`delete_known_file`), 만든 폴더는 비었을 때만 지운다(`remove_made_folder`). 삭제 실패(이미 없음 제외)는 FAIL 로 센다. 비지 않은 폴더는 NOTE 로 남긴다. Host 들의 LOCALAPPDATA 폴더(`lad_*`, diagnostics)는 지우지 않는다 — test root 안에 남는다.
  - F2: `none` 은 **정상 control**(오류 주입 없는 3 s 시작)이고 조기 fault 가 아니다. 주장은 "이 시험이 Host 에 소켓을 열지 않는다(이 시험의 client 연결 0)" 로 고쳤다. loopback listener 에 다른 로컬 프로그램이 닿을 수 없다는 주장은 하지 않는다.
  - F3: 모든 launch 를 `--bind-address 127.0.0.1` 과 빈 포트에 묶었다(이전 run() 은 bind 주소·포트가 없어 제품 포트·모든 interface 였다 → 방화벽 창 위험). --prelogin worker 는 자기 test seam(`GNLINK_STREAM_TEST_BIND_ADDRESS/PORT/SECONDS`)으로 묶었다. 각 launch 는 이번 실행 폴더 안의 LOCALAPPDATA(`e2e_isolated_environment`)를 쓴다 — 사용자 것이 아니다.
  - F4: 세 경로(run·run_with_client·prelogin) 모두 6b9cf79 부터 `--host-power-management off` 를 받는다(확인). run_with_client 의 Hello 전 gate 는 power 줄 전체도 요구한다.
    - 무client case(std·winrt·none·worker)의 **효과 0 근거는 main() 순서**다: power 정책을 HostPowerKeepalive 생성 전에 명령줄에서 bind 하고, OFF 면 요청이 하나도 없다.
    - 각 Host 가 말한 `host power management=off` 줄은 사후 **기록**으로 확인한다(gate 가 아님 — 어차피 아무것도 연결하지 않는다). none 은 입력·power 두 줄 모두를 확인한다.
- **P2-1**(Codex 6b9cf79 SOURCE, ba2eaf2): power 스위치를 host 의 **하나의 parser**(`parse_args`)가 다른 스위치와 같은 token·arity 규칙으로 읽는다(`Args::hostPower`).
  - 따로 argv 를 훑던 `bind_host_power_policy` 는 제거했다. 그것은 다른 스위치의 피연산자를 power 스위치로 잡았다.
  - main 은 parse_args 를 먼저 한다(인자와 --config profile 만 읽고, 기계에 작용하지 않음). 그다음 `args.hostPower` 로 HostPowerKeepalive 를 만든다.
  - `startup_process_setup`(우선순위·로그 prefix)은 그 뒤에 온다. 이것은 parse_args 가 세우는 값을 읽지 않는다(`REMOTE60_NATIVE_NORMAL_PRIORITY` 는 profile 이 지우거나 세우는 목록에 없음).
  - 회귀: 창 제목 `"--host-power-management"` → 제목(오류 없음, 기본 ON). 그 뒤 stray `off` → 여전히 스위치 없음(OFF 아님). 제목 뒤의 진짜 스위치 → OFF. `--bind-address` 의 피연산자 → 스위치 아님. 자기 피연산자만 소비.
- **P2-2**: gate 가 한 launch 의 **모든** `host power management=` 줄과 `input injection ` 줄을 본다. 일치하는 줄의 앞이든 뒤든 다른 줄이 하나라도 있으면 거부한다.
  - power OFF → input OFF → power ON: 거부.
  - input OFF 뒤 enabled 나 다른 mode: 거부.
  - compile-time policy 줄: 명시 OFF 가 아니므로 거부.
  - 같은 OFF 줄 두 번: 통과.
- **④ 순수 fixture**(f4689df, `standby_legacy_host_test`, **13/13**, 순수 loopback 실행): `--old-host`(게시 0.2.163) case 들의 대역이다. 그 case 들은 이 PC 에서 NOT RUN·미검증이고, VM 은 선택 후속이다.
  - legacy host 모델: loopback UDP 소켓이다. 표준 이전 Host 가 standby kind(311–316)에 하는 그대로, **아무것도 하지 않는다**(모르는 kind 는 버림). 응답도, "Unsupported" 도 보내지 않는다(옛 wire 에는 그런 것이 없다). 받은 것은 kind 별로 기록한다.
  - 그 상대로 **제품의** StandbyLease/StandbyRunner 를 돌린 결과:
    - StandbyHello 정확히 kHelloAttempts(6)개, 그다음 Unsupported. 그 뒤 2.5 s 동안 추가 송신 0.
    - activation 인자 없음, Activate 0, 일반 Hello 0.
    - admission 도중 release → 즉시 멈춤(3 s 동안 추가 0).
    - runner StatusOf = Unsupported, TakeActivation 거부.
  - 자동 일반 연결 대체는 앱 판단이다: `StandbyKeep.onTap(UNSUPPORTED) == Cold(releaseFirst = true)`(StandbyKeepTest.kt:65, Kotlin 단위).
  - 가짜 Unsupported ACK·결과 stub 은 없다. **게시 Host 실행도, 보류된 EVIDENCE 의 복구도 아니다** — 옛 wire 의 침묵을 모델로 두고 제품 client 코드를 돌린 것이다.
- 시험(순수, ba2eaf2·f4689df): legacy_host **13/13**, record_match **9/9**, power_policy_record **27/27**(P2-1 5개, P2-2 4개 추가), injection_guard **20/20**. Codex 독립 빌드·실행: power 18/18·guard 20/20(6b9cf79 기준). host 변형 7개와 영향 시험 빌드 OK.

**결정 5 — prelogin 자격 증명 gate 의 관측 provider(설계·구현 범위만, 미구현; viewer_prelogin_login 실행 금지 유지)**
- 지금 gate 의 관측(`logon_ui_target.cpp`):
  - `PasswordFieldIdentity()`: GetForegroundWindow → pid → 같은 session(ProcessIdToSessionId) → image(LogonUI, test build 는 `GNLINK_STREAM_TEST_CRED_TARGET_TITLE`).
  - `identity_of_focused()`: UIA GetFocusedElement → IsPassword → RuntimeId hash.
  - `ReplaceWithUnicode()`: 같은 관측으로 clear 전과 typing 전에 identity 를 재검증한다.
- 설계:
  - `logon_ui::ObservationProvider` 인터페이스를 둔다: `Foreground()` → {hwnd, pid, sameSession, imagePath, ok}, `FocusedElement(hwnd, pid)` → {isPassword, runtimeIdHash, ok}, `InputDesktopName()`.
  - 제품 구현: 지금 그대로의 Win32/UIA 호출(이름만 옮김).
  - **판정 코드**(같은 session·image/title·password·identity 일치·재검증)는 provider 를 읽는 제품 함수 그대로 공유한다. 결과를 stub 으로 바꾸지 않는다. 항상 focus=true 나 항상 성공도 쓰지 않는다.
  - test 구현: 새 test target 만(예: `GNLinkStreamCredRecordTest` = record-only + `REMOTE60_CRED_OBSERVATION_SEAM`, 출하 gate marker 추가).
    - 관측값을 시험이 쓰는 대본에서 읽는다(시험 소유 파일/pipe, 단계별). foreground pid/image/session, focused element password/identity, 조회 실패, 단계 사이의 변화가 들어간다.
    - 쓰기(clear `UIA.SetValue`, typing `SendInput`)는 r5 경계에서 기록만 된다.
  - 시험 case(별도의 새 시험; 금지된 viewer_prelogin_login 이 아님):
    - 유효한 대상 → clear 1·typing(units·fnv) 기록.
    - 거부 → clear·typing 기록 0: 다른 pid/image, password 아님, 전경 없음, 조회 실패, 오래된 identity.
    - clear 와 typing 사이에 대상이 바뀜 → 제품 재검증 → typing 0.
    - 사용자 Default 에서 창 전경화·Tab·SetFocus·커서 조작은 하지 않는다.
- 구현 범위:
  - `logon_ui_target.{hpp,cpp}`: 관측을 provider 로 옮긴다(제품 동작 불변).
  - provider 주입점: `host_control_session.cpp prelogin_cred()` 의 TargetFn/InjectFn 가 이미 주입식이다.
  - 새 CMake target 과 marker, 새 e2e 1개.
- **격리 실기 미검증**: 실제 Windows foreground·SYSTEM/LogonUI·UIPI·인증 성공. 승인된 격리 OS 에서만 확인한다.

**r4 — Finding-3 결정 ⑴ 구현(ka-r3 6b9cf79, 92f33ca 위; 실행 없음)**
- 제품: `host_power_policy.hpp` — `--host-power-management on|off`. 명령줄만 받고, 기본 ON 이다(제품 동작 불변).
  - `native_video_host_main.cpp` 에서 HostPowerKeepalive 생성 **전에** bind 한다(생성자의 ES_SYSTEM_REQUIRED 요청 포함).
  - 불변이고 setter 가 없다 → directory·control·viewer·runtime tune·stream 재시작으로 바꿀 수 없다.
  - 그 밖의 값이면 시작을 거부한다(exit 65). bound 값은 한 줄로 남긴다: `host power management=on|off (local startup policy, …)`.
  - `host_args.cpp` 는 그 값을 일반 인자로 읽지 않는다.
- HostPowerKeepalive(`host_log.hpp`): 생성자는 `explicit HostPowerKeepalive(bool)` 하나뿐이다(기본 생성자 없음).
  - OFF 이면 생성·stream 경계·소멸 어디서도 execution-state 요청과 wake 가 0 이다. streaming 상태만 추적한다.
  - wake 는 private 이라 다른 호출자가 정책을 우회할 수 없다.
  - 모든 효과는 OS 경계(`os_input_effects.hpp`)를 지난다. `SetThreadExecutionState` 래퍼를 추가했고, record-only build 에서는 기록만 된다.
- guard: 인자 = `--input-injection-mode none --host-power-management off`. 이번 launch 가 **두 줄 모두 한 줄 전체로** 말해야 통과한다.
  - 거부: power on(기본 줄), 인자 거부(`invalid --host-power-management value`).
  - 아직(0): power 줄 없음(구 build).
  - 표의 argv 열은 모두 이 두 인자로 바뀌었다. --prelogin launch(standby·punch·fatal)에도 붙인다 — power 객체는 첫 줄부터 있다.
- **④ 구 바이너리**: `--old-host` / `REMOTE60_E2E_OLD_BIN` 은 이 PC 에서 띄우지 않는다. 인자를 무시하고 power 효과를 유지하기 때문이다. 해당 case 는 **NOT RUN, 미검증**으로 출력한다(admission 의 old-host 두 블록, punch 의 OLD_BIN backstop). 대안(승인된 격리 환경 / 검토된 source-equivalent test build)은 결정 대기다.
- 시험:
  - `host_power_policy_record_test`(record-only, 순수) **18/18**. 확인한 것:
    - binding: on/off/last/오타/누락/철자 틀린 switch(→ 기본 ON 유지 → gate 거부).
    - OFF: 생성·경계·반복·소멸에서 효과 0.
    - ON: 생성자 ES_SYSTEM_REQUIRED, 경계의 ES_DISPLAY_REQUIRED + wake 1회(SendInput ±1, broadcast), 두 번째 wake 없음, 해제. 모두 기록만이고 수행 0.
  - `e2e_injection_guard_test` **20/20**.
  - host 변형 7개와 영향 시험 12개 빌드 OK. 제품 GNLinkStream.exe 에 record-only marker 없음, 출하 gate self-test 52/52.
- wake 열의 의미: 위 guard 를 쓰는 9개 시험은 OFF 를 확인한 launch 에서만 연결하므로 wake·execution-state 요청이 0 이다(소스·record 시험 기준, 실행 전). 아래 표의 "예" 는 **이전 실행(guard 전)의 노출**이다. MigrationTest 를 쓰는 시험도 같은 OFF 인자로 0 이다.

**① shared guard — `e2e_injection_guard.hpp`(92f33ca)**
- 인자: `e2e_injection_off_arg()` = 정확히 ` --input-injection-mode none`. parser 가 지원하는 Disabled 값이다(`host_input_inject.cpp parse_input_injection_mode`). unknown fallback 을 쓰지 않는다. config 는 쓰지 않는다(profile 이 runtime override 전체를 지우므로).
- 확인: `e2e_wait_injection_off(이번 launch 의 출력, 20 s)`. 통과 조건은 한 줄 전체 `input injection disabled (unsupported mode) mode=none` 에 줄 끝(`\r`/`\n`)이 붙은 것뿐이다.
  - 불인정(-1): `input injection enabled` 가 출력 어디에든 있음, 다른 mode 값(none2·bogus), config OFF 줄, `failed to load --config`.
  - 아직(0): 줄 끝 전, 침묵(→ 20 s 뒤 거부).
- 실패: `e2e_injection_setup_failed()` → `FAIL SETUP … RESULT: INVALID`, `std::_Exit(2)`. client 객체·소켓이 생기기 전이다. Host 는 job(KILL_ON_JOB_CLOSE)과 함께 끝난다.
- 모순: `e2e_injection_contradicted()` = 입력 로그의 `injectionEnabled=1`.
- 순수 부정 대조 `e2e_injection_guard_test` **16/16**(이 PC, Host 없음).

**하네스·launch 별 표**(ka-r3 7205206 기준 file:line — ba2eaf2 는 이 시험 파일들을 바꾸지 않음. "연결 0" = 실패 시 client 생성 전 종료)
- 두 열은 서로 다른 질문이다. **exe 열**: 그 실행 파일이 OS 효과(입력·전원·wake)를 실제로 수행하는 build 인가. **도달 열**: guard 이전 실행에서 그 launch 가 생성자 power 요청과 stream wake 경로에 실제로 이르렀는가.
- 도달 열은 **소스 순서에 따른 정적 판단이며 관측이 아니다.** 생성자 power 는 main() 이 HostPowerKeepalive 를 만들 때다. stream wake 는 첫 capture 시작(첫 client Hello 뒤, `streamControlActive` 기본 true)과 그 뒤의 경계다. "모든 시험이 실제로 wake 했다" 는 일괄 관측으로 쓰지 않는다.
- r5 현재: 아래 guard launch 는 모두 `--host-power-management off` 다 → 생성자 power 요청 0, wake 0. 근거는 정책 bind 순서와 record-only 순수 시험이다(실행 전).

| 시험 | launch / case | argv(r5) | 이번 launch OFF 확인 | 실패 시 | client 입력 | exe — OS 효과 수행? | guard 전: 생성자 power 도달 | guard 전: stream wake 도달 |
|---|---|---|---|---|---|---|---|---|
| host_standby_admission_e2e | StartHost: standby·realdir·relay·… | mode none + power off (`:317`) | StartHost `:388`(pipe), adopt `:369` | INVALID exit 2, 연결 0 | 없음 | 제품 GNLinkStream.exe — **수행** | 예 | 예 — product client 가 세션·영상 |
| 〃 | --prelogin worker | 같은 인자(`:317`) | 없음 — client 0 | — | client 0 | 제품 — 수행 | 예(생성자는 첫 줄 근처) | 아니오 — client 0 |
| 〃 | --old-host(baseline) | — | — | **NOT RUN, 미검증**(Finding-3 ④) | — | baseline 제품 — 수행 | (과거 실행: 예) | (과거 실행: 세션 case 에서 예) |
| host_punch_reply_e2e | armed·move·ack·badtoken·expiry·late | 같은 인자(`:290`) | StartHost `:361`, adopt `:342` | INVALID, 연결 0 | 없음(Hello·control) | 제품 — 수행 | 예 | 세션이 성립한 case 만 가능(정적) |
| 〃 | prelogin | 같은 인자 | 없음 | — | client 0(worker exit 14) | 제품 — 수행 | 예 | 아니오 |
| 〃 | REMOTE60_E2E_OLD_BIN | — | — | **NOT RUN**(backstop) | — | — | — | — |
| host_migration_e2e | carried·unlisted·rejected·down | 같은 인자(`:172`) | StartHost `:300`, adopt `:278` | INVALID | client 0(등록만) | MigrationTest — **수행**(record-only 아님) | 예 | **아니오 — client 0** |
| host_reboot_delegate_e2e | 1 host | 같은 인자(`:186`) | `:410`, adopt `:219` | INVALID | 재부팅 요청(입력 아님) | MigrationTest(GNLinkStream.exe 로 staging) — 수행 | 예 | 가능 — 세션 |
| android_session_reboot_e2e | 1 host | 같은 인자(`:197`) | `:230`, adopt `:218` | INVALID | 재부팅 요청 | 제품 — 수행 | 예 | 가능 — phone 세션 |
| bwe_observe_e2e | 1 host(자기 창 capture) | 같은 인자(`:393`) | `:429`, adopt `:423` | INVALID | 없음 | 제품 — 수행 | 예 | 예 — 영상 240 s |
| host_control_deadline_e2e | 1 host | 같은 인자(`:224`) | `:267`(staging host.log), adopt `:262` | INVALID | 없음 | 제품 — 수행 | 예 | 가능 — 원시 소켓 연결이 capture 를 시작하는 경우 |
| host_two_dispatcher_e2e | 1 host | 같은 인자(`:252`) | `:293`(staging host.log), adopt `:287` | INVALID | 없음 | 제품 — 수행 | 예 | 가능 — 세션 |
| host_fatal_exception | run(): std·winrt(startup fault) | 같은 인자 + 127.0.0.1(`:199`) | 없음 — 이 시험 소켓 0. power off 줄은 사후 기록 | — | 0 | MigrationTest — 수행 | 예(생성자는 throw 전) | **아니오 — configure 단계에서 throw** |
| 〃 | run(): none(정상 control) | 같은 인자 + 127.0.0.1 | 사후 기록(입력·power 두 줄) | — | 0 | MigrationTest — 수행 | 예 | **아니오 — client 0** |
| 〃 | run_with_client(): probe-trap·wrapper-trap·wgc-throw·wgc-false | 같은 인자 + 127.0.0.1(`:253`) | Hello 마다 직전 `:288` | case 종료, Hello 0, INVALID 계수 | bare Hello | MigrationTest — 수행 | 예 | 아니오 — capture 전 종료(정적) |
| 〃 | run_with_client(): normal | 같은 인자 + 127.0.0.1 | Hello 마다 직전 `:288` | 〃 | bare Hello | MigrationTest — 수행 | 예 | 가능 — encoder 줄까지 감 |
| 〃 | --prelogin worker(log root) | 같은 인자 + seam 127.0.0.1(`:395`) | 사후 기록(power 줄) | — | client 0 | MigrationTest — 수행 | 예 | 아니오 — 첫 단계에서 throw |
| os_input_record_e2e(신규) | record-only Host | 인자 없음(power ON 이지만 기록만) | `:193` record-only 줄, adopt `:184` | INVALID | Tab·move·click·text → 기록만 | **InputRecordTest — 수행 안 함(기록만)** | 기록만(수행 0) | 기록만(`ctx none`, 수행 0) |
| control_resume, viewer_mouse_xbutton, viewer_paste, viewer_release_all | — | — | — | **기본 REFUSED exit 3**(Host 이전) | — | — | 실행 안 함 | 실행 안 함 |
| viewer_prelogin_login | — | — | — | 실행 금지(무변경) | — | — | — | — |

**② real-input 4 — 기본 REFUSED 유지, 자동 회귀는 설계안만**
- `REMOTE60_ALLOW_REAL_INPUT_TEST=1` 은 승인된 격리 목적지(VM)를 위한 기술 스위치일 뿐 권한이 아니다. 에이전트는 켜지 않는다(helper 주석에 명시). 거부는 wmain 첫 줄이라 창·clipboard·hook·Host 전이다. exit 3/REFUSED 는 PASS 로 세지 않는다.
- **자동 회귀 설계안**(미구현). 공통 구조: Host 는 `GNLinkStreamInputRecordTest`(제품 수신·정책·dispatch, OS 효과만 기록). 판정은 Host 측 `[input] injected seq=…`(dispatch)와 `[os-input-record] …`(sink)를 seq·session·payload 로 짝짓는다. 각 시험에 부정 대조를 둔다. 항상 성공 stub·Queue/ACK 판정·원 OS 단정 유지는 하지 않는다.

  | 시험 | record-sink 로 자동 검증할 것 | 부정 대조 | 남는 실 OS 항목(승인 VM) |
  |---|---|---|---|
  | control_resume | 끊김→resume 뒤 같은 세션의 key-up 이 sink 에 그 seq 로 도착. 끊김 동안 큐의 폐기/유지 | resume 실패 시 sink 도착 0 | OS 키 상태 복구 |
  | viewer_mouse_xbutton | capability 협상 후 X1/X2 down/up 이 sink 에 XBUTTON 으로 | 구 Host(capability 없음)에는 송신 0 / 미지원 시 sink 0 | 대상 앱의 실제 버튼 처리 |
  | viewer_paste | paste gate·baseline·취소 정책 뒤의 key 순서가 sink 에. clipboard 는 fixture backend(사설 station) | 실패/취소 시 키 sink 0 | 실제 OS clipboard/붙여넣기 |
  | viewer_release_all | held 집합 → up 생성 → 세션 종료 전 sink 도달 | 아무것도 안 눌렸으면 up 0 | 실제 OS 키/버튼 해제 |
  - 남은 결정: clipboard 의 fixture backend(사설 window station, parkclip 방식)를 record-only build 에서 쓸지는 소유자·Codex 결정이다. focus 판정(`target_has_focus`)은 record-only 에서도 실제 전경을 읽는다 → 자기 창을 전경으로 가져오지 않는 seam 이 필요한지도 함께 결정한다.

**③ fatal_exception(92f33ca)**
- run_with_client 5 case: Hello 는 이번 launch 의 한 줄 전체 OFF 확인 뒤에만 보낸다(`:238`). 다른 상태면 그 case 를 즉시 끝내고 Hello 0, `kInjectionInvalid` 로 계수한다. 확인 전에 죽은 Host 에는 Hello 를 보내지 않는다.
- run() 3 case: client·socket 자체가 없는 startup fault 다. 기록: "OFF 줄 미확인, startup-fault 분리, 연결 0·입력 0".
- 로그와 `fatal_prelogin_logroot` 는 신뢰 root(`REMOTE60_TEST_TMP`, `D:/remote/remote/.claude/test-tmp` 바로 아래 plain folder, link 없음) 안에 이번 실행의 새 폴더(CreateDirectory 신규)를 만들어 그 안에 둔다.
  - 유일한 재귀 삭제 `remove_inside_run()` 은 canonical 경로가 그 폴더 안이고 `..`·link 가 없을 때만 한다.
  - root 가 유효하지 않으면 실행을 거부한다(아무것도 만들거나 지우지 않음).
  - 이전: exe 옆에 로그를 쓰고 `fatal_prelogin_logroot` 를 시작·종료 때 remove_all 했다 → build 가 저장소 밖이면 저장소 밖 재귀 삭제다. **과거 실제 삭제 여부는 확인하지 않았다**(이 사본은 이번 세션에 실행하지 않았다). 바깥 경로는 정리하지 않고 보존한다.
- 빌드만 했다. 실행하지 않았다.

**④ wake(Finding-3, Codex 미결) — 표의 마지막 열**
- `host_log.hpp:92 wake_display_for_remote_session()` = SendInput 상대 이동 +1/−1 두 개 + `PostMessage(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, −1)`.
- `HostPowerKeepalive::SetStreaming(true, true)` 의 not-streaming→streaming 경계에서 실행된다. `streamControlActive` 기본값이 true 라서 **첫 capture 시작에서 1회**(`host_startup_capture.cpp:280`), 그 뒤 stream-inactive→active 와 desktop rebuild 경계마다 실행된다.
- fatal_exception 주석 기준으로 Host 는 첫 client Hello 뒤에 capture 를 시작한다. 그러므로 guard 이전에 **wake 경로에 이르렀을 수 있는** 것은 client 가 붙어 capture 를 시작한 launch 다 — **정적 판단이고, 실행별 실제 호출·OS 반영·횟수는 관측하지 않았다**(반환값을 버리는 코드). launch 별 도달 여부는 위 표의 두 "도달" 열에 있다. 이 작업용이 관여한 실행 중 그 조건에 해당할 수 있는 것:
  - AVD ke fixture Host 4대(세션마다; mode none 과 무관).
  - sharpen 실행.
  - kae_* 11회(product client 세션).
  - 작업용2 의 ka-pc 는 그쪽 집계 범위다.
- client 0 인 launch(migration, fatal 의 무client case, prelogin worker 등)는 stream wake 에 이르지 않는다고 판단한다. 다만 **생성자 power 요청(ES_SYSTEM_REQUIRED)은 client 와 무관하게 모든 launch 에 해당한다**(guard 이전).
- client 가 없던 이 작업용의 loopback 프로브 4회(test-tmp, 각 4–12 s)는 capture 전이라 해당 없다고 판단한다(소스 기준).
- record-only build 에서는 기록만 된다(`os_input_effects.hpp`). **GNLinkStreamMigrationTest 는 record-only 가 아니다** — 제품과 같은 OS 효과를 수행한다(표의 exe 열 참조). wake 를 피하려면 record-only build 로 옮기거나 제품 옵션이 필요하다.

**과거 실행 판정·집계**(이 작업용 보존 로그, `inj_tally.py`)
- 주입 enabled Host 로그: sharpen 21 실행(`caplife/sh_r2`)뿐이다. `mode=window`, 대상 `GNLINK-SH-PATTERN`(시험 자신의 창), PostMessage mouse down 200·up 200·move 11, key 0, `osInjectionConfirmed=0`. wake 는 로그 줄이 없다(소스로 판단).
- `host_standby_admission_e2e` kae_* 11회: Host stdout 미보존. client 입력 0(소스). 주입 ON(default)·wake 경로는 있었다.
- AVD ke: ke_r21 Host 4대 `mode=none` 실측. wake 는 별도로 있었다.
- `viewer_prelogin_login`(검증용 10-26~27 실행분): 실 입력 경로가 있었다 — 자격 증명 SendInput, Tab SendInput, pointer, wake. 수는 미확인이다.

**`--config` 판단 기록**: profile 은 `clear_runtime_env_overrides()` 로 runtime override 를 모두 지운 뒤 JSON 에 적힌 것만 세운다. 그래서 `ENCODED_EXPERIMENT_FORCE` 가 사라져 h264 를 거부했다(d9a3a91 이전 kae_inj 중단 원인). 공통 기본은 none 이다(Codex).

**남은 것**
- 실행 전 선행조건: ⑴ Codex SOURCE(이 패킷), ⑵ wake 결정(Finding-3), ⑶ SLOT_REQUEST.txt 사용자 삭제. 그 뒤 9개 guard 시험과 os_input_record_e2e 를 실행한다.
- real-input 4개 record-sink 전환 구현, prelogin 재설계 구현: 결정 대기.
