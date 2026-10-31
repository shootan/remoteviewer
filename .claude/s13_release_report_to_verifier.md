# 작업용 → 검증용 검사 요청 (task t-5ahw0t5x, 2026-09-07)

## 1. 커밋 (Git MCP, 모두 refactor/viewer-split, push 없음)
- 40b836e docs(policy): AGENTS.md "Agent Role Separation" 3역할 절차 재작성 + CLAUDE.md 절 추가, docs/history.md #408
- d980ff0 docs(policy): 검증용 문장("계획 변경이 필요하다고 판단한 부분은 Codex 의 수정·확정 전에는 작업용에게 위임하지 않는다. 변경 없이 OK 한 독립 범위는 바로 위임할 수 있다.") AGENTS.md·CLAUDE.md 원문 반영, #408 추가
- 1005ebe fix(viewer): 변형 R — viewer_frame_gate.cpp note_reference_sync 의 `gate.resumeAnchorPending = false;` 삭제(주석 정정), viewer_frame_gate_state.hpp 주석, viewer_frame_gate_test.cpp(수신기 순서 헬퍼 feedDecoded/decodedEmpty/decodeFailed, T-P11a/b 전환, 신규 T-P11g/h/i/j), viewer_udp_recovery_test.cpp(S13 형상 (a′) pinPresentAnchor), docs/history.md #409, docs/구현계획.md P11 문구
- acd8233 chore(release): product_version.hpp 0.2.103, docs/history.md #410, docs/작업목록.md 1.0/1.1/1.3
- 작업 트리 clean(.claude/ 미추적 제외). 폐기 지시된 step32_release3_docs.py / git_add_416 / git_commit_416 은 실행하지 않음.

## 2. 수정 전 FAIL / 수정 후 PASS 로그
- 수정 전(1287883 코드): .claude/s13_prefix_frame_gate_1287883.log — FAIL 36건(T-P11a/g/h/i/j·b)
- 수정 전(1287883 코드): .claude/s13_prefix_recovery_1287883.log — S13 (a′) transitions +3·요청 +1 (결정적 재현), 형상 0/1 PASS(타이밍)
- 수정 후: .claude/s13_postfix_frame_gate.log — PASS
- 수정 후 recovery 단독 ×3: .claude/s13_postfix_recovery_run1.log / run2 / run3 — PASS(세 형상 transitions 0·요청 0)
- 참고(처음 재현): .claude/test_sweep_0.2.102c.txt + .claude/test_sweep_fail_remote60_viewer_udp_recovery_test.log

## 3. 전체 sweep 결과 파일 (40종 각각)
- .claude/s13_postfix_sweep1.txt : 39 PASS + 1 FAIL — FAIL = udp_control_e2e_test(환경). 실패 로그 .claude/s13_postfix_sweep1_fail_remote60_udp_control_e2e_test.log
- .claude/s13_postfix_sweep2.txt : 39 PASS + 1 FAIL — 동일 e2e(환경). .claude/s13_postfix_sweep2_fail_remote60_udp_control_e2e_test.log
- .claude/release_0.2.103_sweep.txt (릴리스 바이너리, 1005ebe+버전 bump) : 38 PASS + 2 FAIL
  - udp_control_e2e_test — 환경: `netstat -ano | findstr 43000` → UDP 0.0.0.0:43000 = PID 13856 GNLinkStream.exe(설치된 라이브 호스트). 중지하지 않음.
  - viewer_udp_recovery_test — **S12** FAIL (S13 아님). .claude/release_0.2.103_sweep_fail_remote60_viewer_udp_recovery_test.log:162
    `viewer_udp_recovery_test.cpp:1294: host.corrupted_keys() == 1 -- a zeroed IDR went out`.
    원인(코드 확인): S12 는 `host.CorruptNextKey(); host.SendFrame(true,…); CHECK(corrupted_keys()==1)` 순서로 SendFrame 직후 검사하는데, 비동기 인코더 호출에서는 강제 IDR 이 그 호출에서 안 나올 수 있어 corrupt 는 뒤 pump 에서 일어남 → 하네스 타이밍. 0.2.100 부터 있던 시나리오이며 1005ebe 는 viewer_frame_gate 만 바꿈(FakeHost·S12 미접촉). 이번 계획 범위 밖이라 **수정하지 않고 발견사항으로 보고**(#410 에 기록). 같은 run 에서 S13 세 형상·S14·S15 는 PASS.
    특성 파악용 릴리스 바이너리 단독 재실행 .claude/release_0.2.103_recovery_standalone.log — PASS. (상쇄 근거로 쓰지 않음.)
- S13 은 수정 후 모든 실행에서 PASS: 단독 3 + sweep 2 + 릴리스 sweep 1 + 릴리스 단독 1 = 7/7.

## 4. 실행·재시도 횟수
- frame_gate: 수정 전 1, 수정 후 단독 1 + sweep 3(포함 릴리스) — 재시도 없음
- recovery: 수정 전 1, 수정 후 단독 3, sweep 3(그중 릴리스 sweep S12 FAIL 1), 릴리스 단독 1(특성 파악) — FAIL 을 덮기 위한 재실행 없음
- 릴리스 빌드 1회(.claude/release_build_0.2.103.log BUILD_EXIT=0, 0 errors). 설치본 1회 생성, 재생성 안 함.
- qwinsta: 테스트 전 콘솔 세션 확인(rdp-tcp Active 없음).

## 5. 설치본
- dist/GNLinkSetup-0.2.103.exe — 3,466,240 bytes
- sha256 c263758bba03c8c2f3a3e2e9e6eeae96649a8739b96526e352e4e8b0e4268daf
- 임베드 버전 0.2.103(0.2.102 문자열 없음)
- 빌드 트리 = 1005ebe + product_version.hpp bump (= acd8233 의 제품 소스와 동일; acd8233 은 그 외 docs 만)
- 0.2.102 설치본·로그는 보존, "결함 포함·미승인" 표기(#410). 설치·push·서버 배포 없음.

## 6. 보존 회귀
- host_sender_epoch_test [A]/[B]/[B']/[C]/[D], host_input_target_rect_test, host_encode_epoch_test — 모든 sweep PASS
- frame_gate T-P11b(전부 held → 예산 뒤 트립)·T-P11c PASS, recovery S14/S15 PASS

## 7. 미검증
- 사용자 실기: 호스트 재시작·재로그인 뒤 NAS 로그(401 복구), 회사 클라 재로그인, UAC 2회(secure_input.log target==virt·mapped==in, host epoch-gate/sender dropped, 첫 프레임 auEpoch==curEpoch, viewer Congested 0), 정적 타이핑·영상 회귀, 원점 변경 배치
- 다중 모니터 장비 없음. e2e 는 라이브 호스트 점유로 미검증.

## 8. 판단 요청
- (a) S12 간헐 하네스 FAIL 을 범위 밖 발견사항으로 두고 0.2.103 후보를 검사 대상으로 진행할지, (b) NEEDS_CHANGES 로 S12 하네스 수정(예: 키가 나갈 때까지 SendFrame 반복 또는 pump 뒤 검사)을 같은 task 에 넣을지. (b) 면 제품 소스 무변경이라 설치본 재생성은 불필요하다고 봄(테스트 exe 만 재빌드).
- task 상태: 버스에 completed 로 표시돼 있어 working 으로 되돌림(검증용 OK·Codex 최종 확인 전).
