# directory-retry-flake r1 — Codex 조사 판정·후속 범위

2026-10-22 / 조사 task **t-azc166pv** / 보고 `.claude/report_directory_retry_flake_r1.md`.

**조사 근거 OK. 0.2.162에서 분리해 ledger/후속으로 관리한다.** cached-silent의 현재 hello 변경과 독립된 기존 상태 전환·시험 샘플 설계 문제라는 결론을 수용한다. **후속은 A의 진단 의미를 아래처럼 보완하고 B의 단계 기반 시험을 함께 채택**한다. 제안 A를 그대로 적용해 실제 관찰 timeout을 숨기거나, B를 짧은 시간창 샘플로 대체하는 것은 승인하지 않는다.

현재 통합9f54912 및 승인된 rel/0.2.162 로컬 실기 후보 준비는 계속한다. 이번 후속을 그 브랜치에 추가하거나 기존 통합/hello 판정을 다시 열지 않는다.

## 근거와 결론의 범위

- 보고 전체와 `HostAgent::Start`/health 조회/cooldown/observe/Loop, `RunHost`/cached-silent/inactive 기대를 읽었다. 관련 두 파일이5063cd9→83c3ac5에서 불변임을 확인했다. cached-silent는 heartbeat 성공/그 이후 punch까지 가지 않으므로 이 실패 기제에 hello/PunchAny 변경이 실행되지 않는다.
- base 원문 두 회차를 직접 분리해 대조했다: `01_base.out`은 cycle4 observe 실패 뒤 sleep202ms에서 stop·timeout 문구 FAIL, `02_base.out`은 sleep0ms에서 stop·observations 문구 PASS다. 고정150×100ms+300ms 뒤 단일 `StatusLine()` 샘플이라는 코드와 일치한다. 전체10+10회 분포·타이머/부하 자료는 조사 보고 귀속이며 Codex가 다시 실행한 것은 아니다.
- **비동기 상태 전환의 어느 지점을 샘플했는지로 이 문구 차이를 설명할 수 있다.** 이를 메모리 데이터 경합의 증거로 쓰지 않는다. 데이터 경합이 전 제품에 없다고 보편적으로 증명한 것도 아니다.
- base5/10·candidate6/10은 “분포가 동일함을 입증”한 수치가 아니다. **두 쪽에서 기존 기제가 재현됐고 새 hello 논리의 회귀라는 근거가 없다**고 적는다. 50% 발생률/독립 시행 확률을 실망 사실로 일반화하지 않는다.
- **범위 주의:** 이 결론은 cached-silent 상태 실패에 해당한다. 같은 시험 파일의 `a silent host loses to the relay`는 실제 `rv.PunchAny`(`directory_retry_test.cpp:638`)를 호출한다. 그 baseline1/10 실패까지 “hello 경로 미도달”이라고 합치면 안 된다. relay 실패는 별도 미확정 항목으로 기록한다. inactive의 단계/기대 불일치도 별도 case로 관리한다.

## A — 진단 사유 보존을 수정 채택, 실제 timeout도 보존

plain HTTP의 port+1은 기존 **문서화된 legacy 기본 규칙**이다. 광고가 없다는 사실만으로 서버 설정이 틀렸거나 관찰을 시도하면 안 된다고 결론내리지 않는다. 실제 그 포트가 살아 있을 수도 있다. 따라서 “광고 없음 이유만 영구 유지”로 실제 관찰 실패를 지우는 단순1602행 guard는 채택하지 않는다.

별도 후속의 진단 계약은 다음이다.

- **HTTP·광고 없음·기본 규칙 주소·관찰 실패:** “주소 안내가 없어서 기본 규칙으로 시도했고 응답이 없었다”는 두 사실을 함께 유지한다. 예: `directory has no observe advertisement; observation via the legacy default timed out`. 문구는 기존 표시 형식에 맞춰 다듬어도 되지만 **서버 오설정 확정**이나 **네트워크 timeout만**으로 축약하지 않는다.
- **HTTPS·광고/지정 포트 없음:** 현재처럼 보낼 주소가 없다는 이유를 표시하며 임의 port를 만들거나 요청을 추가하지 않는다.
- **명시 pin/유효 광고 주소가 있으나 무응답:** 기존 timeout 진단 의미를 유지한다. HTTP 광고가 없어도 기본 주소에서 관찰이 성공하고 heartbeat가 성공하면 정상 online으로 넘어가야 한다.
- health 자체 조회 실패와 성공했으나 광고가 없음을 구분한다. cooldown cycle에서도 마지막 이유/조준 출처가 유효하다면 유지한다. `FetchObserve…`의 bool 하나나 `StatusLine()` 문자열 검색으로 두 상태를 추측하지 않는다. 필요한 최소의 명시적 진단 상태를 사용한다.
- **주소 선택·ready 의미·관찰 횟수/간격·health cooldown·heartbeat·토큰/인증/재등록·실제 연결 정책은 바꾸지 않는다.** status를 안정화하려고 ready=false로 HTTP fallback을 막거나 요청을 줄이지 않는다. 새 wire/API·서버/설정 변경도 없다.

이는 표시/진단 계약의 제한된 보수다. A를 적용하면 B가 필요 없다는 결론은 금지한다.

## B — 클라이언트 단계에 결합한 결정적 시험 채택

`/healthz` Count2 직후부터1.5초 안에 읽으라는 제안은 **여전히 시간창에 의존**한다. 서버가 요청을 받았다는 것만으로 클라이언트가 응답을 해석해 상태를 썼다고 보장되지 않는다. 임의 sleep을 조금 앞당기는 것도 같은 문제다.

- fake directory/실제 HostAgent의 **명시적 상태 전이 완료·같은 cycle/세대**를 관찰한다. 필요하면 시험 전용 barrier/clock/관측 hook을 실제 생산 루프의 경계에 둬 다음 단계를 제어할 수 있다. 생산 규칙을 복제한 별도 성공 stub으로 대체하지 않는다. 대기는 bounded이며 전제 미도달은 실패다.
- cached-silent는 건강 조회 뒤 짧게 보이는 일시 문구가 아니라 **광고 없음 + 기본 주소의 실제 관찰 실패를 처리한 단계**와 cooldown에서의 진단을 확인한다. 이어서 가짜 디렉터리가 유효 광고/관찰 응답을 주면 정상 회복·heartbeat/online이 되는 대조도 둔다. 광고 없음만 보고 시도를 막는 잘못된 변경도 잡아야 한다.
- HTTP legacy fallback 실제 성공, HTTPS 미광고, 명시 pin/광고 후 무응답을 구분한다. 요청/관찰/재등록 상한과 응답 정책도 확인한다.
- inactive는 “heartbeat3회 기다림”과 “전체≤2회 기대”의 모순을 해소한다. **어느 응답 단계/관찰 구간의 횟수와 상태인지** 계약을 먼저 명시하고 그 단계에서 count·token 보존/재등록/online을 대조한다. 목표 수/기대 수 숫자를 임의로 완화해 PASS시키지 않는다. 전 시험 helper를 일괄 개편할 필요는 없다.
- 기존 timeout-only 덮어쓰기·일시 상태만 샘플하는 변이가 의미 있는 반례에서 실패해야 한다. “observations 또는 timed out이면 PASS” 같은 느슨한 허용이나 성공할 때까지 반복은 하지 않는다.
- pause/load 시나리오에서도 같은 상태 결과가 되는지 제한된 반복으로 확인한다. 전량 suite10회 재조사로 되돌아갈 필요는 없다. 테스트 공급 경계는 출하 제외·고정 빌드 분리/양성 대조를 확인한다.

## 시기·담당·기록

1. **조사 t-azc166pv는 근거/ledger를 연결해 마무리**한다. 판정 상태는 “기제 확인·미수정”이다. 원본 FAIL을 소급 PASS로 바꾸지 않는다.
2. 검증용은 **별도 후속 구현 task 하나와 정본**에 위 A/B를 묶고 담당을 연결한다. **0.2.162 로컬 후보 준비를 우선 배정한 뒤**, 여유 담당이 독립 브랜치에서 병행할 수 있다. 사용자 실기 응답을 기다려야만 시작하는 장벽은 없다. 차기 유지보수 후보이며 특정 새 버전 번호나0.2.162 backport를 지금 확정하지 않는다.
3. 구현·자체 시험→검증용 독립 핵심 단계/부정 대조→Codex 고정 후보 검토로 잇는다. 별도 제품·시험·history/계획 상태·Git MCP commit을 유지하고, 현재 rel 후보에는 섞지 않는다.
4. cached-silent/inactive/relay 후보를 구분한 ledger와 다음 담당을 명시한다. viewer_udp_recovery/SH control·clipboard/hello와도 같은 원인으로 합치지 않는다. 출하본/실서비스/서버 설정 조작·root 밖 삭제·게시·push는 승인하지 않는다.

0.2.162 통합 판정은 이미 `.claude/codex_integ_0162_r1.md`로 검증용에 전달했다(seq13389). 그 회신을 기다리며 rel 준비를 멈출 이유는 없다. 현재 사용자 조치 없음. Codex는 조사 보고/제품·시험 소스/대표 원문 대조 및 미추적 판정서/인계 작성만 했고 제품·시험 수정/빌드/실행·commit·push는 하지 않았다.
