# report ua2-apk r1 — 캡처 방식 선택을 '이 PC의 이번 연결에만' (APK) (작업용1, 인계 t-ui7wvkxk → 검증용 remote#yj8mrmrx, 2026-10-26)

**고정 후보: `fix/apk-prelogin-select` 5f73ad1** (APK 0.2.32 단일 브랜치 — ① picker e8123e4, ② SH r2 6d5c032 위 2 commit, tracked clean)
- 4df7db7 **제품**: 설정 UI·저장소·JNI(`nativeQueryDesktopCaptureBackend`/`nativeGetDesktopBackendStatus`)·공용 세션 backend 범위.
- 5f73ad1 **시험**: Kotlin 단위 `DesktopBackendPresentationTest` 8, shared core GDI, AVD `ua2` 시나리오.
- 계약: Codex `codex_rel_0163_r1.md` §1, 검증용 seq 13951 정정(34 flags bit0 StatusV1 요청에만 90 답, changeCount, bit4 NotYetApplied, 설정 화면 보일 때 조회). host 쪽은 0.2.163 capture-lifecycle UA-2(게시본)를 그대로 쓴다.

## 바꾼 것
1. **전역 저장값 자동 적용 중단**:
   - `SessionPersistence` 는 `desktop_backend` 를 더 이상 읽지 않는다. 옛 APK 가 남긴 키는 다음 저장 때 삭제한다.
   - 접속 시 재전송(`pendingDesktopBackendSync`)은 제거했다.
   - 다른 영상 설정·선명도 저장값은 그대로다.
2. **이번 연결에만 적용**:
   - 버튼은 연결된 PC 에 즉시 요청한다. 선택은 이번 연결의 메모리에만 있다(`sessionDesktopBackend`).
   - 새 연결(수동·디렉터리)마다 비운다 → Host 기본값부터 시작한다.
   - Apply 는 backend 를 싣지 않는다.
   - 문구: "이 PC의 이번 연결에만 적용 · 다음 연결은 PC 기본값으로 시작 · 창 캡처는 항상 WGC".
3. **요청 vs 실제 적용**:
   - 켜지는 버튼은 Host 답(90)의 `active` 이다.
   - 줄 문구는 상태별로 다르다:
     - 기본: "지금 PC가 쓰는 방식: X"
     - 요청을 보낸 뒤: "요청: Y · 실제 적용: X"
     - 요청이 답에 아직 반영 전: "요청: Y · 적용 중… (지금: X)"
   - Host 사유를 함께 표시한다(창 캡처=WGC / DXGI 격리 / 로그인 전 DXGI만 / 아직 적용 안 됨).
   - 답 전에는 "확인하는 중", 답하지 않는 구 Host 에는 "확인 불가"를 띄운다 — 성공처럼 켜지는 버튼은 없다.
   - 연결 전에는 비활성이다. 설정 화면이 보이는 동안 **약 2초 주기(상태 poll 지연 포함)**로 조회한다(question=backend 0; `elapsed >= 2000ms` 를 status poll 에서 검사, 저장 Host 로그 간격 약 2.2~2.3초). *(r2 정정: 이전 "2 s 이하" 는 구현보다 강한 표현이었다.)*
   - 규칙은 순수 Kotlin `DesktopBackendPresentation`.
4. **발견·수정한 기존 결함 (필수 배선)**: 공용 세션 `RequestDesktopCaptureBackend`/`DesktopBackendControl::Request` 가 1..2 만 받아, 5dbfc1a(2026-08-03)가 넣은 **확인한 이전 경로(공용 세션의 1..2 범위 검사)가 GDI = 3 요청을 거부했다**(옛 UI 에선 Apply 실패 문구로만 보였다). *(r2 정정: "그 뒤 한 번도 닿지 않았다" 는 전 배포 이력 단정이라 소스로 확인한 범위로 한정한다.)*. → 1..3 으로 고쳤다. Host 는 원래 1..3 을 받는다(host_control_session.cpp:1915).

## 시험
| 시험 | 결과 |
|---|---|
| `DesktopBackendPresentationTest` (APK 단위, 전체 257/0 중 8) | PASS |
| shared core: Request(3) 송신·4 무시 | PASS (`[shared-core-test] PASS`) |
| ① 회귀: `client_session_prelogin_select_test` 38/38, `client_session_role_park_test` 13/13 | PASS |
| **AVD `ua2` (seam 없는 APK, 시험 Host, 화면 밖 layered 패턴 창)** | **13/13** |

**AVD `ua2` 단계** (uiautomator 로 렌더된 컨트롤의 bounds·enabled·selected·text 를 확인하고 adb 실탭):
1. 옛 APK 상태를 심었다: 전역 `desktop_backend=2`(WGC) + `sharpness=strong`.
2. 연결 시 **Host 에 backend 변경 요청 0건** — 저장된 WGC 를 보내지 않았다.
3. 연결 중 버튼 3개가 활성이고, 켜진 버튼은 Host 값 DXGI 다. 줄은 "지금 PC가 쓰는 방식: DXGI (창 캡처는 WGC)".
4. GDI 를 **실탭**하면 Host 에 `desktop-backend-request backend=gdi` 1건이 가고, 줄이 "요청: GDI · 실제 적용: DXGI (창 캡처는 WGC)" 가 된다. 앱 로그 흐름: tap → request → APPLYING → APPLIED.
5. 연결 해제 → 재연결: 추가 요청 0건(누적 ['gdi'] 그대로), 줄은 "지금 PC가 쓰는 방식: DXGI" — 요청 표시 없음.
6. 저장소에서 `desktop_backend` 키가 사라졌고 `sharpness=strong` 은 남았다.

연결 직후 "확인하는 중"(CHECKING)이 표시된 것은 앱 로그로 확인했다(`desktop_backend_ui state=CHECKING`).

- 스크린샷: `.claude/caplife/sh_r2/ua2_7/ua2_2_connected.png`, `ua2_3_after_pick.png`, `ua2_4_reconnected.png`.
- 세션: console, RDP 없음, SLOT_REQUEST CreateNew → 삭제, 이 PC 화면 무조작(패턴은 화면 밖 layered 창).

## 첫 실행에서 드러난 것 (고친 순서대로)
- 하네스:
  - `adb shell` 인수의 따옴표 소실.
  - 재연결 뒤 정지 창에서 프레임 없음 → pulse 추가.
  - 이미 선택된 창의 "지금 보는 대상" 머리글을 카드로 오인 → 정확한 제목 매칭.
- **제품**: GDI 탭이 아무것도 보내지 않음 → `desktop_backend_tap` 진단 로그(남겨 둠)로 위 4번 결함을 확인했다.

## 한계·미검증
- **실제 backend 전환**: 이 시험의 대상은 **창**이라 Host 는 WGC 로 캡처한다(사유 표시로 확인). 바탕화면 대상에서 GDI/WGC 로 실제 전환되는 화면은 보지 않았다 — 이 PC 화면을 캡처·송출하게 되므로 피했다.
- 연결 전 설정 화면: 수동 연결 화면에서는 설정 탭이 없어 N/A 다. 비활성 규칙은 단위 시험으로만 확인했다.
- 다른 Host·계정 전파: 저장값이 없고 연결마다 비우는 구조로 막았다. 두 Host 를 오가는 실행 시험은 하지 않았다.
- 실 폰은 하지 않았다.
- 버전·서명·게시·push 는 하지 않았다(0.2.32 통합에서).


## r2 — Codex codex_ua2_apk_src_r1.md N1~N3 보수 (2026-10-26)
**고정: `fix/apk-prelogin-select` cf8c021 (제품) · 41de4cf (시험)**
- 제품: `DesktopBackendControl` 에 intent {asked, sent, sentSeq} 를 두고 JNI 상태 배열을 12칸으로 넓혔다(… roleKnown, intentAsked, intentSent, intentSentSeq). `DesktopBackendPresentation` 을 다시 썼다 — 상태 NOT_CONNECTED·CHECKING·NO_ANSWER_HOST·IDLE·APPLYING·APPLIED·STORED_FOR_DESKTOP·REFUSED. 점등 = **처리된** requested. "이 연결만" 문구는 FLAG_SESSION_SCOPED 일 때만. 저장 키 `desktop_backend` 제거(연결 한정).
  - N1: 처리됐지만 거부된 요청(prelogin WGC/GDI → DXGI 전용)은 APPLYING 에 머물지 않고 REFUSED + DXGI 안내, 다시 선택 가능.
  - N2: 구 Host(첫 pong 지연·조회 무응답)는 CHECKING → NO_ANSWER_HOST; 변경은 기존 wire 로 보내되 선택 완료 점등·연결 한정 단정 없음. 재연결 시 이전 상태 전파 없음.
  - N3: 문구 정정(위 26·28행).
- 시험: `DesktopBackendPresentationTest` 13, APK 단위 262/0 (통합 후보에선 263/0). Kotlin 변이(r1 규칙 복원) → 2 FAIL. ⚠️ A/B/A 지연 응답 순서는 이 변이로 판별되지 않는다(한계로 기록).
- AVD: `ua2` 13/13 (현 Host), `ua2_old` 7/7 (기준 Host `C:\work\gnlink-wt\fix-capture-lifecycle-baseline\Release\GNLinkStreamMigrationTest.exe` 821535a7…).
- 미검증: 실기 Host 의 로그인 전 화면에서 실제 backend 전환 화면, A/B/A 지연 응답의 실 Host 재현.


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


### r3 통합 확인
최종 후보 APK(8782f387…)에서 `ua2` 13/13 이다. 아래 rel 절을 참조한다.
