# AVD guard r3 + gate TEMP T — SOURCE

2026-10-28 · **NEEDS_CHANGES — 잔여 T1/J1**

고정: T **8a84198157837370a47d4c87f782eb44c755844b**, feat의 T merge **3a3c77f**, AVD **de2d60a35a5af1745a39bf221ce7110f43b6df2c**와 avd_guard_r3_snapshot. **기존 G1과 fixture의 G2 보수는 수용한다. 잔여는 T cleanup의 실제 junction 하위 진입과 새 job runner의 편입 실패 종료 확인이다.** 실제 AVD 실행은 job runner 잔여 보수까지 보류하며, T self-test는 해당 cleanup 보수 전 재실행하지 않는다. 두 범위를 합쳐 다른 승인된 구성 단위를 전량 보류하지 않는다.

## 고정 자료

- report_gate_temp_T.md SHA-256 acc42fccdbe05bf080cd76e779142db1f25896d855d82e39c0c7ab84a4f9af25 일치.
- report_avd_guard_r3.md SHA-256 0de32f5e1b0499d75de8cc751cde8d3d60a0ab0ba374c9c4994594807fd40956 일치.
- avd_guard_r3_snapshot/SHA256SUMS.txt의 **9파일을 전부 직접 대조**했다. ke_fixture a81328d9…·ke_slot d6444009…·orchestration 2534bcc7…·stub_harness eae99a9d… 및 나머지 r2와 같은 해시가 맞다.
- 소스는 고정 git object로 읽었다. 실제 Host/Viewer/AVD/adb·clipboard·운영 작업은 하지 않았다. 작업용의 root 15/15·self-test 53/53·orchestration 18/18·mutant는 보고 근거다. 아래 두 제한 probe만 직접 수행했다.

## 수용 — G1과 fixture G2

fixture는 readiness 존재와 별개로 HasExited를 먼저 거부한다. Dead-Child를 다음 Host 시작 전과 fixture_ready 게시 전에 적용해 node/기존 harness의 알려진 종료를 거부한다. 300 ms 대기를 생존 보장의 본질로 삼지 않고, 실제 상태 검사와 현재 run의 readiness를 함께 쓰는 방향을 수용한다.

fixture 전체 try/catch/finally 및 child별 독립 cleanup을 수용한다. 한 child stop 오류가 다른 child cleanup까지 건너뛰지 않는다. slot의 drive→anchor 사전 경로/타입 검사도 생성 앞으로 이동했다.

job runner의 **정상 편입 경로**는 새 kill-on-close job의 limit 재조회, suspended command 생성, job 편입과 membership 확인 뒤 resume다. fixture crash 뒤 residual 종료, job active=0 확인, 강제 종료/미확인 비성공 구분과 slot의 충분한 정리 예산을 수용한다. 정상 fixture cleanup과 바깥 job을 별도 방어로 둔 구조다. 다만 아래 J1은 새 코드의 실패 분기에 남아 있다.

기존 제품 9fec1f9의 Unknown establish 실패→현재 tap 1회 fallback SOURCE 수용은 유지한다. E13/E13b 실제 APK 행동과 실 clipboard·peer_lost 원인 등 EVIDENCE 미결은 그대로다.

## T1 — topdown=False walk는 junction 진입을 막지 못함

root 사전 검사, TEMP fallback 제거, 자기 새 run 생성은 수용한다. **cleanup의 “link를 따라가지 않음”은 아직 성립하지 않는다.** 현재는 `os.walk(run, topdown=False, followlinks=False)`가 하위로 먼저 내려간 뒤 `_is_link`를 검사한다. 이 Windows/Python 환경에서는 NTFS directory junction 안으로 이미 내려간다. junction 자체를 마지막에 남겨도 그 아래 일반 파일은 `_is_link(file)=false`이며, lexical prefix 역시 run 아래로 보여 os.remove 대상이 된다.

실제 junction을 사용해 고정 8a84198 cleanup을 제한 직접 probe했다.

- 모두 D repository의 자기 새 검토 폴더다. run/link junction의 target은 **같은 검토 폴더의 sibling**, 사용자/외부 경로가 아니다.
- `os.remove`와 `os.rmdir`를 **기록 대역으로 교체**했다. 삭제는 수행하지 않았다.
- Python 3.14.3, `_is_link(run/link)=true`임에도 cleanup이 다음 대상을 remove에 넘겼다.

```
lexical: .../run/link/sentinel.txt
resolved: .../sibling/sentinel.txt
actual deletions: 0
sentinel preserved: true
```

자료: `.claude/codex_ka_e_review/gate_T_8a84198/junction_probe.json`, 동결 gate/root test, junction_probe_base.txt 및 자기 junction/sentinel을 보존했다. **실제로 바깥 파일을 지운 사고 재현이 아니라, 현재 함수가 run 밖 삭제 대상을 선택함을 관찰한 안전 probe**다.

하위 directory로 **들어가기 전에** lstat/reparse를 검사하고, link/junction은 traversal 목록에서 제외해 남기고 실패로 보고한다. 명시적인 postorder 순회 또는 topdown에서 먼저 prune한 뒤 역순 디렉터리 회수 등으로 구현할 수 있다. leaf 검사나 followlinks=False 하나만으로 대체하지 않는다. cleanup root 자체의 타입/소유 경계도 확인한 뒤 시작한다.

현재 root_test의 내부 link 대조는 **일반 파일 하나를 link라고 대역 처리**하므로 directory junction의 진입을 검증하지 않는다. 소유 run의 junction → 같은 테스트 소유 sibling sentinel로 대조하고, 삭제 target 기록에서 sibling 선택 0·sentinel 보존·link 보존·left/비성공을 확인한다. 새 외부 경로 삭제 시험은 필요 없다. 정상 자기 fixture cleanup도 유지한다.

## J1 — 편입 실패의 종료 미확인을 “종료”로 출력

`e2e_job_run_tool.cpp`의 AssignProcessToJobObject/IsProcessInJob 실패 분기는 TerminateProcess 결과와 5초 Wait 결과를 버리고 “ended before it ran”을 출력한 뒤 2로 끝난다. 편입되지 못한 child는 **자기 job의 kill-on-close로도 회수할 수 없으므로** 이 분기에서 실제 종료 확인을 구분해야 한다.

고정 원문을 Win32 API 대역과 함께 빌드했다. CreateProcess는 **실제 child를 만들지 않는 대역**, Assign=false, Terminate=false, Wait=WAIT_TIMEOUT으로 주입한 직접 결과:

```
shim: NO child created
shim: Assign FAILED
shim: Terminate FAILED
shim: wait TIMED OUT
job_run: ... ended before it ran
rc=2
```

자료: `.claude/codex_ka_e_review/avd_guard_de2d/e2e_job_run_tool.cpp`, failure_shim.hpp, job_failure_probe.exe, failure_probe.log. 실제 suspended/잔여 process를 만든 시험이 아니다.

probe exe SHA-256: b70088dc2f4c0bb8de9c027c749e8ff6fdb60025d320d9efcb0f7cbbd80082e5.

필요한 보수는 작은 실패 처리다. **종료 시도와 signal 확인을 검사**하고, 종료 미확인은 ended_confirmed=0/INVALID로 정확히 보고한다. suspended이므로 “실행하지 않음”과 “프로세스 종료 확인”을 구분한다. 같은 소유 handle로 회수하고 진단에 그 상태를 남긴다. ResumeThread 실패도 확인해, 재개되지 않은 command를 정상 대기 상태로 넘기지 말고 같은 job의 종료 확인·비성공으로 연결한다. API 성공을 가정한 단정문만 고치고 확인을 생략하지 않는다.

원하면 이미 검수된 공용 child/job 실패 처리 규칙을 재사용할 수 있다. child 없는 API 대역으로 assign/membership 실패, terminate 실패/timeout, resume 실패의 진단·반환을 확인하면 된다. 정상/crash/residual 18개를 다시 설계하거나 전체 제품 빌드를 추가할 필요는 없다. 새 실패 대조의 실제 프로세스가 필요하다면 검증된 바깥 job 안 자기 stub만 사용한다.

## TEMP 과거 기록 정정 수용

앞의 **이 세션 1회**를 전체 횟수로 사용하지 않는다. 최신 보고는 해당 작업용 세션의 기본 TEMP self-test **8개 호출(10-26 4, 10-28 4)**이며, 57fa793 검증분 1회는 그 안에 포함된다. 프로젝트 전체 **43 호출/8 세션(9-29~10-28)** 집계는 작업용의 보존 기록 조사 결과로 인용한다. Codex가 사용자 영역 JSONL을 재집계한 것은 아니다.

호출 수·실제 self-test 실행 수·PASS check 수·중복·실제 전체 임시 경로/파일 목록/개별 삭제 결과는 구분한다. 43회 삭제 성공 또는 43개 폴더라고 바꾸지 않는다. 확인된 소스상 cleanup 경로와 실제 호출 기록은 보존하되 당시 TMP 환경 값이나 현재 파일 부재를 무영향 증거로 쓰지 않는다.

보고서에는 별도로 전후 `%TEMP%/gnlink-fixed-server-gate-*` 패턴 개수 조회 0→0이 적혀 있다. 따라서 최종 문구는 **TEMP 내용 읽기 자체가 전혀 없었다**고 하지 말고, 이 제한 조회 사실과 추가 정리/복구 없음·현재 값으로 과거 무영향 판정 불가를 구분한다. 추가 TEMP 탐색이나 과거 재현은 하지 않는다.

commit 메시지의 경로 escape 오타는 코드 경로 오류와 구분한다. 그 한 줄을 고치려고 기존 공유 commit을 amend/rebase하지 않는다. 다음 기존 기록에 코드 경로가 정확하다는 정정을 남기면 된다.

## 다음 단계

기존 작업용1의 한 보수 packet에 **T1 traversal + J1 종료 확인**과 대조를 연결한다. G1/fixture G2·제품 fallback의 이미 수용한 변경은 다시 작성하지 않는다. T의 잔여를 이유로 무관한 승인 구성 단위를 막지는 않지만, 현재 AVD는 사용해야 하는 job runner의 J1 보수 고정본까지 기다린다. 그 이후 T self-test와 AVD 실행 범위는 각각 닫고, 실제 AVD/clipboard EVIDENCE를 별도로 확인한다.

검토 문서·자기 snapshot/probe만 만들었고, 제품 편집·실제 Host/Viewer/AVD/adb·운영·설치·외부 삭제·commit/push는 없다. 사용자 현재 조치 없음.
