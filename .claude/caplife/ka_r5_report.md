

## r5 — Codex N4: 시험이 저장소 밖을 지우던 경로 (codex_keepalive_r2_spike_r4.md, 2026-10-27)
**고정: `spike/host-admission-r3` 357ee5e** (f4ddc9a 위 1 commit, 시험 코드만: `apps/directory/test/` 7파일, 제품 파일 변경 0 — `git diff --name-only f4ddc9a 357ee5e` 전부 test/).

### ① 이전 실행에서 실제로 지운 경로 (사실대로)
- 대상은 전부 `%TEMP%` = `C:\Users\shota\AppData\Local\Temp` 의 **그 실행 프로세스 pid 이름 경로**다: `remote60-directory-test-<pid>.json`, `remote60-directory-logs-<pid>\`, `remote60-update-<pid>\`, `remote60-envbound-<pid>.json`(run.js·relay_env_bound_test.js), 그리고 run.js 가 부르는 child 의 `remote60-observe-contract-<pid>.json`·`remote60-proxy-test-<pid>.json`·`remote60-connect-diag-<pid>.json`·`remote60-wake-resend-<pid>.json`·`remote60-wake-control-<pid>.json`. 또 내 미추적 보조 `caplife/run_standby.js` 가 `remote60-standby-test-<pid>.json` 을 같은 방식으로 지웠다.
- 실행: 저장된 suite 2회(`ka_dir_suite.txt` 10-26 20:36, `ka_r4_dir_suite.txt` 23:07) 외에 개발 중 run.js 재실행·relay_env_bound_test 단독/변이 실행·run_standby.js 실행이 여러 번 있었다(대화 기록 기준, 정확한 횟수·pid 는 기록되지 않음).
- 각 경로는 같은 프로세스가 시작 시 force 삭제 → 생성 → 종료 시 삭제한 자기 파일이다. **pid 를 남기지 않아, 시작 시 같은 이름의 남의 파일이 있었을 가능성은 기록으로 배제할 수 없다**(이름 충돌일 때만 해당). 지금 `%TEMP%` 의 `remote60-*` 항목 0개.
- 추가 바깥 cleanup 은 하지 않았다.

### ② 보수 (357ee5e)
- `test/test_tmp.js`: 루트 = `REMOTE60_TEST_TMP`, **`.claude/test-tmp/<이름>` 바로 아래 폴더만**(만들기 전 경로 모양 검사 + 생성 뒤 실경로 재검사). `file()`·`remove()` 는 실경로(링크면 거부)가 루트 안일 때만 동작. 루트 미설정이면 suite 가 아무것도 쓰기 전에 멈춘다.
- `run.js`: 데이터·로그·업데이트 경로를 루트 아래로, 모든 child 에 `REMOTE60_TEST_TMP` + `TEMP/TMP/TMPDIR`=루트 → os.tmpdir() 를 쓰는 child 도 루트 안에 머문다. scratch 폴더 시험 4개(recovery_fault·relay_wire·account_admin·device_credential)는 설정 시 그 루트를 따른다(미설정이면 예전대로 자기 worktree `.claude/test-tmp`).
- 내 보조 `caplife/run_standby.js` 도 같은 가드로 옮겼다(재실행 relay_standby_r4_test ALL PASS).

### 확인
- 실행: `REMOTE60_TEST_TMP=D:\remote\remote\.claude\test-tmp\ka-r5-n4`, `node test/run.js` → **rc0, `^PASS` 815, `^FAIL` 0, 마지막 줄 `RESULT: ALL PASS`** (`caplife/ka_r5_dir_suite.txt`; r4 와 같은 구성·같은 개수). 실행 중 `%TEMP%` 에 새로 생기거나 바뀐 항목 0.
- 부정 대조(실제로 거부됨): 루트 미설정 → run.js rc1(아무것도 안 씀) · `%TEMP%\remote60-x.json`·루트 밖 `..\other.json`·루트 자체 삭제 → 3건 모두 거부 · `C:\work\...\scratchY` 를 루트로 → 거부, 폴더 생성 안 됨.
- ⚠️ 가드 첫 판은 루트 모양 검사 전에 mkdir 해서, 부정 대조 중 **빈 폴더 `C:\work\gnlink-wt\ka-r3\scratchX` 를 만들었다**(내가 만든 빈 폴더, 저장소 밖이라 지우지 않고 둠 — 사용자 판단). 순서를 고친 뒤 같은 시험에서 폴더가 생기지 않음을 확인했다.
- 루트 안에 남은 것: 기존 scratch 시험이 정리하지 않는 `account-admin-*`·`device-credential-*`·`server-recovery-*` 폴더와 `standby-server.log`(루트 안, 그대로 둠).
- TEMP/TMP=루트를 주면 `os.tmpdir()` 가 루트를 돌려준다(child 경로 근거).
- 다음: ② 범위(monotonic counter 내구 예약·Pong/HelloAck 역순/손실/cancel·lease 별 socket·이동 proof 응답·Punch 유실 bounded 회수).
