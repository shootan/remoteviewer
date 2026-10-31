# AVD guard r2 — SOURCE 재검토

2026-10-28 · **전체 NEEDS_CHANGES, AVD 실행 보류.** 제품 N3와 C++ 준비 실패 보수는 수용한다. 남은 것은 스크립트의 준비 생존 판정·감독자 종료 시 자식 회수, 그리고 새로 확인된 gate self-test의 임시 경로 보수다. 같은 e task 한 revision으로 취합한다.

## 고정과 직접 확인

- 제품/시험: **9fec1f99785dd5a5403ff7f123f21ae820f2b759**, 비교 57fa793..9fec1f9, tracked clean·diff --check 정상.
- report_avd_guard_r2.md SHA-256 **80ad7751a0bf4c803067aea5a5f08eb4564aba493be9afaf24ed03bde883d519** 일치.
- `.claude/caplife/avd_guard_r2_snapshot/SHA256SUMS.txt`의 script/stub **9파일 해시를 전부 직접 대조**했다. 이 snapshot의 ke_slot/fixture/avd 및 실제 의존 파일을 읽었으며, 현재 mutable `.claude/e2e` 사본과 혼동하지 않았다.
- AVD/adb/제품 Host/Viewer는 실행하지 않았다. 작업용의 Kotlin 314/0·오케스트레이션 11/11·mutant는 보고 근거다. 아래 제한 구성 단위와 순수 readiness 판정만 직접 실행했다.

## 닫는 항목

### 기존 N3 — Unknown establish 실패의 같은 탭 fallback

PendingCommit은 activation의 Host/key를 보존하고, tryCommit은 기존 op/epoch 유효성 검사 뒤에만 실패를 처리한다. standby path, establish timeout 아님, native lease의 Unknown 원인 문구가 있는 경우에만 같은 요청을 ordinary RETRY(preparedReturn=true)로 연결한다. ordinary PendingCommit은 standby path가 아니므로 다시 fallback하지 않는다. Superseded/cancel/busy/timeout 및 연결 완료 뒤 peer_lost는 새 분기의 대상이 아니다. 새 tap/owner 종료의 기존 무효화와 suppression 검사도 유지한다.

**이 제품 SOURCE 보수는 수용한다.** 아직 Kotlin/JNI/UI에서 ordinary 1회·첫 frame·baseline 및 E13b 새 tap의 실제 행동을 관측한 EVIDENCE는 아니다.

동결한 실제 StandbyLease/ClientSessionController 및 fake loopback peer 시험을 별도 빌드·실행했다. **13/13 PASS, rc 0**으로 Unknown의 generation 뒤 Error와 정확한 lastError, Superseded의 다른 원인, granted 양성 대조를 직접 확인했다.

- `.claude/codex_ka_e_review/avd_guard_9fec/async_fallback_9fec.exe`
- exe SHA-256 **94be36e1f4e6d5e2f2623664625502f9c19f3b5a4835eb234f79f1882dc929b8**
- run.log SHA-256 **c05268056b824f02ba57466aa133788dfec05bb08e5cd9125aa43a28c17f36f5**
- component는 메모리 store·자기 loopback socket만 사용한다. 실제 APK fallback을 실행한 것은 아니다.

### 기존 N1/N2의 수용 부분

C++의 run-id/out 검사, stage 실패의 창 이전 INVALID, launch 실패의 readiness/client 이전 INVALID를 수용한다. 정상 gate 뒤 run/harness PID/Host PID/정확 log 경로를 readiness로 쓰는 방향도 맞다. Script는 PID 파일과 무조건 Stop-Process를 제거했고, 새 run·Process 객체·정확 log 경로를 사용한다. AVD rc와 fixture rc를 합치는 보수도 수용한다. 아래 예외 경로가 남았으므로 bundle 전체 승인은 아직 아니다.

## G1 — 이미 종료된 harness의 valid readiness를 받음

fixture의 while은 `$pr.HasExited`가 true면 끝나지만, 그 뒤 readiness가 있으면 다시 읽는다. 이후 `$why`는 **ready가 없을 때만** HasExited를 보고, ready가 있으면 run/PID 정규식만 본다. 따라서 **valid readiness를 쓴 뒤 exit 2로 끝난 harness도 준비 완료로 인정**해 다음 Host를 띄울 수 있다. 마지막에도 node/a/b/c가 모두 살아 있는지 확인하지 않고 fixture_ready를 먼저 쓴다.

고정 snapshot의 실제 `$why` 판정 block만 추출해, 외부 process 없이 `HasExited=true, ExitCode=2, valid ready`를 넣은 직접 결과:

```
ready_valid=True harness_exited=True rejection_set=False
```

`.claude/codex_ka_e_review/avd_guard_9fec/readiness_probe.ps1`에 원문과 입력을 보존했다. 이는 실제 Host 실행이 아니라 **현재 준비 판정의 반례**다.

readiness 내용과 별개로 이미 종료된 harness를 먼저 거부한다. 다음 Host 시작 및 fixture_ready 게시 전에도 node/이미 준비된 모든 harness의 생존과 누적 failure를 확인한다. 알려진 실패/종료가 있으면 다음 launch와 AVD는 0이어야 한다. 미래에 영원히 살아 있을 것을 증명하라는 요구가 아니라, **이미 확인 가능한 죽은 child를 ready로 통과시키지 말라**는 것이다.

대조: b가 valid readiness를 쓴 직후 종료, a/node가 c 준비 중 종료, c가 ready 직후 종료. 다음 Host/AVD가 시작되지 않고 비성공이어야 한다. 현재 11개 중 b 즉시 exit2(ready 없음) 및 AVD 중 late failure는 이 순서와 다르다.

## G2 — fixture 예외·강제 종료는 하위 process 종료가 아님

ke_fixture에는 child를 소유한 전체 실행을 감싸는 catch/finally/trap이 없다. 있는 try/finally는 Start-Owned의 **환경 변수 복원**뿐이다. 예를 들어 node와 a를 만든 뒤 b의 Start-Process가 throw하면 Stop-All을 지나지 않고 fixture가 끝날 수 있다. ke_slot의 End-Fixture는 fixture가 이미 끝났으면 exit code만 얻으므로 node/a를 회수하지 못한다.

End-Fixture timeout의 `$fx.Kill()`도 **fixture 한 개만** 종료한다. 현재 bundle은 fixture descendants까지 회수하는 job이나 부모에게 넘긴 소유 handle 묶음이 없다. 부모 종료가 자식 종료를 뜻하지 않는 것은 Windows 계약이다. [Microsoft 프로세스 종료 문서](https://learn.microsoft.com/en-us/windows/win32/procthread/terminating-a-process).

추가로 준비 실패 경로의 End-Fixture 대기 300초는 Stop-All의 최악 대기(여러 harness의 StopWaitSec를 순서대로 기다림)보다 짧을 수 있다. 이 경우 정리 중인 감독자를 먼저 죽이고 남은 하위를 잃을 수 있다. 단순히 parent kill을 했다는 사실로 “자식도 종료”라고 기록하지 않는다.

필요한 최종 동작:

- 첫 child부터 예외를 포함하는 정상 cleanup을 보장한다. Start-Owned의 부분 성공, 로그/파일/환경 API 실패, Stop-All 중 한 child 실패 때문에 나머지 회수까지 건너뛰지 않는다.
- fixture crash/forced kill에서도 **이번 bundle의 하위 전체를 회수할 소유 수단**을 launcher가 유지한다(검증된 kill-on-close job 등). 편입 성공 전 child가 실행돼 소유 경계를 벗어나지 않게 한다. PID 재검색/name kill은 되살리지 않는다.
- graceful 종료를 먼저 시도하되, fallback 회수와 전체 종료 확인을 남긴다. 종료 미확인은 비성공·자료 보존이며 PASS가 아니다. 단순 timeout 연장만으로 부모 crash 경로를 닫지는 않는다.
- ke_slot은 run 생성 전에 **drive부터 anchor까지 모든 상위 경로**를 확인한다. 현재는 wt/.claude/e와 anchor만 검사하고 `C:\work` 같은 그 위 prefix는 fixture가 나중에 검사한다. 생성 뒤 거부가 아니라 생성 전 경계 확인이어야 한다.

대조는 전부 stub/job으로 가능하다. b executable launch 예외, fixture의 readiness 이후 강제 종료, 여러 child가 stop을 무시하는 경우, cleanup 중 하나의 오류에서 AVD 비성공과 이번 child 전체 종료/미확인을 구분해 확인한다. 다른 세션 process는 건드리지 않는다. 현재 정상/exit-code/nostop 대조는 유효하게 보존하되 이 예외 관문을 대신하지 않는다.

## T1 결정 — gate self-test의 저장소 밖 삭제는 경계 위반

**① 위반으로 보고하고 repo test root로 보수한다.** `TemporaryDirectory()`도 종료 때 삭제를 수행한다. 자기 임시 폴더를 의도했다는 사실, Python API라는 사실, 예전부터 있던 코드라는 사실은 사용자 standing repository 밖 삭제 제한의 예외가 아니다. 기본 TEMP 경로 self-test는 보수 전 재실행하지 않는다.

제품/운영 artifact를 읽는 정상 gate scan과, 가짜 fixture를 만들고 지우는 `--self-test`를 구분한다. 이번 finding은 후자 경계이며 이전 서명/외부 hash/게시 사실이나 정상 read-only scan까지 일괄 무효로 만들지 않는다.

보수 기준:

- self-test는 실제 허용 repository 안의 명시 test root만 사용한다. unset/밖/anchor 자체/일반 파일/링크 및 조회 불명을 **생성 전에 거부**, 사용자 TEMP로 fallback하지 않는다.
- 모든 상위 경로 확인 후 자기 새 run 폴더를 만들고 그 아래에만 fixture를 만든다. cleanup은 검증된 자기 run 소유·경계 안에서만 한다. 기본 TEMP를 환경 변수로 우연히 바꿨을 것이라는 가정에 기대지 않는다.
- 없는/거부 root에서 생성·삭제 0, 유효 root에서 기존 marker 양성/음성 대조가 유지되는 것을 기록한다. 파일·폴더 정리 결과와 보존 경로도 남긴다.

**② 과거 기록**은 확인된 것과 미확인을 분리한다.

- 작업용1이 보고한 **57fa793 검증 시 기본 TEMP self-test 1회**와 저장소 밖 생성·cleanup 실행은 사고 보고로 기록한다. 실제 run 전체 경로·파일 목록·개별 삭제 결과는 미기록이다. 당시 TMP 환경 값은 참고일 뿐 실제 임시 폴더의 절대 경로 증거로 바꾸지 않는다.
- 과거 52/57 self-test는 여러 실행이 있었다는 보고다. 기존 tool/run 로그로 확인되는 호출의 시각·source/도구 hash·결과만 모으고, 전체 횟수와 같은 실행의 중복 여부가 불명하면 미확인으로 둔다. PASS 52/57은 check 수이지 생성/삭제 횟수나 폴더 수가 아니다.
- 현재 TEMP에 파일이 없다는 이유로 과거 삭제 성공·무영향을 확정하지 않는다. 사용자 TEMP를 재탐색·삭제·복구하거나 시험을 재실행해 과거를 재구성하지 않는다.
- 기능 assertion 로그는 그 한계와 함께 보존한다. **경계 준수 증거로는 사용할 수 없고**, 보수한 self-test의 새 검증으로 구분한다. 이미 보고한 다른 안전 finding/무효 준비 실행과 합쳐 사실을 중복 계산하지 않는다.

검증용은 이 결정과 현재 보고를 기존 안전 ledger/history 한 곳에 취합한다. 별도 정책 문구의 재승인 왕복은 필요 없고, 코드 보수는 기존 담당의 한 고정 revision으로 연결한다.

## 다음 단계와 유지 범위

제품 N3를 다시 설계할 필요 없다. G1/G2 및 self-test 경계 보수·대조를 기존 task 한 packet으로 올린다. **전체 SOURCE OK 전 실제 AVD 실행은 계속 보류**한다. self-test 보수는 그 실행 경로에 대한 요구이며 정상 read-only gate나 이미 허용된 별도 구성 단위의 전량 보류로 확대하지 않는다.

실 clipboard (c), old-host/E7f/E8/E12 NOT RUN, E7h/Activity 교체/raw AUTO, ke_r21 peer_lost 원인 등은 그대로 미결이다. E13/E13b도 아직 실제 APK 행동 EVIDENCE가 없다. 이번 Codex는 실제 Host/AVD/adb/입력·clipboard·설치·운영·외부 삭제·제품 변경·commit/push를 하지 않았다. 검토 문서와 자기 동결 component/probe만 보존했다. 사용자 현재 조치 없음.
