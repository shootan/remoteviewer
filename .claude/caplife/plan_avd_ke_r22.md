# keepalive-r2 e — AVD 실행 계획 (ke-r22), 고정 artifact (2026-10-28)

**아직 실행하지 않았다.** 슬롯 순서는 검증용 batch → 작업용2 ka-pc e2e → 이 AVD 다. 검증용이 차례를 알리면 아래를 그대로 실행한다. 이 실행은 SOURCE 수용(Codex codex_avd_guard_src_r4.md)의 실행 조건을 따르며, AVD EVIDENCE 나 출하 승인이 아니다.

## 고정
| 항목 | 값 |
|---|---|
| source | feat-apk-0233 **9f9a91c852419a2d2848be73aab077d779b0550c** (tracked clean) |
| e2e_job_run.exe | DC4C5049E8E9D32A8C9096005A2702C95EF30CCE2D3F1998BD4005DA8C89462F |
| GNLinkStreamPinnedSelectTest.exe (pinned Host) | D806E375019BC5EEC850443EFDA64CC35289F1A7A73CFFBE473BCF8391E12EAA |
| remote60_mobile_abr_session_e2e_test.exe (하네스) | 722BF981137966CB7BE817EFCED6836E1BC1FD99FA52A5AF0779E6F6443217BD |
| ke-off.apk (9f9a91c, `assembleDebug -Pgnlink.testDirectoryUrl=http://10.0.2.2:18410`; BuildConfig `STANDBY_AUTO_PREPARE=false`) | 8C6422FE7104F8272242A9C51A64AC3D7A98989886A855F1D89CBEE55F23828D |
| ke-on.apk (같은 source, 위 + `-Pgnlink.testStandbyAutoPrepare=true`; `STANDBY_AUTO_PREPARE=true`) | 64A021188C7B6A90492226B16F084DECA4CCC78B8163F58CC3482B71CB3A8F37 |
| 스크립트 | `avd_guard_r3_snapshot` 9파일과 현재 `.claude/e2e` 사본의 해시가 **모두 일치**한다(2026-10-28 대조). |

- 빌드 로그: `.claude/e2e/gradle_off_9f9a.txt`, `gradle_on_9f9a.txt` (rc 0).
- 이전 APK(6451ec1)는 `ke-off.6451ec1.apk` / `ke-on.6451ec1.apk` 로 보존했다.
- 이번 빌드는 슬롯 요청 파일이 있어 측정 runner 가 멈춰 있는 동안 했다(CPU 만, 에뮬레이터·GPU 없음).

## 실행 직전 확인 (하나라도 다르면 실행하지 않음)
1. `git -C feat-apk-0233 rev-parse HEAD` = 9f9a91c, tracked clean.
2. 위 artifact 5개의 sha256 이 표와 같다.
3. `.claude/e2e` 사본이 r3 snapshot 의 `SHA256SUMS.txt` 와 같다.
4. 슬롯: 검증용이 내 차례라고 알렸다. `C:\work\gnlink-wt\SLOT_REQUEST.txt` 는 검증용이 정한 대로 다룬다(남이 만든 slot 은 건드리지 않는다).
5. emulator-5554: `get-state=device` 이고 `ro.kernel.qemu=1` (ke_slot 이 직접 확인한다).

## 실행
- `sh_run.ps1` 로 측정 슬롯을 잡은 채 ke_slot 을 실행한다(이전 ke 실행과 같은 방식):
  `powershell -NoProfile -ExecutionPolicy Bypass -File C:\work\gnlink-wt\feat-apk-0233\.claude\e2e\ke_slot.ps1 -Tag ke-r22`
- ke_slot 의 기본 동작:
  - 새 run 폴더 `.claude\e2e\runs\ke-r22-<시각>-<pid>`
  - fixture 는 `e2e_job_run` 의 job 안에서 실행 (directory + a/b/c pinned Host, 공용 guard 의 두 OFF 줄 gate)
  - `fixture_ready` 확인 후 `ke_avd.py`
  - fixture 종료 → rc
- 시나리오: ke_avd 의 E1–E11, E13·E13b (E13 계열은 activation 실패 fault 와 같은 탭 fallback).
  - **NOT RUN**: E7f, E8, E12 (old host 필요). ke_avd 가 따로 기록하며 PASS 에 넣지 않는다.
  - **E7h**: ke_avd 안에 그대로 있으므로 이번에 실행된다(이전 미실행).
- clipboard: 에뮬레이터 clipboard 를 바꾸는 단계는 **없다**(결정 ① c). 공용 5554 clipboard 와 사용자 sentinel 은 건드리지 않는다.
- Host 3개는 입력 none, 전원 off, 두 줄 gate, 실행 중 감시, 끝에서 Finalize 를 거친다. old host 는 띄우지 않는다.

## 기록할 것
- run 폴더, `slot.txt`(fixture/AVD rc, job_run 줄), `fixture.txt`, `fixture_result.txt`
- 각 Host 의 readiness 와 `host_log`, avd `result.json`(results / not_run / marks)
- 최종 rc 와 E 별 PASS/FAIL/NOT RUN, 잔여 프로세스 0 확인
- 실패나 INVALID 는 그대로 보고한다. 1회 통과로 간헐 결함(ke_r21 peer_lost)을 닫지 않는다.

## 미결 (그대로)
- 실 clipboard (c), old/E7f·E8·E12 NOT RUN, ke_r21 peer_lost 원인
