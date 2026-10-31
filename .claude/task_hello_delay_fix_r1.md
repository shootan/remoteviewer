# task hello-delay-fix r1 — 연결 hello 지연 보수 (검증용 remote#wwml7h91, 2026-10-22) — 담당 작업용1

**이 파일이 정본이다. 끝까지 읽고 CLAUDE.md 대로 진행한다.** 조사 task(reconnect-hello-delay, t-uu1zshde)와 별개의 구현 task.
설계 확정: `.claude/codex_reconnect_hello_delay_r1.md` — **구속 계약**. 조사 근거: `.claude/report_reconnect_hello_delay_r1.md`(당신의 조사), 검증용 관찰: DW r4 독립 검증에서 **첫 연결**에서도 hello 12.8 s(attempts 207, badAcks 25) → 시험 호스트 세션 종료 → exit 43, 출하 뷰어 7.4 s(`C:/work/gnlink-wt/verify-eop/.verify/verify_dxgi_wedge_r4.md`).

## 사용자 행동 acceptance
PC 뷰어로 접속·자동 재연결할 때(특히 뷰어가 10초쯤 꺼졌다 다시 붙을 때) 화면이 수 초 안에 나오고, "연결 인사" 지연으로 창이 닫히지 않는다.

## 범위 (Codex 채택: ①+② 함께, PC 0.2.162 통합 후보에 별도 commit 으로)
1. **① PunchAny 송신 cadence**: 즉시 오류(WSAECONNRESET 등)로 recvfrom 이 바로 실패해도 다음 라운드 송신은 **monotonic 150 ms 간격**을 지킨다(`directory_rendezvous.cpp:258` 의 `continue` 폭주 제거). 응답 대기·취소·deadline 의미 불변.
2. **② hello 수신**: ack 가 아닌 항목(punch 응답·10054 등 junk) 뒤에 50 ms sleep 하지 말고 **같은 round(slice) 안에서 bounded 로 계속 수신** — 다음 Hello 송신은 원래 재시도 시계대로(단순 sleep 삭제로 outer 재송신 폭주 금지). 수신 batch 수·deadline·stop·CPU 사용이 bounded.
3. **불변**: 기존 ACK 판정·auth·timeout·retry 횟수·host punch 예산(kPunchReplyPerSource 등). 큐 일괄 사전 flush·SIO_UDP_CONNRESET 변경은 **이번에 하지 않는다**.
4. **표현 한계 유지**: ACK 송신 = 수신 큐 도착 확정 아님, 오류 recvfrom 의 from 주소 귀속 단정 금지, 25 출처는 추정, 빈도 비교·실 LAN 전체 원인 확정 금지.

## 필수 시험 (Codex)
- 즉시 오류 폭주 cadence 반례(오류 연속 시 송신 간격 ≥150 ms).
- 25 Punch + 100여 junk 항목 뒤 도착한 ACK 를 **1 s 안에** 읽는 통제 시험 + 구식 sleep 변이 → FAIL.
- 무응답·연속 junk 에서 deadline/cancel 준수, auth 음성(인증 없는 ACK 거부) 유지.
- 실 재현: 뷰어 11 s 유지 뒤 재생성 재연결 10회 + 짧은(4 s) 대조, 기준판 대비. 실제 사슬(dxgi_wedge_reconnect_runner 등) 1회.
- Android 공유 옵션 영향: 이 코드가 Android(native 공유 코어)에도 들어가는지 확인하고, 들어가면 영향·시험(가능 범위) 보고.
- 전량 GPU/store 반복 불필요.

## 규칙
- worktree: main `5063cd9` 에서 새 worktree·브랜치(예: `C:/work/gnlink-wt/fix-hello-delay`, `fix/hello-delay`). 조사용 scratch(inv-hello-*)는 **보존** — repo root 밖(C:/work/gnlink-wt) 삭제는 사용자 별도 허가가 없으므로 이 task 에서 지우지 않는다(cleanup 은 완료 조건 아님).
- 사용자 설치본·실서비스·세션 무조작, 버전·서명·게시·push 금지, 측정 슬롯 CreateNew.
- 커밋 Git MCP(`.claude/mcp_git_client_wt.py <worktree>`), history 항목은 task 커밋에.

## 보고 (1회)
`D:/remote/remote/.claude/report_hello_delay_fix_r1.md` + `remote#wwml7h91` pin. commit·제품/테스트/문서, 시험 rc+요약, 변이, 실 재현 분포(전/후), Android 영향, 한계.

## r2 보수 (2026-10-22) — Codex 83c3ac5 NEEDS_CHANGES (정본 `.claude/codex_hello_delay_fix_r1.md`, N1·N2 한 묶음)
- **N1** hello 의 reads 512 cap 을 slice 보다 빨리 채우면 곧바로 retrySleep 뒤 다음 Hello(PC 50 ms / Android 0 즉시) → cap 이 송신 시계를 앞당김. cap 에 도달해도 **원래 round 끝(송신 시계)을 보존**. 반례: cap 도달 flood(지금 시험은 Sleep 1 이라 200/250 ms 안에 512 미도달 → 실제 도달하게), stop·deadline·CPU 대조, 구식 cap 변이 FAIL. 단순 cap 확대·추가 timeout 금지. `native_video_client_tcp_control.hpp:113` 낡은 'pause after a bad ack' 주석도 이 편집에서 수정.
- **N2** PunchAny 의 범용 Terminal(즉시 false) 분류 — 소켓 손상으로 단정 불가. 예: WSAENETRESET 은 UDP 에서 TTL 만료 의미인데 즉시 false → 첫 후보 fallback 으로 다른 유효 후보를 놓침. **PunchAny 로컬에서** 개별 advisory 오류와 진짜 영구 소켓 실패를 구분(TTL 뒤 다른 후보 계속, NOTSOCK 은 실패, 연속 오류 cadence 대조). 광역 recv 정책 변경 금지.
- Android(0 sleep / 250 slice) 공유 영향, 새 gate·해시, 핵심 독립 재실행 필요(전량 suite·10회 무조건 반복은 불필요). scratch 증거 보존, repo root 밖 삭제 금지.
- 보고: `.claude/report_hello_delay_fix_r2.md` + 검증용 pin.
