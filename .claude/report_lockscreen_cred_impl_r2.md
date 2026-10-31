# report lockscreen-cred-impl r2 — Codex 550a944 NEEDS_CHANGES L1~L6 보수 (작업용1 remote#rqxbj3jb, 2026-10-22)

task t-rs9nhsdv / rev r2 (정본 `.claude/task_lockscreen_cred_impl_r1.md` 'r2 보수', 계약 `codex_lockcred_impl_r1.md` L1~L6) / kind=implement-report / reply_to=r2 보수 지시.

## 고정 후보
- worktree `C:/work/gnlink-wt/fix-lockscreen-cred`, 브랜치 `fix/lockscreen-cred`, 기준 main `5063cd9`, HEAD **`8ecaa8f`** (워킹트리 깨끗). 버전 인상·서명·게시·push 없음.
- r2 커밋: 제품 **`959bf45`** · 시험 **`63d3230`** · gate+문서 **`8ecaa8f`** (r1 커밋은 r1 보고 그대로, `550a944` 위에 쌓음).
- 출하 구성 빌드 `C:/work/gnlink-wt/fix-lockscreen-cred-build` (Release, 05:13 빌드; 959bf45 의 제품 파일 mtime 은 모두 그 이전) sha256:
  - GNLinkInputService.exe `1827319CC314CEBF776B0C81F0FA61F63AD38A440D298D8CC6B704010B8A41BE`
  - GNLinkStream.exe `6E0902D7E553FF62D62683B6C49BE253AE93C3E1DE852E372BE1B363A8E3DDAB`
  - GNLinkViewer.exe `4C7B66947868F2E39F0600B9F11E2F1EEDA8CDBE4B5A6947220026678E2A62FC`
  - (installer_payload 사본 동일 해시.) UI e2e 는 같은 소스의 in-repo 빌드 `build-lc` — 산출물 공유 안 함.

## L1~L6 (층: 논리=단위·격리 / 제품UI=실제 뷰어 창·제품 코드, 시험 빌드 seam / OS=실기)
- **L1 공용 relay 세션 격리** — 결과·capability 캐시·멱등을 `(control cookie, requestId)` TxKey 로; `SecureUnlockQuery.clientSessionCookie`(24 B) 추가, 서비스 status/terminal/중복 검사도 cookie 대조, challenge 응답은 cookie·purpose echo 필수. 다른 세션이 파이프를 쥔 동안 새 요청은 즉시 busy(상대 문맥 덮어쓰기 없음). `EndSession(cookie)` 이 그 세션 맵·큐 정리 + Release 를 앞에 넣고 진행 중 교환 취소. 논리: relay 22(교차 세션 같은 requestId=2 · busy · EndSession), lockcred 52 의 cookie 사례, gate fixture echo. 변이 R1(TxKey=requestId 만) FAIL.
- **L2 입력 직전 커밋 fence** — one-shot agent: Job → 대상 확인 → Ready → 서비스 `stillValid()`(서비스 실행·커밋 마감 20 s·topology·caller 생존·caller 세션·console·잠김·활성·발급 시 계정) → Go(allow/deny) → 입력 → Answer. Deny/무응답이면 **무입력** RejectedStaleTopology/NoAnswer. 논리: channel 15(실제 파이프·자식 프로세스, allow/deny), lockcred CommitFence 사례. 변이 R2(fence 항상 허용) FAIL.
- **L3 거부 위치** — 서비스는 전달과 같은 해석(`logon_only_target_session`/`target_session`)으로 **실제 대상 세션** 기준 거부; 일반 입력 agent 는 주입 직전 자기 세션 + 붙은 데스크톱으로 재거부(`locked_plaintext_refused`, 첫 16건 후 200건마다 로그). 키 up·nav·포인터 통과. 논리: lock refusal 4(신규 `secure_input_lock_refusal_test`). 변이 R3(서비스 검사 제거) FAIL. agent 자기 세션 경로는 OS 미검증(아래).
- **L4 암호 칸 identity·교체** — identity = pid(상위 32) + 창 ^ UIA RuntimeId FNV 해시; `ReplaceWithUnicode(expectedIdentity,…)`: 같은 요소 확인 → writable ValuePattern clear(실패·읽기 전용·UIA 실패는 `kNotTypedError=21`→TargetNotReady, “pattern 없음”과 구분) / pattern 없고 modifier 미보유일 때만 같은 SendInput 내 Ctrl+A → clear 뒤 같은 요소 재확인 → 입력. 부분 SendInput 은 `PendingReleases` 로 자기 down 의 up 만. 논리: logon_ui 21(표), **identity UI 8**(실제 창 2개 PasswordField 구분, REMOTE60_ALLOW_UI_TEST). 변이 R4(요소 없는 identity)·R5(PendingReleases 없음) FAIL.
- **L5 마감·취소·세대** — relay 교환 overlapped + 마감(challenge 35 s 보유 / 입력된 자격 증명 130 s) + 취소 이벤트(Stop·해당 세션 EndSession); 실패 시 파이프 반환·capability 무효화; capability 는 서버 pid 결속·120 s 신선·60 s 갱신. 뷰어 결과 메시지 wparam = stage | role<<16 | controlRunGen<<32, WndProc 가 다른 gen 버림; 새 `Run()` 시작에 다른 gen 의 대기 비밀번호 폐기. 논리: relay stall/cancel/caps 사례. 제품UI: e2e lock-screen 의 L5 stale-gen 단계. 변이 R6(교환 마감 없음)·R7(창 gen 검사 없음) FAIL(R7 은 watcher 로 hang 대신 FAIL).
- **L6 gate** — `automation/gnlink_check_fixed_server.py` TEST_MARKERS 에 `GNLINK_SIS_TEST_`, self-test 에 GNLinkInputService.exe 단독 ASCII·UTF-16 부정 대조. gate/self-test PASS(부정 대조가 실제로 걸림).

## 시험 결과 (exit 0 + 마지막 요약 줄 확인)
- 제품UI e2e (`remote60_viewer_prelogin_login_e2e_test`, 사용자 확인 “이 PC 사용해라”, SLOT_REQUEST CreateNew → **삭제 완료**, 설치본·실서비스·실세션 무조작, 콘솔 세션): lock-screen **38/38**, prelogin 79, not-prelogin 28, android-session 43, session-ends 41, capture-fallback 34, foreign-peer 37. 산출 `C:\work\gnlink-wt\lockcred-scratch\e2e_r2\*.out`·`cap_*`.
- 단위: lockcred 52 · channel 15 · relay 22 · logon_ui 21 · identity UI 8 · lock refusal 4 · secure_pipe_client_gate 19/9 PASS. 최종 비-UI sweep 52 중 51 + gate fixture 보수 뒤 나머지 1 PASS.
- 제외·미해결: `ka_relay_client_test` 기준 5063cd9 에서도 동일 실패. **S1 client_session_role_park 간헐 미해결** (브랜치 2/39, 기준 0/35; 경로는 diff 밖이나 1회 통과로 닫지 않음).
- 변이 R1~R7 각 해당 시험 FAIL → 원복 PASS (r1 의 M1~M7·E1/E2 도 유지).

## 미검증 (이 PC 에서 볼 수 없음 → 현장 묶음)
- 실제 Session0 서비스 → 콘솔 세션 SYSTEM agent 경계(같은 세션 asInvoker PASS 는 증거 아님), agent 자기 세션 마지막 확인 경로, 콘솔 폴백, 실제 LogonUI·Enter 제출·해제, 재부팅 로그인 전 화면, UAC, APK.
- **APK 0.2.30 호환성 영향(무변경)**: 새 Host 의 잠금 화면에서 휴대폰 글자 입력은 거부됨(화면·탭은 됨) — history 에 명시.

## 현장 묶음 (사용자 1회, 검증용 경유) — `C:\work\gnlink-wt\lockcred-scratch\field_bundle.md`
r1 묶음 A①~⑦·B·C·D 그대로, 핵심: **A⑥ Session0→console 경계**: `secure_input.log` 에 `lock-cred: the pipe's client was refused` 없음 + `lock-cred requestId=… typed session=<콘솔>` 있음, `--lockcred-agent <32hex>` 가 세션 1·SYSTEM 으로 떴다 사라짐(명령줄에 id 만).
**A⑧ 갱신(요청대로 구분)**:
- ⑧-a 유휴 보유 없음: 한쪽 세션이 잠긴 채 가만히 있으면(진행 요청 없음) 다른 호스트의 Ctrl+Alt+U·'Windows 로그인' 이 즉시 통과해야 함 — 대기가 생기면 결함.
- ⑧-b 진행 중 트랜잭션은 직렬화(제한 있음): 한쪽이 실제 요청 중이면 다른 쪽은 busy 로 곧바로 거절될 수 있음(문맥 덮어쓰기 없음). 보유는 challenge ≤35 s / 입력된 자격 증명 ≤130 s, 종료 응답·세션 종료 시 즉시 해제 → 그 뒤 재시도 통과가 정상, 한도 초과 지속이면 결함. 보이는 모습(전용 busy 문구 없음 — 거절은 InternalError=12 로 옴): 잠금 화면/로그인 전 'Windows 로그인' 은 결과 창 “원격 PC가 입력하지 못했습니다 (오류 0).” (challenge 단계 거절; seal 단계에서 걸리면 “(오류 170)”=ERROR_BUSY), 연결 끊긴 세션 Ctrl+Alt+U 는 “원격 PC가 잠금을 해제하지 못했습니다 (단계 12, 오류 0)”; 뷰어 로그 `challenge rejected: error`.

다음 담당: 검증용 — 고정 후보 HEAD `8ecaa8f` 로 UI e2e 독립 실행 → Codex 검토.
