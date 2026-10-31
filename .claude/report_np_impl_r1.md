# report np-impl r1 — ⓪ 계측 / relay 라벨 + ①b 조기 relay (작업용2, 2026-10-27)

task np-impl / r1 / kind=implement (t-aoc9dnfy) / reply_to=t-wosscw8k. 정본 판정 `codex_np_design_r1.md`, 설계 정본 `proposal_np_r2.md`.

## 고정 후보
- **고정 후보 `5db95dd`** = 제품 c803768 + 시험·문서 5db95dd(제품 변경 0), branch `feat/np-early-relay`. 기준 spike/host-admission-r3 **0a5e71d**. worktree `C:/work/gnlink-wt/np-impl` 은 clean 이다.
- 범위: 제품 / 테스트 / 문서(아래 §1). 버전·서명·게시·push 는 하지 않았다. 운영 서버·NAS·공유기는 바꾸지 않았다.
- 원본 hash(sha256 앞 16): `server.js` 20cdf0a67d3c54bc, `directory_rendezvous.cpp` d2ca3e2994a74056.
- 빌드 `C:/work/gnlink-wt/np-impl-build`(Debug, 시험 seam 은 시험 target 에만):
  - GNLinkViewer.exe aead6655b5106d50 — mutant 원복 뒤 commit 소스로 다시 빌드한 것(이전 빌드 8d27fe1e… 와 소스 동일, 링크 산출물만 다름)
  - remote60_punch_any_test.exe 9da7c49baa4f21d3(5db95dd 소스)
  - remote60_np_early_bootstrap_probe.exe b4492168b59fdd0e
  - 이 빌드는 검증용이 독립 실행할 고정 산출물이 아니다. 검증용 build-verify 는 따로 만든다.

## 1) 변경

**제품**
- `apps/directory/server.js`
  - `/api/connect {relayEarly:true}` 를 받는다. **legacy relay lease** 에만 `relayEarly:"accepted"` 를 주고, eligible 항목에 `early` 와 `earlyWirePort` 를 기록한다.
  - `relayScheduleReply` 는 그 connect 의 **wire tuple** 에서 온 punch 에만 0 ms(+120×i, 3회)로 답한다. 나머지는 지금처럼 `RELAY_GRACE_MS` 다.
  - 이유 필드는 요청했을 때만 준다(`standby`/`no-relay`/`not-accepted`/`no-wire-port`). eligible·answering 로그 줄에 `relayEarly=`·`early=1` 이 붙는다.
- `directory_rendezvous.{hpp,cpp}` (PC·APK 공용)
  - `punch_answer_is_valid`: 49 B·magic·size 49·version ≥ 2·kind Punch 만 응답으로 인정한다.
  - `PunchAny(…, PunchRaceOptions, …, PunchRaceReport*)` 를 새로 둔다. 기존 시그니처는 legacy 로 위임하므로 APK JNI 는 변경 없이 컴파일된다.
  - **early 모드**: relay 응답을 보관하고, T0+1000 **전**의 direct 응답을 우선한다. commit 시점에 보관된 relay 가 있으면 선택하고(datagram 경로도 같음), 없으면 4 s 예산 안에서 계속 기다린다.
  - cancel 이 선택보다 우선하고, 선택은 한 곳에서 1회만 한다.
  - 비일치 출처는 tuple 별로 집계한다(최대 8, 나머지는 개수만, 원문 없음).
  - 시험 빌드 전용 clock seam 을 추가했다(`REMOTE60_RENDEZVOUS_TEST_SEAM`, 기존 recv seam 과 같은 경계).
- `connect_candidates.hpp`: `CandidateKind::Relay` 를 추가했다. 10-22 분석 §6 의 relay → public 라벨 손실을 정정한다.
- `directory_session_client.{hpp,cpp}`: 플래그를 보내고, 요청했을 때만 응답을 파싱한다.
- `directory_session_bootstrap.{hpp,cpp}`:
  - 요청의 `relayEarly` 와 `relayCommitMs`(1000)를 받는다. accepted 일 때만 early 경합을 쓴다.
  - 기존 `[attempt]` 두 줄 형식은 유지하고, `relayEarly=` 만 덧붙였다.
  - 새 `[native-video-client][race]` 줄: mode·commitMs·decision·chosenMs·firstDirect/RelayMs·relayHeld·rounds·invalid·strangers·openMs·from=tuple/n/punch/firstMs/bytes.
- `viewer_startup.cpp`: PC 뷰어가 `relayEarly=true` 를 보낸다.

**테스트**
- `punch_any_test.cpp`(+376 줄): 가상 시계 경계, legacy 불변, 형식 검사, 비일치 집계, 취소, 실 소켓, 필드 표본 replay.
- `apps/directory/test/relay_early_test.js`: `run.js` 의 relay 서버에서 실행한다.
- `np_early_bootstrap_e2e.js` + `np_early_bootstrap_probe.cpp`: 제품 `directory_session_open` 을 감싼 시험 도구이며 출하하지 않는다.
- `directory_session_client_test` 의 relay kind 기대값을 바꿨다("unknown" → Relay, 의도된 계약 변경).

**문서**: `apps/directory/README.md` relay 절, `docs/history.md` 항목.

## 2) 시험 (모두 격리: loopback·시험 서버·가짜 Host. SLOT 불필요, GPU·화면 사용 없음)

| 시험 | 결과 | 비고 |
|---|---|---|
| remote60_punch_any_test | 5db95dd: rc 0, `ok` 61 / FAIL 0, **3회 반복 동일**(c803768: 60/60 ×6) | 아래 반례 전부 |
| 부정 대조 mutant m1(datagram 경로 commit 검사 삭제)·m2(형식 검사 삭제)·m3(hold 없음) | 각 1·2·9 FAIL | 원복 hash 동일 확인 → rc 0 |
| directory suite (`node test/run.js`, 시험 루트 `.claude/test-tmp/np-impl`) | **RESULT: ALL PASS, rc 0, PASS 832 / FAIL 0** | relay_early 16 포함(아래) |
| np_early_bootstrap_e2e (새/구 서버 × 새/구 뷰어 × Host 응답/무응답, grace 1500) | 26/26, rc 0, **×3** | 무응답 Host 선택 ms: 새+새 1014·1011·1014 / 나머지 1501~1516 / 응답 Host 는 전부 direct |
| remote60_directory_session_client_test, connect_candidates, connect_op_gate, directory_retry(133 checks) | PASS rc 0 | |
| GNLinkViewer.exe (Debug) 빌드 | rc 0 | 실행하지 않음(사용자 화면 위 창 금지) |
| NDK 30 aarch64 `-fsyntax-only -Wall -Wextra` directory_rendezvous.cpp | 출력 0 | APK 전체 빌드는 하지 않음 |
| remote60_host_punch_reply_e2e_test | **SKIPPED(77)** | 실 listening Host 를 띄우는 시험(REMOTE60_ALLOW_HOST_E2E). Host 코드는 변경 0 이라 실행하지 않음 |

**punch_any 반례**(가상 시계 — scheduler 와 무관하게 정확한 시각):
- relay@20 + direct@999 → direct 999.
- direct@1000 → relay 1000. direct@1001 → relay 1000.
- relay 없음 + direct@1001 → direct 1001(1000 에서 끝나지 않음).
- relay@2500 → relay-after-commit 2500.
- direct@1500 + relay@2500 → direct.
- 모두 무응답 → 예산 4 s 에서 none.
- relay@10 + private@30 → private.
- commit > 예산 → 예산 끝에 보관된 relay.
- legacy: direct@360 / relay@2500 → direct. relay@20 / direct@300 → relay@20 — 플래그를 요청한 connect 에만 쓰는 이유를 보여 준다.
- 형식 오류 5종(magic·48 B·kind·version 1·1 B) → 무시, invalid=5(양 모드).
- 비일치 유효 Punch(127.0.0.1:42000 ×2) → strangers n=2/punch=2/firstMs=100, 선택 안 됨.
- 20 tuple → 8 추적 + 12 untracked.
- 취소: relay 보관 중 1000 에 stop → cancelled. 600 에 stop → cancelled.
- 실 소켓: direct 개방 300 → 310 ms direct(relay 0 ms 보관). direct 없음 → 1009 ms relay.
- **필드 표본 replay**: PC 뷰어 direct 281·301·324·338·357·361·369·382·415 + Host 최대 482 → **10/10 direct**, 각자 시각. APK 554·626 은 기준점이 달라(connect→chosen) 별도 참고로만 확인했다(2/2). 중계였던 9건 → 1000 에 relay(기존 ≈2502).

**relay_early (서버)**:
- 플래그 없음 → 필드 없음, 406 ms(grace 400).
- accepted → 13 ms, 응답 ≤3회.
- 같은 wire IP 의 다른 소켓 → 414. 뒤이은 무플래그 connect → 400.
- standby → `standby`(accepted 아님). relay 없음 → `no-relay`, 조기 응답 없음.
- **조기 응답은 bind 하지 않음**: 살아 있는 relay 세션 뒤에 accepted connect 를 받고 punch 에 15 ms 응답했다. Hello 를 보내지 않았고, 원래 세션은 양방향 media 가 유지됐다.

## 3) 직접 확인 / 미검증 구분

- **직접 확인**: 위 표(이 PC, 콘솔·RDP 무관한 pure-logic·network(loopback) 시험).
- **미검증**:
  - **제품 UI 변화(렌더 증거 없음)**: `CandidateKind::Relay` 때문에 relay 세션에서 `result->relay` 가 이제 참이 된다. 그래서 toolbar 의 relayPath 표시가, 지금까지 direct 로 보이던 회사→집 중계 접속에서 **중계로 표시**된다. 검증용 독립 검사에서 실제 제품 창으로 확인해 주십시오(DOM 하네스≠제품 UI).
  - 형식 검사는 legacy 경합에도 적용된다. 공용 native 를 다시 빌드하는 APK 도 이 변화를 받는다(Host·relay 의 실제 응답 형식은 소스로 확인).
  - 실 NAT(회사 → 집) 필드 효과. 게시 뒤 KPI 로 본다(`proposal_np_r2.md` §KPI).
  - 실 GNLinkViewer 창 실행과 첫 화면 시각.
  - Android 전체 빌드와 APK 동작(APK 변경 없음).
  - host_punch_reply e2e(SKIPPED).
  - 운영 서버는 이 server.js 가 아니다. 게시 때 의존 모듈을 함께 올리고 재시작해야 한다(R2 절차).
- 구 서버는 기준 0a5e71d 의 server.js 사본이다(KA r3 포함 — 운영 서버보다 새것이다). relayEarly 를 모른다는 점은 운영 서버와 같다.
- "구 뷰어" e2e 는 새 바이너리에 플래그만 끈 것이다. 서버가 보는 차이는 플래그뿐이므로 서버 호환 판정으로는 동등하다. 다만 구 바이너리 자체를 실행하지는 않았다.

## 4) APK 순서 조정 (보고)

- JNI `nativeDirectoryConnectAny` 의 `g_rendezvous.PunchAny(...)` 호출부를 feat/apk-0.2.33 이 `e->rendezvous` 로 다시 쓴다(같은 hunk). 그래서 **이번에 APK 는 변경 0** 이다.
- 0.2.33 위에서 할 일은 두 가지이고, 둘은 함께 가야 한다.
  1. JNI: commit 창 인자를 PunchAny 옵션으로 전달하고 report 를 로그로 남긴다.
  2. Kotlin: `DirectoryClient.connect` 가 `relayEarly:true` 를 보내고 응답 파싱 결과를 JNI 에 넘긴다.
- **플래그만 먼저 보내면 relay 가 즉시 이기는 회귀**가 생긴다(legacy 경합 시험이 증명).
- 공용 native 만 다시 빌드한 APK 는 legacy 경합을 그대로 쓴다(형식 검사·Relay 이름만 받음).

## 5) 남은 결정·위험

- **형식 검사 version ≥ 2**: 구 Host·relay 는 모두 `UdpHelloPacket` 기본값(version 2)으로 답한다. 소스 확인: Host `Punch()`·`punch_reply`, 서버 `buildPunchPacket`. 미래에 version 을 올려도 통과한다.
- **wire port 일치 조건**: 관찰 socket 과 relay punch 의 출발 port 가 NAT 에서 다르면(목적지 port 의존 mapping) 조기 응답이 grace 로 내려간다. 안전한 쪽(기존 동작)이다.
- 삭제 후보는 `deletion_candidates.md` §G 에 3건 추가했다(이번 실수로 worktree 루트에 남긴 실패 로그 1건 포함). 삭제는 하지 않았다.

## 6) 다음 담당
- **검증용**: 고정 5db95dd 를 독립 빌드·실행(build-verify)한 뒤 Codex SOURCE 를 요청한다. 실행: `node test/run.js`(REMOTE60_TEST_TMP), `np_early_bootstrap_e2e.js`(NP_PROBE_EXE, NP_OLD_SERVER = `git archive 0a5e71d apps/directory` 사본), `remote60_punch_any_test`.
- **작업용2**: 결함 보수 요청에 대기. APK 부분은 0.2.33 병합 뒤 착수한다.


## r2 — Codex `codex_np_impl_src_r1.md` N1 보수 (2026-10-27)

**고정 후보 `e16ad0d`** (5db95dd 위 1 commit, worktree clean, `git diff --check 0a5e71d..e16ad0d` rc 0). 제품은 `server.js` relay 응답 fence 한 곳이다. 나머지는 시험·문서·rendezvous 헤더 주석이다.

**N1 보수**: `relayScheduleReply` 의 각 예약 응답(조기·grace 모두)은 송신 직전에 두 조건을 확인한다.
- 자기를 예약한 eligibility 기록이 그 wire IP 의 **현재 기록**인지(객체 동일성)
- **미만료**인지

둘 중 하나라도 아니면 보내지 않고 `reply n/3 … dropped: superseded|expired` 로그를 남긴다. 이미 선로에 나간 datagram 은 회수하지 않는다.
- 교체된 client 는 계속 punch 하므로, 새 기록 아래에서 응답을 받는다. 그 port 가 새 기록의 early port 가 아니면 grace 를 따른다.
- legacy grace 응답에도 같은 fence 가 걸린다. 이유는 같은 종류의 경합(교체 뒤 옛 grace 응답이 새 경합에 섞임)이기 때문이다.

**시험**

| 시험 | 보수 후 | c803768 서버(부정 대조) |
|---|---|---|
| `relay_reply_fence_test` — server.js 의 함수 원문을 가상 timer·Date·send recorder 로 실행, 네트워크 없음 | **9 PASS** | **7 FAIL** |
| `relay_early_test` 같은 tuple 반례(실 서버) | 21/21, **×3** | **FAIL `afterB=2 total=3`** — Codex 의 stale 2 재현 |
| punch_any(라벨 정정 후) | 61/61 ×3 | — |
| np_early_bootstrap e2e | 26/26 | — |

`relay_reply_fence_test` 의 사례:
- 무교란 early 0/120/240
- 같은 tuple B 가 50 ms 에 교체 → 그 뒤 0 · B 의 응답은 B 의 grace 뒤
- 두 응답 사이 만료
- 같은 IP 의 다른 tuple 이 교체 → 옛 기록 0, A 의 다음 punch 는 새 기록 아래 grace
- 무교란 legacy 2500/2620/2740
- legacy 를 2000 ms 에 교체 → 0
- 기록이 sweep 됨 → 0

`relay_early_test` 의 같은 tuple 반례 순서:
1. A accepted → X 에서 punch → 첫 응답 29 ms.
2. 같은 소켓 X 로 B(무플래그) connect 완료 30 ms.
3. 400 ms 동안 X 로 온 A 응답 0.
4. B 의 punch → 401 ms(grace 400).

- 전체 832 묶음은 다시 돌리지 않았다(Codex: 매 수정마다 불필요). relay 서버 구간만 실행했다.
- 서버 시험 실행기는 저장소 밖 미추적 `.claude/np_impl_logs/run_relay_early.js` 다(run.js 와 같은 env).

**근거 표현 정정**
- **replay**: 측정 집합은 PC 뷰어 **9건**(281~415 ms, 같은 T0)이다. Host 482(wake → Host punch)와 APK 554/626 은 **별도 참고**다. relay 9회는 **direct 를 뺀 합성 입력**이며 정책 결과만 보여 준다. 현장 직접 비율이나 원인을 증명하지 않는다. 시험 label·history 를 같이 고쳤다.
- **위 §2 의 `필드 표본 replay` 줄은 이 절로 대체한다.** (§2 "PC 뷰어 direct … + Host 최대 482 → 10/10" 은 틀린 묶음이다.)
- **version ≥ 2**: 현재 Host·relay 의 v2 응답을 받는 **정책 범위**다. 미래 호환 보장이 아니다(rendezvous 헤더 주석에 명시). §5 의 "미래에 version 을 올려도 통과한다" 는 "이 검사는 version 을 이유로 거부하지 않는다" 로 읽는다. 호환 판정 근거가 아니다.
- **flag=false**: 새 바이너리의 **구 요청 모드**다. 구 바이너리 실행이나 운영 서버 동등이 아니다(구 서버 = 0a5e71d 시험 서버).

**미검증(변경 없음)**: 실 NAT, 제품 창 relayPath 표시, APK(0.2.33 뒤 JNI·Kotlin 공동 배선), KA N1~N3(기반 0a5e71d 의 별도 미결 — NP 검토가 승인하지 않음).
