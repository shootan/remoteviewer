# apk-decoder-inbox C r2 — SOURCE 패킷 (2026-10-30, task_apk_inbox_c_r1.md r2 절 / 정본 codex_apk_inbox_c_src_r1.md)

## 1. 고정
- worktree `C:\work\gnlink-wt\apk-inbox-c`, branch `feat/apk-inbox-c`, base **79b5c25**, **HEAD d50415e**(tracked 변경 0).
- r1(…5fe90a9) 위의 r2 commit은 세 개입니다:
  - **e394c13** N1, N3 (제품)
  - **8a40eff** N2, §8 (a) (제품)
  - **d50415e** 음성 대조 (시험만)
- diff:
  - 전체: `caplife/c_inbox_r2.diff` (79b5c25..d50415e, `1215edfc…b2b4`)
  - r2만: `caplife/c_inbox_r2_delta.diff` (5fe90a9..d50415e, `503ccdd9…4738`, 11파일 +726/−91)
- SUMS: `caplife/c_inbox_r2_SHA256SUMS.txt` (`c75efd66…b99a`) — 바뀐 17파일의 d50415e blob sha256.
- worktree 밖에 만든 것, push·서명·게시, 실기(AVD)는 모두 0입니다.
- untracked는 worktree 안의 다음뿐입니다: `neg/`, `build-neg/`, `build/`, `apk_out*/`.

## 2. 보수 (Codex 항목별)
| 항목 | 보수 | 위치 |
|---|---|---|
| **N1-a** | `MarkLoss(seq, usableKeyOutside)`:<br>• 손실 뒤 **첫 usable IDR 전까지의 앞 delta를 큐에서 제거**합니다(`chainTrim`).<br>• IDR이 없으면 전부 제거하고, gap은 가장 새 seq 뒤에서 엽니다.<br>• 손실 뒤 IDR이 inbox 밖(pending slot·투입 중)에 있으면 큐는 그 suffix이므로 trim도 gap도 하지 않습니다.<br>worker의 dequeue에 방어 `BeyondGap`을 둡니다. | decoder_chain_inbox.hpp, sink `NoteChainLossLocked` |
| **N1-b** | IDR이 실제로 CommitQueue되면 `inbox_.OnKeyCommitted(seq)`가 **그 IDR보다 앞의 gap만** 닫습니다. 그 뒤에 생긴 손실의 gap은 남습니다.<br>sink는 committed-key marker를 둡니다:<br>• 이미 commit된 IDR보다 앞의 손실은 회복된 것으로 봅니다(`lossRecovered`).<br>• marker는 codec reset, inbox clear, 무효화 때 지웁니다. | 같은 곳, sink commit 지점 |
| **N3** | 30회 자동 재정렬 안전판을 **삭제**했습니다. 오래되거나 중복된 AU는 몇 번이 와도 버리고 셉니다. 재시작은 명시적 reset 뒤 새 IDR로만 합니다. | decoder_chain_inbox.hpp |
| **N2** | `RequestKey`: limiter `Request`와 descriptor merge를 **keyLedgerMu_ 한 구간**에서 합니다(RX, select answer, owed, sink wants, pipeline 모두).<br>scheduler의 `NextAction`(pending 소비)과 accepted descriptor take도 **한 구간**에서 합니다(parkMu_ → keyLedgerMu_, 로컬 작업만).<br>`ClearKeyLedger`(새 pick·reset)는 descriptor와 limiter pending을 함께 소비합니다.<br>descriptor 없는 KeyframeRequest는 **취소로 처리해 보내지 않고** 셉니다(`KeyRequestsDroppedCancelled`). legacy로 읽지 않습니다.<br>held 검사: snapshot과 version을 잡고, sink에는 lock 없이 묻고, lock 아래에서는 **같은 version의 snapshot만** 처리합니다. 그 사이 새 held는 통째로 남습니다.<br>sink 조회와 network는 ledger lock 밖에 둡니다. | native_video_client_session.cpp/.hpp |
| **§8 (a)** | owed 의무의 **limiter 채택 8회가 상한**입니다. 소진 뒤 새 episode 예산은 없습니다.<br>세션은 8번째 owed 요청이 송신되거나 drop된 뒤 sink에 `OnDecoderKeyOwedExhausted(token)`을 알립니다(기본 virtual + binding adapter).<br>sink 동작:<br>• 다른 token이면 무시합니다(`coveredStale`).<br>• 현재 token이면 **covered**: 자기 요청 0(owed와 같은 IDR), 최종 window 4 s를 둡니다.<br>• window가 지나도 usable IDR이 없으면 **coveredExhausted**(열린 episode는 `MarkExhausted`).<br>• gate를 거짓으로 지불하지 않습니다.<br>• 늦은 IDR은 정상 decode되어 의무를 지불하고 상태를 닫습니다.<br>• 재무장·reset·새 surface도 상태를 닫습니다.<br>• pipeline 같은 독립 원인은 그대로 유효합니다. | session.cpp, sink, adapter |

⚠️ **owedAttempts_는 `Request().queued` 시점에 증가합니다.** 따라서 8은 **limiter 채택(queued) 8회**이고, "wire 송신 8회"가 아닙니다. 송신은 `OnDecoderKeyRequestSent`, 취소는 `KeyRequestsDroppedCancelled`/`Stale`로 따로 셉니다.

**새 DebugStatus 필드**(plain 빌드에도 있음): `chainTrim`, `lossRecovered`, `beyondGap`, `covered`, `coveredExh`, `coveredStale`. r1의 `chainResync`는 N3로 삭제됐습니다.

## 3. 음성 대조 (각 보수가 이전 source·mutant에서 빨개짐; `c_neg_run.ps1`, neg\<name>을 각자 build dir에서)

**이전 source 5fe90a9** (sink, chain 헤더, 세션 소스):
- pure `decoder_chain_inbox_r2_contrast_test` **2 FAIL**:
  - Codex N1-a 반례: `first=12 key=0`
  - N3: `admitted=1`
- `android_decoder_inbox_c_test` **6 FAIL**:
  - [C9] N1-b: `fed=2 tailGap=1 episodes 0->2 gapDrops=1`
  - [C11] §8 5건
- session test는 5fe90a9 세션 소스가 새 hpp로 **빌드되지 않아** mutant로 대조했습니다.
- **[C10](sink 수준 N1-a)은 5fe90a9에서도 PASS**였습니다. 이 순서에서는 옛 slot도 d3을 떨어뜨리고, 없는 필드를 최대값으로 읽었습니다(d50415e에서 필드 존재를 요구하도록 고침). 따라서 N1-a의 old-source 대조는 **pure 반례**입니다.

**현재 source에서 보수 하나씩 되돌린 mutant**:

| mutant | 빨개지는 대조 |
|---|---|
| r2_no_trim (MarkLoss가 trim 안 함) | pure 3, contrast 1, inbox_c [C10] |
| r2_no_keycommit (OnKeyCommitted 무효) | pure 1 (sink C9는 keyOutside가 막음) |
| r2_no_keyoutside | 없음 (OnKeyCommitted가 막음) |
| r2_no_keyoutside + no_keycommit | inbox_c **[C9]** |
| r2_no_consume (ClearKeyLedger가 pending 안 비움) | 없음 (빈 descriptor 취소가 막음) |
| r2_empty_legacy (빈 descriptor를 legacy로 송신) | 없음 (pending 소비가 막음) |
| r2_no_consume + empty_legacy (r1의 취소 경로 재현; RequestKey는 여전히 한 구간) | session **[S5]** |
| r2_held_clear_all (held version 비교 없음) | session **[S6]** |
| r2_publish_split (publish를 두 구간으로) | session **[S9]** |
| r2_no_exhaust_notice (owed 소진 통지 없음) | session **[S8] 2** |

- N1-b와 N2(취소)는 각각 **이중 방어**라서, 한쪽만 되돌리면 다른 쪽이 막습니다. 둘 다 되돌려야 대조가 빨개집니다.
- 처음 쓴 S9는 control thread 자기 publish여서 split을 구분하지 못했습니다(mutant PASS로 발견). 시험 thread publish로 다시 쓴 뒤 split에서 FAIL했습니다.

**새·보강 대조 (현재 source PASS)**:
- [S2] accepted 지점의 owner 변경 + 새 pick
- [S5] dequeue 직전 pick + owner 변경 → host 수신 0
- [S6] held 검사 중 새 pipeline 요청 보존(2회)
- [S7] publish 안 owner 변경 → stale, 송신 0
- [S8] owed: limiter 채택 8회이고 **host 수신도 8회**(최종 1회 + 반복 5회 모두 8) → covered → 4 s 뒤 covered-exhausted, 추가 요청 0 → 늦은 IDR decode로 종료
- [S9] 다른 thread publish를 400 ms 열어 둠 → 취소 0, 401 ms에 수신
- [C9] 뒤 손실의 gap 유지, IDR commit 전 reset 시 gap 유지
- [C10] slot trim
- [C11] stale token 무시, covered 자기 요청 0, exhausted 뒤에도 gate 미지불, pipeline 유효, 늦은 IDR, 새 surface
- pure N3: 40+40회, 정상 증가, 명시 reset

## 4. 회귀 (HEAD d50415e, 전체 tree 빌드 error 0; exit 코드와 RESULT 줄을 함께 읽음)
| 시험 | 결과 | 반복 |
|---|---|---|
| decoder_chain_inbox_test | 51/51 exit 0 | 5/5 |
| decoder_chain_inbox_r2_contrast_test | 2/2 | 5/5 |
| android_decoder_inbox_test | 12/12 | 5/5 |
| android_decoder_inbox_c_test | 41/41 | 5/5 |
| android_decoder_inbox_session_test | 22/22 (S1 5, S2·S3·S5·S6·S7·S9 각 2, S8 5) | 5/5 |
| android_decoder_liveness_test | 68/68 | 5/5 |
| owed_key 32 · geometry 10 · binding 17 · decode_stats 42 · session_liveness 9 · pending_slot 21 · prelogin_select 60 · idr_recovery 49 | 각 exit 0 | 1회 |

## 5. APK (d50415e 깨끗한 tree, `c_apk_build_r2.ps1`, `apk_out_c2`)
**제품 바이트의 commit은 8a40eff**입니다(d50415e는 시험만 바꿈). 빌드는 d50415e의 깨끗한 tree에서 했고, 빌드 전후 모두 tracked 변경 0이었습니다. 빌드 스크립트는 `c_apk_build_r2.ps1`(`8cc0c981…a183`)입니다. SUMS는 `apk_out_c2\SHA256SUMS_c2.txt`(`0ce2feea…94b5`)이고, 경계 확인은 `build\apk_check_c2.txt`입니다.

| APK | sha256 | bytes | build | 용도 |
|---|---|---|---|---|
| plain-debug-c2.apk | `ccd965c7f8263fcef8ce8185cfc70a2dcf3a69a8994359c7de179e49d994660c` | 20,717,756 | `assembleDebug -Pgnlink.testDirectoryUrl=http://10.0.2.2:18410` | 120 s arm (검증용은 자기 빌드 hash로 교체) |
| on-debug-c2.apk | `5650c87e8456e96dc1e778528ab54bd9dcd84b31a8ff337dff3aa5414b3fb1b6` | 20,717,756 | 위 + `-Pgnlink.testStandbyAutoPrepare=true` | `-ApkOn`(hash만) |
| trace-debug-c2.apk | `ae03facf8ce61d6499462b408e01b0ab04a6062d5c1267ec43a0b96d127aec91` | 20,739,164 | 위 plain + `-Pgnlink.livenessTrace=true` | 별도 진단 |
| release-c2-unsigned.apk | `6f934a99e78d1dc7501b57eb749b667b0247c09ff8a2e41d9af1e783bc90e39e` | 10,488,097 | `assembleRelease` | release 경계(서명·게시 없음) |
| seam-debug-c2.apk | `58993f240f451893815fc45a05a71dc81c87e8a49d4a25a077cb5b11f03a3a73` | 20,725,276 | `assembleDebug -Pgnlink.shTestSeam=true` | seam 경계 |

경계(실제 바이트의 문자열 수):
- **C 코드**: 다섯 APK 모두 native 4/4에 있습니다.
- **plain/on**: trace 0/4, seam 0, `WaitIdleForTest` 0, fixture 주소 1(dex). BuildConfig는 `LIVENESS_TRACE=false`이고, `STANDBY_AUTO_PREPARE`는 plain false, on true입니다.
- **trace**: trace 4/4, `LIVENESS_TRACE=true`.
- **release**: trace 0, `WaitIdleForTest` 0, 제품 주소 1, fixture 주소 0.
- **seam**: seam 표지 1, 제품 주소.
- r1 경계와 같습니다.

⚠️ **r1 패킷 §6 정정**: r1은 *"plain-debug-c1 17.4 MB"*라고 적었지만 틀렸습니다. Codex가 직접 연 r1 C plain은 20,710,732 bytes였습니다. r2 plain-debug-c2도 20,717,756 bytes이고, B plain-r5(20,509,196)와 비슷한 크기입니다.

## 6. 기록 정정과 측정 계획 갱신
- **정정**: r1 패킷의 *"검토를 받지 않은 B-only 배치에서 파생"*은 틀렸습니다. `75fb1cd1…` B-only는 **앞서 직접 diff 검토를 받고 한정 허용된** 배치이고, C 배치(`c1_shoff_batch.ps1`)는 그 범위에서 파생했습니다.
- **offSha**: debug 빌드는 바이트 재현이 되지 않습니다. 검증용의 **깨끗한 tree 독립 빌드 hash**로 고정합니다(Codex 허용). 이때 함께 기록할 것:
  - commit과 clean 상태
  - build 명령과 `-P` 인자, toolchain
  - BuildConfig와 endpoint
  - debug/release, signer, ABI
  - trace/seam/auto-prepare 속성
  - 실제 설치 대상
  - 배치 변경은 경로·hash·그 고정 기록만으로 제한하고, 실행 전 snapshot hash를 남깁니다. hash 교체만으로 source 동등성을 주장하지 않습니다.
- **지표 정의**:
  - `inboxDrops`와 `gapDrops`는 B와 C에서 정의가 다릅니다(r1 패킷 §9 표). 비율을 직접 비교하지 않습니다.
  - C r2의 `inboxDrops`에는 N1-a trim(`chainTrim`)도 들어갑니다.
  - 요청 수는 `recWants`/`recSent`/`keyTaken`과 Host 수신으로 봅니다.
  - covered 상태는 `covered`/`coveredExh`로 봅니다.
- **trace `peakPre`**: C에서 갱신하지 않으므로 trace C run의 0은 **미계측**입니다. 측정값이 아닙니다.
- **video_debug 줄**이 r1보다 약 80자 늘었습니다. 512 KB 회전 표지를 run 뒤에 확인합니다.
- **측정 스크립트** `c_measure.ps1`을 **`fefd6c08…126e`로 갱신**했습니다. r2 필드 6개를 추가했고, covered·coveredExh는 상태값이므로 처음→끝 값으로 읽습니다. r1의 chainResync는 없습니다. 이전 판 `aacb50ea`는 r2 필드를 읽지 못합니다.
- **나머지 계획**(r1 패킷 §9)은 그대로입니다: 120 s SH-off, B1/B2 한정 baseline, 원인별 요청, 같은 구간 fps, 연결 지속, age.

## 7. 미검증·한계
- **실기 미실행**입니다. C r2의 효과는 실제로 측정하지 않았습니다.
- 다음도 미검증입니다:
  - SH-on
  - terminal UX와 명시 retry의 실제 사용자 행동(§8 exhausted 뒤 사용자 경로 포함)
  - 4 MiB 초과 IDR의 실제 빈도
- **세션 대조의 범위**: 제품 세션 + loopback fake host + stand-in NDK입니다. 실제 MediaCodec이나 GNLinkStream이 아닙니다.
- **5fe90a9 세션 소스는 새 hpp로 빌드되지 않습니다.** N2와 §8의 old-source 대조는 위 mutant(같은 경로의 r1 동작 재현)로 대신했습니다.
