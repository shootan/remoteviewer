# LK-UX retry r4 — SOURCE 재확인

2026-10-26 / 검증용 remote#yj8mrmrx / 고정 후보 **8d8655ef8cbf3a60aa0ee502e08000ac81f4cd13** / 검토 diff **9ff6c15..8d8655e**.

**SOURCE OK. N1 잔여 두 곳을 닫는다. N2-a/N2-b는 r3의 OK를 유지한다.** 요청한 제품·시험 diff와 r4 보고를 읽었다. 후보 worktree HEAD 일치·tracked clean 및 diff whitespace check rc0를 확인했다. Codex의 새 빌드·행동 시험은 없으며, EVIDENCE는 예정된 R1 통합 고정 후보의 검증용 독립 검사에 귀속한다.

## N1 보완 확인

1. `apps/native_poc/src/host_unlock_relay.cpp:171`에서 Exchange의 end를 **min(now + 상대 budget, gate의 절대 deadline)**으로 제한한다. 쓰기/읽기의 기존 대기가 같은 end를 사용하므로, budget 계산 뒤 지연되어 시한 직전에 쓰더라도 응답 대기 시간이 새로 연장되지 않는다. r3의 mu_ 아래 live·시한 확인과 WriteFile 개시, 기존 CancelIoEx/overlapped 종료 회수도 유지된다.
2. 같은 파일 `:630`의 최종 mu_ 구역 안에서 **live와 절대 시한을 함께 검사한 뒤 hold/cache를 결정**한다. 예비 검사와 채택 사이의 별도 잠금 구간이 제거됐다. A가 이미 종료됐으나 B의 ChallengeIssued가 늦게 도착하면 stage17/LockCred purpose로 정리하고 B를 hold하지 않는다. 기존 소유권·오래된 응답 무효화는 유지된다.

## 시험 근거의 범위

추가 시험(8)은 1750ms op end 직전 약1650ms에 challenge 쓰기를 진행시키고 서비스 응답을900ms 지연시켜, caller의2000ms wait 안에 stage17이 돌아오고 hold가 없는지 검사한다. 시험(7)의 adopt hook도 실제 최종 채택 구역 바로 앞으로 이동해 시한 이후 채택 반례를 겨냥한다. 두 시험은 지적한 두 위치를 구분하는 소스 근거로 수용한다.

**relay84/84×4 rc0, 시험(8)1746ms, M10 clamp 제거 시2004ms 무응답 FAIL, M11 최종 시한 검사 제거 시 stage3/hold FAIL은 작업용 보고 인용**이다. Codex 또는 검증용 독립 실행 PASS로 바꾸어 기록하지 않는다.

## 다음 담당·완료 경계

현재 LK-UX SOURCE 추가 보수 요청은 없다. 검증용은 기존 LK task와 R1 통합 흐름을 이어서, 고정 통합 후보에서 명시적 재누름→기존 요청 종료→새 challenge/교체 입력, 다른 연결 Busy·비간섭, 오래된 응답 무효, 실패/무응답 후 재시도, 구 조합을 독립 확인한다. 새 viewer 시험 표식과 ForTest hook의 출하 define/바이트/제품 호출 감사는 기존 통합 gate에 포함한다. 같은 소스 판정을 위해 전량 시험을 다시 반복할 필요는 없다.

UIA 자동 AuthFailed 감지 보류 및 실제 SYSTEM/Winlogon/LogonUI·잘못된 암호 후 재시도 실기 한계는 유지한다. **이번 판정은 SOURCE 승인이고 통합 EVIDENCE·게시 완료 또는 실 OS 성공 선언이 아니다.** PS/UA·SH 및 승인된 R1 통합 준비는 계속 가능하다.

Codex 변경은 미추적 판정서와 운영 인계뿐이다. 제품·공개 문서 수정, 빌드, 설치, commit/push는 없으며, 구현 기록·계획 상태·Git MCP 커밋은 기존 기능/통합 task에 취합한다.
