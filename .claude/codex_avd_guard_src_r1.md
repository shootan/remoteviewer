# AVD guard + activation fault — SOURCE r1

2026-10-28 · **NEEDS_CHANGES, AVD 실행 보류.** 고정 57fa793205d0d2c41725236ada46cc5b47998565와 해시로 고정한 실행 스크립트를 검토했다. 공용 guard 호출 자체와 시험 fault의 출하 분리는 수용하지만, 준비 실패 전파·run/process 소유 및 실제 fallback 실패 phase가 남는다. 아래 세 항목을 기존 작업용1 e task의 한 보수 묶음으로 연결한다.

## 고정과 스크립트 처리 결정

- 비교 ccf6455..57fa793의 6파일. 공용 d665957 merge는 기존 검수 범위를 유지한다. diff --check 정상, tracked 변경 없음.
- report_avd_guard.md SHA-256 ab2563da6befe514e0f8586c1b577c9ec8d0c6cc5f489f782a6042f99f8c7f7e 일치.
- **이번 한정 실행의 SOURCE 자료는 미추적 스크립트라도 전체 내용·해시·불변 snapshot으로 고정해 포함할 수 있다.** 추적 파일로 옮기는 별도 commit을 이번 검토의 선행 관문으로 추가하지 않는다. 다만 두 변경 파일만이 아니라 실제 wrapper/import/helper까지 같은 packet으로 고정한다. 반복 사용하는 하네스를 추후 정식 automation 위치로 편입하는 일은 기존 구현 정리에서 처리할 수 있다.
- 읽고 보존한 snapshot: .claude/codex_ka_e_review/avd_guard_57fa/scripts_snapshot/. 이번 검토는 다음 해시에만 적용된다. 이후 변경은 새 고정 hash로 연결한다.

| 파일 | SHA-256 |
|---|---|
| ke_fixture.ps1 | 51373fdff4ad5d8afc9dbfa0d198ed62ffcb4cf8e5b22cd677ff366a8f2931c8 |
| ke_avd.py | a120b238416ce8076d79122928974b4daaddf7a510e1f9fb9c9c45660c9d3e16 |
| ke_slot.ps1 | 0f908fd3728a6545d4d665feda2d3f59fa994b6941f528194eb1de70b721ee60 |
| ka_avd.py | a6f787fc2e540f41a19bf1abefe54037c9810d1cfc31285e35c02d00cdfd5456 |
| ka_delay_proxy.js | 6303f50e995a20bc17e766a15f422de16f5af24efb3e5b916046289e6b7e8189 |

## 수용한 부분

mobile_abr의 정상 Host launch 경로는 input none + power OFF를 붙이고, suspended child의 공용 job 검증/편입 뒤 재개한다. 이번 launch의 두 OFF 줄을 기다리고 monitor를 붙인 뒤, StopPolling → job 전체 종료 확인 → log 쓰기 handle close → final file 읽기/Finalize → job close의 순서를 유지한다. 결과 전 final_check_all도 연결됐다. 공용 guard를 자체 변형하지 않은 점을 수용한다.

fault는 REMOTE60_PINNED_SELECT_TEST_SEAM 안에서만 StandbyActivate를 Unknown으로 한 번 거부하며, table.Activate 전에 반환한다. 일반 제품 빌드의 분기에는 들어가지 않고, 정상 table 동작·wire 규격을 바꾸지 않는다. TEST_MARKERS의 pinned-select-test/REMOTE60_PINNED_TEST_ 추가도 범위에 맞는다. 제품/시험 exe의 실제 marker/hash 및 17/17·56/56·57/57은 작업용 보고 근거이며 Codex가 그 바이너리를 실행한 결과가 아니다.

이 guard 연결을 이유로 mobile_abr의 모든 기존 scenario를 새로 승인하지 않는다. 이번 실행 범위는 e task의 **directoryhost 기반 지정 AVD 시나리오**다. parkclip 등 다른 scenario의 window-station/clipboard fixture 배선은 별도 기존 관문을 따른다. old-host와 E7f/E8/E12의 NOT RUN 분리도 수용한다.

## N1 — Host/fixture 준비 실패가 AVD 시작을 막아야 함

C++에서 injOff=false인 경우라도 **launched=false이면** `if (launched && !injOff)`를 건너뛴다. 뒤의 directoryHost 분기는 readiness 파일을 항상 만들고 내용만 failed로 쓴다. ke_fixture.ps1은 내용/프로세스 상태를 판정하지 않고 **파일 존재 또는 60초 경과** 뒤 다음 Host로 진행한다. ke_slot.ps1도 fixture의 성공 계약을 확인하지 않고 AVD를 시작한다. 따라서 stage/launch 실패, failed readiness, timeout을 거부하는 전체 연결이 없다.

필요한 최종 동작:

- Host를 만들지 못했거나 guard를 확인하지 못하면 INVALID로 중단하고 **ready를 만들지 않는다**. non-directory client 생성으로도 진행하지 않는다. launched=false는 gate 통과가 아니다.
- readiness는 이번 run의 identity와 실제 guard 성공에 연결된 긍정 결과여야 한다. fixture는 실패 내용·누락·timeout·조기 종료를 모두 실패로 반환한다.
- bundle launcher는 fixture 전체 성공과 필요한 a/b/c의 유효 readiness를 확인하기 전 **AVD install/clear/start 및 client 조작 0**이어야 한다. 일부 Host만 준비됐을 때도 진행하지 않는다.
- 후반 monitor/final 판정이 INVALID/FAIL이면 bundle 최종 결과도 그대로 실패로 묶는다. 현재 ke_slot의 최종 rc는 AVD rc만 사용하므로, AVD가 통과했다는 이유로 Host/harness 최종 실패를 성공으로 바꾸지 않는다.

대조는 Host/AVD 없이 stage/launch 실패, failed 파일, readiness timeout, fixture nonzero, late Host INVALID를 각각 넣어 client/다음 launch 0과 bundle 비성공을 확인할 수 있다. 정상 control은 같은 gate를 통과해야 한다.

## N2 — 새 run과 실제 소유 process로 준비/종료 대상을 연결

현재 두 PowerShell 파일은 Tag로 만든 기존 폴더를 -Force로 다시 사용한다. 같은 root의 ready/pid/log가 남을 수 있다. ke_slot은 더 이상 만들지 않는 old까지 포함해 pid 파일을 읽고, Wait-Process 결과와 무관하게 Stop-Process -Id를 다시 호출한다. 파일의 PID만으로는 **이번에 만든 살아 있는 process**임이 보장되지 않는다. 정상 종료 뒤 PID 재사용 또는 기존 run의 pid가 남은 경우를 구분하지 못한다.

필요:

- 실행 전 허용 anchor 바로 아래 **새 run 폴더**를 원자적으로 만든다. 이미 존재, 경계 밖 Tag, 링크/reparse 경로, 확인 불가는 child 시작 전에 거부한다. 예전 run의 ready/pid/fault/log를 사용하거나 덮어쓰지 않는다.
- 이번 launcher가 얻은 process handle/job 또는 동등하게 검증된 소유 handle로만 기다리고 회수한다. PID 텍스트만 읽어 무조건 종료하는 경로는 제거한다. handle을 프로세스 사이에 전달할 수 없다면 생성 시점·image·run identity를 확인한 **같은 handle**을 사용한다. 이미 종료된 process는 다시 PID로 kill하지 않는다. 새 run이 시작하지 않은 old는 준비/종료 목록에서도 제외한다.
- Host 최종 종료·guard 결과·각 child 종료/출력을 bundle 결과 전에 수집한다. 실패/미확인 cleanup을 조용히 버리고 성공을 내지 않는다. 다른 세션의 process/slot은 건드리지 않는다.
- hostlog의 `glob(run-<PID>-*)[0]` 선택도 이번 run의 실제 staging log 경로로 연결한다. PID가 같다는 이유로 남은 과거 scratch log를 고르지 않는다.

이 packet의 허용 AVD 대상은 slot이 관리하는 emulator-5554다. ke_slot의 명시 serial 지정 범위로 고정한다. ke_avd/ka_avd를 임의 KA_SERIAL 환경의 독립 명령으로 실행하는 것은 승인 범위에 포함하지 않는다. 실제 호출 전 emulator identity/slot 소유가 맞는지도 확인하고, 물리 기기에 install/pm clear하지 않는다.

## N3 — Host Unknown 거부는 gen=0 fallback을 재현하지 않음

**앞 결정②의 가정을 정정한다.** Host activation 거부가 곧 nativeConnectStandby gen=0이 될 것으로 본 것은 잘못됐다. 고정 제품 소스의 순서는 다음과 같다.

1. standby_adopt는 runner.TakeActivation 뒤 ConnectOpGate.Adopt를 부른다.
2. controller.Connect는 generation을 올리고 worker를 시작하면 true를 반환한다.
3. gate는 그 generation을 즉시 반환한다. worker의 UDP activation handshake는 그 뒤에 실패할 수 있다.
4. Kotlin은 gen>0으로 PendingCommit을 만들고, 나중의 establish 실패는 tryCommit의 directory_connect_failed 경로로 간다. 이 경로에는 같은 탭 ordinary fallback이 없다.

따라서 이번 Host fault의 **Unknown 응답은 실제 activation 실패를 만들지만**, E13의 `standby_activate_failed gen=0 ... mine=true` 및 그 뒤 fallback을 현재 제품에서 보장하지 않는다. handler 단위 17개는 이 async adopt/establish 경계를 검증하지 않는다. 그중 Hello 대조도 실제로는 StandbyHello이며 ordinary session Hello 실행을 직접 입증한 시험은 아니다(ordinary Hello의 Host handler 배선은 이번 diff에서 바뀌지 않았다).

Codex 제한 직접 probe는 고정 controller/gate 원문과 기존 handshake callback 경계를 사용했다. handshake를 붙잡고 Adopt 반환 뒤 실패시킨 결과:

```
Adopt gen=1 current=1 before=Connecting after=Error
error=standby activation refused (test handshake)
```

.claude/codex_ka_e_review/avd_guard_57fa/async_probe.cpp/exe, build.log/run.log 보존. exe SHA-256 0906018d25a64f4fe5c9e19a60444ea41eaff2ae27d077acc66f3db554015b62. 자기 loopback UDP socket과 handshake callback만 사용했고 실제 Host/fault packet/AVD/JNI/OS 입력은 실행하지 않았다. 이 결과는 실제 Host Unknown 패킷 실행 기록이 아니라, **gen 반환과 handshake 완료가 분리된 실제 controller/gate 동작의 직접 확인**이다.

보수 방향은 기존 “쓸 수 없는 lease의 같은 탭은 ordinary로 한 번 fallback” 계약에 맞춘다. **현재 명시적 tap의 standby activation이 Unknown으로 establish 전에 실패한 경우**도 같은 의도를 유지한 한 번의 ordinary fallback 대상으로 연결한다. gen=0으로 가짜 성공 조건을 맞추거나 UI를 동기 handshake 대기로 바꾸지 않는다. pre-adopt gen=0 실패와 post-adopt establish 실패를 구분해 원인을 보존한다.

반드시 유지할 경계:

- 같은 owner/epoch/handle/op와 아직 최신인 tap에만 적용한다. 끊기·로그아웃·HOME·새 탭/계정·취소 뒤에는 fallback 0이다.
- Superseded/다른 기기의 takeover·이미 연결 완료된 session의 peer_lost는 이 Unknown 첫 연결 fallback과 구분한다. 그것을 자동 재탈취/일반 자동 재연결로 확대하지 않는다.
- 원래 실패한 session/lease는 자기 소유 경계에서 정리하고, ordinary fallback은 최대 1회다. fallback 자체 실패 후 재반복하지 않는다. fallback까지 preparedReturn의 Seed/Hold를 유지한다.
- E13은 실제 도달한 phase·같은 tap/owner·후속 ordinary 성공과 첫 frame을 확인한다. gen=0 한 가지 문자열만을 강제하지 않는다. 정상 activation 대조, 취소/새 tap 대조와 함께 구성 단위부터 검증한다.

이것은 이번 검토에서 확인한 **제품 실패 경로의 잔여**이므로 기존 e task에서 보수하고 고정 SOURCE로 다시 연결한다. 새 native fault로 gen=0만 강제한 결과를 Host Unknown 뒤 fallback의 검증으로 바꾸지는 않는다.

## 남은 범위와 다음 단계

입력 none/power OFF 자체, marker 추가 및 Unknown fault의 시험 한정 구조는 다시 설계할 필요 없다. 위 N1/N2/N3와 고정 script snapshot을 한 revision으로 묶는다. 준비/종료 대조와 async 실패→같은 tap fallback 대조는 Host/AVD 없는 fake/sleeper/component로 먼저 확인할 수 있다. 소스 검수 전 AVD 재실행은 계속 보류한다.

실 clipboard는 기존 결정 (c)대로 미검증이다. E7h/Activity 교체/raw AUTO, ke_r21 peer_lost 원인·실 NAT/비용은 기존 미결이다. 이번 Unknown 실패 경로 보수가 기존 peer_lost까지 해결했다는 근거는 없다. old-host NOT RUN과 금지된 viewer_prelogin_login_e2e 범위도 유지한다.

추가 실행 기록 주의: marker gate의 --self-test는 기존 코드에서 tempfile.TemporaryDirectory를 쓴다. 이후 재실행은 repository 내부의 검증된 private TMP/TEMP로 한정해 실제 root를 기록한다. 이번 보고의 “하네스/스크립트 TEMP 사용 0”을 모든 시험 도구의 실제 TEMP 생성·삭제 0으로 확대하지 않는다. Codex는 그 self-test를 기본 TEMP에서 실행하지 않았다.

이번에는 소스·스크립트 읽기, 불변 사본과 자기 제한 probe만 수행했다. 실제 Host/Viewer/AVD/clipboard·설치·운영·외부 삭제·제품 편집·commit/push는 없다. 사용자 현재 조치 없음.
