# lockscreen-cred-impl r2 — Codex 고정 후보 검토

2026-10-22 / task **t-rs9nhsdv** / 후보 **8ecaa8f570a73cf431d7709a653f82f84317a331** (제품959bf45, 시험63d3230, gate·문서8ecaa8f; base5063cd9).

**판정: NEEDS_CHANGES.** r1의 연결별 결과 격리, Ready→Go 권한 재검사, 실제 대상/일반 agent의 잠금 입력 정책, UIA 요소 구분·교체·부분 key-up 정리, overlapped I/O·취소·capability 수명·뷰어 세대 검사, SIS gate 보완은 확인했다. 독립 제품 UI 검증도 이번에는 확보됐다. 남은 보수는 아래 **L2-R / L5-R / UX-R / L1-R 한 묶음**이다. 기존 task에서 처리하며 새 기능·중복 task를 만들지 않는다. 통합/출하 승인이나 실제 OS 잠금 해제 성공 판정은 아니다.

## 직접 확인한 근거와 한계

- 작업용 보고 `.claude/report_lockscreen_cred_impl_r2.md`, 독립 기록 `C:/work/gnlink-wt/verify-lockcred/.verify/verify_lockcred_r2.md`, r1→r2 제품/시험 변경과 관련 원문을 대조했다. 구현 worktree HEAD와 tracked clean을 확인했다.
- 검증용 build-vs 산출물 SHA256을 직접 계산해 기록과 대조했다: InputService `0c59d3d775ac35963280282b48442ae8bd132c91e133703e711e4e40784af7a9`, Stream `a655d4cc27e6c02b51b26a6ee309526e91dce5b66aa375236a158a6e71480332`, Viewer `dafed71cc5e107727fb2cfb7640a97113b33984ef10ce8eeef7362552b3f7290`.
- `.verify/r2/*.out`의 실제 요약 줄을 확인했다: lockcred52, channel15, relay22, target21, identity UI8, lock refusal4, policy17/17, seal/reboot PASS, gate9/self-test19, 출하 5exe seam0/시험6·4. 종료코드와 실행 환경은 검증용 기록을 인용한다. Codex가 빌드나 제품 테스트를 재실행한 것은 아니다.
- 독립 제품 UI 로그 lock-screen38 / prelogin79 / not-prelogin28 및 이전 연결 결과 폐기·비암호 칸 무입력·바이트 단위 교체·비밀 무유출 줄을 확인했다. `lock_screen_result_typed.bmp`, `lock_screen_current_generation.bmp`를 직접 열어 입력과 로그인 성공을 구분하는 안내 및 현재 연결 결과 표시를 확인했다. 시험 서비스는 같은 사용자/세션의 격리 빌드이며 SYSTEM/실제 LogonUI가 아니다.
- A6 e2e의 `sent=0 hostRefusals=0`은 뷰어 보류 경로다. 호스트 실제 거부는 별도 lock refusal4의 근거로 구분한다. 일반 agent 최종 정책의 실제 OS 경로까지 확인했다는 의미는 아니다.

## L2-R — 입력 직전 fence가 원래 challenge 만료를 검사하지 않음 (필수, 높음)

`apps/native_poc/src/secure_unlock_service.cpp:380~384`는 `commitDeadline = p.nowMs() + kLockCredCommitMs`로 **새 20초**를 만들고, Ready 시 이 마감만 검사한다. 원래 `us.ctx.expiresMs`는 최초 sealed 접수 시(312행)에만 검사한다.

예: 발급 t=0, 만료 t=30초, sealed 접수 t=29초, child Ready t=31초. 현재 fence는 새 마감 t=49초 이내이므로 다른 조건이 같으면 Go를 허용한다. r1 L2가 요구한 원래 요청의 expiry를 입력 승인 시점까지 보존하지 못한다. 현재 CommitFence 시험(445행 이하)은 접수 직후 21초를 더해 **새 20초 마감**만 깨므로 이 반례를 잡지 못한다.

**보수:** commit 승인 마감은 원래 발급 만료와 처리 시간 상한 중 더 이른 것으로 한다(예: `min(us.ctx.expiresMs, start + kLockCredCommitMs)`). 기존 challenge와 같은 만료 경계 의미를 사용한다. 만료 시 무입력·비밀 정리·종료 결과를 유지하고 자동 재발급/자동 재입력하지 않는다. Ready/Go 구조 자체는 유지해도 된다.

**필수 반례:** 원래 만료 직전에 유효하게 sealed 접수 → 원래 만료 뒤이되 새 20초 이내에 Ready → clear/type0. 원래 만료 전 정상 입력과 처리 상한 초과 거부도 유지한다. 원래 expiry 검사를 제거한 변이가 실패해야 한다.

## L5-R — 큐가 차 있으면 보유 마감이 적용되지 않음 (필수, 높음)

`host_unlock_relay.cpp:229~240`는 predicate가 false인 `cv_.wait_for` timeout일 때만 `drop_pipe()` 한다. 마감이 이미 지났어도 `queue_`가 비어 있지 않으면 predicate가 즉시 true이고 command를 꺼낸다. 255행의 busy 분기는 계속 이전 heldCookie를 기준으로 새 요청을 거절한다. 큐가 계속 차 있는 동안 **challenge≤35초 / typed≤130초**라는 절대 상한은 보장되지 않는다. 현재 22개 시험은 정상 종료·무응답 exchange·EndSession 취소를 보지만, 대기열이 빈 순간 없이 보유 마감을 넘는 경우는 없다.

**보수:** 큐의 유무와 별개로 절대 보유 마감을 확인하고, 지난 transaction은 소유 worker가 I/O/pipe와 pending 결과를 정리한 뒤 다음 command를 처리한다. 남은 보유 시간보다 긴 exchange로 보유 상한을 다시 넘기지 않도록 예산을 연결한다. 다른 쿠키의 요청·capability·반복 status가 마감을 연장해서는 안 된다. 미확인 상태를 성공으로 바꾸거나 이전 transaction을 새 pipe에서 이어가지 않는다.

**필수 반례:** 짧게 주입한 동일 제품 보유 마감 + 계속 nonempty인 큐(다른 쿠키 busy 요청/상태·probe 포함)에서도 마감에 pipe 반환 → 다음 Host 요청 통과. challenge/typed 양쪽과 조기 terminal/EndSession 반환을 확인한다. busy 큐가 마감 검사를 우회하는 변이는 실패해야 한다. 실제 130초 반복 시험을 여러 번 할 필요는 없다.

## UX-R — A⑧-b 직렬화는 허용, “오류 0” 안내는 반려 (필수, 중간)

**A⑧-a 유휴 무점유 / A⑧-b 실제 transaction 중 제한된 직렬화 구분은 OK**다. 단 위 L5-R 보수 전에는 절대 상한을 입증했다고 적지 않는다.

`host_unlock_relay.cpp:255~269`는 challenge busy를 일반 InternalError12로 보내며 별도 오류 필드가 없다. sealed busy만 ERROR_BUSY170이 있다. `viewer_unlock.cpp:586,610~611`의 “단계12, 오류0” / “입력하지 못했습니다 (오류0)”는 사용자가 무엇을 기다리고 언제 재시도할지 알 수 없어 이 UX로 승인하지 않는다.

**보수:** challenge와 sealed에 실제 busy를 식별할 수 있는 명시적 결과를 유지해 잠금/로그인 버튼 및 기존 disconnected unlock 경로에서 같은 원인·재시도 안내를 표시한다. 예: “다른 Windows 로그인 요청을 처리 중입니다. 해당 요청이 끝나거나 취소된 뒤 다시 시도하세요.” 일반 InternalError 전체를 busy로 해석하지 않는다. 패킷 크기를 유지하는 별도 stage 추가 방식은 이 범위에서 허용한다(현재13~15 뒤 빈16 등의 번호는 검증용이 충돌 확인 후 확정). purpose/reserved에 오류를 숨기거나 비밀을 자동 재사용/재전송하지 않는다. 실제 0과170 수치만 바꾸는 것으로 끝내지 않는다.

**필수 행동 근거:** 격리 제품 UI에서 A 실제 pending → B 요청 → 정확한 busy 안내·B 무입력 → A 종료/취소 뒤 B의 수동 재시도 성공. challenge 거부와 sealed 거부, 일반 오류가 busy로 오표시되지 않는 대조를 포함한다. 기존 A transaction의 결과가 B 결과에 섞이지 않아야 한다.

## L1-R — EndSession 뒤 늦은 challenge/status가 cache를 다시 만듦 (필수, 중간)

`EndSession:492`는 map을 지우고 active I/O를 취소하지만, 이미 꺼낸 challenge command는 취소 후에도 361행 `challengeResults_[key] = out`를 무조건 실행한다. Status 역시 385행에 `statusAnswered_[key]`를 무조건 만든다. sealed 결과에 있는 “아직 존재하는 transaction만 publish” 방어와 다르다. 종료와 교환 완료가 겹치면 정리된 세션의 map이 다시 생긴다. 새 cookie로 옛 성공이 전달된다는 판정은 아니지만 L1의 종료 정리 계약이 남아 있고, 연결 churn 시 잔존이 누적될 수 있다.

**보수/시험:** command 접수 시의 살아 있는 session/transaction 세대를 완료 publish 시 다시 확인한다. 종료된 소유자의 challenge/status/실패·busy 완료도 cache를 재생성하지 않으며, 다른 살아 있는 cookie는 지우지 않는다. 이미 끝난 cookie를 매번 영구 tombstone에 쌓는 방식으로 누수를 옮기지 않는다. EndSession을 challenge 응답 대기 중 및 status 응답 대기 중에 끼운 시험에서 map 잔존0·다른 연결 정상·다음 Host 통과를 확인한다. 기존 sealed 취소 반례도 유지한다.

## 다음 검증 범위와 남은 실기

- 검증용이 위 네 항목을 **한 유효 보수 지시**로 기존 t-rs9nhsdv/작업용1에 연결한다. 변경된 service/relay/결과 UI의 반례·대조와 해당 gate를 독립 확인하고, 고정 후보에서 영향 있는 lock-screen/prelogin/disconnected 결과 UI를 확인한다. 이미 통과한 무관 GPU·전체 회귀를 새로 전량 반복할 필요는 없다. 제품 수정과 함께 history·계획 상태·Git MCP 커밋을 같은 task에 취합한다.
- r1의 L3/L4/L6는 이번 소스·격리 근거 수준에서 보수 확인. L1/L2/L5는 위 잔여 부분 때문에 아직 닫지 않는다. 작업용 고아 LifecycleTest2개는 소유 작업용이 경로/생성 시각을 고정해 정리하고 결과만 연결한다. Codex가 이름만 보고 타 프로세스를 종료하지 않는다.
- 실제 Session0 SYSTEM 서비스→콘솔 SYSTEM child, agent 최종 차단, 실제 LogonUI 암호 교체·Enter·해제, 재부팅 로그인 전, UAC, APK A~D는 계속 **미검증**이다. 자동 검증 보수 이후에도 상태는 테스트 후보·실기 대기이며 실기 성공/일반 릴리스 완료가 아니다. S1 간헐은 미해결로 유지하고 영향 분리를 계속한다.
- APK0.2.30 잠금 화면 암호 입력 차단 영향과 S5 미결을 유지한다. 이번 검토로 APK sealed 대응·버전 인상·서명·게시·설치·push를 추가 승인하지 않는다.

Codex 변경은 이 미추적 검토서/인계 메모뿐이다. 제품 변경·빌드·실서비스 조작·commit·push 없음.
