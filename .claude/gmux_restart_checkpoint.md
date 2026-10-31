# GMux 재시작 체크포인트 — 2026-09-07 (Claude remote#0gk0hr8u, task t-xox45oo5)

## ① 현재 상태 (git)
- 브랜치 `refactor/viewer-split`, **HEAD = 1287883** (origin/refactor/viewer-split 대비 28 커밋 앞, push 보류).
- 추적 파일 미커밋 변경: **없음**(clean). 미추적은 `.claude/` 뿐(작업 스크립트·로그·보고서; 저장소 밖 취급).
- 즉 되돌릴 diff/patch 없음. `product_version.hpp` 는 0.2.102(ded9060 에서 커밋됨).
- 설치본: `dist/GNLinkSetup-0.2.102.exe` (3,466,240 B, 1287883 트리 전체 Release 빌드 산출, 0.2.102 임베드 확인; `dist/` 는 gitignore). **미설치**, 서버 미배포.
- 이 회차 커밋(전부 Git MCP): f911845·e997b97(P10) / 03b0d13·dbb7fc0(P9) / 1607404(P11) / 23b0f3c(test) / a9114ff(0.2.101) / 927b9f4(P11 gate·sender) / 6a9aa32(P9 dispatch) / c3c3d76(viewer) / ded9060(0.2.102) / **1287883(sender 허용 판정 지점·단일 dispatch·S15)**.

## ② 완료 / 진행 / 미검증
- 완료(코드+테스트 PASS): Codex seq 1062/1064 필수 3건은 **1287883 에 구현됨** — (a) sender 허용 판정을 pacing 뒤 첫 datagram 직전으로, active fence 는 현재 epoch 만 통과(old·미태그·미래 폐기), 예외 정의 명시, 테스트 [A]/[B]/[B'](dequeue 뒤 첫 datagram 직전 정지→flush→재개)/[C]/[D]; (b) `CaptureState::InputTargetSnapshot`(version·mode·HMONITOR·gen, lock) + `secure_dispatch`(주입 포트) + 브로커 `SendInputEventWithRect/TextWithRect`; (c) secure 분기·재probe·default broker fallback·텍스트 4곳 모두 공통 helper 경유, 가짜 포트 테스트 [2c] 7케이스; (d) recovery S15 = 제품 SenderState 스레드 경유 출력 스트림을 뷰어가 수락(Congested 0·요청 0).
- 검증 수치: `remote60_host_sender_epoch_test` PASS×2, `host_input_target_rect_test` PASS, `viewer_udp_recovery_test` 단독 5회 PASS(run11~15). 전체 sweep(0.2.102b, 1287883 빌드) 40종 = 38 PASS + 2 FAIL: `udp_control_e2e_test`(설치 host 가 43000 점유, 환경 영향 추정)·`viewer_udp_recovery_test`(sweep 중 1회 간헐 FAIL, 시나리오 미확인 → 실패 로그 보존하도록 러너 수정 후 재sweep 진행 중).
- 진행 중(중단 지점): 실패 로그 보존 sweep `.claude/edits/run_all_tests.sh` 백그라운드 실행 중 → 결과 `.claude/test_sweep_0.2.102c.txt`, 실패 시 `.claude/test_sweep_fail_<name>.log`. 체크포인트 시점 e2e 테스트 실행 중(약 50s), 이후 recovery 등 10종 남음(~2분). 종료해도 무해(테스트 프로세스만; 라이브 GNLink 무관). 빌드 프로세스 없음.
- 미완료(보류): ▶ history #408 검증 줄 기입 + `.claude/edits/step32_release3_docs.py`(history #408 append·구현계획 P9/P11 문구) + docs-only 릴리스 커밋(`git_add_416.json`/`git_commit_416.json`, 메시지 "0.2.102 re-cut") — **새 3역할 절차(검증용 Claude OK) 전에는 실행하지 말 것**. ▶ 보고서 `.claude/viewer_stabilization_followup_report.md` 는 1287883 반영까지 갱신됨(검증 수치 최신화 필요). ▶ Codex 에 최종 완료 요청 금지(검증용 Claude OK 후).
- 미검증(사용자 실기, RDP 종료 상태): 호스트 재시작·재로그인 뒤 NAS host.log/`log_upload.diag`, 회사 클라 재로그인 뒤 viewer.log, UAC 2회(`secure_input.log` target==virt·mapped==in 두 번째도 기록, 호스트 `epoch-gate`/`sender dropped`/`secure-input target rect … reason=dispatch`, 뷰어 Congested 0), 정적 타이핑·영상 회귀, 다중 모니터 장비 없음.

## ②-b 체크포인트 직후 확인된 결함 (미수정, 승인 대기)
- 재sweep(`.claude/test_sweep_0.2.102c.txt`, 실패 로그 `.claude/test_sweep_fail_remote60_viewer_udp_recovery_test.log`): 40종 = 38 PASS + 2 FAIL — e2e(환경 추정) + **recovery S13 shape(a) 간헐 FAIL**(Congested 진입 seq 29, decodeQueueLagUs 782,291, recvGap 1.3ms, heldResume=1, 요청 1).
- 원인(코드 확인): c3c3d76 이 `FrameGate::note_reference_sync` 첫 줄에 `gate.resumeAnchorPending = false` 를 넣었는데, 실제 수신기(`viewer_video_receiver_frame.cpp:260`)는 이 함수를 **모든 decoded 프레임**에 대해 호출한다 → 보유 재개 프레임 자신이 decode 되는 순간 pending 이 풀려 뒤따르는 신선 burst 가 옛 floor 로 판정된다. 단독 실행에서 통과한 것은 present 스레드가 burst 사이에 presentedCap 을 전진시킨 타이밍 덕분(부하 시 실패). frame_gate 단위테스트는 key 에만 `decoded()` 를 호출해 잡지 못했다.
- 판정(Codex 확인): **미해결 제품 회귀**로 유지 — 단독 실행 PASS(run11~15)로 상쇄하지 않는다. c3c3d76 의 뷰어 pending 규칙은 실제 수신 경로에서 보유 프레임 decode 직후 무효화되므로 현재 릴리스 후보(1287883/0.2.102 설치본)는 이 결함을 포함한다.
- 제안 수정(검증용 Claude 계획 검토 입력): (1) `note_reference_sync` 의 pending 클리어를 `if (in.keyFrame)` 블록 안으로 이동(키프레임 decode 만 종료; 비키 decode 는 유지). (2) 회귀: 실제 수신기 호출 순서를 그대로 모사 — 프레임마다 admit → decode → `note_reference_sync`(비키 포함) → 다음 admit, 그리고 빈 출력(`note_decode_empty`)·decode 실패 경로까지 포함해 "보유 재개 프레임이 비키로 decode/빈 출력으로 수락된 뒤에도 pending 유지, 첫 신선 프레임이 anchor" 를 검사; frame_gate 단위테스트 Rig 의 `decoded()` 를 비키에도 호출하는 변형과, recovery S13 을 sweep 부하 조건(직전 테스트 뒤 연속 실행)에서 재확인. **검증용 Claude/Codex 승인 전 코드 변경 안 함.**

## ②-c GMux 대화 복원 관련 발견 (Codex 읽기전용 조사, 2026-09-07)
- 사용자 보고: GMux 재시작 후 Claude 대화는 복원되지만 Codex 대화는 복원되지 않는다.
- 불일치(코드): `C:/work/GMux/servers/agent-host/src/history.ts:235-259` `codexLineToRow` 는 `event_msg`(user_message/agent_message) 줄만 행으로 만든다. 현재 Codex 원본 롤아웃 `C:/Users/shota/.codex/sessions/2026/09/05/rollout-2026-09-05T12-31-56-01a06f9f-7701-72a1-b4b0-153223697fb0.jsonl`(약 7.95MB)에는 `response_item` message(user 73 / assistant 121)만 있고 해당 `event_msg` user/agent 는 0개 → 이 소스의 UI history reader 는 빈 결과를 낸다. **모델 thread resume 성공 여부와 UI transcript replay 는 별개**다(resume 는 되어도 화면 이력은 비어 보일 수 있음).
- 원본 보존: 위 롤아웃 파일은 그대로 보존됨(수정·이동 없음). Codex 재개 ID 01a06f9f-7701-72a1-b4b0-153223697fb0.
- 조치 상태: GMux 제품 코드(history.ts)는 **수정하지 않음** — 새 3역할 절차/대상 확정 후 별도 작업으로 진행. GNLink 후속 수정(②-b 포함)도 체크포인트 상태 유지.

## ③ 재시작 후 다음 조치
1. `git status` 로 clean 확인(추적 변경 없어야 함), HEAD 1287883.
2. sweep 결과 확인: `.claude/test_sweep_0.2.102c.txt` + `.claude/test_sweep_fail_*.log`(recovery 간헐 실패 시나리오 식별). 필요 시 `remote60_viewer_udp_recovery_test` 단독 재실행.
3. 새 3역할 절차(Codex 설계 → 검증용 Claude 계획 확정 → 작업용 Claude 구현 → 검증용 Claude OK → Codex 최종) 적용: 이 체크포인트·`.claude/viewer_stabilization_followup_plan.md`·`_p11_v2.md`·`_report.md`·history #401~#407·1287883 diff 를 검증용 세션에 넘김. 검증용 OK 후에만 #408 기입·docs 릴리스 커밋·최종 보고.
4. 정책 문서(AGENTS.md/CLAUDE.md) 3역할 절차 갱신은 Codex 가 별도 task 로 보낼 예정 — 아직 미지시, 역할/세션 배치 확인 질문 대기 중.
5. 설치·push·서버 배포는 계속 보류.

## ④ 재개 방법
- 이 세션(Claude CLI) 실제 resume UUID: **dd28ca05-bad5-4493-b6fc-fdf1362cce8b** (트랜스크립트 `C:\Users\shota\.claude\projects\D--remote-remote\dd28ca05-bad5-4493-b6fc-fdf1362cce8b.jsonl`). A2A 짧은 ID 는 재시작마다 바뀜: 재시작 전 `remote#0gk0hr8u` → **재시작 후 `remote#fjq7vvgr`(displayName 작업용claude, a2a_whoami 확인)**. 새 절차 주소: Codex `remote#ktuqt7y9`, 검증용 Claude `remote#1528xbin`.
  - 재개: `cd D:\remote\remote` 후 `claude --resume dd28ca05-bad5-4493-b6fc-fdf1362cce8b` (또는 `claude -r` 로 목록에서 선택).
- Codex(감독) 재개 ID: 01a06f9f-7701-72a1-b4b0-153223697fb0 (A2A remote#wslm89zu).
- 재개 직후 `a2a_inbox` 는 Claude 쪽에서 신뢰 불가(메모리 참고) — Codex 가 필요한 내용을 다시 pin 으로 보내거나 `.claude/a2a_codex_reply.md` 로 전달.

## ⑤ 2026-09-07 갱신 (작업용 remote#fjq7vvgr)
- HEAD acd8233 chore(release) 0.2.103 후보 (← 1005ebe S13 변형 R ← d980ff0 ← 40b836e). 트리 clean. push 없음.
- 설치본 dist/GNLinkSetup-0.2.103.exe 3,466,240 B sha256 c263758b… (1회 생성, 재생성 금지 조건: 검사에서 변경 요구 시만).
- 릴리스 sweep 38+2: e2e 환경, recovery S12 하네스 간헐(범위 밖 발견, 미수정). S13 7/7 PASS.
- 상태: task t-5ahw0t5x working, 검증용 remote#1528xbin 검사 요청 전송(seq 1116/1117). 전문 .claude/s13_release_report_to_verifier.md. 대기 중 — Codex 최종 확인 요청은 검증용 OK 뒤에만.
- (⑤ 추가) 검증용 우편함 보류분 2통(seq 1114 계열) 수신 → e08de14 test(viewer) T-P11d 문구 정정(#409 보완). 순서 불일치(acd8233 → e08de14)와 설치본 식별(1005ebe+bump 트리, 제품 소스 == e08de14) 고지, 재빌드는 검증용 회신 전 보류. S12 (a)/(b) 판단 대기.
- (⑤ 추가 2) 검증용 최종 검사 = NEEDS_CHANGES 경미 1건(T-P11d 문구, e08de14 로 기반영) + 발견사항 F-22/#411 요구 → 94f5d74 docs(viewer). 테스트 exe 만 재빌드·1회 PASS(.claude/tp11d_frame_gate_e08de14.log), 제품 바이너리·설치본 불변(tp11d_product_bins_before/after.txt). 재검사 요청 전송. HEAD 94f5d74. S12 = Codex 범위 판단 대기.
- (⑤ 추가 3) 검증용 seq 1118 기준: F-22 단정 두 지점(1294·1302) 정정 → fefccfd docs(viewer). HEAD fefccfd. 회신 전송(seq 1123 계열). 검증용 "구현 OK" 대기, S12 (b) 는 Codex 확정 대기.
- (⑤ 추가 4) Codex 가 S12 하네스 수정을 task 범위에 포함(검증용 seq 1126 + 보충 3건) → edb1ec9 test(viewer): send_key_and_wait_on_wire(강제 키 1회, wire 관측, 상한 30회/1500ms), 정확한 IDR seq 의 key publish 단정(published_key), corrupted_keys==1 유지·끝 단정, S12 order 로그. 단독 5/5 PASS(비동기 형상 2/5 관측), sweep 38+2(e2e 환경, gdi 환경). 제품 바이너리·설치본 불변. HEAD edb1ec9. 검증용 재검사 요청 전송. F-22 수정됨, #412.
- (⑤ 추가 5) 검증용 재검사 통과 → Codex 에 "구현 OK" 송부됨(제품 1005ebe·acd8233 / 테스트 e08de14·edb1ec9 / 문서 4건 / 설치본 c263758b…). GDI 캡처 테스트는 환경 변화로 미검증 처리. Codex 최종 확인 대기 — 그때까지 변경·재빌드·설치 금지, task working 유지.
- (⑥ 종결) Codex 최종 확인 완료 → t-5ahw0t5x completed 로 정리(완료 텍스트에 커밋 목록·설치본 식별·미검증 항목 명시). HEAD edb1ec9 clean. 설치·push·서버 배포는 사용자 지시 후. 다음 단계: 사용자 승인 시 0.2.103 설치 → 실기(UAC 2회·업로드 재로그인·정적 타이핑·원점 변경) → NAS 로그 검증.

## ⑦ 2026-09-08 (작업용 remote#nt5lxb96, 새 세션)
- t-2occguzn 호스트 pktmon 캡처 1차 회차(11:24:50~11:31:41, 사용자 UAC 승인) → stop.request 정상 종료·etl2pcap·NAS 11:27 로그 대조 → report_1127.md / capture_coverage.md(검증용 정정 반영: 패리티 충돌, gen @48, NDIS 하한, −375 ms). 2차 helper Start/Stop-GnlinkCapture.ps1 준비만(검증용 재검토 통과), 실행 금지.
- t-gmwzjgy1 docs(field) 커밋 fd1f220 (history #415, 구현계획 0.2.103 실기 상태, 작업목록 1.0.1). 검증용 검사 → Codex 최종 대기. HEAD fd1f220 clean.
- t-741x721k 는 사용자 취소(재개 금지), .claude/field-20260908/ 스냅샷 보존.
- (⑦ 추가) 4a92d5e docs(field): field_test_2026-09-08_0.2.103.md 9절 "11:27 캡처 후속", history #415 보정 1줄, 작업목록 1.0.1 제거(Codex 3파일 배치). HEAD 4a92d5e clean. 검증용 검사 대기(t-gmwzjgy1·t-2occguzn working).
- (⑦ 추가 2) abd136e docs(field): 작업목록 1.0.1 복원(검증용 정정), #415 변경 파일 줄에 field_test 9절 추가. HEAD abd136e clean. 검증용 검사 → Codex 최종 대기.
- (⑦ 종결) Codex 최종 확인 → t-2occguzn·t-gmwzjgy1 completed(seq 1280/1281). HEAD abd136e clean. 제품 수정·2차 캡처·설치·push 는 승인 범위 밖 — 다음 수정 계획은 Codex 별도 task. 2차 helper 는 .claude/packet-capture-20260908/ 에 준비만.
