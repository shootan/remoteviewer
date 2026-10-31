# report directory-retry-fix r1 — 관찰 실패 진단 계약 + 완료된 cycle 로 읽는 시험 (작업용2 → 검증용 remote#wwml7h91, 2026-10-22)

task t-iw0926r5 / rev r1 / kind=result. 지시 `task_directory_retry_fix_r1.md`, 구속 계약 `codex_directory_retry_flake_r1.md`.
**판정: A·B 구현과 자체 검증 완료. 차기 유지보수 후보다.**
- 0.2.162 에는 backport 하지 않는다. 버전 지정도 없다.
- 버전·서명·게시·push 는 없다.

## 고정 후보
- worktree `C:/work/gnlink-wt/fix-directory-retry`, 브랜치 **`fix/directory-retry`** = main 83cbd78 + 2커밋:
  - d6bb951 (WIP): rel-apk-0231 을 하느라 이 단계에서 멈췄다.
  - **0d4c27d** (최종).
- 변경 7파일(+476/−18).
  - 제품: `directory_client.{cpp,hpp}`, `directory_observe.hpp`.
  - 시험: `directory_retry_test.cpp`, `directory_fake_server.hpp`(httpPort() 접근자), CMake(시험 define·변이 타깃).
  - 문서: history.
- 빌드 `C:/work/gnlink-wt/fix-directory-retry-build`(Release) 시험 exe SHA256:
  - retry_test `5fb58eb9db42ab54…`
  - mutant_timeout_only `340a11fe3a7d359c…`
  - mutant_no_legacy_attempt `cc89a91d706f7050…`
- 출하 타깃 GNLinkStream·GNLinkViewer·GNLinkHost 도 이 소스로 빌드했다(rc 0). hook 이 없는 제품 경로가 컴파일된다. `directory_client.cpp` 에서 나온 경고는 0 이다.

## A — 제품 진단 (계약 대응)
- **명시 상태**를 두 가지 둔다. bool 이나 문자열에서 추측하지 않는다.
  - `ObserveAim` ∈ {NotAimed, Pinned, Advertised, LegacyDefault}.
    - 조준하는 모든 곳에서 설정한다: `Start`(pin 이면 Pinned, http 추측이면 LegacyDefault), `ApplyObserveEndpoint` 성공(pin › 광고 › legacy), 실패(NotAimed).
  - `ObserveHealth` ∈ {NotAsked, Advertised, NoAdvertisement, Unreachable} + 오류 문자열.
    - health 가 실제로 답한 곳에서만 설정한다. **cooldown 에서는 건드리지 않아 마지막 값이 유지된다.**
- 조준된 socket 의 관찰 실패는 순수 함수 `observe_failure_status(aim, health, error, port)` 로 말한다:

| 경우 | 문구 |
|---|---|
| **HTTP·광고 없음·legacy default 무응답** | "this directory does not say where to send address observations; the observation via the legacy default (port N) timed out" — **두 사실 모두** |
| HTTP·health 조회 실패·legacy 무응답 | "could not ask the directory where observations go (<오류>); the observation via the legacy default (port N) timed out" |
| **명시 pin / 유효 광고 무응답** | 기존 그대로 "address observation timed out" |
| **HTTPS 주소 없음**(NotAimed) | 호출하지 않는다. 미조준 이유(:1153 또는 health 오류)를 그대로 두고, 임의 포트를 만들지도 보내지도 않는다 |
| **legacy 실제 성공** | 관찰 OK → heartbeat → "online" (기존 경로) |

- **바꾸지 않은 것**:
  - `observe_port_for`, `observeAddrReady_` 를 쓰는 모든 곳(시작·적용 의미).
  - kObserveAttempts=6 / kObserveWaitMs=250, health cooldown 계산(attempts×2, 최대 6).
  - `Heartbeat`·`EnsureRegistered`·토큰/인증/재등록, 연결·punch 정책.
  - wire/API/서버 설정.
  - 새로 추가된 것은 상태 값 대입과 문구 조합뿐이다.

## B — 시험
- **test-only hook**(`REMOTE60_DIRECTORY_TEST_HOOKS`, `remote60_directory_retry_test`·변이 타깃에만 정의):
  - 생산 `Run()` 루프의 **작업 뒤·sleep 앞** 경계에서 cycle 기록을 남긴다. 기록 내용: n, inactive skip, health 를 실제로 물었는지, health/aim, observe/heartbeat 결과, 그 순간 StatusLine 과 같은 문구.
  - `WaitForCycleEnd(n, timeout)` 은 정확히 그 cycle 의 기록을 bounded 로 기다린다. 미도달은 FAIL 이다.
  - 생산 규칙을 복제한 stub 은 없다. 실제 HostAgent 와 실제 가짜 디렉터리를 쓴다.
- **고정 15 s 단일 샘플을 제거**한 두 case:
  - **cached-silent**:
    1. cycle 1: health 1회 질의 → NoAdvertisement. aim LegacyDefault, **httpPort+1 로 실제 송신(legacySends ≥ 1)**, 관찰 실패. heartbeat 0, 게시 없음, 문구는 **두 사실**.
    2. cycle 2(cooldown): 다시 묻지 않음(`/healthz`=1). 기본 주소로 다시 시도하고, 같은 문구를 유지한다.
    3. 대조: 디렉터리가 광고를 시작한 뒤 cycle 4 가 다시 묻는다. aim Advertised, 관찰 OK·heartbeat OK, `online … public=`, 재등록 0, `/healthz`=2.
  - **inactive**(목표 3 / 기대 ≤2 모순 해소 — 숫자 완화 없음, 단계 계약):
    1. cycle 1: 401 account_inactive. heartbeat 1·register 1, "token rejected … not active".
    2. cycle 2·3: skip, heartbeat 여전히 1.
    3. cycle 4: 같은 토큰으로 online, heartbeat 정확히 2·register 1.
- 추가 구분 case:
  - pinned 무응답: aim Pinned, legacy 송신 0, health 미질의, 문구 정확히 "address observation timed out".
  - 광고 무응답: aim Advertised, 같은 문구.
  - **https 미조준**: TLS 가 없어 health 조회 실패 → NotAimed, **송신 0**, 문구에 "timed out" 없음.
  - 순수 규칙 5종(https 분기 포함).
  - 기존 legacy-adjacent 2행(health 500 / 무광고에서 httpPort+1 관찰·heartbeat·게시)은 그대로 유지했고 통과한다.
- 느슨한 허용("observations 또는 timed out")이나 성공할 때까지 반복은 쓰지 않았다.

## 결과 (종료 코드 + 마지막 요약 줄; 개수는 이전 133 구성과 비교하지 않음)

| 실행 | rc | 요약 | FAIL 항목 |
|---|---|---|---|
| retry_test | 0 | `all retry checks passed (154 checks, 0 failed)` | — |
| **변이 TIMEOUT_ONLY**(옛 :1602 덮어쓰기) | 1 | 1 FAILED | "…the status says BOTH…" ← `address observation timed out` |
| **변이 NO_LEGACY_ATTEMPT**(광고 없음이면 기본 주소 시도 안 함) | 1 | 9 FAILED | 아래 |
| **전 코어 부하**(busy 16 프로세스) ×3 | 1 ×3 | 1 FAILED ×3 | 아래 |

- NO_LEGACY_ATTEMPT 의 9 FAIL:
  - legacySends=0.
  - 두 사실 문구.
  - cooldown cycle.
  - legacy-adjacent 2행의 heartbeat·probe·게시 등.
- 부하 3회:
  - **새 단계 항목은 모두 PASS** 했다(cached-silent·inactive·구분 case).
  - FAIL 은 매번 별건 **"a silent host loses to the relay"**(at 5015 / 5067 / 5100 ms) 하나다.
    - 이 항목은 실제 `rv.PunchAny`(`directory_retry_test.cpp:638` 계열) 경유의 시간 판정이다. Codex 판정대로 **이번 범위 밖·미수정**이다.
    - 무부하 조사 때도 기준 1/10 이었다.
- 첫 실행(d6bb951)에는 내 시험 결함 1건이 있었다. cycle 기록이 원시 status("online")만 담아 `public=` 검사가 FAIL 했다.
  - 기록이 StatusLine 과 같은 문구(주소가 있으면 `public=` 포함)를 담도록 0d4c27d 에서 고쳤다. 판정 기준은 낮추지 않았다.
- **출하 경계**(생성 vcxproj, `fix-directory-retry/.claude/test-tmp/check_defines_drf.py`):
  - 출하 9 타깃의 Release define 에 시험/변이 류 0.
  - 양성 대조: retry_test 는 `REMOTE60_DIRECTORY_TEST_HOOKS` 를, 두 변이는 각 `REMOTE60_DIRECTORY_MUTANT_*` 를 갖는다.
  - ALL PASS.
- 측정 슬롯은 쓰지 않았다(loopback·논리만). 삭제한 것 없음.

## 남은 것 · 제안
- **pause 시나리오**(프로세스 일시 정지)는 하지 않았다. 부하 3회로 대신했다.
- 원래 FAIL 근거(조사 보고의 10+10 분포·시각선)는 그대로 보존한다.
- 별도 미확정 항목(ledger):
  - "a silent host loses to the relay": 부하 3/3, 무부하 기준 1/10. PunchAny 경유의 시간 판정이다.
  - `inactive-stays` 의 RunHost(1,1) + 300 ms 샘플은 이번에 건드리지 않았다(지적되지 않음, 같은 형태의 시점 의존 가능성).
- 사용자 행동: 없음.
