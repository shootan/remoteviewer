# directory-retry R2 SOURCE 재확인

2026-10-28 / Codex remote#tt7fcvky → 검증용 remote#7fsfy22z / 기존 dirretry-rebase task.

**SOURCE OK — 고정34f22de에서 T1·T2를 닫고 시험용 인접 port 장치 보수를 수용한다.** 제품 진단 SOURCE 수용은 유지하며 이번 diff에는 제품 동작 변경이 없다. 검증용의 안전한 독립 실행으로 연결한다. EVIDENCE/게시 완료 판정은 아니다.

## 고정 대상

- `fix/directory-retry-r2` **34f22defb536aedf48fe5143b5adc6d62d6987f1** = d2f1243+34f22de, diff **af9a6af..34f22de** 전체5파일. HEAD 일치·tracked clean·whitespace rc0.
- HostAgent 추가 동기화는 `REMOTE60_DIRECTORY_TEST_HOOKS` 안에만 있고, CMake는 retry 시험/변이 target에만 hook과 NO_CYCLE_GATE를 정의한다. 출하 주소 선택·health/observe/재시도/인증·KA/NP 정책을 바꾸지 않는다.

## T1: 단계와 live 계수 — 닫음

실제 loop의 cycle record 직후, 다음 sleep/cycle 전에 **bounded gate**를 둔다. StagedHost의 Wait는 이전 cycle을 하나씩 release하고 원하는 n에서 loop를 붙잡는다. 따라서 그때 읽는 fake directory/LegacySends 계수와 Script 변경이 같은 stage에 결합된다. 과거 record만 고정한 뒤 서버/worker가 다음 cycle로 진행하던 이전 경합을 막는다.

gate는60초 timeout 후 표시를 남기며 fixture 종료 시 loud failure로 검사한다. Stop도 대기를 깨워 종료할 수 있다. n보다 낮은 record의 재조회와 현재 멈춘 cycle의 live 계수를 혼동하지 않는 현재 검사 흐름을 대조했다.

느린 inactive·cached-silent는17초 지연을 두고도 같은 stage 결과를 요구한다. NO_CYCLE_GATE 변이가 그 느린 pass4건에서 실패했다는 보고는 원 지적을 겨냥한다. 최초7초로 변이가 통과한 결과를 숨기지 않고, cycle4 이후까지 지연하도록 바꾼 점도 타당하다. **177/0 반복·mutant4/2/12 FAIL은 작업용 실행 귀속**이며 Codex가 이 긴 suite를 새로 실행한 것은 아니다.

## T2: cache/삭제 실행 경계 — 닫음

exe_directory 기반 cache를 제거하고 RunHost/StagedHost/PunchHarness를 모두 검증된 `REMOTE60_TEST_TMP` 아래로 옮겼다. entrypoint에서 미설정/길이·이름·직접 자식/anchor·reparse를 확인한 뒤에만 진행하며, 삭제 helper도 root 바로 아래 leaf만 허용한다. exe가 C: build에 있어도 cache/삭제가 그 옆으로 가는 기본 경로는 없어졌다.

Codex 직접 확인:

- 이 PC의 `D:/remote/remote`, `.claude`, `.claude/test-tmp`는 현재 일반 디렉터리이고 reparse가 아니다. 고정 anchor/부모라는 전제를 확인했다. 독립 실행 때도 같은 실제 경계를 확인하고 고유 root를 쓴다.
- fixed retry exe에서 root-guard 표식을 먼저 확인한 뒤 **미설정/바깥 root 두 거부 경로만** 실행했다. 각각rc2로 네트워크 시험 전에 거부했다.
- 바깥 root probe `C:/work/gnlink-wt/codex-dirretry-reject-only`는 실행 전후 존재하지 않았으며 생성되지 않았다. cache 삭제/전체 suite/제품 프로세스를 실행한 것이 아니다.

현재 helper를 임의 portable checkout이나 link가 바뀐 anchor까지 보편 안전하다고 확대하지 않는다. root/부모의 실제 경로·link 전제와 자기 자료 소유를 실행 시 확인한다. 이미 보고한 저장소 밖 생성·삭제는 소급 승인/무해 판정으로 바꾸지 않는다. “남은 파일0”도 기존 동명 파일 피해 없음의 증거는 아니다.

과거 실행/횟수·migration1회와 r1의 “삭제 없음” 정정은 최신보고에 남긴다. migration의 같은 cache 방식은 별도 미수정이므로 **해당 T2 보수 전 실행하지 않고** 원본/삭제 후보를 보존한다. 새 바깥 정리나 삭제 권한 질문은 필요 없다.

## 인접 port fixture

TCP bind(0)의 연속 cursor와 UDP 제외 범위가 겹치면 연속 시도가 함께 실패할 수 있다는 OS 측정은 원인을 설명하는 근거다. random base로 TCP B/UDP B+1을 직접 bind하고 최대256회 뒤 loud-fail하는 보수는 **시험 장치만** 바꾸며 legacy fallback이 실제port+1로 성공해야 한다는 판정은 그대로다. 동적으로 고른 loopback 포트이고 사용자 프로세스/OS 예약 설정을 바꾸지 않는다.

보고의 실제 PowerShell 재현22/32, C++ 정상회차177/0×2와 같은 제외범위 경계에서의 C++ 직접 재현 미수행은 구분한다. 이전run2의 정확한 점유/예약 원인이 사후 확정됐다고 하거나 모든 미래 실행의 성공 확률을 보장하지 않는다. 실패하면 지금처럼 setup 미도달을 드러내야 한다. shared fake header 사용자에 대한 컴파일과 실제 행동 실행도 별개로 유지한다.

## 다음 담당

검증용은34f22de의 독립 산출물/고유 내부 root에서 완료 stage·느린 observer·실제 legacy-adjacent 두 행·대표 부정 대조를 확인한다. 원본 포트 실패/구time-window FAIL은 보존하고 정상 결과와 구분한다. 이후 KA/NP 고정 통합과 실제 진단 표시 영향 범위를 확인하며 이미 닫힌 무관 suite를 매 commit 반복하지 않는다.

제품 변경·제품 빌드·전체 시험/서버·사용자 앱/설치·삭제·commit/push는 Codex가 하지 않았다. 직접 실행은 위 guard 거부 경로와 Git/경로 확인뿐이다. 미추적 판정서 외 history/계획·Git MCP commit은 같은 구현/통합 작업에 취합한다. 사용자 현재 조치 없음.
