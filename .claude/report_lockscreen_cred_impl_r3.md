# report lockscreen-cred-impl r3 — Codex 8ecaa8f NEEDS_CHANGES L2-R / L5-R / UX-R / L1-R (작업용1 remote#rqxbj3jb, 2026-10-22)

task t-rs9nhsdv / rev r3 (정본 `.claude/task_lockscreen_cred_impl_r1.md` 'r3 보수', 근거 `codex_lockcred_impl_r2.md`) / kind=implement-report.

## 고정 후보
- worktree `C:/work/gnlink-wt/fix-lockscreen-cred`, 브랜치 `fix/lockscreen-cred`, HEAD **`53506ab`** (워킹트리 깨끗). 버전·서명·게시·push 없음.
- r3 커밋 (8ecaa8f 위):
  - 제품 **`e6e49d8`** (host_unlock_relay h/cpp · poc_protocol.hpp Busy=16 · secure_unlock_service.cpp · viewer_unlock.cpp)
  - 단위 시험 **`5dfba94`**
  - e2e **`b6ff40a`**
  - 문서 **`53506ab`**
- 출하 구성 빌드 `fix-lockscreen-cred-build` (Release, e6e49d8 소스) sha256:
  - GNLinkInputService `B22C456153ACC2CCEF6E3FF6962FAE2024E070B59109A0E0444F189B8DE6C67C`
  - GNLinkStream `31AA069B1100F04D447DC31829C384470FC152667FCAA8C33EE0DAFCC1EB58E5`
  - GNLinkViewer `5362EAD1A4D2297A3D9FABF505788F8B15D8482C31A697D1E73927B9A5A66F9A`
  - gate `--dir` 9/9 · self-test 19 PASS.
- UI e2e 는 in-repo `build-lc` 에서 돌렸다(산출물 공유 없음).

## 항목별
### L2-R — Ready fence 마감
- `secure_unlock_service.cpp`: `commitDeadline = min(us.ctx.expiresMs, now + kLockCredCommitMs)`. 경계는 `ChallengeState::Verify` 와 같다(지나면 만료).
- 만료 시에는 무입력·비밀 zero·RejectedStaleTopology 종료다. 재발급이나 재입력은 없다.
- 반례 (lockcred 54/54):
  - 만료 3 s 전 접수 → 만료 1 s 뒤 Ready: typed 0.
  - 대조: 만료 직전 Ready: typed 1.
  - 처리 상한 초과 사례는 기존대로 유지된다.
- 변이 N1(원래 만료 제거) → 반례 FAIL.

### L5-R — 보유 절대 상한
- relay 는 대기 뒤 **대기열 유무와 무관하게** `now >= holdUntil` 이면 `expire_hold` 한다.
  - 파이프를 반환한다.
  - 살아 있는 트랜잭션이면 결과를 정리한다: 입력된 것은 UnlockUnconfirmed(종결), challenge 만 받은 것은 Timeout(종결 — 늦은 seal 은 보내지 않음).
  - lockCredRequests 를 지운다.
- 보유 세션의 challenge/status/sealed 교환은 `budget()` = min(정상 마감, 남은 보유)이다. 우리 예산이 끊은 timeout 은 서비스 실패가 아니므로 "supported" 를 무효화하지 않는다.
- 보유 중인 연결이 아니면 status 는 서비스로 보내지 않는다(다른 연결에서 이어 묻지 않음).
- 다른 쿠키의 요청·capability·반복 status 는 마감을 연장하지 않는다.
- 같은 세션의 두 번째 challenge 가 거절돼도(Q1 pending) 그 세션의 다른 보유 트랜잭션 파이프는 놓지 않는다.
- 반례 (relay 37/37 ×3):
  - typed: 보유 2 s(시험 seam), 자기 status(각 20 ms 지연)와 다른 세션 busy 요청으로 대기열이 계속 찬다. 결과: 다음 Host(별도 relay)가 2.3 s 에 challenge 발급, Y 는 UnlockUnconfirmed, 마감 뒤 서비스로 간 status 0회.
  - challenge: 보유 1.5 s + busy 홍수. 결과: 다음 Host 통과, Y 의 늦은 seal 은 Timeout 이고 서비스로 보내지 않음.
  - 조기 반환: holder EndSession → 즉시 반환. 기존 최종 답 반환 사례도 유지된다.
- 변이:
  - N2(대기열이 빌 때만 마감)는 **단독으로는 통과**한다. 교환별 예산이 같은 상한을 지키기 때문이다(예산만 제거한 N2c 도 통과 — 두 겹).
  - 둘 다 제거한 N2b(= r2 동작)는 FAIL이다: 다음 Host 8.5 s 대기, UnlockUnconfirmed 아님, 마감 뒤 status 210회.

### UX-R — Busy
- `UnlockStage::Busy = 16`. relay 가 Busy 로 답하는 경우는 둘이다:
  - ① 같은 relay 의 다른 세션이 보유 중일 때: challenge(41, purpose 유지)·sealed(종결, ERROR_BUSY).
  - ② **다른 Host 가 서비스의 단일 파이프를 쥔 경우**: 연결 시도가 1 s 동안 계속 `ERROR_PIPE_BUSY` 면 Busy 로 판정한다(control 의 3 s 대기 안에 1.1 s). 이 경우가 A⑧-b 의 실제 경로이고, r2 에서는 InternalError 였다.
- 파이프가 없는 경우(FILE_NOT_FOUND)·무응답·연결 실패는 그대로 InternalError 다.
- busy 인 capability 질의는 기존 답을 유지하고 5 s 뒤 다시 묻는다(다른 Host 가 바쁘다는 이유로 버튼을 1분 숨기지 않음).
- 뷰어(모든 역할 공통)의 Busy 결과 창 문구:
  > 다른 연결이 이 원격 PC의 Windows 로그인(잠금 해제) 요청을 처리하고 있어 보내지 않았습니다.
  > 그 요청이 끝나거나 취소된 뒤(길어야 2분) 다시 누르세요.
- 연결 끊긴 세션 unlock 경로에서 Busy 는 저장 비밀번호를 지우지 않는다. 자동 재시도·재전송은 없다.
- **제품 UI 근거** (e2e `--lock-screen`, 새 단계):
  - 시험 프로세스가 "Host A" 로서 시험 서비스의 실제 unlock 파이프에서 **실제 잠금 challenge 를 발급받아 보유**한다(stage 3).
  - 제품 뷰어 툴바 클릭 → 비밀번호 창 → 입력 → **Busy 문구 창**(“오류” 없음) → 칸은 `untouched` 그대로, 다른 칸 0 → A 연결은 서비스가 계속 응답(무사).
  - A 종료 뒤 **수동 재시도**(다음 단계)로 교체 입력 성공 → 해제 확인.
  - 대조: 같은 연결의 일반 실패(12) 창은 “원격 PC가 입력하지 못했습니다 (오류 0).” 이고 Busy 문구가 없다.
  - 캡처 `C:\work\gnlink-wt\lockcred-scratch\e2e_r3\cap_lock-screen\lock_screen_busy.png` · `lock_screen_general_error.png`.
  - 범위 밖: sealed 단계 busy(같은 Host 의 두 control 세션)는 UI 에서 구성하지 않았다. 근거는 relay 단위 시험(16·종결·ERROR_BUSY·서비스 미전송)이고, 같은 문구 함수로 표시된다.
- 변이:
  - N5(busy 를 InternalError 로): relay FAIL.
  - **U1(파이프 busy 미인식, 제품 UI)**: e2e 의 Busy 창 검사 FAIL(“오류 0” 표시). 원복 44/44.

### L1-R — 늦은 결과 게시
- 트랜잭션은 challenge 큐잉 때 번호(`liveTx_[key] = ++txGenSeq_`)를 받고, EndSession 이 그 쿠키의 번호를 지운다.
- 다음 결과는 `IsLiveLocked`(같은 번호가 살아 있음)일 때만 남긴다: challenge·status·sealed·busy·연결 실패 결과 모두.
- 살아 있지 않은 seal·status 는 서비스로 보내지 않는다.
- 영구 tombstone 은 없다. 남는 항목은 살아 있는 세션의 것뿐이다.
- 반례: challenge 응답 400 ms 지연 중, 그리고 status 응답 지연 중에 EndSession → 그 쿠키 잔존 0. 다른 살아 있는 세션의 결과·항목 수는 그대로이고, 다음 Host 는 통과한다. 기존 sealed 취소 반례도 유지된다.
- 변이 N3(challenge 무조건 게시) → 잔존 1 FAIL, N4(status 무조건 게시) → 잔존 1 FAIL.

## 시험 (종료 코드 + 마지막 요약 줄)
- e2e (이 PC 콘솔 세션, qwinsta console Active; SLOT_REQUEST CreateNew → **삭제 완료**; 설치본·실서비스·실세션 무조작):
  - lock-screen **44/44** ×2
  - prelogin 79/79
  - not-prelogin 28/28
  - 산출 `C:\work\gnlink-wt\lockcred-scratch\e2e_r3\`
- 단위:
  - relay 37/37 ×3
  - lockcred 54/54
  - secure_unlock_service 12/12
  - prelogin_cred_host 43/43
  - secure_pipe_client_gate PASS
  - viewer_window_proc_isolated 42/42 ×2
    - 다른 작업자의 AVD 배치가 화면을 쓰는 동안 첫 실행에서는 swapchain/picker 화면 캡처 항목 2~5개가 FAIL했다.
    - 배치가 끝난 뒤 재실행에서 PASS. display-capture 의존이며 r3 diff(결과 문구)와 무관하다.
- 무관 회귀 전량은 지시대로 반복하지 않았다(GPU·android-session 등).

## 미검증·유지
- OS 실기 A~D 는 미검증이다: 실제 SYSTEM 서비스 → 콘솔 agent, 실제 LogonUI, 재부팅, UAC, APK.
- S1 role_park 간헐은 미해결이다.
- APK 0.2.30 잠금 화면 입력 차단 영향은 유지한다.
- 현장 묶음 `C:\work\gnlink-wt\lockcred-scratch\field_bundle.md` A⑧-b 를 r3 문구와 절대 상한으로 갱신했다. 다른 쪽 host_app.log 에는 `[lock-cred] challenge requestId=… status=16` 이 남는다.

다음 담당: 검증용 — `53506ab` 로 UI e2e 독립 실행 → Codex 검토.
