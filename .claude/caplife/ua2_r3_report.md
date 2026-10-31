

## r3 — Codex codex_ua2_apk_src_r2.md N1·N3 잔여 (2026-10-27)
**고정: `fix/apk-prelogin-select` 482a901 (제품) · 3e4d567 (시험)** — picker r3(9afcec7·3a17444) 위.
- N1 "처리됨 ≠ 바뀜": `DesktopBackendPresentation`
  - APPLIED 는 **지금 캡처(active) = 요청** 일 때만. "요청: X 적용됨 · 지금 바탕화면 캡처: X".
  - POLICY_REPLACED: 처리됐고 PC 정책이 다른 방식(effective, 예: DXGI 격리 → WGC)을 쓰며 그것이 지금 캡처. "요청: DXGI 접수 · PC 정책으로 WGC 사용 · 지금 바탕화면 캡처: WGC (DXGI 문제로 WGC 사용)".
  - RECEIVED: 처리됐지만 지금은 다른 방식으로 캡처(유휴 저장, 또는 재시작이 이전 방식으로 복구). "요청: GDI 접수 · 아직 이 방식으로 캡처하지 않음 · 지금 바탕화면 캡처: DXGI". wire 에 없는 원인은 만들지 않는다. terminal(무한 적용 중 아님), 점등은 처리된 요청.
  - 이 연결의 요청이 없고 Host 상태도 아직 없음(0 필드·NotYetApplied, 창 대상 아님) → CHECKING(확정 IDLE 아님). 창 대상이면 "지금 창 캡처: WGC" 는 사실이라 IDLE.
  - prelogin 거부 REFUSED terminal 유지.
- N3 capability: `ClientSessionController::DesktopBackendCapability()` — known 과 answers 를 **HostRoleGate 한 번의 Load**(같은 pong 의 flags)에서 만든다. JNI `nativeGetDesktopBackendStatus` 는 이것만 쓴다(두 시점의 두 읽기 제거).
- 시험:
  - `DesktopBackendPresentationTest` 16 (+3): (a) GDI 처리·DXGI 캡처 → RECEIVED(후속 조회도 RECEIVED), 실제 GDI 가 되면 APPLIED(d); (b) 격리 → POLICY_REPLACED; (c) 요청 없음 + 0 필드/NotYetApplied → CHECKING; 옛 응답(seq3 < sentSeq5, 최신과 같은 코드) → APPLYING. APK 단위 **266/0**.
  - Kotlin 변이(r2 규칙: 처리=적용, 요청 없으면 바로 IDLE) → **3 FAIL** (`handledButNotCapturingIsReceivedNotApplied`, `quarantineShowsTheRealOne`, `noRequestAndNoStateYetIsCheckingNotIdle`), 복원 후 266/0.
  - native P10(`client_session_prelogin_select_test`): 읽기 직후 stopPoint 에서 지원 Host 의 첫 pong 을 통과·처리시킴 → 스냅숏은 known=0 answers=0(한 시점). **60/60**. 변이(r2 JNI 순서: answers 먼저, known 나중) → `FAIL known=1 answers=0`, 복원 후 60/60.
  - AVD `ua2` 13/13 (noseam 99cce1ea…, 창 대상 경로).
- 근거(소스 읽기만): 바탕화면 대상 Host 의 소유 스냅숏 답은 active 기본 DXGI(=1, `host_backend_policy.hpp:35`)라 0 이 아니고, 다른 owner 스냅숏은 0 필드+NotYetApplied → CHECKING 뒤 약 2초 재조회로 해소. **바탕화면 대상 IDLE 줄은 이번 AVD 에서 실행하지 않았다**(ua2 는 창 대상), `host_capture_lifecycle_ua_e2e_test` 도 이번에 미실행.
