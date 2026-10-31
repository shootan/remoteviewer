# KA ② e — SOURCE 및 목록 복귀 결정

2026-10-28 / Codex remote#tt7fcvky → 검증용 remote#7fsfy22z / 기존 keepalive-r2 e task.

**NEEDS_CHANGES — 아래 N1~N5와 D1 표시 계약을 같은 e 작업에 취합한다.** 모델A·스위치ⓐ·세션을 새 entry로 만드는 방향은 유지한다. 이전 공용82679b2 SOURCE/EVIDENCE를 다시 여는 것이 아니라, 이번 APK 배선과 새 공유소켓 변경의 경계를 검토한 결과다. SOURCE/전체 EVIDENCE/출하 완료 승인은 아니다.

## 고정 범위

- APK **d130407bed893f0999854b600158c3f0519f30be**, 비교 **5c871ed..d130407** 전체14파일.
- 공용 **3326c842dbc5358b791bc245443dc3856aa43434**, 비교 **82679b2..3326c84** 전체4파일. 검사 시 양쪽 HEAD 일치·tracked clean·diff --check rc0. 해당 lease blob은 두 후보에서 같다.
- Kotlin/JNI/DirectoryClient/Book·기존 gate/table/clipboard/lifecycle 호출과 공용 socket/시험의 전체 변경을 읽었다. 보고의최신② e 및 ke_r7 원문도 대조했다.30/30·305/0·74/74·57×3·빌드 gate는 **작업용 실행 귀속**이다. Codex가 AVD/실 Host/Gradle을 독립 실행한 것은 아니다.

## D1 결정 — 새 종료 신호는 이번 e에 추가하지 않는다

**기존 session 종료/timeout을 사용하는 전환 tail은 조건부 수용**한다. 명시적 재tap이 새gen으로 rollover하는 것도 사용자 activation이므로 허용한다. 자동 재activation·자동 재탈취는 여전히0이다. grant를 즉시 끝내는 새 wire/인증 계약을 이번 보수의 필수 선행조건으로 만들 필요는 없다.

다만 현재 **PC 목록 직후 READY/“대기 중·준비됨”을 Host grant 반환 완료로 읽는 표시**는 고친다. 구분해야 할 사실은⑴ local reader/dup handoff 종료 ⑵ entry/Ping 생존 ⑶ Host의 이전 grant/영상 정리다. Parked/!handedOff/Ping Ok는⑶의 확인이 아니다.

기존 wire로⑶을 확인하지 못하면 “이전 연결 정리 중일 수 있음/상태 미확인”을 표시하면서 **누르면 명시적으로 다시 연다**는 의미를 전달할 수 있다. local handoff 중의 보류와 Host 정리 미확인을 별도 상태로 다룬다. 단순13초 타이머를 grant 종료 proof로 바꾸거나, 미확인 때문에 재tap/끊기를 무기한 막지 않는다. UI의 준비됨을⑴⑵에 한정해 쓸 경우⑶까지완료됐다고 보이는 “즉시/모두정리” 의미는 제거하고 정본도 같은 정의로 갱신한다.

관측된약13초는 이번 환경의 전환 tail이지 보편 상한이 아니다. 그동안 Host영상/relay부하가 남는다는 사실을 비용에 별도계수하고 안정 standby 영상0과 합산해 PASS하지 않는다. **held input release와 clipboard 차단은 tail을 핑계로 미루지 않는다**. 이행동을 안전한소유fixture로 확인하고, 실제 실패가 나오면 기존 제어 flush/fence부터 보수한다. 이후 별도 종료신호가 필요해지는 실제근거가 있으면 그때 protocol 변경을 합의한다.

## N1 (P1) — 보류된 옛 탭이 새 탭/개별 끊기 뒤 재실행됨

`connectToDirectoryHost`는 lease가 handedOff면 `heldTap`을 저장한다. 그러나 이후 다른PC의 새탭이 READY/Cold로 진행할 때 **기존heldTap을 무효화하지 않는다**. `keepTick`은 local handoff가 끝나면 그 옛tap을 실행한다. `ConnectIntents.fallbackRuns`는 owner/lifecycle/account/list만 검사하며 최신 사용자 선택을 검사하지 않는다. planner.onManualTap은 이fallback을 무효화하지 않는다.

허용되는 순서: A로 복귀tap이 handoff 때문에 보류→B를 새로tap해 연결중/활성화→A handoff완료→keepTick이 옛A를 실행→B의 연결을 superseded로 취소하거나 A를 나중에 표시한다. **최신 탭 우선**과 어긋난다.

같은A의 **카드 끊기**도 heldTap을 지우지 않는다. lease release뒤 옛fallback이 실행되면 connectToDirectoryHost 시작의 suppression.release가 사용자 해제를 풀고 A를 다시 연결할 수 있다. 이는 개별 끊기 후 자동 재생성0의 직접 위반 경로다.

필요: 사용자 intent에 sequence/currency를 부여해 새tap·개별끊기·cancel이 이전 held intent를 폐기하게 한다. UI helper 재진입과 실제 새사용자tap을 구분해 옛 보류가 자신을 새의도로 재승인하지 않게 한다. 반례는 A보류→B새tap→A완료, A보류→A끊기/취소→tick, 빠른A/B교차이며 stale A의 activation/connect/화면열기0이다. 이 항목은 **소스상 허용되는 interleave**이며 Codex가 실제 APK에서 재현했다는 주장은 아니다. 보고의미시험 첫전환/다른tap·취소와 함께 실제 UI로 확인한다.

## N2 (P2) — 동기 capability 경로는 연결된 lease socket을 재관찰하지 못함

3326c84는 `ObserveFrom`에서만 peerTied를 확인해 fresh socket을 연다. 그러나 `NeedCapability`는 그대로 **기존 socket으로 CapabilityFn을 호출**한다. PC용 `standby_directory_capability`는 그 callback 안에서 `standby_observe_on`을 직접 호출하므로 ObserveFrom의 reopen을 거치지 않는다. session이 connect한 공유socket은 여전히 Host/relay peer에 묶여 있어 directory의응답을 받지 못한다.

Codex 직접제한재현: `.claude/codex_ka_e_review/sync_observe_probe.cpp`는 후보의 실제lease/기존FakeHost·Echo를 사용한다. 동기factory의 첫observe 성공→session과같이dup을Host에connect→close→SessionOver→fresh capability로 Start 재시작을 실행했다.

```
REVIEW capability_calls=2 initial_observe=1 after_session_observe=0 needs_capability=1
```

exe SHA **706f45445b07e5d8afe21141fd9937f0d5f399bd845770124f3e5f58f78cbdd4**. exit0은 결함재현이며 제품PASS가아니다. 자기127.0.0.1 endpoint만 실행했고 제품파일은 바꾸지 않았다.

필요: async ObserveFrom뿐아니라 **새 capability를 얻는 모든 진입**에서 tied socket을 안전하게 새socket으로 전환하도록 공통화한다. 정상재사용의같은endpoint는유지하고, Lost/re입장만 새관찰·새relay lease로 처리한다. syncfactory 세션후 Lost/재입장 및 async경로를 각각 검사한다. ordinary/shared=false 경로는 그대로 보존한다.

`preparedSocketShared`로 shared descriptor의 shutdown을 피하고 join뒤 close만 하는 보수 방향은 수용한다. 현재 Windows성공을 POSIX동등성으로 확대하지 않고, AVD가 찾아낸실패와 실제fixed흐름을 별도근거로 유지한다.

## N3 (P1) — 옛 resume 삭제와 함께 clipboard 복귀 baseline도 제거됨

`resumeKept` 삭제는 UA-2 M1을 해결하는 방향이지만, 그 안의 **복귀 clipboard baseline/읽기 불가 hold**도 함께 사라졌다. 현재 MainActivity에는 nativeSeedClipboardBaseline 호출과 clipboardResumeHold=true 경로가 없고, standby activation도 공통 tryCommit에서 hold=false로 간다. 새 session의 `nativeTakeClipboardPushRequest`는 `pushLocalClipboardToHost("session_started")`를 허용한다.

따라서 A를 목록으로 보낸 동안 phone/B에서 복사한 text가 **A의 새 controller에는 baseline이 없어** 재activation 때 A로 push될 수 있다. 원 `codex_keepalive_r2.md`는 clipboard r3 계약을 유지하며, 단순 새 session 전환을 그 정책 폐기 승인으로 주지 않았다.

필요: standby에서 해당 PC로 복귀할 때 현재 읽을 수 있는 phone 값을 **그 새 binding/controller의 baseline**으로만 seed하고 보내지 않는다. 읽을 수 없으면 실제 clip_changed 전까지 session_started/focus push를 보류한다. cold 명시 신규 접속과 prepared 복귀의 필요한 구분을 명시하고, r3 계약을 복구하기 위해 옛 park 세션 모델을 되살리지는 않는다. A→대기→B/phone에서 copy→A tap에서 자동 push0·이후 실제 새 copy1회·read 실패 hold·다른 PC baseline 오염0을 검사한다. 이 역시 소스 경로 지적이며 이번에 실제 clipboard를 조작하지 않았다.

## N4 (P2) — active+standby 합계 한도를 한 곳에서 계산하지 않음

native SessionTable은 이제 활성 entry만 세고, Book은 recent-key lease만 센다. evictions는 recent 목록에서 빠졌는지만 보며 **다른 활성 entry의 점유를 받지 않는다**.

예: raw-IP의 활성 M과 recent A/B/C가 함께 존재하면 native table은 M1개만 센다. manual은 recentKey가 없어 recent A/B/C가 변하지 않고, lease evictions도0이다. 결과는 **active M+standby A/B/C=4**가 될 수 있다. 특히 실제 코드의 `autoConnect` launch extra는 수동 연결을 시작하고, 기존 로그인 복귀의 loadHosts→considerPreconnect는 별도로 진행한다. AUTO_PREPARE=ON이면 뒤쪽 경로는 manual M의 점유를 빼지 않고 recent3대를 준비할 수 있다. 이 경로를 실제 AVD에서 실행한 것은 아니며, 일반 로그인 화면에서 수동 버튼으로 이동할 때의 접근 제한과도 구분한다.

필요: logical active+standby union과 staging을 공통 정책으로 계수한다. steady≤3, staging≤1·유한 시한,4번째 실패 시 기존3대 유지·성공 때 standby LRU만 회수한다. 수동 대상을 포함하지 않으려면 접근 조건/예외를 명시적으로 제한해야 하며 현재 코드의 암묵적4대는 승인하지 않는다. raw/manual 상태는 현재 AVD에서 미검증이므로 가능한 제품 동등/실 UI 반례에 연결한다.

## N5 (P2) — Activity executor/늦은 완료의 종료 경계

새 `standbyExecutor`는 Activity마다 만들어지는데 onDestroy는 directoryExecutor만 shutdown한다. lease 작업을 한 번 실행한 후 Activity가 종료/재생성돼도 idle worker가 끝나지 않는다. 동시에 endStaging은 stale inc라도 failures/notBefore를 수정해 endAll/새 inc 뒤의 지연 완료가 새 상태에 영향을 줄 수 있다.

필요: owner/pending을 먼저 무효화하고 필수 native 회수를 완료하는 bounded teardown 뒤 executor를 끝낸다. ReleaseAll을 queue에 넣자마자 shutdownNow로 버리는 보수는 하지 않는다. UI에서 network/join을 무한 기다리지 않고, 종료된 Activity의 callback이 새 owner를 갱신하지 않게 한다. endStaging도 current key/inc를 검증한 뒤 새 metadata를 갱신해야 한다. 반복 Activity 종료/재생성의 thread·lease 누적0, old-job 완료→new-inc의 staging/backoff 불변, cleanup 실행 누락0을 검사한다.

## 유지/남은 검증

- 모델A·스위치ⓐ의 기본 분리, read-only directory 지원 확인, 무응답을 구 Host 확정으로 굳히지 않는 수정, 카드 상태 갱신, 신규 entry/gate/session-link의 Disconnect→Over 순서는 수용한다. source의 UA-2 M1은 옛 resume 경로 제거로 닫는 방향이 맞으며, 최종 고정 APK에서는 asked 없음→실제 Host 기본/IDLE 표시까지 확인한다. r7의 CHECKING만으로 새 Host 상태 확인 완료라고 하지 않는다.
- ke_r7의30/30은 작업용 행동 근거로 보존한다. 첫 전환 도중 다른 tap/cancel, **active 화면 중** 상대 takeover 뒤 자동 재탈취0, prelogin 역할의 실 UI 경로는 독립 최종 후보에서 여전히 필요하다. standby 중 타기기 사용만으로 active 종료 UI를 대체하지 않는다.
- 첫 정상 frame4.3초 등은 실측 그대로 보존하고 tap→사용 가능 picker와 선택→presented를 분리한다. 그림이 도착했다는 결과로 목표 p95≤1초를 증명하지 않는다. stable 영상0·tail 부하·held input·clipboard·실 NAT/비용/실폰·PC 교차도 기존 후속 그대로다.

검증용은 D1과 N1~N5·미시험 행동을 **같은 e task의 한 유효 지시서**로 취합해 기존 작업용1에 전달한다. 공용 수정은 같은 writer가 ka-r3에 고정한 뒤 APK에 순차 통합한다. NP/dirretry/maint의 별도 범위를 덮어쓰거나 새 병렬 writer를 만들지 않는다. 제한 반례와 해당 diff를 재확인한 뒤 검증용 독립 실행→통합/승인된 게시 절차로 연결한다.

Codex 제품 수정·제품 빌드·AVD/실 Host/clipboard·사용자 설치본/운영 NAS 조작·삭제·commit/push 없음. **검토 전용 C++ probe의 새 빌드·loopback 실행**만 위에 명시했다. 미추적 판정서/프로브 외 history/계획·Git MCP commit은 같은 구현 작업에 취합한다. 사용자 현재 조치 없음.
