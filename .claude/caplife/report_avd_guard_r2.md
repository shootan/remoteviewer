# keepalive-r2 e — AVD guard r2 (Codex codex_avd_guard_src_r1 N1·N2·N3), 실행 0 (Host·AVD 없음)

task=keepalive-r2(e) / rev=AVD guard r2 결과 / 57fa793 위

## 고정 (한 revision)
- **tracked**: feat-apk-0233 **9fec1f99785dd5a5403ff7f123f21ae820f2b759** (부모 57fa793). tracked clean, `git diff --check` 정상.
  - 제품 Kotlin: `MainActivity.kt` (+17/−1), `StandbyKeep.kt` (+18)
  - 시험: `StandbyKeepTest.kt` (+21), `mobile_abr_session_e2e_test.cpp` (+44/−5), 새 `standby_async_fallback_test.cpp`, CMake (+11)
- **미추적 스크립트 불변 snapshot**: `.claude/caplife/avd_guard_r2_snapshot/` (읽기 전용 사본 + `SHA256SUMS.txt`). 원본은 `feat-apk-0233\.claude\e2e\`.

| 파일 | SHA-256 |
|---|---|
| ke_fixture.ps1 | afa1fcac08e3dea202edcf303cb425d0adcf3d7e15efe5b814a96d54d68634bf |
| ke_slot.ps1 | b0b30e3dc1a14f8d3cd9700a6b82102098ea4f7db71949b23353c2a2246410bb |
| ke_avd.py | 669248e34bf35a44166d00119a754f7c6012e15a8da2180d787ac243f99eb835 |
| ka_avd.py | a6f787fc2e540f41a19bf1abefe54037c9810d1cfc31285e35c02d00cdfd5456 (변경 없음) |
| ka_delay_proxy.js | 6303f50e995a20bc17e766a15f422de16f5af24efb3e5b916046289e6b7e8189 (변경 없음) |
| ke_orchestration_test.ps1 (새) | d986c5b9ad1359697a27b7fc777befc7e1dce0b9303592576b36db2f6c8df532 |
| stubs/stub_harness.py (새) | cceeed9c712f3f9ecc2953ec00e4240d33871778a008c52cf2d4d799e43536f9 |
| stubs/stub_node.py (새) | e5973498cd849bedbc4f923c9519001a17dc670ec3c58cc87be33173087fd0a0 |
| stubs/stub_avd.py (새) | dd9606fed668e81e69e2fad8b86efe436506e7c38671b49f7d80d01c23cd1c51 |

- 빌드 산출물: 하네스 exe `722BF981137966CB7BE817EFCED6836E1BC1FD99FA52A5AF0779E6F6443217BD`. pinned host 는 57fa793 그대로(`D806E375…`).

## N1 — 실패 전파 (준비 확인 전 client 0, 최종 실패는 bundle rc 까지)
- **하네스**(`mobile_abr --scenario directoryhost`):
  - `--run-id`(ASCII)와 `--out` 이 필수다. `--out` 에 이미 readiness 가 있으면 INVALID 다.
  - staging 실패는 **창이나 Host 를 만들기 전에** INVALID exit 2 다. launch 실패도 INVALID exit 2 이고, readiness·client·relay 를 만들지 않는다. gate 실패는 공용 guard 의 INVALID 다.
  - readiness 는 gate 통과 뒤에만 쓴다(임시 파일 → `MoveFileEx`). 형식: `ready run=<id> harness_pid=<pid> host_pid=<pid> host_log=<정확한 경로>`.
- **fixture**(감독자로 바꿈):
  - Host a→b→c 를 차례로 띄운다. 각 Host 는 **이 run id 와 그 harness 자신의 pid** 를 담은 readiness 가 있어야 준비로 인정한다.
  - 다음은 실패로 본다: failed readiness, 다른 run 의 readiness, 없음(timeout), 먼저 끝난 harness, node 조기 종료.
  - 실패하면 **다음 Host 를 띄우지 않고**, 띄운 것은 자기 handle 로 정리하고, `fixture_ready` 를 쓰지 않고 exit 2 다.
  - 모두 준비되면 `fixture_ready.txt` 를 원자적으로 쓴다.
  - 이후 먼저 끝나는 child 는 late failure(`fixture_failed.txt`)로 기록하고, 최종 exit 1 이 된다.
- **slot**:
  - `fixture_ready.txt`(이 run id) 를 확인하고 fixture 가 살아 있어야 AVD 를 시작한다. 그 전에는 **AVD install/clear/start 와 client 조작이 0** 이다.
  - 실 AVD 일 때는 `emulator-5554` 가 `get-state=device` 이고 `ro.kernel.qemu=1` 인지 확인한다. 아니면 AVD 를 시작하지 않는다.
  - 최종 rc 는 fixture≠0 이면 3, 아니면 AVD rc 다. AVD 단계에서 오류가 나도 `finally`/`trap` 으로 fixture 종료를 항상 실행한다.

## N2 — 새 run 과 소유 handle
- slot 이 `\.claude\e2e\runs\<tag>-<시각>-<pid>` 를 **새로** 만든다(`-Force` 없음, 이미 있으면 거부). anchor 와 경로의 link 도 확인한다.
- fixture 는 root 가 anchor 바로 아래인지, link 가 없는지, a/b/c/dir/ready 등이 아직 없는지 확인한다. 어긋나면 거부하고 아무것도 시작하지 않는다.
- **pid 파일은 전부 없앴다.**
  - 모든 child(node, harness, fixture)는 `Start-Process -PassThru` 의 Process 객체로 잡고, 즉시 `.Handle` 을 취득해 그 handle 로 기다리고 종료한다.
  - 정지는 각 harness 의 stop 파일 → 그 handle 로 대기 → 안 끝나면 같은 handle 로 Kill → 그 결과를 실패로 기록한다.
  - old host 는 목록에서 뺐다.
- `ke_avd.hostlog` 는 이 run 의 readiness 에 적힌 **정확한 `host_log` 경로**만 쓴다. run id 가 맞지 않으면 예외다. pid glob 은 제거했다.

## N3 — Unknown 은 generation 뒤의 실패 → 같은 탭 1회 ordinary fallback (제품)
- `StandbyKeep.ACTIVATION_UNKNOWN_ENTRY = "activation: entry unknown to the host"`. native lease 가 내는 문구 그대로다.
- `fallbackAfterEstablishFailure(pathKind, establishTimedOut, lastError)` 는 `standby` 이고, timeout 이 아니고, lastError 에 그 문구가 있을 때만 true 다.
- `MainActivity.tryCommit` 의 establish 실패 분기 동작:
  - 맨 앞의 기존 current 검사(op+epoch, stale 이면 이 분기에 오지 않음)를 지난 뒤에만 판단한다.
  - `standby_activate_failed … mine=true step=establish` 를 기록하고, `releaseStandby`, `connectToDirectoryHost(RETRY, preparedReturn=true)` 를 한다.
  - fallback 의 연결은 ordinary 라서 다시 fallback 하지 않는다(최대 1회).
  - pre-adopt(gen=0) 경로는 그대로다. Superseded, 취소, busy, stale/새 탭, 연결 완료 뒤 peer_lost 는 대상이 아니다.
- E13 은 phase 를 그대로 기록한다(pre-adopt gen=0 이든 establish 든 mine=true, 1회). 확인 항목은 그 뒤 ordinary `directory_connect` **1회**, standby 아닌 gen, `select_ready`(첫 frame)다.
- **E13b(새 탭 대조)**: fault 를 건 C 를 탭한 직후 B 를 탭한다. C 에 fault 가 적용되고, C 의 fallback(ordinary connect of C)이 0 이고, B 가 열리는지 본다. **AVD 미실행, 구문 확인만** 했다.

## 대조 결과 (Host·AVD·adb·창 0)
- `standby_async_fallback_test` **13/13 ×3, exit 0**. 실제 StandbyLease + ClientSessionController 를 fake host(loopback)에 붙였다.
  - **Connect 가 Host 응답 전에 true** 다(상태 Connecting).
  - 그 뒤 Error 가 되고, lastError 는 `standby activation: activation: entry unknown to the host` 다(Kotlin 상수와 일치).
  - superseded 는 다른 문구다.
  - granted 양성 대조는 연결된다.
- Kotlin:
  - `StandbyKeepTest` 24/24, APK 단위 시험 전체 **314/0**.
  - **보수 전 동작**(판단을 false 로 고정 = 57fa793)으로는 새 시험 **1 FAIL**, 복원 후 PASS.
- 실 하네스(Host 없이):
  - 없는 host exe → exit 2 "could not be staged", readiness 없음
  - run id 없음 → exit 2
  - stale readiness 가 있는 out → exit 2 (기존 파일 무변경)
  - GNLinkStream 프로세스 0
- **오케스트레이션**(`ke_orchestration_test.ps1`, 대역 stub 으로 ke_slot→fixture→harness→AVD 전 경로) **11/11, exit 0**, 실행 뒤 잔여 프로세스 0. 로그 sha256 `fcd596785d9550615d0885202c4fbd1076d4860c1f82988d5a19ed9fe04fb8b4`:

  | 경우 | 결과 |
  |---|---|
  | 정상 | a·b·c 시작, AVD, rc 0, pid 파일 0 |
  | b 즉시 exit2 / b failed / b 다른 run / b 무응답(timeout 5 s) | AVD 0, **c 미시작**, rc 2 |
  | node 즉시 종료 | Host 0, AVD 0, rc 2 |
  | c late exit2 (AVD 중, AVD 자체는 PASS) | **rc 3** |
  | a 가 stop 무시 | 자기 handle 로 kill, **rc 3** |
  | AVD rc 1 | rc 1 |
  | 이미 a 가 있는 root | fixture exit 2, 아무것도 시작 안 함 |
  | anchor 밖 root | exit 2 |

- **보수 전 등가 mutant**(다른 파일은 good 사본, 한 번에 하나). 복원 후 해시 일치:
  - **m1** slot 이 readiness 무시(57fa793 동작) → 5 FAIL (실패 경우마다 AVD 가 시작됨)
  - **m2** bundle rc = AVD rc (57fa793) → 2 FAIL (late, nostop 이 rc 0)
  - **m3** fixture 가 readiness 내용을 안 봄(57fa793: 파일 존재만) → 2 FAIL (failed/foreign 에서 c 시작, AVD)
  - 첫 m2 시도는 mutant 문자열이 주석으로 문장을 잘라 slot 파싱 오류가 났다(8 FAIL). 그 결과는 버리고, 파싱 정상인 m2 로 재측정한 값이 위 수치다.

## 사고 기록 (정직하게)
1. **%TEMP% 생성·삭제 1건**: 57fa793 검증 때 `gnlink_check_fixed_server.py --self-test` 를 기본 TEMP 에서 한 번 실행했다. 그 self-test 가 `tempfile.TemporaryDirectory(prefix='gnlink-fixed-server-gate-')` 로 저장소 밖 %TEMP% 에 폴더를 만들고 지웠다.
   - 실제 경로·파일 목록은 미기록이다. 당시 이 세션의 TMP 는 `C:\Users\shota\AppData\Local\Temp` 였다(참고).
   - 이후 다시 실행하지 않았다. 다시 실행한다면 저장소 내부 TMP/TEMP 로 한정한다.
   - r2 보고의 "하네스/스크립트 TEMP 0" 은 그 시험 도구까지 포함한 말이 아니었다. 이렇게 정정한다.
2. 오케스트레이션 1차 실행에서 slot 의 목록 인자 처리 오류로 AVD 단계가 예외로 끝났다. 그때 fixture 4개와 그 stub child 들이 남았다. 각 run 의 `stop_fixture` 로 **자기 정지 경로**를 통해 끝냈다(잔여 0 확인). 이후 slot 에 `trap`/`finally` 를 넣었다. 이 실패 결과는 판정에 쓰지 않았다.
- 시험 산출 폴더(저장소 안): `feat-apk-0233\.claude\e2e\runs\orch-*`, `_harness_*`, `_orch_*`. 남겨 두었다.

## 그대로인 것
- 실 clipboard (c) 미검증, old-host NOT RUN, E7f/E8/E12 NOT RUN, ke_r21 peer_lost 원인 미확정.
- E7h·Activity 교체·raw AUTO 는 EVIDENCE 미결이다. AVD 실행은 SOURCE 판정 전까지 보류한다.
