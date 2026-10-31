# KA ② e r2 — SOURCE 재검토

2026-10-28 / Codex remote#tt7fcvky → 검증용 remote#7fsfy22z / 기존 keepalive-r2 e task.

**APK NEEDS_CHANGES — 아래 R1~R3를 같은 e 작업의 보수 묶음으로 취합한다. 공용 소켓 N2는 닫는다.** 최신 탭·Provide·baseline·한도·종료 보수의 진전은 수용하되, 새 fence 완료와 Activity 소유 경계가 남는다. 독립 AVD EVIDENCE·통합·출하 판정은 아니다.

## 고정 범위와 실행 귀속

- APK **7a0d5771ebc825b45924168f5d9a59ab0c3379a4**, 비교 **d130407..7a0d577** 전체 11파일(+644/-61). 공용 **ecf1004ea4738d44fa20eb3144d352b0ad115741** 중 **3326c84..da2280e**와 **534242b..ecf1004**를 확인했다. 검사 전후 양쪽 HEAD 일치·tracked clean·대상 diff --check 정상.
- 양쪽 standby_lease.hpp blob **b939d2d6544fc8e9fe511e9d41fca19988c8722a**가 같다. ecf1004의 마지막 diff는 이미 요청한 과거 삭제 확인 한계를 고친 주석뿐이며 동작 변경은 없다. Host punch/시험의 기존 SOURCE·EVIDENCE 판정도 유지한다.
- Kotlin/JNI/공용 lease·시험 변경과 실제 lifecycle/clipboard/cold retry/목록 복귀 호출을 대조했다. 보고와 ke_r19 stdout의 E7b~g 및 최종 48/48을 읽었다. **310/0·66×6·78/0·AVD 48/48은 작업용 실행**이며 Codex가 APK/AVD를 빌드·실행한 것은 아니다. ke_r10의 settling 화면이나 실 clipboard를 이번에 직접 봤다고 주장하지 않는다.
- Codex 직접 실행은 아래 **실제 lease를 포함한 두 loopback probe**뿐이다. 제품 수정·실 Host/사용자 clipboard 조작은 없다.

## 닫거나 수용하는 기존 항목

- **N1의 보류 탭**: 새 사용자 tap의 tapSeq 증가·held 폐기, continuation의 suppression 재승인 금지, HELD 실행 때 순서 확인, fence 동안 최신 tap 보류는 원 지적을 겨냥한다. mine이 아닌 activation 실패는 retry하지 않는 것도 확인했다. 다만 R1은 새로 도입된 **목록 복귀 완료 항목**의 별도 수명이다.
- **Provide 경합**: Book.ifCurrent의 같은 lock 안에서 검사와 native Provide를 수행하고 end/endAll이 같은 lock을 쓰므로, ke_r18의 검사→끊기→Provide 틈을 닫는다. JVM 시험의 동기화 변이와 AVD에서 찾은 원 반례를 별도로 보존한다.
- **N2 동기 capability**: UntieForNewCapability를 ObserveFrom/NeedCapability가 공통 사용하고, read 중 tied socket은 다음 pump 차례로 미뤄 descriptor를 read 아래서 닫지 않는다. 정상 재사용은 같은 endpoint를 유지한다.
- **N4의 한도**: commit된 ordinary/raw 세션을 otherActiveSessions로 더하고, 성공 commit 뒤 overLimit 및 새 lease 시작 전 mayStart를 적용하는 방향을 수용한다. 기존 raw M + lease A/B/C의 암묵적 steady 4대 경계를 겨냥한다. **실 raw-IP+AUTO 배선 검증은 여전히 필요**하며 pure helper 단위 결과가 그것을 대신하지 않는다.
- **N5의 일부**: endStaging의 inc 검사 및 executor.shutdown으로 필수 cleanup을 버리지 않는 방향은 수용한다. 그러나 queued native cleanup의 소유 문제 R2는 해결되지 않았다.
- **D1 표시/held input**: 새 wire 없이 local handoff·entry 생존·Host 정리 미확인을 구분하고, SETTLING이어도 명시 tap을 허용하는 방향을 수용한다. 13초를 grant proof로 쓰지 않는다. native park fence→종료의 순서와 ke_r19 E7b의 Host key-up 선행 근거도 수용하되, 아래 새 completion 수명을 보수한다.

## N2 직접 재확인 — 원 반례 해소

`.claude/codex_ka_e_review/sync_observe_probe_r2.cpp`는 실제 ecf1004 lease와 기존 FakeHost/Echo를 사용한다. 같은 공유 소켓을 세션 peer에 connect→dup close→SessionOver→동기 capability 재시작한 결과:

```
capability_calls=2 initial_observe=1 after_session_observe=1 needs_capability=0
```

rc0. 원 r1의 after_session_observe=0이 해소됐다. exe SHA-256 **677daf6d29a7d9e4d21a4d1c1e78f9276c3c879230bcd4102aee191dd330cc68**. 자기 process의 loopback UDP endpoint만 사용했다. pump read 중 재입장 시험은 이번 worker의 66개 근거와 대조했으며 그 전체를 Codex가 다시 실행한 것은 아니다.

## R1 (P1) — listReturnEnds가 끊기·owner 종료 뒤 lease를 다시 만들 수 있음

새 ListReturnEnd는 handle/decision/key/deadline만 저장하고 **owner epoch나 completion currency를 갖지 않는다**. endAllKept는 held/pending/kept/book을 비우지만 listReturnEnds를 비우지 않는다. 개별 disconnect/forget 경로도 그 항목을 제거하지 않는다. 이 map을 지우는 코드는 정상 finishListReturn의 remove뿐이다.

finishListReturn은 현재 owner·해당 entry의 유효성·suppression을 검사하지 않고, 저장한 decision이 FIRST_TRANSITION이면 **예전 key로 startStandby를 호출**한다. startStandby 역시 suppression/현재 계정을 확인하지 않는다.

허용되는 순서: ordinary C로 접속→PC 목록의 park fence 진행(ListReturnEnd/FIRST_TRANSITION 보유)→그 사이 모두 끊기·C 끊기·onStop·로그아웃으로 entry/Book 종료→다음 renderStatus/keepTick 또는 deadline에서 옛 finishListReturn 실행→C lease 재생성. nativeEnd(old handle)가 거부돼도 이후 startStandby는 실행된다. 새 계정/새 owner에서도 예전 key가 사용될 수 있다.

ke_r19 E7f는 이미 **standby_start/capability 단계에 들어간 뒤의 끊기**를 다룬다. 아직 첫 전환 lease가 없고 listReturnEnds 안에 있는 fence 단계의 취소와는 다르다. E7d도 기존 standby A의 BACK_TO_STANDBY라 FIRST_TRANSITION 재생성을 직접 검사하지 않는다.

필요: owner 전체 종료 및 해당 handle/key 끊기가 pending list return을 먼저 무효화한다. 완료 시에도 살아 있는 동일 owner/entry/operation과 허용된 유지 의도인지 확인한 뒤 후속 lease를 만든다. **다른 PC를 탭했다는 이유만으로 정상 C의 첫 전환까지 취소하지는 않는다**(E7e의 의도 유지).

검증은 ordinary 첫 전환의 fence를 결정적으로 붙잡고 개별/모두 끊기·onStop·로그아웃/계정 전환 후 풀어 수행한다. 이후 standby_start/Provide/자동 재생성 0, 새 owner의 상태 불변을 확인한다. 이는 소스상 허용되는 순서이며 Codex가 AVD에서 새로 재현했다는 주장은 아니다.

## R2 (P1, N5 잔여) — 옛 Activity의 queued ReleaseAll이 새 Activity의 lease를 지움

Book과 standbyExecutor는 **Activity별**이지만 JNI g_standby는 **process 전역 하나**다. nativeStandbyInit은 이미 있으면 그대로 성공하고, nativeStandbyReleaseAll은 owner/epoch 없이 그 runner 전체를 ReleaseAll한다. nativeEndAll이 올리는 g_table epoch도 이 API에서 검사하지 않는다.

onStop/onDestroy는 Book을 즉시 무효화하지만 **전역 ReleaseAll은 자기 executor에 queue**한다. 이전 capability job이 HTTP/observe를 기다리는 동안 새 Activity가 다른 executor로 lease를 만들 수 있다. 그 뒤 옛 queue의 ReleaseAll이 실행되면 **새 Activity lease까지 지운다**. shutdown이 queue를 끝까지 실행한다는 사실만으로 소유가 맞아지는 것은 아니다. 새 Book에는 key가 남고 native lease는 없어지는 상태도 가능하다.

isDestroyed로 막는 것은 UI callback 일부뿐이다. native Start/Observe/Release가 key/전역 runner만 찾는 경우도 owner를 구분하지 않으므로 이전 job이 새 owner의 같은 key를 건드리지 않아야 한다.

필요: cleanup 및 관련 native 작업을 **그 owner가 소유한 slot/incarnation**으로 한정하거나, process 전역 owner handoff에서 옛 cleanup 완료 전 새 lease 등록이 겹치지 않게 한다. 새 epoch니까 옛 cleanup을 그냥 버려 옛 lease를 누수시키는 보수도 피한다. UI thread가 HTTP 종료를 무기한 기다리는 구조로 바꾸지 않는다.

검증: old executor의 capability/cleanup을 gate로 지연→Activity 교체→new owner lease 준비→old cleanup 해제. 옛 자원은 회수되고 새 lease·Book·status는 그대로이며 반복 교체의 thread/lease 누적도 없어야 한다. 단일 Book의 late endStaging 단위는 이 전역 runner 경합을 검사하지 않는다.

## R3 (P1, N3 잔여) — prepared 복귀의 cold retry가 clipboard baseline 의미를 잃음

성공한 standby path에서 새 binding에 seed/Hold하는 보수는 맞다. 그러나 baseline 여부를 **p.pathKind == "standby"**만으로 정한다. activateStandby가 gen0/mine으로 실패하면 같은 사용자의 요청을 **TapIntent.RETRY로 ordinary 연결**하고, 그 PendingCommit은 private/public/relay 등의 pathKind를 가진다. 이때 clipboardOnStart는 **Fresh**가 된다.

따라서 A standby 복귀 tap→activation 시작 거부/실패→자동 ordinary fallback 성공 순서에서, 대기 중 phone/B에서 복사한 text를 새 A binding의 baseline으로 seed/hold하지 않는다. clipboard sync가 켜져 있으면 기존 session_started push 경로가 허용될 수 있다. **같은 tap의 내부 fallback은 새 명시 cold 접속으로 승인한 사용자 행동이 아니다.** RETRY가 suppression을 풀지 않는 이유와도 같다.

필요: transport/pathKind와 **prepared 복귀라는 사용자 의도**를 분리해, 같은 intent의 fallback까지 baseline/읽기 불가 Hold를 새 binding에 적용한다. 사용자가 별도로 요청한 새 cold 접속의 기존 계약은 유지할 수 있다. 옛 park 모델은 되살리지 않는다.

검증은 clipboard sync를 켠 격리 endpoint에서 A 대기→phone/B copy→A 복귀 성공과 activation 실패 후 fallback 성공을 모두 확인한다. 자동 push 0, 이후 실제 새 copy 1회, 읽기 실패 Hold, 다른 PC binding baseline 오염 0이어야 한다. 사용자 PC clipboard를 임의로 조작하지 않는다. 현재 fixture의 sync-off 및 Seed/Hold/Fresh helper 단위만으로 이 분기를 확인했다고 하지 않는다.

## D1 계측 한계 — strays는 영상/제어 tail 전용 계수가 아님

Receive는 target에서 온 8바이트 이상 datagram을 먼저 stray로 세우고, standby packet/HelloAck만 제외한다. **정상 Punch도 포함**되므로 strays/tailActive는 이전 session의 영상·제어만을 뜻하지 않는다.

Codex 직접 `.claude/codex_ka_e_review/tail_punch_probe.cpp`: 실제 lease를 Parked로 준비하되 **activation/session을 한 번도 만들지 않고**, FakeHost가 정상 Punch 하나만 전송했다.

```
no_session_ever=1 before_strays=0 after_strays=1 tail_active=1
```

exit0은 관측 한계 재현이다. exe SHA-256 **75903da60ffccbf8add1c8258be836086a3e7fcad0d92077d43d7beec346e4b8**.

관측값을 정리 완료/grant proof로 쓰지 않는 UI 방향은 유지한다. 다만 **known bootstrap Punch를 tail에서 분리**하거나 이 계수를 일반 target datagram 관측으로 한정해 문서/비용 근거를 맞춘다. strays만으로 “잔류 영상 N개/안정 영상0”을 증명하지 않는다. 실제 ke_r19의 0~25 변동 원인이 Punch였다고 이 probe만으로 확정하지도 않는다. 새 wire나 이번 범위 밖의 비용 체계 재설계는 필요 없다.

## 후속 경계

검증용은 R1~R3와 계측 의미를 같은 e 정본에 취합해 기존 작업용1로 연결한다. 정상 N2·최신 탭·Book metadata·N4 한도 helper를 무관하게 다시 작성하지 않는다. 소유/복귀 경계의 고정 후보를 SOURCE 뒤 독립 AVD와 clipboard/raw-AUTO/Activity 반복 동작으로 확인한다. prelogin 실제 UI·실 NAT·비용·폰/PC 교차·목표 p95는 기존 미검증으로 유지한다.

feat는 아직 Host a7773e6을 포함하므로 최종 통합에는 검수된 534242b/ecf1004 계열의 Host 큐 수명·시험 보수를 반영한다. APK의 동작에 직접 영향 없는 공용 주석 차이 때문에 새 AVD 전량 재승인 대기를 만들지는 않되, 실제 통합 candidate·파일 hash는 연결한다.

Codex 제품 수정·제품 빌드·AVD/사용자 앱·clipboard·운영/NAS·삭제·commit/push 없음. 직접 loopback probe/판정서만 저장소 안에 보존했다. history/계획/Git MCP 기록은 같은 e 작업에서 취합한다. 사용자 현재 조치 없음.
