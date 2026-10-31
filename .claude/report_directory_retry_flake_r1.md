# report directory-retry-flake r1 — directory_retry 간헐 FAIL 제한 조사 (작업용2 → 검증용 remote#wwml7h91, 2026-10-22)

task t-azc166pv / rev r1 / kind=result. 지시 `task_directory_retry_flake_r1.md`, 근거 `codex_hello_delay_fix_r1.md`(directory_retry 절).
조사만 했다. 제품·시험은 수정하지 않았다.

## 결론
- **판정: 시험 설계 결함(고정 시점 단일 샘플이 상태가 바뀌는 경계에 놓임) + 제품 상태 문구 계약의 불일치.** 제품 스레드 경합은 아니다.
- **hello 변경과의 관계: 없음.** 근거 수준은 소스 + 실측이다.
  - 관련 파일은 5063cd9..83c3ac5 diff 에 없다(`directory_client.cpp`, `directory_retry_test.cpp` 불변).
  - cached-silent 경로는 heartbeat 가 성공해야 `Punch()` 를 부른다(`directory_client.cpp:1606-1617`). 그래서 PunchAny/Hello 에 닿지 않는다.
  - 기준판과 후보의 FAIL 비율이 같다(아래).
- **분포** (같은 PC, 번갈아 각 10회, 성공할 때까지 반복하지 않음):

| | 기준 5063cd9 | 후보 83c3ac5 |
|---|---|---|
| cached-silent "reason is about the server" | **FAIL 5/10** | **FAIL 6/10** |
| 같은 항목, 내 부하 없는 회차만(01–03 + 후보 탐침 00) | 1/3 | 2/4 |
| 다른 간헐 `[inactive]` 2항목 | 3/10 (05, 09, 10) | 1/10 (04) |
| 다른 간헐 "a silent host loses to the relay" | 1/10 (10) | 0/10 |

  - 검증용의 기준 3/3 PASS 와 후보 1/4 FAIL 은 이 분포(약 50%)와 맞는다. 50% 에서 3회 연속 PASS 확률은 12.5% 다.
  - 전부 rc 와 마지막 요약 줄로 읽었다. 133 checks 구성은 같다.

## 1. 기전 (file:line, 소스는 83c3ac5 = 5063cd9 과 같음)
- **시험 샘플 시점**: `RunHost`(`directory_retry_test.cpp:115-184`).
  - cached-silent 는 `RunHost(dir, "cached-silent", 99, 99, true)`(`:1274`) 다. 목표 heartbeat/register 99 회는 도달할 수 없으므로 대기 루프 150×`sleep_for(100ms)`(`:168-174`)를 끝까지 돈다.
  - 그 뒤 `sleep_for(300ms)`(`:175`) → `agent.StatusLine()` **한 번**(`:177`) → `agent.Stop()`(`:178`).
  - 샘플 시점은 에이전트의 어떤 사건에도 묶여 있지 않다. 루프 반복 시간(실측 회당 ≈109–115 ms, timer resolution 1.0 ms 에서도)에만 묶여 있다.
- **HTTP 의 "추측" observe 주소는 처음부터 ready 다**: `HostAgent::Start`(`directory_client.cpp:459-472`).
  - 지정 포트가 없고 http 면 `observe_port_for({}, httpPort, false)` = httpPort+1(`:215-220`)로 조준하고 `observeAddrReady_ = true` 로 둔다.
  - 이 시험의 가짜 디렉터리 observe 포트는 OS 가 고른 다른 포트다(`directory_retry_test.cpp:1238-1242`의 의도). 그래서 그 추측 주소로는 관측이 성공하지 않는다.
- **health 질의와 cooldown**: `FetchObserveEndpointFromHealth`(`directory_client.cpp:1110-1141`).
  - 광고가 없으면 상태를 **"this directory does not say where to send address observations"**(`:1131-1133`)로 쓴다.
  - 그리고 `observeFetchCooldown_ = attempts*2`(`:1135`)를 둔다. 이후 cooldown 동안은 상태를 쓰지 않고 false 를 돌려준다(`:1120-1123`).
  - 결과적으로 health 는 1·4·… 번째 cycle 에만 묻는다. 로그에서 health `ms>0` 이 n=1·n=4 였다. `/healthz` 2회로 시험의 `<=6` 조건과 맞는다.
- **매 cycle 끝에 "timed out" 으로 덮어씀**: `HostAgent::Loop`(`:1591-1602`).
  - health 가 실패해도 `RefreshObservedAddress()`(`:1179-1194`, 6회×250 ms = 약 1.5 s)는 ready 인 추측 주소로 관측을 시도하고 실패한다.
  - 그러면 `if (observeAddrReady_) SetStatus("address observation timed out")`(`:1602`)가 실행된다.
  - 주석(`:1599-1601`)은 "구체 이유를 timeout 으로 덮지 않는다" 이다. 하지만 이 보호는 `observeAddrReady_ == false`(https·포트 0) 에서만 동작한다. **http 추측 주소에서는 구체 이유가 매 cycle 덮인다.**
- **그래서 상태는 바뀌기를 반복한다**. heartbeatSeconds=5(`directory_retry_test.cpp:150`), cycle 은 약 5.09 s 다.
  - "observations" 문구는 health 를 묻는 cycle(1, 4) 의 **health 직후 ~ 그 cycle 의 observe 끝(약 1.5 s)** 동안만 남는다.
  - 그 밖의 시간에는 "address observation timed out" 이다.
  - cycle 4 의 창은 시작 후 약 15.2–15.3 s ~ 16.75–16.8 s 다.
- **샘플이 정확히 그 창의 끝에 걸린다**: 아래 실측에서 Stop(≈샘플 직후)은 시작 후 16.7–17.0 s 였다(부하 없는 회차).

## 2. 실측 시각선 (cached-silent; `parse.py`, 로그의 cycle workMs/sleptMs 로 계산)
- "stop into c4" = cycle 4 시작부터 Stop 까지의 시간이다. Stop 이 sleep 중에 오면 work + slept, work 끝에 오면 work 다.
- "창 끝" = cycle 4 의 observe 끝(= c4 work) 이다.

| 회차 | 판정 | c1..c3 ms | c4 work(창 끝) | c4 slept | stop into c4 | Stop 시각(시작 후) |
|---|---|---|---|---|---|---|
| 00 후보(탐침) | FAIL | 15243 | 1531 | 206 | 1737 | 16.98 s |
| 01 기준 | FAIL | 15247 | 1557 | 202 | 1759 | 17.01 s |
| 01 후보 | FAIL | 15274 | 1529 | 202 | 1731 | 17.01 s |
| 02 기준 | PASS | 15238 | 1538 | 0 | 1538 | 16.78 s |
| 02 후보 | PASS | 15194 | 1564 | 0 | 1564 | 16.76 s |
| 03 기준 | PASS | 15207 | 1580 | 0 | 1580 | 16.79 s |
| 03 후보 | PASS | 15194 | 1561 | 0 | 1561 | 16.76 s |
| 04–10 | (부하 겹침) | 15164–16007 | 1518–1642 | 0–2115 | — | 16.71–19.76 s |

- 09 후보는 cycle 5 까지 갔다(21.7 s).
- **PASS 는 모두 Stop 이 observe 끝 직전·직후(sleptMs=0)**, FAIL 은 모두 sleep 에 들어간 뒤(≥201 ms) 였다.
  - 즉 샘플이 창 끝(≈16.75 s)보다 조금 앞이면 PASS, 조금 뒤면 FAIL 이다.
  - 검증용 회차(fail `sleptMs=201`, pass `sleptMs=0`)와 같은 모양이다.
- timer resolution 은 전 회차 1.0000 ms 였다(NtQueryTimerResolution). coarse 15.6 ms 가 원인이라는 가설은 이 PC 에서는 해당하지 않는다.
- **부하 기록**:
  - 04–10 회차(12:47 이후 종료)는 내가 실행한 integ-0162 r2 merge·configure·전체 빌드·LK/DW 사슬과 겹쳤다. 04 는 회당 4 min 49 s 로, 평소 3 min 47 s 보다 길었다.
  - 이 회차들의 FAIL 은 부하로 샘플이 더 늦어진 쪽으로 기울었을 수 있다. 그래서 위 "부하 없는 회차" 를 따로 적었다.
  - 다른 작업자의 부하는 보지 못했다.
- 같은 기전의 다른 항목 `[inactive]`:
  - `RunHost(..., "inactive", 3, 1, ...)`(`:1095`)는 heartbeat 3회를 기다린다. 그런데 기대는 `<= 2`(`:1098-1099`) 이고, 그 다음 항목은 status 가 "online" 으로 시작하기를 기대한다(`:1100-1101`).
  - 따라서 샘플은 cycle 4 heartbeat(≈15.6 s) 와 cycle 5 heartbeat(≈20.7 s) 사이여야 한다.
  - 대기가 길어지면(부하, 05 기준: Stop 이 cycle 5 heartbeat 뒤) `<=2` 가 FAIL 한다.
  - 대기가 짧으면(09·10 기준) 아직 "token rejected" 라 "online" 항목이 FAIL 한다.
  - 같은 종류의 시험 설계 결함이다. 이번 범위 밖이라 시각선만 확인했다(05 기준 Stop = cycle 5 + 416 ms).

## 3. 시점 (모두 hello 이전)
| 시점 | 커밋 | 날짜 |
|---|---|---|
| cached-silent 시험과 `:1602` 조건과 health cooldown | 422196b | 2026-09-10 |
| RunHost 150-루프·heartbeatSeconds=5 | f1cd2fb | 2026-09-09 |
| kObserveAttempts=6·kObserveWaitMs=250 | 27cd2f3 | 2026-07-29 |

## 4. 최소 수정안 (제안만, 결정은 Codex)
- **A (제품 계약을 주석대로)**: `Loop`(`:1602`)에서 이번 cycle 의 health 가 "광고 없음" 이고 조준이 추측(httpPort+1)뿐이면, timeout 으로 덮지 않고 서버 쪽 이유를 유지한다.
  - 그러면 상태가 바뀌기를 반복하지 않고, 시험의 고정 샘플도 결정적이 된다.
  - 다만 실 http 디렉터리가 +1 에서 실제로 듣지 않는 경우의 사용자 문구가 바뀐다. 이것은 상태 계약 변경이므로 별도 근거로 올려야 한다.
- **B (시험만)**: 고정 대기 뒤 단일 샘플 대신 **명시적 단계에서 관찰**한다.
  - 예: 시험이 cycle 로그를 읽을 수 없으니, `/healthz` 두 번째 요청 수신 직후(가짜 디렉터리 Count 가 2 가 되는 순간)부터 그 cycle 의 observe 가 끝나기 전에 상태를 읽는다.
  - 또는 "health 를 물은 cycle 동안에는 서버 이유" 를 계약으로 정하고 그 구간을 기다린다.
  - **문구를 느슨하게 하거나("observations" 또는 "timed out") 성공할 때까지 반복하는 방식은 하지 않는다.**
- `[inactive]` 도 같은 방식으로 한다: 목표 heartbeat 수(3)와 기대(<=2)가 서로 어긋난다. 고정 시간 대신 "cycle 4 heartbeat 직후" 처럼 관찰 단계를 명시한다.

## 산출물 · 정리
- exe(검증용 빌드 사본):
  - 기준 `cfe0ee14…`(= `verify-hello-base/build-vs`), 후보 `806a7dda…`(= `verify-hello/build-vs`).
  - 실행은 `C:/work/gnlink-wt/integ-0162/.claude/test-tmp/dirretry/{base,cand}` 에서 했다. 시험은 exe 옆에 fixture json 을 쓰므로, 검증용 빌드 트리에는 쓰지 않았다.
- 로그 `…/dirretry/out/*.out`, 파서 `parse.py`, timer 질의 `timer_res.ps1`.
- 삭제한 것 없음(repo root 밖 포함). 측정 슬롯 미사용(loopback·논리만).
- 사용자 행동: 없음.
