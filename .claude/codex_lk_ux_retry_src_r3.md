# LK-UX retry r3 — SOURCE 재확인

2026-10-26 / 검증용 remote#yj8mrmrx / 후보 **9ff6c15cc149c6e8977a15867111771599c7045b** / diff **598339a..9ff6c15**.

**N2-a/N2-b SOURCE OK. N1의 취소 뒤 새 Write 문제도 닫힘. 전체 시한의 wait/채택 적용 두 곳만 보완 필요해 최종 SOURCE는 NEEDS_CHANGES다.** 요청 범위의 제품·테스트 diff와 직접 호출부를 읽었고 worktree HEAD9ff6c15/tracked clean을 확인했다. 새 빌드/실행 재현은 하지 않았다. EVIDENCE는 예정한 R1 통합 후보 독립 검사에 귀속한다.

## 닫은 부분

- **N1 실제 Write 개시:** `Exchange:177~186`은 caller가 회수하는 같은 mu_ 아래 liveTx·절대 시한을 확인하고 overlapped WriteFile을 개시한다. 무효화가 먼저면 Io::Withdrawn/쓰기0, 시작이 먼저면 이미 개시한 I/O로 구분된다. 예전의 expired→1ms 연장은 제거됐다. supersede가 아직 쓰이지 않은 Withdrawn은 A의 pipe/대기를 보존한다. 기존 false18 뒤 새 Write 개시 지적은 닫음.
- **N2-a 보낸 B의 추적:** !exchanged && sealSent이면 waiting을 세워 B의 requestId를 후속 poll/다음 명시 retry 대상으로 유지한다. 로컬 봉인 실패는 sealSent=false이므로 B를 추적하지 않는다. tentative 입력의 TargetNotReady/실패·stale·timeout 등을 그대로 안내하는 분기도 수용한다. 입력 완료를 모를 때도 암호/암호문을 자동 재전송하지 않는다.
- **N2-b 검증 후 표식:** requestId 일치 후 LockCred purpose를 확인한 뒤 challengeIssued를 세운다.17 응답도 purpose를 확인한 뒤 별도 supersededAnswer를 세우며 caller는 원시 stage17만으로 A 종료를 추정하지 않는다. r2 지적은 닫음.
- viewer_lock_cred_retry_test는 실제 viewer_unlock 교환을 사용하는 방향이고, 새 status-poll fault는 기존 e2e 전용 define 안에 있다. 통합 출하 define/byte gate에서 새 `GNLINK_VIEWER_TEST_STATUS_POLLS`까지 포함한다. callback ForTest setter는 제품 호출/런타임·env wiring이 없는지 기존 통합 감사와 함께 확인한다.

## N1 잔여 — 하나의 절대 시한을 끝까지 적용

### 1) Exchange의 응답 대기 end는 여전히 상대 budget으로 새로 만들어진다

`host_unlock_relay.cpp:171`은 **GetTickCount64()+deadlineMs**를 end로 사용한다. 이 budget은 worker가 Exchange에 들어오기 전 계산한다. gate는 Write 직전에 command의 절대 시한을 보지만, WaitForMultipleObjects의 left(196~199)는 gate->deadlineMs로 clamp하지 않는다.

따라서 budget을 계산한 뒤 지연되었다가 **절대 시한 직전** gate를 통과하면, 쓰기는 적시에 시작했어도 Read의 새 상대 budget이 원래 op end를 넘어갈 수 있다. 이 경우 caller는18로 돌아갔는데 worker/pipe는 옛 상대 시한까지 기다릴 수 있다. r3의 '취소 뒤 새 쓰기'는 해결됐지만 **전체 op의 대기 시한**은 아직 같은 end가 아니다.

최소 보완: gate가 있을 때 Exchange end를 **min(현재+normal budget, gate의 절대 end)**로 잡고 write/read 대기 모두 그 end를 따른다. 만료 후 새 Read를 시작/기다리며 시간을 추가하지 않는다. 이미 시작한 I/O의 CancelIoEx/종료 대기는 기존처럼 수명 보장을 유지한다. caller wait/250ms 여유를 늘리지 않는다.

### 2) 채택 시한 검사와 실제 hold/cache는 다른 임계 구역이다

`620~636`은 mu_ 아래 inTime을 계산하고 잠금을 놓는다. 이후 `639~656`의 **실제 hold_for/lockCredRequests_/challengeResults_ 채택**은 다시 mu_를 잡고 live만 확인한다. 그 사이 op end는 지났지만 caller의 바깥 wait는 아직 끝나지 않은250ms 구간이면, live=true여서 만료된 ChallengeIssued를 새 hold/cache로 채택할 수 있다.

최소 보완: **실제 채택하는 기존 최종 mu_ 임계 구역에서 live와 절대 시한을 함께 검사한 값**으로 hold/cache를 결정한다. 이전의 별도 검사만 믿지 않는다. 만료라면 A 종료 사실17을 유지하고 B의 발급 성공/hold를 만들지 않는다. 기존 채택 block 안의 조건/출력 정리로 가능한 범위이며 새로운 wire/설계를 요구하지 않는다.

두 반례도 이 지점에만 집중한다: (a) Write budget 계산 후 잠시 멈췄다가 op end 직전에 송신, 서비스 응답을 늦춰 wait가 원래 절대 end에서 종료되는지 (b) 예비 inTime 검사 뒤 최종 채택 직전에 멈춰 end를 넘기고 해제, ChallengeIssued 채택/hold0 및 A17 보존. 현재 adopt hook은 **예비 검사 앞**이므로(b)의 검사→commit 틈까지 직접 고정하지 않는다. 실제 채택 block으로 검사를 합치면 그 block 직전 hook으로 검증하면 된다.

이는 deadline 적용의 남은 두 위치이며 이미 닫은 소유자/쓰기 gate/N2를 다시 설계하거나 전체 UI/GPU 검증을 반복할 요청이 아니다.

## 다음 담당·근거 귀속

작업용 보고의 relay81×3·viewer5·e2e68×2/79/28·M7/M8/M9는 작업용 실행 인용이다. 기존 지적을 구분하는 반례와 소스 방향은 수용하지만 Codex/검증용 독립 PASS로 쓰지 않는다. 독립 통합 EVIDENCE 대기는 SOURCE 결함으로 추가 계산하지 않는다.

검증용이 기존 LK task에 위 **N1 잔여만 취합 보수**해 새 고정 diff를 주면 그 범위로 닫는다. PS/UA·SH 독립 작업과 통합 준비는 계속 가능하다. UIA 자동 실패 감지 보류/실 SYSTEM·LogonUI 한계는 유지하며 새 UAC 승인/기기 설치/서비스 교체는 수행하지 않는다. Codex 산출물은 미추적 판정서/인계뿐이고 제품 commit/push는 없다.
