# apk-decoder-inbox C r1 — SOURCE 패킷 (2026-10-30, task_apk_inbox_c_r1 / codex_apk_inbox_c_impl_go_r1.md)

## 1. 고정
- worktree `C:\work\gnlink-wt\apk-inbox-c`, branch `feat/apk-inbox-c`, base **79b5c25**.
- commit은 세 개입니다. 제품 소스는 앞의 둘에 있고, 세 번째는 시험만 바꿨습니다.
  - **0832e37**: 구현과 sink 수준 대조.
  - **53e3fdb**: 세션 수준 대조와 시험 hook(TEST_HOOKS 빌드 전용).
  - **5fe90a9**: 시험만 고침(S3 결정화, liveness [13] 경합).
- 전체 diff는 `caplife/c_inbox_r1.diff`(79b5c25..5fe90a9, `3b69c148…4b00`)이고, 바뀐 파일 16개의 sha256은 `caplife/c_inbox_r1_SHA256SUMS.txt`(`e3811dd3…4222`)에 있습니다.
- APK는 **53e3fdb의 깨끗한 tree**에서 빌드했습니다(tracked 변경 0). 5fe90a9는 시험만 바꿨으므로 제품 바이트와는 관계없습니다. SUMS는 `apk_out\SHA256SUMS_c1.txt`(`74a3034f…f07`)입니다.
- worktree 상태: 5fe90a9에서 tracked 변경은 0입니다. untracked는 worktree 안의 `neg/`(음성 소스), `build-neg/`, `apk_out/`, `build/`뿐이고, worktree 밖에 만든 것은 없습니다. push, 서명, 게시는 하지 않았습니다.

| APK | sha256 | build | 용도 |
|---|---|---|---|
| plain-debug-c1.apk | `9496645943955da504032f94fb898198abf5dfef12a8c4e6995ed8284c2de3db` | `assembleDebug -Pgnlink.testDirectoryUrl=http://10.0.2.2:18410` | 검증용 120 s arm (B의 plain-debug-r5와 같은 조건) |
| on-debug-c1.apk | `575c444ecc21bb280e8b8a3acd857a8eae75c34352f99956ee53fd4fc3c689c7` | 위 + `-Pgnlink.testStandbyAutoPrepare=true` | ke 하네스 `-ApkOn`(hash만 기록, 설치 안 함) |
| trace-debug-c1.apk | `902664d6e9fa0e9983c5373d32158285700938326d44b540f625d1e9ad755417` | 위 plain + `-Pgnlink.livenessTrace=true` | 별도 진단용. plain과 수치를 섞지 않습니다 |
| release-c1-unsigned.apk | `8da6cb14865828a8217173e7ffea847b0705027cf1b0df2331fbc9a18eee853a` | `assembleRelease` | release 경계 확인용(서명·게시 없음) |
| seam-debug-c1.apk | `df62bee5e37cafe6f84e3e22a8e9d9302447e387ddd15cc3c30c52d47246589d` | `assembleDebug -Pgnlink.shTestSeam=true` | SH seam 빌드 경계 확인용 |

## 2. 구현 (설계 r3 대비)
| 항목 | 구현 | 위치 |
|---|---|---|
| 순서 | 같은 generation에서 seq가 중복이거나 더 오래되면 버리고 셉니다(`(int32_t)(b-a)>0`, wrap 안전). 조립기가 seq 순으로 전달하므로(`native_video_client_shared_core.cpp` PopDelivery :906-915, :1058) **방어용**입니다. 같은 drop이 30번 연속되면 순서를 다시 잡는 **안전판**을 추가했습니다(셉니다) | `decoder_chain_inbox.hpp` |
| inbox overflow (8 frame / 4 MiB, 상한 그대로) | ① IDR+suffix가 들어가는 **가장 이른 IDR**을 남기고 그 앞의 옛 chain은 건너뜁니다(손실 아님). ② IDR은 있지만 어떤 suffix도 안 들어가면 마지막 IDR을 남기고 가장 새 frame을 자릅니다(tail 손실). ③ IDR이 없으면 전부 버립니다(손실) | 같은 파일 Trim |
| tail gap | 손실 뒤의 delta는 RX에서 거절합니다. 손실 지점 뒤의 IDR이 오면 열립니다. 4 MiB를 넘는 frame은 그 자리에서 거절합니다(손실) | Offer |
| ③을 고른 이유 | 지속 과부하(B1/B2의 에뮬레이터)에서 codec이 따라가지 못하는 delta를 더 먹이지 않고, 요청한 IDR로 바로 넘어갑니다. 대신 tail cut(②)보다 화면에 보이는 옛 frame이 적습니다 | — |
| episode | 손실당 1개입니다. t0에 요청하고, 이후에는 **실제 송신** 뒤 1 / 2 / 4 s마다 요청합니다(최대 4). 4번째 뒤 **최종 window 4 s**(`kFinalWindowUs`)를 기다린 다음 exhausted가 됩니다(한 번 기록하고 입력은 막지 않음). 닫히는 것은 손실 지점 뒤의 IDR이 **CommitQueue**될 때뿐입니다. 후보를 다시 잃으면 같은 episode가 이어집니다. 나간 적 없는 want는 `kReissueUs` 4 s 뒤에 다시 냅니다(attempt 아님) | KeyRecoveryEpisode |
| 후보(보류) | 손실 지점 뒤의 IDR이 inbox, pending slot, 또는 지금 codec에 들어가는 중(mu_ 해제 상태)이면 요청하지 않습니다 | `RecoveryCandidateLocked` |
| owed 합치기 | gate가 IDR을 빚지고 있는 동안(KeyLost / Rearm, 세션 8회 × 1 s) episode는 **자기 요청을 더하지 않습니다**. 같은 IDR 의무를 하나의 교환으로 봅니다(4+8 아님). 그 commit이 episode도 닫습니다 | `TakeDecoderKeyRequestAs` |
| 원인 집합 | recovery / codec-up / abort / owed / pipeline / legacy의 bit 집합으로 둡니다. 합칠 때 모든 원인을 남기고, 더 새 sink epoch가 오면 옛 sink 원인만 버립니다 | `decoder_key_request.hpp` |
| D8 | 세션이 held(limiter 거부)와 accepted(pending) 두 지점에서 sink에 `DecoderKeyRequestValidCauses`로 묻습니다. 유효한 원인이 0이면 보내지 않습니다(`KeyRequestsDroppedStale`). 송신 교환이 시작되면 `OnDecoderKeyRequestSent`가 retry 시계를 시작합니다. lock은 짧은 조회에만 잡고 network 동안에는 잡지 않습니다. Android binding adapter가 token으로 owner를 거릅니다 | session.cpp, adapter |
| await-key / pending slot | 둘 다 같은 episode의 chain 손실로 칩니다. slot의 손실 지점은 **버려진 frame**입니다(도착한 IDR이 아님. r1 구현 중 고친 결함으로, mutant `m_slot_loss_key`) | sink |
| 유지 | N1 예약과 KeyDelivered, 생성 21회, 2 s detach, RX와 control은 codec을 기다리지 않음, wire / limiter / transport / 설정 / 상한 | — |
| 진단 | DebugStatus(즉 video_debug)에 chainSkipped, chainDup, chainStale, tailGap, recovery, episode, asks, episodes, recClosed, recExhausted, recWants, recSent, recMoot, keyTaken, keyEpoch를 추가했습니다. trace와 무관하게 **plain 빌드에서도 출력**됩니다. B와 다른 상시 진단 비용은 이 문자열뿐입니다 | — |

## 3. 자체 대조 (Windows, 제품 sink + stand-in NDK / 제품 세션 + fake host)
- `decoder_chain_inbox_test` **45/45**: 결정표 ①②③, 순서·wrap·안전판, MarkLoss, schedule(t0/1/3/7), 최종 window, exhausted, reissue, 후보 보류, 같은 episode 재개, 무효화, descriptor 합치기.
- `android_decoder_inbox_test` **12/12**(r1 기준선. ac587dd에서는 [1]–[5]가 FAIL).
- `android_decoder_inbox_c_test` **31/31**(plain 빌드). 확인 항목:
  - [C1] 순서와 wrap
  - [C2] 4 MiB 초과 IDR
  - [C3] 후보를 다시 잃음
  - [C4] t0/1/3/7, 최종 window 뒤 exhausted(약 11.1 s), exhausted 뒤 IDR 정상 decode, 새 pick 뒤 새 episode
  - [C5] D8: pick, commit, 합친 원인의 일부만 무효, 후보 보류 중 moot, abort, 늦은 send
  - [C6] binding adapter: owner, park, unbind
  - [C7] 4 s 과부하: 상한 유지, **가장 새 IDR**에서 재시작
  - [C8] codec create 3 s 지연
- `android_decoder_inbox_session_test` **9/9**(제품 세션). **host가 받은** 요청으로 판정합니다.
  - [S1] 정확히 4회이며, 첫 요청 기준 0 / 1003 / 3011 / 7022 ms입니다. exhausted가 되고, keepalive gap은 56 ms이며, 다음 IDR은 decode됩니다.
  - [S2] accepted 지점: 다른 PC가 bind된 뒤의 요청은 **보내지 않습니다**.
  - [S3] held 지점: 그 자리에서 버리고, accepted 지점에도 가지 않습니다.
- 반복:
  - session과 liveness는 시험을 고친 뒤 각 **5/5**입니다.
  - inbox_c와 inbox는 각 3/3입니다.
- 최종 회귀(HEAD 5fe90a9, 전체 빌드 0 error): **§7 표**.

## 4. 음성 대조 (각 경계가 이전 소스 / mutant에서 빨갛게)
`neg\<name>`에 sink 파일, chain 헤더, 세션 소스를 복사하고 한 곳만 바꾼 뒤 별도 build dir에서 빌드했습니다(`c_mutants.py` 824fbe15…, `c_neg_run.ps1` 00017241…).

| 소스 | 빨개지는 대조 (FAIL 수) |
|---|---|
| **79b5c25 sink** (이전 소스) | inbox_test 6, inbox_c_test 24, session_test 5(S1 등), liveness [6][12](C 기준으로 바꾼 두 항목) |
| m_dropall (chain 무시, 넘치면 전부 버림) | pure 5, inbox_test [3], liveness [6](IDR 유지) |
| m_nodedupe (순서 검사 없음) | pure 5, inbox_c [C1] |
| m_anyidr (아무 IDR이나 episode를 닫음) | pure 2(재실행), inbox_c [C4] 2, session S1 3 |
| m_attempt_on_want (want에서 attempt를 셈) | pure 8, inbox_c 3, session S1 3 |
| m_notailgap (손실 뒤 delta 허용) | pure 6, inbox_test [2b], inbox_c [C2] 2 |
| m_nocandidate (후보 보류 없음) | pure 2, inbox_c [C3] 2 |
| m_exhaust_blocks (exhausted가 입력을 막음: "멈추고 PASS" 꼴) | inbox_c [C4], session S1 |
| m_no_owed_coalesce (owed 위에 episode 요청을 더함, 4+8) | inbox_c [C2][C3], liveness [12] |
| m_slot_loss_key (slot 손실 지점을 도착한 IDR로 잡음) | inbox_c 4 |
| m_skip_accepted (accepted 지점 검사 생략) | session [S2] 2/2(보냄) |
| m_skip_held (held 지점 검사 생략) | session [S3] 2/2(accepted에 도달) |

- 첫 negative pass에서 다음 두 결과는 판정에 쓸 수 없었습니다.
  - m_anyidr의 pure 결과: 헤더를 `""`로 include해서 src 사본이 먼저 잡혔습니다. `<>`로 고친 뒤 다시 돌렸습니다.
  - 일부 mutant의 S3 결과: 당시 S3 설정이 불안정했습니다. 재실행한 것만 표에 썼습니다.

## 5. 기존 시험을 바꾼 것 (이유와 함께)
- **fixture 정렬**:
  - owed_key, binding, decode_stats, liveness는 frame을 seq 0으로 만들고 있었습니다. 순서를 아는 inbox에서는 모두 같은 AU가 됩니다.
  - 그래서 조립기처럼 증가하는 seq를 주도록 바꿨습니다.
- **정책이 바뀌어 기준이 바뀐 것**(설계 r3가 r1의 "가장 오래된 것부터 버림"을 대체):
  - liveness [6]: 이제 IDR을 **유지**하고(빚 없음), tail 손실은 1 episode / 1 요청입니다.
  - liveness [12]: 거절된 IDR은 owed가 되어 그 의무로 요청되고, sink는 두 번째 요청을 더하지 않습니다.
  - inbox_c [C2]와 [C3]의 owed 경로도 같은 규칙입니다.
- **inbox_test [2]**:
  - IDR 뒤의 10 delta를 3 ms 간격으로 보내게 했습니다(이 case의 다른 구간과 같은 간격).
  - 한꺼번에 보내면 그 자체로 별도 손실(10 > 8)이므로 새 [2b]에서 따로 확인합니다.
  - Rx 대역은 descriptor를 받고 "송신"을 알려 주도록 바꿨습니다(세션과 같은 동작).
- **liveness [13]**: released seq를 다음 attach **전에** 읽게 했습니다. 시험 자체의 경합이었고, C 빌드에서 1/3 FAIL이 났습니다. 79b5c25 sink는 5/5였는데 worker가 늦게 와서 드러나지 않았던 것입니다.

## 6. 경계 (plain / trace / release, 실제 바이트에서 문자열 수로 확인)
- plain:
  - native 4/4 ABI에 C 코드 있음, `liveness-trace` 0/4, `WaitIdleForTest` 0/4.
  - dex에 fixture 주소 1, BuildConfig `LIVENESS_TRACE=false`.
- trace: `liveness-trace` 4/4, `LIVENESS_TRACE=true`.
- release: C 코드 4/4, trace 0, `WaitIdleForTest` 0, dex에 제품 주소(gnlink.shotan.net) 1, fixture 주소 0.
- seam: dex에 seam 표지 1.
- `assembleRelease -Pgnlink.livenessTrace=true`는 거부됩니다("is for debug builds").
- 시험 hook(`SetTestKeyRequestHook`, `TestConfigureKeyframeLimiter`)은 `REMOTE60_CLIENT_SESSION_TEST_HOOKS`일 때만 들어가고, APK와 제품 빌드에서는 정의되지 않습니다.
- APK 크기: plain-debug-c1은 17.4 MB로, B의 plain-debug-r5(20.5 MB)보다 작습니다. `.so` 크기는 비슷하므로(arm64 3.41 vs 3.37 MB) 차이는 그 밖의 묶음에서 나옵니다. 원인은 조사하지 않았습니다.

## 7. 최종 회귀 (HEAD 5fe90a9, tracked 변경 0, 전체 tree 빌드 error 0, 각 1회; exit 코드와 RESULT 줄을 함께 읽음)
| 시험 | 결과 |
|---|---|
| decoder_chain_inbox_test | 45/45 exit 0 |
| android_decoder_inbox_test | 12/12 exit 0 |
| android_decoder_inbox_c_test | 31/31 exit 0 |
| android_decoder_inbox_session_test | 9/9 exit 0 |
| android_decoder_owed_key_test | 32/32 exit 0 |
| android_decoder_geometry_test | 10/10 exit 0 |
| android_decoder_binding_test | 17/17 exit 0 |
| android_decoder_decode_stats_test | 42/42 exit 0 |
| android_decoder_liveness_test | 68/68 exit 0 |
| android_decoder_session_liveness_test | 9/9 exit 0 |
| decoder_pending_slot_test | 21/21 exit 0 |
| client_session_prelogin_select_test | 60/60 exit 0 |
| android_decoder_idr_recovery_test (제품 세션, owed 재요청 경로, 긴 시험) | 49/49 exit 0 |
- `udp_control_e2e_test`는 이 환경에서 SKIPPED(exit 77)입니다. `native_video_client_shared_core_test`는 PASS 줄 형식이 아니어서 exit 0만 확인했습니다.

## 8. 미검증·한계
- **실기(AVD) 미실행**: C의 효과는 아직 실제로 측정하지 않았습니다(아래 9절, 검증용).
- **SH-on, terminal UX, 명시 retry 표면의 lifecycle, 4 MiB 초과 IDR의 실제 빈도, control-only 원인**: 미검증입니다.
- **discontinuity**: 조립기 자신의 요청(pipeline)과 그 뒤 await-key episode가 따로 요청할 수 있습니다. 서로 다른 원인으로 남겨 두었습니다(설계 r3 "다른 원인 요구는 지우지 않음").
- **owed 합치기 — 정정과 Codex 결정 항목**:
  - 코드가 실제로 하는 일: sink는 `gate_.OwedToken() != 0`인 동안 episode의 요청을 막습니다(`android_video_decoder.cpp:769-770`).
  - `keyOwed_`는 그 IDR이 **지불될 때**(`selection_ack_gate.hpp:154`)나 reset으로만 내려갑니다. sink는 세션의 owed 시도 수(8)를 모릅니다.
  - 그래서 세션의 owed 8회가 IDR 없이 끝나면 **그 뒤에는 아무도 요청하지 않습니다**. Host의 주기 IDR이나 명시 경로(새 pick, 새 surface 등)를 기다립니다.
  - B는 owed와 상관없이 overflow 때마다 요청했으므로, 이것은 C의 **새 성질**입니다.
  - "owed 8회를 다 쓴 뒤의 요청 주체"는 **Codex가 결정할 항목**입니다. 후보:
    - (a) 지금처럼 둡니다(owed 8이 상한).
    - (b) 소진 뒤에는 episode가 자기 예산으로 이어 받습니다(세션이 owed 소진을 sink에 알려야 함).
  - 이번 commit에서 제품 코드는 바꾸지 않았습니다.
- **age**:
  - inbox에는 frame / byte 상한만 있고, age는 상한이 아니라 **측정**입니다(설계 r2 §1). 측정은 trace 빌드에만 있습니다(`ageQueue` / `agePop`, LivenessTraceLine).
  - case ①은 들어가는 IDR 가운데 **가장 이른** 것을 남기므로, 옛 내용이 최대 8 frame(30 fps에서 약 270 ms) 남을 수 있습니다. 설계가 이를 받아들이고 측정하라고 했으며, trace APK의 `ageQueue` / `agePop`이 그 측정입니다.
- **trace 지표 하나가 갱신되지 않음**: `ltInboxPeakPreFrames_` / `ltInboxPeakPreBytes_`(trace의 inbox peakPre)를 C에서는 갱신하지 않습니다. trace C run에서는 0으로 보이며 측정값이 아닙니다. trace B는 값이 있었으므로 비교하지 않습니다.
- **S3 token 설정에서 limiter가 +1 s 재요청을 받은 이유**: 찾지 못했습니다. 제품 limiter는 C가 바꾸지 않았습니다. 시험은 min interval 설정으로 결정화했습니다.

## 9. 검증용 독립 Android 120 s 관측 계획 (SOURCE 뒤, 검증용이 실행)
- **독립 빌드의 hash**:
  - 이 toolchain에서 **debug 빌드는 바이트 재현이 되지 않습니다**. 같은 53e3fdb tree를 두 번 빌드하니 plain-debug가 7ae63ca7…와 94966459…로 달랐습니다.
  - release는 두 번 모두 8da6cb14…로 같았습니다.
  - 그러므로 검증용은 **자기 빌드**(같은 commit, 같은 `-P` 인자)의 hash와 BuildConfig를 기록하고, 배치의 `offSha`를 그 값으로 바꿔야 합니다.
- 배치는 `caplife/c1_shoff_batch.ps1`(`f7b3dda6…4b72`)이고, 검토를 받지 않은 B-only 배치에서 파생했습니다.
  - 검토된 r3(`ab_shoff_batch_reviewed_r3.ps1` 6c519f11…)와 비교한 정규화 diff: A arm 2줄 삭제, B arm 3줄을 C arm 3줄로 교체, 주석 4줄 추가. 다른 줄은 같습니다.
  - arm: `c1-shoff`, ApkOff = plain-debug-c1(작업용 빌드 `9496645…`. 검증용 빌드로 교체), ApkOn = on-debug-c1(hash만).
- **video_debug에 C 필드가 실리는지**:
  - `MainActivity.kt:6892`가 `nativeGetVideoDebugStatus()`(= sink DebugStatus 전체)를 그대로 `diagnosticsLog.log("video_debug", …)`에 넘깁니다.
  - `SessionDiagnosticsLog.kt`는 파일에 줄 전체를 append합니다. 자르지 않습니다. logcat의 `Log.i`는 길면 잘릴 수 있지만 측정은 파일(app_final.log)로 합니다.
  - 파일은 512 KB에서 회전합니다(`MAX_LOG_BYTES`). B2 run의 app_final.log는 216 KB, video_debug 한 줄은 약 670 자였습니다. C는 줄당 약 260 자가 늘어 약 265 KB로 추정합니다.
  - run 뒤 `--- previous session chunk ---` 표지가 있으면 회전된 것이니 확인하세요.
- **지표 정의 (B와 C가 다른 것: 이름이 같다고 비교하지 않음)**:

| 필드 | B (79b5c25 이전) | C |
|---|---|---|
| inboxDrops | overflow로 버려진 frame | 체인 손실로 잃은 frame 전부: overflow ②③, 거절된 gap delta, 4 MiB 초과 |
| gapDrops | worker가 gap 뒤에 버린 delta | **RX에서** 거절한 gap delta(inboxDrops에도 포함) |
| keyWant | 원인별로 want flag를 세운 횟수 | 원인별 손실 기록 횟수. 요청 수가 아닙니다(요청은 recWants / recSent / keyTaken) |
| 그 밖 | — | chainSkipped(①에서 건너뜀, 손실 아님), episodes, asks, recSent, recExhausted, recMoot 등은 C에만 있습니다 |
- 나머지 조건은 B와 같습니다.
  - adb pin `7035cf5e…`과 30 s adb 관찰.
  - 하네스 r12(E 파일 13개 = `avd_guard_r12_snapshot`).
  - `KE_PLAN=e2-shoff`, `KE_WATCH_SEC=120`, relay trace, server.js `372e6eb8…`.
  - 첫 nonzero rc에서 중단, sh_run CreateNew 슬롯, 자기 해제.
- 측정은 `caplife/c_measure.ps1`(`aacb50ea…`)로 합니다. `ab_r4_measure.ps1` 7f020f17에 두 가지를 더했습니다.
  - watch fps: **같은 SEGMENT 안**의 첫 video_debug와 마지막 video_debug의 out 차이를 실제 시각 차이로 나눈 값.
  - C 필드의 watch 동안 변화량.
- 비교 기준선(같은 정의, B1/B2):

| | B1 (01:52) | B2 (02:17) |
|---|---|---|
| watch SEGMENT | 123.007 s | 122.675 s |
| Host keyframe-request / s (SEGMENT 분모) | 229 → **1.862** | 163 → **1.329** |
| Host [keyframe] / bytes (watch) | 229 / 380286 | 163 / 269846 |
| relay h2c / c2h B/s (watch) | 44250 / 625 | 44071 / 596 |
| watch out fps (같은 구간 video_debug 차) | **14.04** | **17.04** |
| watched / peer-lost | 120398 ms / 0 | 120746 ms / 0 |

- C 채택 판단은 GO 문서 기준입니다.
  - **두 가지가 함께** 필요합니다:
    - 중복 회복 요청이 원인별로 줄었다는 근거(Host 요청/s와 recSent, episodes, recMoot)
    - 영상 갱신(첫 영상, watch fps / gap, 지연, control, 메모리)이 유지되거나 나아졌다는 근거
  - 요청 수나 대역만 내려간 것은 완료로 세지 않습니다.
  - B1과 B2의 변동 폭(fps 14.0–17.0, 요청 1.33–1.86/s)과 측정 부하, 경계 오차를 함께 봅니다.
- trace APK 진단을 돌린다면 plain 수치와 섞지 않고 따로 봅니다.
