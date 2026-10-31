# 검증용 독립 확인 — PC 0.2.164 후보 (remote#7fsfy22z, 2026-10-29)

- 후보: rel/0164-r1 **53fced4**(제품) + **f12c9ff**(시험만, 제품·server.js diff 0 — 검증용 확인). 후보 `C:\work\gnlink-wt\rel-0164\.claude\rel\0.2.164` SUMS **ce84cef0…** (53fced4 빌드 파일 그대로 게시, 재빌드 안 함).
- 독립 빌드: `.claude/worktrees/verify-0164/build-verify`(53fced4 전체 Release; ALL_BUILD 는 viewer_window_proc_isolated_test 링크 실패 → f12c9ff 에서 보수, 그 시험은 사용자 화면을 덮어 실행 안 함).
- 근거: Codex `codex_rel_0164_split_decision.md` ② 의 lane.

| lane | 결과 | 원문 |
|---|---|---|
| 1 final Release/flag/marker | 후보 SUMS 11 OK, UTF-16 "0.2.164" Client/Host/Stream/Viewer 1·Setup 5, "0.2.163" 0, 시험 표식 11종 0, gate `--dir` ALL PASS 9, `kPrepareRecentOnSignIn=false`(client_shell_main.cpp:1923) | 이 파일 |
| 2 directory_client punch+cycle | host_agent_punch 9/9 rc0. directory_retry 4회 중 1회 176/177(:709 single-use, fake dir 가 같은 token 재발급하는 시험 타이밍 결함으로 판단, 보수 요청 seq15977) 3회 177/177 | `directory_retry*.log`, `host_agent_punch.log` |
| 3 KA admission ×2 / punch_reply | 60/0, 72/0(product-client real-directory), 108/0 — job_run residual 0, OFF 최종 판정 2·2·6 launch | `host_e2e/` |
| 4 ka_pc managed(V1)·shell | managed ALL PASS(60), shell 148/0 — OFF 1·5 launch | `host_e2e/` |
| 5 directory/NP legacy/fence | 53fced4 시험으로는 고정 sleep 결함으로 매회 다른 ECONNREFUSED(node spawn ~1.07 s, server ready 0.97–2.48 s 실측) → f12c9ff 로 suite ×3 845/0 rc0, NP e2e ×3 26/0 (구 서버 = a0f991a archive) | `dir_suite*.log`, `np_e2e_*.log` |
| 6 maint/gate sandbox | deploy_test 97/0 rc0(임시 폴더 worktree 안, 자기 정리), gate `--self-test` 56 PASS(test root 잔여 0) | `deploy_test.log`, `gate_selftest.log` |
| 7 구 APK 0.2.32 × 새 Host/server | **INVALID(하네스 결함)** 7db9c27 실기 1회: S ①② PASS ③ FAIL, P ② PASS ③ FAIL, 나머지 NOT RUN. ③ = 하네스가 Desktop 탭 id 0 을 누르고 PinnedSelectTest seam 이 설계대로 거부(pinned_test_only; Host list 는 pinned 80809746 1개). 정리 residual 0·OFF 2줄·슬롯 자기 해제. 보수 위임(작업용1) | `oa_runs/oa-s-20261030020307-33228`, `oa-p-20261030020459-43180`, `old_apk_lane_summary.txt` |

- Host e2e 환경: 슬롯 CreateNew 21:56:03 → 자기 해제 22:02:17, 시험 exe hash 전후 동일, 콘솔 세션, 설치 제품 무접촉.

- lane 2 보수 확인(1b10da9, 시험만: directory_retry_test.cpp — fake directory 가 같은 token 재발급을 멈춘 뒤 single-use 판정): `remote60_directory_retry_test.exe` 9DFBC632… 재빌드, 반복 8회 완주 모두 `all retry checks passed (179 checks, 0 failed)` rc0(`retryfix_1..8.log`; 1회차 `^PASS` 줄 178 은 요약 179/0 과 한 줄 차이 — 출력 줄 섞임으로 보이며 FAIL 0). 9회차는 도구 실행 시간 한도(30분)로 중단돼 결과 아님(`retryfix_9.log` 100/0 미완), 잔여 프로세스 0 확인. 10회 계획 중 8회 완주.
- lane 7 2차(548d199, 검토 runner 5533a518, 02:57–03:02): S ①②③ PASS(pinned 73206964, 첫 영상 PNG 직접 확인) ④ FAIL(로그인 Password IME 가 viewer 를 덮어 tap 이 키보드에 감 — 하네스) ⑤⑥⑦ NOT RUN; P NOT RUN(첫 nonzero 중단). 미해명: 17:59:59 앱 NOT_CONNECTED→status error(하네스 입력 없음), Host reason=none 03:00:01 — 원인 조사 위임. 정리 residual 0·OFF 2줄. `oa_runs/oa-s-20261030025800-36232`
- lane 7 r7f(9cec590) 검증용 독립 stub: pick 53/53, pmclear 12/12, redact 7/7, orch 26/26 rc0. relay trace 사본: 53fced4 server.js 2742143d → 사본 52918c04, 53fced4 대비 삭제 줄은 relay send 3줄뿐(trace callback 인자 추가), oa_relay_trace_server check: 양성(7a79b09 relay_trace_test 전체+실제 [relay-trace] 줄) / 음성(53fced4+env → FAIL, relay 자체 정상) RESULT PASS(`oa_r7f_stub/trace_check.log`). 잔여: same_viewer 자료 부재=무변화 통과(r7g 요청).
- lane 7 3차(ff1c72a, runner 5405f48b, 07:23–07:25): S ① PASS, 1b FAIL(BACK 뒤 mIsInputViewShown 만 true, PNG 상 키보드 사라짐 — 하네스 IME 판정 과민), 2–7·P NOT RUN. 정리 residual 0·OFF 2줄. r7i 위임. `oa_runs/oa-s-20261030072336-44052`
- lane 7 4차(c45fb9b, runner 9deb8057, 07:59–08:02, SH-off·relay trace): S ①·1b(IME hidden, mIsInputViewShown 내부만)·②·2s(SH off settings)·③(pinned 71175314 → 첫 영상)·3s(requested=off, sharpen_start 0) PASS, **3w FAIL**: Host 08:01:57.806 udp control session ended reason=peer-lost(마지막 수신 control 08:01:48.848 keyframe-request seq=1). 앱 수신 thread(tid 32366)가 codec config 23:01:49.365 → MediaCodec started 51.327(약 2 s) — 0.2.32 RX 동기 codec 생성; relay c2h 공백 약 1.97 s(snap 23:01:50.813Z) 뒤 c2h 재개(55.814Z age 18 ms)에도 Host peer-lost → 앱 NOT_CONNECTED 23:01:59.233. 4–7·P NOT RUN. 정리 residual 0·OFF 2줄·슬롯 해제. `oa_runs/oa-s-20261030075939-35052`
- lane 7 P 독립(c45fb9b, runner 1f6c866a, 08:31:49–08:32:58): **INVALID** — P precondition "app did not start with an empty sign-in store (no auto_login in 30 s)". 원문: 앱 로그 파일 pull 이 30 s 내내 실패(_cur.log 없음). logcat: pm clear 23:32:12 → am start 23:32:22 → proc 3881 start 26.5 → MainActivity.<init> 검증 258 ms(1753 bytecodes/s) … 23:32:53 LoginFlow 검증 310 ms(9.66 bytecodes/s) 등 cold-start class 검증이 끝나지 않은 상태에서 23:32:54 하네스 종료(signal 9). base.dm 없음(설치 직후 첫 실행, 프로필 없음). 판정 대상 0. fixture a exit 1(연결 전 종료 결과), residual 0, OFF 2줄, 슬롯 해제. `oa_runs/oa-p-direct-check-20261030083149-29848`
