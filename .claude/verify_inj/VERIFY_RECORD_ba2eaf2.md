# 검증용 독립 실행 — os_input_record_e2e (ka-r3 ba2eaf2) (remote#7fsfy22z, 2026-10-28)

- 근거: Codex `codex_test_injection_finding.md` 끝 ba2eaf2 판정. 제품 wrapper, power parser, os_input_record_e2e 기록 경계 SOURCE OK. 이 시험 하나만 검증용 독립 실행을 허용받았다. 일반 9개, fatal, 구 Host, real4, prelogin 은 포함하지 않는다.
- 고정: worktree `.claude/worktrees/verify-inj` (detached **ba2eaf2**, tracked clean). 독립 빌드 `build-verify` (Release, VS 17 2022 x64). target 은 `remote60_os_input_record_e2e_test` 와 그 의존 대상만 빌드했다.
  - GNLinkStreamInputRecordTest.exe `98f745ac4de01a2f032b9c87bd1bfe7f755a365ddf636d7f130febe9139d66f9` (marker `os-input-record` 2건, startup 줄 "os input effects=record-only (test build: SendInput, input PostMessage, SetCursorPos, AttachThreadInput, Imm…")
  - remote60_os_input_record_e2e_test.exe `a7f8022fad92224994d9699b5895aea1b660f610c4f10a046d074adce9480eed`
- 실행 조건(소스 확인):
  - record exe 를 staging 에 `GNLinkStream.exe` 라는 이름으로 복사해 띄운다. 제품 GNLinkStream 은 띄우지 않는다.
  - `--bind-address 127.0.0.1`, `REMOTE60_CLIPBOARD_SYNC=0`, job 은 e2e_adopt_or_invalid 로 묶는다.
  - "record-only" 줄을 확인하기 전에는 아무것도 연결하지 않는다.
- 실행 스크립트: `verify_inj/run_record_e2e.ps1`. 슬롯은 CreateNew 로 잡고, 자기가 만든 파일만 지운다.
- 상태: **빌드 완료, 미실행.** `C:\work\gnlink-wt\SLOT_REQUEST.txt`(사용자 삭제 대기) 가 남아 있다.

## 결과
(아래 r1 참조)

## 일반 8 + fatal 독립 실행 준비 (b8dcc93)
- 근거: Codex `codex_test_injection_finding.md:708-768` 의 b8dcc93 판정(일반 8 + fatal 안전 SOURCE OK)과 그 실행 범위 표. `e2e_guard_runtime_test` 는 %TEMP% fixture 결함 때문에 제외한다. --old-host, real4, prelogin 도 제외한다.
- 고정: worktree `.claude/worktrees/verify-inj-b8` (detached **b8dcc93**). 독립 빌드 `build-verify` (Release) 에서 9 시험과 GNLinkStream, MigrationTest, GNLinkCapture 를 빌드했다(빌드 rc 0). 해시는 `b8_hashes_build.txt` 에 있다.
  - GNLinkStream.exe `bcdc5fee…a59a` / GNLinkStreamMigrationTest.exe `9087a81d…267b`: 둘 다 record marker 0, `host-power-management` 문자열 있음.
- 실행: `run_all.ps1` 이 한 슬롯 안에서 record 와 9 시험(admission 은 기본과 `--product-client --real-directory` 두 번)을 순서대로 돌린다. rc 2(INVALID) 가 나오면 나머지는 멈춘다.
- 실행 전 남은 문제: 작업용1 injprobe Host pid 31344(10-28 08:42 부터 client 를 기다리며 끝나지 않음) → 작업용1 에게 종료 요청(seq15001). 스크립트는 저장소·C:\work 경로의 시험 Host 가 모두 끝날 때까지 기다린다(설치 제품은 제외).
- 상태: **빌드 완료, 미실행.** 슬롯 파일은 사용자 삭제 대기다.

## r1 실행 (2026-10-29 17:21~17:26, 슬롯 CreateNew 자기 파일 → 종료 후 자기 삭제 확인)
- 환경: qwinsta console Active(shotan), RDP Listen만. 시작 전 저장소·C:\work 경로의 시험 Host 0개(설치 제품 GNLinkStream pid 27044 는 대상 아님·무접촉). 해시 `r1/hashes.txt`, 원문 로그 `r1/*.log`, 요약 `r1/summary.txt`.

| 시험 | rc | 결과 |
|---|---|---|
| os_input_record_e2e (ba2eaf2) | 1 | **12 checks, 2 failures** — record-only 선언 뒤 연결·wake/monitor-power 는 기록만(ctx none)·입력 6개 dispatch 도달 PASS. FAIL 2: "this session's epoch, from the host's own line" = 0(`[native-video-host][control] session epoch=` 줄을 못 찾음) → 그 결과 매칭이 "epoch 1 = 다른 세션" 으로 FAIL. **시험 판독 결함 추정**(staging host.log 는 시험이 지워 원문 없음) |
| host_standby_admission_e2e | 0 | ALL PASS 60/0, 2 launch 최종 판정 확인 |
| host_standby_admission_e2e --product-client --real-directory | 0 | ALL PASS 72/0 (old-host 없음) |
| host_punch_reply_e2e | 0 | ALL PASS 108/0, 6 launch |
| host_migration_e2e | 0 | ALL PASS 37/0, 4 launch |
| host_reboot_delegate_e2e | 0 | ALL PASS 23/0 |
| android_session_reboot_e2e | 0 | ALL PASS 18/0 |
| bwe_observe_e2e | 0 | ALL PASS 15/0 |
| host_control_deadline_e2e | 0 | ALL PASS 17/0 |
| host_two_dispatcher_e2e | 0 | ALL PASS 24/0 |
| host_fatal_exception | 2 | **INVALID** — none(정상 대조) Host 가 `unsupported codec: h264 (… REMOTE60_NATIVE_ENCODED_EXPERIMENT_FORCE=1)` 로 exit 11, 입력 OFF 줄 전에 끝남 → 최종 판정이 OFF 두 줄 부재로 거부(gate 정상 동작). std/winrt 는 configure 단계 FATAL exit 49(의도). **시험 결함**: none 대조에 encoded-experiment env 가 빠져 정상 경로까지 가지 않음(이전 판정 "no FATAL line" 만으로는 정상 대조가 아니었다). 보존 로그 `.claude/test-tmp/verify-inj/fatal-11144-608191265-0/` |

- 안전 판정: 9 개 launch 계열 모두 "input / power state: no contradiction … end, EOF and OFF lines confirmed". INVALID 1건은 fail-closed 동작.

## r2 재실행 (2026-10-29, ka-r3 ccaf745 독립 빌드 — Codex `codex_inj_record_fatal_src_r10.md` SOURCE OK 뒤)
- 빌드 해시 `r2_hashes_build.txt`: fatal 9f27873b…, record e2e e676405f…, MigrationTest 9087a81d…(b8 빌드와 동일 = 제품 무변경), record host ffedcfd7…(marker 2). 실행 해시 `r2/hashes.txt`, 원문 `r2/*.log`, 요약 `r2/summary.txt`. 콘솔 세션, 슬롯 CreateNew → 자기 삭제 확인.

| 시험 | rc | 결과 |
|---|---|---|
| os_input_record_e2e | 0 | **ALL PASS 12/0** — "this session's epoch, as the host's source says it is 1 (the first session: one 'client connected', no epoch line)", 입력 6개 효과가 이 세션·순서대로 기록만 됨, wake/monitor-power 기록만 |
| host_fatal_exception | 0 | **ALL PASS 26/0** — normal control: 'waiting client' 도달·FATAL 없음·deadline 종료, OFF 두 줄·무상충. std/winrt/IsSupported 등 기존 case PASS. 정리 NOTE: run 폴더 `fatal-25920-609237140-0` 은 비어 있지 않아 보존(test root 안) |

- r1 의 FAIL(record)·INVALID(fatal)는 시험 결함으로 확인됐고, 보수본에서 둘 다 통과.
