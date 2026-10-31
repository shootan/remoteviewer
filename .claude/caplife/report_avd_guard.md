# keepalive-r2 e — AVD 하네스 guard 통합 (실행 0)

task=keepalive-r2(e) / kind=fix(AVD 하네스 guard 통합) 결과

## 고정
- feat/apk-0.2.33 (feat-apk-0233):
  - **ccf6455**: merge ka-r3 **d665957** (공용 guard 최종). 7f07f1a 위다.
  - **48d096a594a072274abac80d9464e0b561614d65**: AVD 하네스 Host 에 guard 적용(HEAD).
  - tracked clean, `git diff --check` 정상.
- 실행 0: AVD·Host·에뮬레이터를 띄우지 않았다. 빌드와 순수 시험만 했다.

## 1) 병합
- `git merge --no-ff d665957` 은 **충돌 0** 이었다. merge-base 는 0fb4676 이고, ka-r3 commit 12개, 36파일, +3494/−180 이다.
- 제품 파일(host_args, host_log, native_video_host_main, host_input_inject, logon_ui_target, secure_input_broker, host_control_session, host_startup_config, os_input_effects 등)과 시험 파일(guard 헤더, 하네스 8개, fatal, runtime, record 시험) 모두 자동 병합됐다.
- feat 쪽 X 경계 파일과 겹친 hunk 는 없다.
- 병합 뒤 순수 시험(모두 exit 0):

  | 시험 | 결과 |
  |---|---|
  | e2e_injection_guard_test | 20/20 |
  | host_power_policy_record_test | 27/27 |
  | client_session_xbutton_fence_test | 75/75 |
  | role_park | 13/13 |
  | prelogin_select | 60/60 |

## 2) AVD 하네스 Host → 공용 guard 그대로 사용
`mobile_abr_session_e2e_test.cpp`(ke_fixture 가 `--scenario directoryhost` 로 부르는 것, 다른 scenario 도 같은 launch 를 쓴다)를 고쳤다. 자체 변형은 없고, ka-r3 의 guard 하네스 8개와 같은 호출 순서다.
- 인자: `e2e_injection_off_arg(dir)` = `--input-injection-mode none --host-power-management off`. 이전의 `--input-injection-mode none` 단독을 대체한다.
- 생성: `CREATE_SUSPENDED` → `e2e_adopt_or_invalid(job, hostPi, "mobile_abr")`. KILL_ON_JOB_CLOSE 를 다시 읽어 확인한 뒤 배정하고 resume 한다. 이전의 Assign + Resume 을 대체한다.
- client 전 gate: `e2e_wait_injection_off`(이 launch 의 로그, 두 OFF 줄 whole, 20 s). 실패하면 `e2e_injection_setup_failed` 로 INVALID exit 2 이고, directoryhost 는 ready 를 쓰지 않는다.
- 실행 중 감시: `E2eInjectionMonitor`(200 ms).
- 끝(directoryhost 와 일반 경로 모두 같은 `endHostJudged`):
  1. StopPolling
  2. `e2e_end_job_confirmed(job)`
  3. host.log 쓰기 handle 닫기
  4. `Finalize(hostEnded, e2e_final_file(hostLogPath))`
  5. CloseHandle(job)
  - 결과 출력 전에 `e2e_injection_final_check_all()` 을 부른다.
  - 이전의 Host 단독 `TerminateProcess` 를 job 종료로 대체했다.
- fixture 위치: StagingDir(`test_scratch_dir`, 저장소 안 build/test-scratch 아래, 이 process 가 만든 폴더만). ke_fixture 의 root 는 `feat-apk-0233\.claude\e2e\<Tag>` 다. 둘 다 저장소 안이다.
- 바이너리 확인(실행 없이): 하네스 exe 안에 gate 문구 1건, UTF-16 `--host-power-management off` 1건, `GetTempPathW` 0건.

## 3) pinned 시험 Host 재빌드 / 해시
| 산출물 (feat build Release) | 이전 (ke_r20·r21 때) | 지금 (ccf6455 소스 + 48d096a) | 확인 |
|---|---|---|---|
| `GNLinkStreamPinnedSelectTest.exe` | `44a43610c61590c2…` | `01F72D725932158B8AF711DD41C30C7C5A50D4CDE0ECB77E343CC040EE97DC79` (13:15:42) | `--host-power-management` 4건, `host power management=` 2건, OFF 입력 줄 1건. record-only 표지 0(실 효과 빌드, 입력·전원은 인자로 끔) |
| `remote60_mobile_abr_session_e2e_test.exe` | `38ddf6cd…`(fixture 기록) / 직전 `67077445…` | `3538935F2359AE37AB2CC2EFDDBAB07A3F172320C3438508F653B0CD9EDEA7E0` | 위 2) |
| `GNLinkStream.exe` (같은 빌드 부산물, AVD 미사용) | `a3ff0dc1…` | `263546BCAB00CCFE…` | — |
| `GNLinkStreamPinnedOld.exe` (baseline old host) | `b6ed8984…` | 재빌드 안 함 | power 옵션 0건 → guard gate 가 거부한다. **NOT RUN**. fixture 가 더 이상 복사하거나 띄우지 않는다. |

## 4) %TEMP% / clipboard
- `mobile_abr_session_e2e_test.cpp`, `e2e_isolation.hpp`, `e2e_station_lock.hpp` 와 AVD 스크립트(`ke_avd.py`, `ka_avd.py`, `ke_fixture.ps1`, `ke_slot.ps1`, `ka_delay_proxy.js`)를 grep 했다. `GetTempPath`/TEMP/TMP/tempfile 사용은 **0건**이다(주석 1건 제외).
  - 참고(고치지 않음): Host 자식의 환경은 LOCALAPPDATA 만 staging 으로 돌리고 TEMP 는 사용자 값을 상속한다(`e2e_isolated_environment`). 하네스 자신은 그곳에 만들거나 지우지 않는다.
- 에뮬레이터 clipboard 를 바꾸는 단계: `ke_avd.py` 에 **없다**. 결정 ① 전까지 넣지 않는다.

## AVD 스크립트 (저장소 미추적 `feat-apk-0233\.claude\e2e\`, 고정 해시)
| 파일 | 변경 | sha256 |
|---|---|---|
| `ke_fixture.ps1` | old host 복사와 실행 제거, fixture.txt 에 "old host: NOT RUN" 기록 | `51373fdff4ad5d8afc9dbfa0d198ed62ffcb4cf8e5b22cd677ff366a8f2931c8` |
| `ke_avd.py` | `OLD_HOST=False`, H 에서 old 제외. E7f·E8·E12(최근 3개에 old 포함)는 `skip_case` → NOT RUN 으로 기록하고 PASS 에 넣지 않는다. 결과 줄은 "x/y PASS, n NOT RUN" | `6b381b79d99a4d53323de2c132110050674306bf4a170740f2b63d83d6e3d3ac` |
| `ke_slot.ps1` | 변경 없음 | `0f908fd3728a6545…` |
| `ka_avd.py` | 변경 없음 | `a6f787fc2e540f41…` |

- 변경 전 사본: `.claude/caplife/ke_avd.py.before_guard`, `ke_fixture.ps1.before_guard`.
- 구문 확인만 했다(Python ast parse, PowerShell parser 오류 0). 실행하지 않았다.
- E12 는 old host 없이 다시 설계해야 한다(후속).

## 미검증 / 다음
- 하네스 guard 의 실제 동작(gate 통과, monitor, Finalize)은 Host 를 띄워야 보인다. 다음 AVD 실행에서 처음 확인한다. 그 전에 SOURCE 판정이 필요하다.
- pinned host 가 directoryhost 등록 전에 두 OFF 줄을 쓰는지는 소스 순서(startup config → directory agent)로만 판단했다. 실행 확인 전이다.
- E12 재설계, R3 clipboard(결정 ①), fallback fault(결정 ②)는 미결이다.

## 추가 — 결정 ② activation 실패 fault (같은 고정 SOURCE 로 묶음)
- commit **57fa793205d0d2c41725236ada46cc5b47998565** (feat-apk-0233, 부모 48d096a, HEAD).
- 5파일: `host_pinned_select_seam.hpp` · `host_standby_wire.hpp` · 새 `host_standby_activation_fault_test.cpp` · CMake · `automation/gnlink_check_fixed_server.py`.
  - `host_control_session.cpp` 는 순변경 0이다(정의를 헤더로 옮겼다).
- fault (seam 빌드 `REMOTE60_PINNED_SELECT_TEST_SEAM` 에서만):
  - fault 파일의 `fail_activation_once=<token>` 이 이 Host 의 **다음 StandbyActivate 하나**를 거부한다.
  - 응답은 기존 `kUdpStandbyStatusUnknown` StandbyPong 이다(만료된 entry 와 같은 응답).
  - token 하나당 1회만 적용되고, 같은 token 은 다시 적용되지 않는다.
  - 로그: `[pinned-select-test] fault: fail_activation applied token=<t> entry=<id> gen=<g> -- refused as unknown (once)`.
  - table 은 변경하지 않는다. 뒤의 activation 과 모든 ordinary Hello 는 영향받지 않는다.
  - 위치: `standby_handle` 의 StandbyActivate 분기 맨 앞(`#ifdef`). fault reader 는 header-only 라 seam 빌드 단위 시험이 같은 코드를 부른다.
- 클라이언트 쪽 경로(소스):
  - Unknown 응답 → `StandbyEntryClient::Lose`
  - → activation 실패 → `nativeConnectStandby` gen 0
  - → `MainActivity` 에서 `mine` 이면 `standby_activate_failed ... mine=true` 를 남기고 `connectToDirectoryHost(host, RETRY, preparedReturn=true)` 로 간다.
- 출하 gate:
  - `TEST_MARKERS` 에 `pinned-select-test` 와 `REMOTE60_PINNED_TEST_` 를 추가했다. self-test 57/57.
  - 부정 대조: pinned host 를 GNLinkStream.exe 로 놓고 스캔하면 "found: pinned-select-test, REMOTE60_PINNED_TEST_" 로 FAIL 한다.
  - 양성: 제품 `GNLinkStream.exe`(`FE4FC231ED9543BF…`)는 "has no test switch" 로 PASS 한다(마커 0, `fail_activation` 0).
  - 스캔용 사본 폴더는 빌드 폴더 안에 만들었고 그 사본만 지웠다.
- 시험:
  - `host_standby_activation_fault_test` **17/17 ×3, exit 0**. 실제 `standby_handle` 을 seam 정의로 빌드했다. fault 파일은 저장소 scratch root 의 이 실행 폴더에 두었다.
    - 파일 없음 / 빈 파일 / 다른 fault 만: accepted (양성 대조)
    - armed: 거부 1회, 로그에 token·entry·gen, table 무변경
    - 같은 token: accepted
    - armed 중 Hello: 무영향
    - 새 token: 1회 더 거부 뒤 accepted
    - 잘못된 token: 무효
  - `host_standby_table_test` 56/56 ×3.
- 재빌드 해시(57fa793): `GNLinkStreamPinnedSelectTest.exe` **`D806E375019BC5EEC850443EFDA64CC35289F1A7A73CFFBE473BCF8391E12EAA`** (fault 문자열 2, power 옵션 4). 앞 절의 01F72D72… 는 이것으로 대체된다.
- AVD 시나리오 E13 (`ke_avd.py`, 미추적, **실행하지 않음**, ast 구문 확인만), sha256 `a120b238416ce8076d79122928974b4daaddf7a510e1f9fb9c9c45660c9d3e16`:
  - **양성 대조**(fault 없음): C 를 대기 → 탭 → `[standby_activate]` + `directory_connect_started … standby=1`, 실패 없음, Host 로그에 fault 없음.
  - **fault**: C 를 다시 대기 → 그 Host 의 fault 파일에 이번 token 을 쓴다 → 탭. 확인 항목:
    - Host 로그에 그 token 의 적용 1회
    - `[standby_activate_failed] … gen=0 … mine=true`
    - 그 뒤 `[directory_connect]` 와 `directory_connect_started … gen=N`(standby 아님)
    - viewer 도달(select)
  - 끝나면 fault 파일을 비운다.
  - fallback 뒤 preparedReturn/baseline 은 clipboard 미검증(결정 ①) 상태라, 이 시나리오에서 실 clipboard 는 건드리지 않는다.

## 문구 정정 (report_e_r3.md)
- :23 → "Activity 의 ReleaseAll 호출을 제거했다. 미사용 JNI export(`nativeStandbyReleaseAll`)와 Kotlin external 선언은 소스에 남아 있고, runner 소멸자도 ReleaseAll 을 호출한다."

## EVIDENCE 미결 (그대로)
- E7h, Activity 교체 실 UI, 실 clipboard(결정 ① = c), raw AUTO, ke_r21 peer_lost 원인 미확정.
- E13 과 guard 통합의 실제 동작은 SOURCE 뒤 AVD 실행에서 처음 확인한다. 그 전까지 AVD 실행은 보류한다.
