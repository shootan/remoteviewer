# directory-retry R2 재이식 SOURCE r1

2026-10-28 / Codex remote#tt7fcvky → 검증용 remote#7fsfy22z / task dirretry-rebase.

**전체 판정 NEEDS_CHANGES — 제품 진단 SOURCE는 수용하며 시험 T1(단계 기록과 live 계수 혼합)·T2(저장소 밖 cache 삭제 경로)를 보수한다.** 재이식 자체의 제품 충돌/KA·NP 훼손은 발견하지 않았다. 전체 SOURCE/독립 EVIDENCE/게시 완료로 닫지 않고 같은 task에서 시험 경계만 보완한다.

## 고정 대상/제품 판정

- `fix/directory-retry-r2` **af9a6af884849885d342ced0df91023112225604**, 기준 **e16ad0d**. 전체7파일 diff와 실제 HostAgent 시작·health·observe 적용·루프/시험 호출을 검토했다. HEAD 일치·tracked clean, whitespace rc0.
- 원83cbd78..0d4c27d와 새e16ad0d..af9a6af의 apps 추가/삭제 행을 직접 비교: **487행 양쪽 동일**. 이식 후 추가 실행 변경이 없고 history 병합 기록만 다르다는 보고와 부합한다.
- `ObserveAim`과 `ObserveHealth`/오류의 명시 상태로 **HTTP 광고 없음+legacy 주소 실제 timeout**의 두 사실을 보존한다. health 조회 자체 실패와 광고 없음을 구분하며 cooldown에서는 마지막 이유가 유지된다. pin/광고의 timeout은 기존 의미, HTTPS 미조준은 이유를 유지하고 임의 port를 만들지 않는다.
- 주소 선택/observeAddrReady·시도 횟수/간격·cooldown·heartbeat·토큰·재등록 규칙을 바꾸지 않은 제한 진단 보수다. KA hostProof 파싱/목적지별 Punch 송신·standby 파일 및 NP 경합/relayEarly는 이 diff에서 변경하지 않는다. 이번 수용이 별도 KA 미결을 승인하는 것은 아니다.
- hook/변이는 CMake PRIVATE 시험 target에만 정의된다. production 상태식 복제 대신 실제 HostAgent의 작업 완료 지점에서 cycle 기록을 남기는 방향도 맞다. 다만 아래처럼 수동 관찰만으로 시험 전체가 결정적이 되지는 않는다.

## T1 — 고정된 cycle과 계속 변하는 서버 횟수를 혼합함

`WaitForCycleEnd(n)`은 cycle n의 **과거 record**를 정확히 돌려준다. 반면 inactive의 heartbeat/register 검사와 cached-silent의 health/송신 횟수는 기다림 뒤의 **live `dir.Count`/LegacySends**다. agent는 다음 cycle로 계속 진행하므로 관찰자 thread가 늦어지면 같은 stage가 아니다. cached-silent의 cycle2 관찰 후 `/healthz` script를 교체하는 순서도 다음 조회 cycle4보다 먼저 실행될 시간창에 의존한다.

Codex가 제품/후보 파일을 바꾸지 않고 검토 전용 probe를 빌드·실행했다.

- `.claude/codex_dirretry_review/stage_observer_probe.cpp`: 후보 `directory_retry_test.cpp`의 원 main만 rename해 include하고 **실 StagedHost/FakeDirectory/HostAgent**를 사용한다. 제품 객체는 후보의 Release 빌드 객체를 링크했다.
- inactive와 같은401→200 응답을 주고 **관찰자만17초 지연**, background agent/서버는 정상 진행시킨 뒤 `Wait(1)`을 호출했다.
- 결과:

```
REVIEW got_cycle1=1 cycle1_heartbeat_ok=0 live_heartbeat_count=2 live_register_count=1 candidate_stage_assertion=0
REVIEW mixed_cycle_snapshot_and_live_count_reproduced=1
```

cycle1은 실제401, cycle2·3은 skip, cycle4는 같은token으로 정상200이었다. **제품이 계약대로 동작해도 새 시험의 cycle1 count 조건은 실패**한다. 새 상태식/네트워크 정책 결함이 아니라 시험 B의 결정성 미완이다. probe exit0은 이 검토 반례를 재현했다는 의미이며 제품 PASS가 아니다. 파일/실행물은 보존했다.

보수: stage의 계수를 해당 cycle record와 함께 snapshot으로 묶거나 시험 전용 **bounded barrier**로 다음 cycle 진행을 통제한다. fixture 응답 변경도 그 barrier 아래에서 다음 health cycle 전에 설치되게 한다. 대기 실패는 loud-fail로 남기고, 실제 생산 경계에 test-only로 연결한다. sleep 시간/허용 count를 늘리거나 OR 조건으로 느슨하게 하지 않는다.

제한 검사: record가 준비된 뒤 관찰자 처리를 일부러 지연해도 inactive의 stage1/3/4 횟수·cached-silent의 cooldown/회복 결과가 같아야 한다. 다음 cycle 진입과 script 변경의 순서를 의도적으로 뒤집으려는 반례도 검사한다. 기존 timeout_only/no_legacy_attempt 부정 대조는 유지한다. unrelated suite 전체를 전량 반복할 필요는 없다.

## T2 — 독립 실행 전에 cache 생성/정리 경계를 저장소 안으로

시험의 `exe_directory()`와 RunHost/새 StagedHost/PunchHarness는 cache를 **실행 파일 옆**에 두고 `DeleteFileA`로 정리한다. 새 StagedHost의 생성/종료는 `directory_retry_test.cpp:228/251`, 기존 RunHost·PunchHarness에도 같은 방식이 있다.

보고된 exe 위치는 `C:/work/gnlink-wt/dirretry-r2-build/...`로, 이 세션의 저장소 root **D:/remote/remote 밖**이다. 현재 시험을 그 위치에서 실행하면 저장소 밖 cache 삭제 호출에 도달한다. test cache/자기 build라는 이유만으로 최상위 삭제 제한의 예외가 되지 않는다. report의 “삭제한 것 없음”과도 구분해 실행 경로를 기록해야 한다. **실제 삭제 성공·기존 동명 파일 여부까지 Codex가 확인한 것은 아니다.** 새 바깥 cleanup을 하거나 사용자에게 삭제 허가를 다시 묻지 않는다.

안전 보수는 다음 중 범위가 작은 방식으로 한다.

- 시험 cache root를 고정 검증된 `D:/remote/remote/.claude/test-tmp/<고유 run>`으로 명시하고 모든 RunHost/StagedHost/PunchHarness 경로에 전달한다. 정리 전 실제 경로/소유·containment를 검증한다. exe 위치에서 root를 추정하지 않는다.
- 또는 고정 exe의 hash를 유지한 채 저장소 안의 고유 실행 디렉터리에 필요한 시험물을 보존해 실행하는 runner로 경계를 강제한다. 실행 위치/CWD만 바꿔서는 `exe_directory()`의 위치가 바뀌지 않음에 주의한다. 바깥 원본/자료는 보존한다.

검증용은 선택한 방식의 실제 root와 자기 생성 자료를 확인한 뒤 독립 실행한다. **Codex의 위 probe는 exe/cache가 모두 D:/remote/remote/.claude/codex_dirretry_review 안에 놓여**, 이 fixture의 cache 정리도 저장소 안에서만 실행됐다. 사용자 설치본/서비스를 조작하지 않았다.

## 보고 결과/잔여의 해석

154/0×3, timeout_only1 FAIL·no_legacy_attempt9 FAIL,845 suite·NP26/26 및 출하 target 컴파일은 **작업용 보고 귀속**이다. Codex는 전체 suite/제품 출하 target을 새로 빌드·실행하지 않았고, 위 제한 probe의 실제 로그와 source/Git을 직접 확인했다.

run2의 adjacent UDP port 확보 실패는 `StartLegacyAdjacent`의32회 setup 시도 후 returnfalse·test의 loud-fail 경로와 부합한다. **해당 동작은 미실행/판정 불가**로 남기는 해석이 맞다. 다른 실행의 PASS로 소급해 닫거나, 다른node/설치본을 점유 원인으로 확정하지 않는다. 근거 없이 사용자/다른 작업의 프로세스를 종료하지 않는다. 독립 실행에서는 실제 확보 성공 또는 실패 오류/port 근거를 남긴다.

원 보고의 전 코어 부하 중 cached-silent/inactive 통과는 작업용 귀속이며, 별도 `silent host loses to relay`의 부하 실패·inactive-stays의 기존 시점 의존 가능성은 이 변경의 해결로 합치지 않는다. NP가 경합을 바꾼 점은 고정 통합 회귀에서 구분한다. 이번 T1은 그 별건과 무관하게 새 단계 시험에서 직접 재현한 문제다.

검증용은 **T1+T2를 하나의 기존 task 지시서**에 취합해 작업용2에게 전달한다. 제품 진단을 다시 설계하거나 KA/NP writer 파일에 중복 수정을 만들지 않는다. 안전 시험 경계/단계 보수→관련 diff 확인→고정 후보 독립 실행으로 연결한다. 기록/계획·Git MCP commit은 같은 구현 묶음에 취합한다.

제품 source 수정·제품 빌드·실 Host/사용자 앱·서비스/운영 서버 조작·저장소 밖 삭제·commit/push를 Codex가 하지 않았다. **검토 전용 C++ probe 빌드/loopback 실행은 위와 같이 수행**했다. 미추적 판정서/프로브 외 관련 공개 기록은 검증용 창구에서 처리한다. 사용자 현재 조치 없음.
