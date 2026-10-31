# test-injection-finding r8 — Codex a8b3407 N1·N2 보수 (r7 대체 아님, r7 위)

task=test-injection-finding / rev=r8 / kind=fix 결과 / 근거=codex_test_injection_finding.md 끝 a8b3407 판정

## 고정
- commit **b8dcc930ea1a2e99951ec7fbcd5fa339d5a04c3b** (ka-r3, 부모 a8b3407). tracked clean, `git diff --check` 정상.
- 11파일 (+506/−110):
  - 공용: `e2e_injection_guard.hpp`
  - 시험: `e2e_guard_runtime_test.cpp`, `host_fatal_exception_test.cpp`
  - pipe 하네스 4: migration, punch_reply, reboot_delegate, standby_admission
  - log-file 하네스 4: android_session_reboot, bwe_observe, control_deadline, two_dispatcher
- 제품 코드 0줄. 실 Host 실행 0.

## N1 — 완료 판정 = 생산자 종료 AND 확인된 EOF AND 최종 텍스트의 OFF 두 줄
- `e2e_read_to_end(h, keepGoing, append, &err)`: 결과를 넷으로 나눈다.
  - **Eof**: ReadFile TRUE + 0 byte, 또는 FALSE + `ERROR_BROKEN_PIPE`
  - **Error**: 그 밖의 실패. 코드를 보존하고, 실패 전에 받은 byte 도 보존한다(예: `ERROR_MORE_DATA`)
  - **Cut**: 소유자가 중단시킨 경우
  - join 은 EOF 의 증명이 아니다.
- `e2e_final_file(path)`: open 실패(없는 파일)와 도중 read 오류는 **미완료**이고, 그 이유를 문장으로 남긴다. `e2e_final_pipe(end, err, text)`: reader 의 결과가 Eof 일 때만 완료다.
- `E2eInjectionMonitor::Finalize(producerEnded, final)`: 최종 snapshot 을 다시 읽지 않고 **넘겨받아** `e2e_final_verdict` 로 판정한다. 하나라도 확인되지 않으면 `e2e_injection_run_invalid` 로 INVALID 다. 조건은 다음 넷이다.
  - 생산자 종료 확인
  - 완료(EOF)
  - `e2e_injection_state == 1` (input·power OFF 두 줄이 whole 로 있음)
  - 상충 없음
- **job 수명**:
  - monitor 는 job 을 빌려 쓴다. Finalize 와 소멸 시점에 `State::job = nullptr` 로 놓는다.
  - pipe 하네스 4개(구조체형)는 `CloseHandle(job)` 을 **Finalize 뒤로** 옮겼다.
  - log-file 하네스 4개는 원래 Finalize 뒤에 닫고 있었다.
  - abandoned launch 는 이미 닫혔을 수 있는 handle 을 건드리지 않는다. INVALID 로 처리하고 "NOT confirmed ended" 라고 말한다. 자식은 job 의 마지막 handle 이 닫힐 때 kill-on-close 로 끝나며, 시험이 그 소멸을 확인한다.
- 하네스 배선:
  - pipe 4: reader 가 `e2e_read_to_end` 의 결과와 오류 코드를 `readResult`/`readErr` 에 남기고, `Finalize(producerEnded, e2e_final_pipe(...))` 로 넘긴다.
  - log-file 4: `Finalize(hostEnded, e2e_final_file(hostLogPath))`

## N2 — fatal 의 startup-fault 판정
- `e2e_startup_fault_verdict(producerEnded, final, &inputSaid)`: 다음이 모두 확인돼야 통과한다.
  - 종료 확인, 완료
  - 상충 없음 (input 줄이 있다면 반드시 명시 OFF 이고, `injectionEnabled=1` 은 없어야 한다)
  - **power OFF 가 whole 로 있음**
  - input 줄이 없는 것은 허용 예외다(입력 경로 설정 전에 throw 하므로). 대신 줄이 있었는지 없었는지를 말한다.
- `--prelogin` worker 는 **삭제 전에** stdout(`e2e_final_file`)을 이 판정에 넣는다. worker 자신의 log 도 완료(EOF)와 상충 없음을 판정한다.
  - 거부되면 `fatal_invalid(L"prelogin", …)` 으로 시험 전체를 INVALID(exit 2)로 끝낸다. 이때 log 를 모두 보존하고, probe_trap 이후는 launch 하지 않는다.
  - worker 종료가 미확인이면 `gKeepLogs` 에 `prelogin` 을 넣어 "its child's end NOT confirmed" 가 정확히 출력되게 했다.
- 같은 결함 형태라서 함께 고쳤다:
  - std/winrt 의 `said_power_off` check(false) → 같은 startup-fault 판정(거부 시 INVALID)
  - none(정상 대조) → `e2e_final_verdict` (두 줄 필수)
  - 모든 case 의 최종 read: `final_output_or_invalid` (종료 미확인이나 미완료 read 면 INVALID)

## 직접 결과 (실행 0 Host: 자기 자신 sleeper + kill-on-close job, 고정 문자열 "host")
- `remote60_e2e_guard_runtime_test` **36/36 exit 0, RESULT PASS, 3회 반복**. 새로 추가한 항목:
  - 없는 log 파일(Codex 재현: 초기 OFF 1 → 최종 0 byte) → INVALID "could not be opened", 자식 종료 확인
  - EOF 까지 읽었지만 빈 파일(`EMPTY_COMPLETE=1 BYTES=0`) → INVALID "does not hold the initial OFF lines"
  - 도중 read 오류: message pipe, `PARTIAL end=2 err=234 bytes=8192`, OFF 줄은 읽은 부분 안에 있음 → INVALID "read error (error 234) after 8192 bytes"
  - 소유자가 끊은 reader(join, EOF 없음) → INVALID; 생산자 종료 미확인 → INVALID
  - 양성 대조: OFF 줄이 있는 파일을 EOF 까지 읽음 → PASS. 익명 pipe 의 broken-pipe EOF(`EOF_PIPE end=1`) → PASS
  - helper 직접 확인: write-only handle → Error(코드≠0), 없는 파일 → 미완료·0 byte·이유 있음, 중단 요청 → Cut
  - startup-fault 수용 2건(input 없음 / 둘 다 있음), 거부 9건: power 줄 없음, input 줄만 있음, power ON, OFF→ON, 다른 mode, `injectionEnabled=1`, 도중 read 오류, 없는 파일, 종료 미확인
  - abandoned launch → INVALID, "NOT confirmed ended", sleeper 소멸 확인
- 부정 대조 mutant(guard 헤더, 커밋 안 함; 복원 후 src 해시 73646FFE… 일치, 재빌드 36/36):
  - **a**: 모든 read 실패를 EOF 로 취급(r7 동작) → 2 FAIL (부분 read 가 PASS 로 통과, write-only 가 Eof)
  - **b**: OFF 줄 요구 제거 → 1 FAIL (빈 완료 파일 통과)
  - **c**: open 실패를 빈 완료로 취급 → 3 FAIL. 없는 파일 case 는 b 의 OFF 줄 판정이 다른 문구로 여전히 INVALID 로 막았다(겹친 방어).
  - **d**: startup-fault 의 power 요구 제거 → 2 FAIL
- 회귀: injection_guard 20/20, power_policy_record 27/27. guard 를 쓰는 17개 타깃 모두 빌드 오류 0.

## 미검증 / 범위
- fatal 과 하네스 8개는 **실 Host 시험이라 실행하지 않았다**(slot·승인 대기). N2 배선과 pipe/log 배선은 컴파일과 소스 대조만 했고, 판정 함수는 위 runtime 시험으로 직접 실행했다.
- prelogin worker 는 job 밖에서 단일 프로세스로 실행된다. "종료"는 그 프로세스 하나의 종료이고, 자손 writer 가 없다는 것은 시험 경로상(첫 단계에서 throw) 전제다.
- `e2e_injection_run_invalid` 의 머리 문구("input / power state contradicted during the run")는 미완료 read 에도 같이 쓰인다. 괄호 안 이유가 "final judgement: … not known complete" 로 구분해 준다.
