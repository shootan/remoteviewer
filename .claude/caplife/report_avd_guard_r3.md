# keepalive-r2 e — AVD guard r3 (Codex codex_avd_guard_src_r2 G1·G2), 실행 0 (Host·AVD·adb 없음)

## 고정
- **tracked**: feat-apk-0233 **de2d60a35a5af1745a39bf221ce7110f43b6df2c** (HEAD). 이력:
  - 9fec1f9
  - → 3a3c77f (merge ka-r3 8a84198, T)
  - → de2d60a (`e2e_job_run_tool.cpp` + CMake)
  - `e2e_job_run.exe` sha256 `016191644ecbc89819cfa93e2b82b7f0308d8ffbe22afaae9873f4411d08fc1b`.
- **미추적 스크립트 불변 snapshot**: `.claude/caplife/avd_guard_r3_snapshot/` (읽기 전용 + `SHA256SUMS.txt`)

| 파일 | SHA-256 |
|---|---|
| ke_fixture.ps1 | a81328d98a6cb31871f314c0a218f84544926c387aff715ae1928fd28e395c23 |
| ke_slot.ps1 | d6444009e9ddbc2e913b32a336eeac643f0226ea5c95168481ebe9308651ecec |
| ke_avd.py | 669248e34bf35a44166d00119a754f7c6012e15a8da2180d787ac243f99eb835 (r2 와 같음) |
| ka_avd.py / ka_delay_proxy.js | 변경 없음 |
| ke_orchestration_test.ps1 | 2534bcc71d4650dd14ac0125e506c8175494dc533106981e3cfdf752190dedba |
| stubs/stub_harness.py | eae99a9d6bc722340129ddd8516f29e59ae79c8925c9a4596f2021fef94754be |
| stubs/stub_node.py / stub_avd.py | r2 와 같음 |

## G1 — 이미 죽은 child 를 ready 로 받지 않음
- harness 를 판정할 때 **`HasExited` 가 먼저**다. readiness 가 유효해도 종료된 harness 는 거부한다("after its readiness: not ready"). 판정 전에 300 ms 를 둔다.
- `Dead-Child`: 지금까지 띄운 child(node 와 준비된 harness 모두) 중 종료된 것이 있으면 실패로 본다. 다음 Host 를 띄우기 전과 `fixture_ready` 를 쓰기 전에 검사한다. 하나라도 죽었으면 다음 launch 0, AVD 0, exit 2 다.

## G2 — 예외·강제 종료에서도 하위 회수
- fixture 본문 전체를 `try/catch/finally` 로 감쌌다. `finally` 에서 `Stop-All` 을 1회 실행한다. 정상 정지 뒤에는 no-op 이다.
- `Stop-All` 은 child 마다 독립된 try/catch 를 쓴다. 한 child 의 오류가 나머지 회수를 건너뛰게 하지 않는다. 오류가 난 child 는 자기 handle 로 Kill 하고 결과를 기록한다.
- **fixture 는 `e2e_job_run` 의 kill-on-close job 안에서 실행된다**:
  - 생성 SUSPENDED → 배정 → `IsProcessInJob` 확인 → resume 순서다. 그래서 fixture 는 job 밖에 아무것도 띄울 수 없다.
  - stop 뒤 grace 가 지나면 job 전체를 종료한다(forced).
  - fixture 가 끝났는데 job 에 남은 것(descendants)은 종료한다(residual).
  - job 의 active 수가 0 이 되면 종료 확인이다.
  - runner 자체가 kill 되면 job 이 닫히면서 kill-on-close 가 나머지를 끝낸다. 이때 slot 은 "NOT confirmed" 로 기록하고 비성공으로 본다.
- slot 쪽:
  - grace 는 `3·(StopWaitSec+15)+60` 초다(fixture 의 최악 정지 시간). runner 대기는 grace+120 s.
  - runner 의 job_run 줄을 `slot.txt` 에 남긴다. fxExit≠0 이면 rc 3 이다.
  - **드라이브부터 anchor 까지 모든 구성요소**에 대해 link 와 비폴더를 검사한 뒤에야 anchor 와 run 폴더를 만든다.

## 대조 (stub 만; Host·node 서버·창·adb·에뮬레이터 0)
`ke_orchestration_test.ps1` **18/18, exit 0**, 모든 경우 뒤 잔여 0. 로그 sha256 `74c23fd7eecb8d37e7df31e57bbb6ed046b47d4b6b94ab77efbf6f6157225ce7`. 기존 11개에 7개를 더했다.

| 경우 | 결과 |
|---|---|
| G1 b 가 유효 readiness 직후 종료 | c 미시작, AVD 0, rc 2 |
| G1 a 가 c 준비 중 종료 (c 느림) | fixture_ready 없음, AVD 0, rc 2 |
| G1 c 가 readiness 직후 종료 | AVD 0, rc 2 |
| G2 b launch throw (경로 없음) | AVD 0, rc 2. fixture_result 에 a exit 0, node ended |
| G2 fixture 가 readiness 직후 crash (자기 정리 없음) | `job_run: child_exit=9 residual=4 forced=0 ended_confirmed=1`, rc 3 |
| G2 nostop 2개 (a, b) | 둘 다 자기 handle 로 kill, c exit 0, rc 3 |
| G2 a 정지 중 오류 1건 | a 는 오류 뒤 kill. b, c, node 는 정상 종료. rc 3 |

**보수 전 등가 mutant**:
- 실행 조건: 바깥 `e2e_job_run` 안에서 실행해, mutant 가 남긴 프로세스도 job 이 회수했다. 다른 파일은 good 사본을 쓰고, 한 번에 하나만 적용했다. 복원 후 해시가 일치한다.
- 결과:
  - **g1** (죽은 harness 를 ready 로 받음 + Dead-Child 없음 = 9fec1f9): **3 FAIL** — 세 G1 경우 모두 AVD 가 시작되고 c 가 시작됐다.
  - **g2c** (전체 cleanup 없음 = 9fec1f9): **1 FAIL** — launch throw 에서 a·node 정리 기록이 없다. 잔여는 job 이 회수했다(심층 방어).
  - **g2j** (job 없이 fixture 직접 실행 = 9fec1f9): **3 FAIL** — crash 뒤 **잔여 4개**가 남았다. 이 잔여가 이후 경우들에도 남아 있어 그 경우들까지 FAIL 했다. 바깥 job 이 `residual=4` 를 회수하고 종료를 확인했다(`ended_confirmed=1`).

## 미검증 / 그대로
- drive→anchor link 검사는 실제 link 를 만들어 시험하지 않았다. 소스와 검사 순서만 확인했다.
- 실 AVD 실행은 SOURCE 판정 전까지 보류한다. 실 clipboard (c), old/E7f/E8/E12 NOT RUN, E7h·Activity 교체·raw AUTO, ke_r21 peer_lost 는 미결이다.
- 시험 산출 폴더(저장소 안 `feat-apk-0233\.claude\e2e\runs\orch-*`)는 남겨 두었다.
