# report apk-0232 r1 — 공용 세션의 로그인 전 picker ack 대기 보수 (작업용1 → 검증용 remote#yj8mrmrx, 2026-10-26)

**고정 후보: `fix/apk-prelogin-select` e8123e4** (worktree C:/work/gnlink-wt/apk-prelogin-select, main 5970f04 위 3 commit, tracked clean; e8123e4 는 history 문서만 — 제품·시험은 c43effd 와 같음)
- 627acdb **제품**: `selection_ack_gate.hpp` · `native_video_client_session.{hpp,cpp}` · `native_video_client_shared_core.{hpp,cpp}` · `android_video_decoder.{hpp,cpp}`
- c43effd **시험**: `client_session_prelogin_select_test.cpp`(신규) · `viewer_prelogin_login_e2e_test.cpp`(`--android-select` 모드) · CMake 대상
- Kotlin 무변경. SH r2·UA-2 는 같은 브랜치 위에 이어 쌓는다(APK 0.2.32 단일 후보, 검증용 seq 14157 허용).

## 원인 (소스)
`RequestWindowSelect(0)` 은 패널에 WindowSelect 를 queue 하고, 송신 직전 `native_video_client_session.cpp:866-869` 가 "역할 = 로그인 전 화면 + allowlist 밖" 이면 **말없이 `continue`** 했다. 그러면 SelectionAckGate 는 ack 를 기다린 채로 남고, 패널은 `desktop_select_requested` 에 머문다. APK 는 REQUESTING 6 s 뒤 목록으로 돌아간다(MainActivity.kt:4314).

## 보수 (Codex `codex_rel_0163_e1_decision.md` 계약 1~6 대응)
1. **발동 조건**: `kCaptureFlagPreloginV1` + 바탕화면(0) 만 로컬. secure desktop·LockCred 는 기존 ack 경로(P4 대조, 부정 대조 S2).
2. **송신·권한**: WindowSelect 를 보내지 않는다. 키프레임 요청(allowlist 안)만 기존 owed-IDR 경로로 보낸다. 가짜 WindowSelected 는 만들지 않는다.
3. **게이트 local 모드**: 같은 tag 의 armed selection 이 ack 없이 계속된다. pending·첫 그림 대기·결합 뒤 straggler 필터는 유지한다. **결합 시점은 디코더에 실제로 들어간 첫 IDR**(Admit 이 통과시킨 candidate 의 KeyDelivered)이다. 그 전의 delta 는 버린다. 디코더가 못 받은 IDR(KeyLost)은 결합하지 않고 다시 요청한다. Host 답은 local 을 완료시키지 못한다. `Refuse` 는 할 수 없는 선택을 실패로 끝낸다.
4. **보고 순서**: 패널의 "selected"(`window_selected: desktop (logon screen)` → APK ACKED → WAITING_FIRST_FRAME)는 **결합 뒤에만**, 그 generation 을 `lastSelectStreamGeneration` 으로 싣는다. ready(select_ready)는 디코더 첫 출력 뒤 — 기존 Android 경로 그대로다.
5. **첫 pong 전 선택**: queue 된 그 op(tag)를 송신 지점에서 판정한다. 바탕화면이면 local 로 전환, 창이면 거부(`window_select_failed: unavailable_prelogin` → APK FAILED → 목록). 이미 교체된 tag 는 아무것도 바꾸지 않는다. 조용한 폐기·영구 대기는 없다.
6. **옛 op·재선택·재접속**: 패널은 현재 tag 만 표시한다. 결합 poll 은 sink 의 현재 tag 와 다르면 버린다. `localSelectTag_` 는 연결마다 초기화된다(P5·P6·P7).
- **Android 디코더 sink**: `BeginLocalSelectionFor`/`LocalSelectionBoundFor`/`RefuseSelectionFor` 가 그 gate 에 위임한다(로그 줄 추가, DebugStatus `localSel=`). 그 밖에는 바꾸지 않았다: selectionTag/incarnation, 단일 decoder, park inhibit, Prepare 의 codec reset.

## 시험 (이 PC 콘솔 — qwinsta console Active, RDP 없음; SLOT_REQUEST CreateNew → 삭제; loopback; 사용자 GNLinkStream pid 2456 무조작)

| 시험 | 구성 | 결과 | 비고 |
|---|---|---|---|
| `remote60_client_session_prelogin_select_test` | 제품 세션 + 제품 gate + 가짜 control host | **38/38** rc0 | exe `9fb836cf…` |
| 같은 시험, 세션만 5970f04 원본 (PRE) | — | **14 FAIL** | 상태가 `desktop_select_requested` 에 머묾 |
| 부정 대조 G1 (Admit 에서 결합) | — | 1 FAIL | IDR 유실 사례 |
| 부정 대조 G2 (미결합 중 delta 통과) | — | 2 FAIL | — |
| 부정 대조 S2 (secure desktop 도 local) | — | 2 FAIL | UAC+LockCred 대조 |
| `viewer_prelogin_login_e2e_test --android-select` | **실제 로그인 전 Host**(GNLinkStream 시험 빌드 `--prelogin`, 첫 pong 3 s 지연) + 제품 공용 세션 + 제품 gate | **37/37** rc0 | 최종 exe `1d0bbfbe…` 에서 재실행 확인 |
| 같은 실 Host 시험, PRE | — | **4 FAIL** | 선택 미완료·첫 출력 없음·창 선택 거부 안 됨 |
| 회귀: role_park 13, shared_core, monitor_select_wire 52, telemetry_window, udp_recv_loop, viewer_selection_gate, window_proc_isolated 55 | — | 모두 PASS | pinned_seam_boundary 는 SKIP(77, 원래 조건부) |
| 회귀 e2e: `--android-session` 43, `--foreign-peer` 37, LK prelogin 79, monitor_fallback 173, android_reboot 17, mobile_abr | — | 모두 PASS | — |
| APK `gradlew assembleDebug testDebugUnitTest` | 4 ABI 네이티브 빌드 | BUILD SUCCESSFUL, 단위 249/0 | debug apk `78b5da7c…` (시험용, 출하 후보 아님) |
| Windows 전체 Release 빌드 | — | rc0 | — |

**실 Host 사례(`--android-select`)의 흐름**:
1. 첫 pong 전에 바탕화면을 선택하면 queue 된다.
2. 역할(0x2000)이 도착하면 같은 op 가 local 로 전환된다.
3. Host 의 실제 IDR 이 결합되어 패널이 selected(gen 1)가 되고, 첫 출력이 나온다.
4. 그동안 Host 는 아무것도 거부하지 않았고 링크는 유지됐다.
5. 역할을 안 뒤의 두 번째 선택은 즉시 local 이 되고, 새 IDR 로 완료된다.
6. 창 선택은 즉시 거부된다.

Host 로그·세션 로그는 `.claude/caplife/apk/r1/`.

**구분 (CLAUDE.md)**: 위는 ⑴ 제품 native 코드 하네스와 ⑵ 실 Host 의 product-equivalent 시험이다. sink 의 decoder 부분(MediaCodec)은 빠졌다 — 들어간 key 를 곧 출력으로 본다. **APK UI(REQUESTING → VIEWER) 실기·AVD 는 하지 않았다.** AVD 실 APK(시험 host fail-closed)로 "로그인 전 Host 에서 바탕화면 → 화면 표시" 를 보는 것이 남은 단계다. 실 폰·실 재부팅 로그인 화면은 실기 대기다.

## 명령
- `.claude/caplife/apk_runs.ps1 -Tag r1` (빌드 → 순수 시험 → 슬롯 → e2e 7)
- `.claude/caplife/apk_mut_run.ps1` (base/PRE/G1/G2/S2 → 원복 → 재빌드)
- `.claude/caplife/apk_pre_real.ps1` (실 Host PRE)
- 독립 실행: `set REMOTE60_ALLOW_HOST_E2E=1 & set REMOTE60_ALLOW_UI_TEST=1` 후 `build\apps\native_poc\Release\remote60_viewer_prelogin_login_e2e_test.exe --android-select`, 그리고 `remote60_client_session_prelogin_select_test.exe`. 재빌드하면 해시가 바뀐다(비결정 빌드).

## 하지 않은 것·남은 것
- 버전·서명·게시·push 는 하지 않았다.
- AVD 실 APK UI 확인 → 0.2.32 통합 단계에서 SH r2·UA-2 와 함께 한다.
- 레거시 `ControlMonitorSelect`(구 Host 용)는 로그인 전 화면에서 여전히 조용히 버려진다. 로그인 전 Host 는 신형이고 모니터 목록을 받지 않아 APK 에 모니터 대상이 뜨지 않으므로 범위 밖으로 두었다 — 기록만 한다.

## r2 — Codex `codex_apk_prelogin_select_src_r1.md` N1·N2 (2026-10-26)
**커밋: 86df4cd(제품) · 1de9dc7(시험)**, `fix/apk-prelogin-select`(UA-2 r2 cf8c021·41de4cf 가 그 위에 있음; SH r3 는 작업 중·미커밋).
- **N1**: `localSelectMu_`(parkMu_ 다음 순서, 네트워크 없음) 아래에서 한 단계씩 처리한다:
  - 직접 경로: sink BeginLocal → panel BeginLocalSelect → tag 게시.
  - queue 전환: sink BeginLocal → (시험 hook) → panel Convert → tag 게시. Convert 가 false 면 tag 를 게시하지 않는다.
  - poll: 읽기·판정·완료.
  - 늦은 A 는 B 가 끝날 때까지 기다리게 되고(또는 B 가 A 다음에 실행되고), B 의 tag 를 덮거나 지우지 못한다.
- **N2**: Android sink 의 보류 프레임을 공유 클래스 `DecoderPendingSlot`(decoder_pending_slot.hpp)으로 옮겼다.
  - **실제 codec queue 직전에** `SelectionAckGate::PermitsQueue`(대기: 없음 / 미결합 local: 현재 candidate IDR 만 / 결합·응답: 그 세대만)를 다시 묻는다. 허가가 없으면 버린다.
  - 새 프레임은 보류 프레임을 앞지르지 않는다(보류 중 새 IDR 은 교체, delta 는 버리고 IDR 요청).
  - bootstrap 재전달도 같은 검사를 거친다. DebugStatus 에 `gateQueueDrops` 를 추가했다.
- **시험**:
  - `client_session_prelogin_select_test` **46/46**. P8: queued A 를 sink 전환 직후 latch 로 멈추고, 그사이 UI 가 Prepare(B)+선택(별도 스레드)한 뒤 A 를 재개 → B 자신의 IDR 로 ACKED(gen 910) + ready. 취소만 한 경우는 A 가 아무것도 완료하지 않는다. 부정 대조 N1(잠금 없음·Convert 무시) → P8 FAIL.
  - `decoder_pending_slot_test` **10/10**. Codex 순서를 재현했다: G 신규 실패 → G 재시도 실패 → H 도착 직전 pump 실패 → H queue 가능. G 는 codec 에 들어가지 않고 H 만 결합된다. 응답 대기 중 보류된 옛 프레임은 버린다. delta 는 보류 IDR 을 앞지르지 않는다. 부정 대조 N2(r1 모양) → 7 FAIL — codec 입력이 `200 100`, 즉 H 뒤에 G 가 들어갔다.
  - APK 빌드(4 ABI) OK. 회귀: shared core PASS, role_park 13/13.
- **한계(정직하게)**: N2 반례는 **제품 슬롯 클래스 + 제품 gate 를 Windows 에서, codec 자리에 대역(입력 버퍼 있음/없음과 IDR 지불)을 두고** 돌린 것이다. Android sink 가 같은 클래스를 queue 경계로 쓰는 것은 소스로 확인했다. **실제 MediaCodec(AVD/실폰)에서 입력 버퍼 고갈을 일으켜 재현한 것은 아니다** — 그런 seam 은 만들지 않았다. 필요하면 다음 단계로 debug 전용 native seam(dequeueInputBuffer 강제 실패)을 제안한다.


## r3 — Codex codex_apk_prelogin_select_src_r2.md 잔여 N1 (poll 의 완료 vs 취소/새 Prepare, 2026-10-27)
**고정: `fix/apk-prelogin-select` 9afcec7 (제품) · 3a17444 (시험)**
- 9afcec7: `ClientEncodedFrameSink::CommitLocalSelectionFor(tag, commit)` 신설 — "아직 이 선택인가 · G 로 결합됐나" 판정과 panel 기록(`ApplyLocalSelectedFor`)을 **sink 의 선택 상태 잠금 하나 아래** 한 단계로. Android sink 는 `LockedState`(nativePrepareVideoSwitch/nativeAbortVideoSwitch 가 잡는 바로 그 잠금) 안에서 commit 을 부른다 → 취소/새 Prepare 는 통째로 앞(아무것도 완료 안 됨) 또는 통째로 뒤(이미 기록된 완료를 그 뒤에 무효화). `PollLocalSelection` 은 이 한 호출만 쓴다(재읽기 추가 아님). 잠금 순서 local → sink → panel 한 방향; sink→local 경로 없음.
- 3a17444: P9 — 시험 hook `SetTestBeforeLocalCommitHook`(시험 빌드 전용)로 poll 을 "bound 확인 뒤·panel 기록 전" 에 세우고, 그 틈에 UI 스레드가 취소(Abort) 또는 새 Prepare B 를 호출(최대 600 ms 기회). FAIL 조건: UI 호출이 끝난 **뒤에** A 가 selected 로 기록됨. B 는 자기 IDR(701)로 완료.
- 결과: `client_session_prelogin_select_test` **56/56**(rc0). 변이(r2 poll 모양 — 읽고 나서 sink 밖에서 기록) → P9 경계 2건 **FAIL**(`at ui done=0 final=window_selected`), 복원 해시 일치 후 56/56. `role_park` 13/13, `shared_core` rc0.
- 미검증: 실 APK 에서 이 틈을 재현하지는 않았다(소스 경계 + 제품 Android sink 와 같은 모양의 GateSink 시험).


### r3 시험 보수 (Codex codex_apk_prelogin_select_src_r3.md — 제품 9afcec7 SOURCE OK)
- 7795289: 실 Host e2e `viewer_prelogin_login_e2e_test` 의 GateSink 에 `CommitLocalSelectionFor` 를 추가했다. 자체 mu 안에서 Android sink 와 같은 callback 이다. 이것이 없으면 기본 Abandoned 로 tracker 가 지워져 과거 37 PASS 를 재사용할 수 없었다.
- 그 target 만 재빌드(exe ed69ea1f…)하고 실제 로그인 화면 Host 로 `--android-select` 를 돌려 **37/37**(rc0, 콘솔 세션, 슬롯 삭제 확인)을 얻었다. 기본 Commit 은 Abandoned 그대로다(옛 read-후-callback fallback 없음).


## evidence A — 실제 APK UI × 로그인 전 역할 시험 Host (Codex codex_apk0232_evidence_design.md, 2026-10-27)
**고정: ed7f4ad** (시험 Host seam `host_control_session.cpp` — REMOTE60_STREAM_TEST_SEAM 전용·기본 off · 게이트 이름 · 하네스)
- `GNLINK_STREAM_TEST_PRELOGIN_ROLE=1`:
  - 수동 연결된 viewer 에게 kCaptureFlagPreloginV1 을 광고하고, worker 와 같은 표면을 제공한다(사용자 데이터 비트 제거).
  - prelogin allowlist 를 **tripwire** 로 둔다. 거부 타입과 효과 있는 타입(unlock·reboot·SAS)은 FORBIDDEN 으로 기록하고 링크를 닫으며, 응답하지 않는다.
  - `=2` 는 비트를 남기는 tripwire 자기 대조다.
  - 실 `--prelogin` worker·디렉터리 인증·캡처는 바뀌지 않았다.
- 첫 실행에서 드러난 것: 처음에는 비트를 지우지 않아 실제 APK 가 clipboard(type 52)를 polling 했고, tripwire 가 그것을 잡았다. worker 와 같은 비트 제거로 고쳤고, 그 현상은 `=2` 대조로 남겼다.
- 결과(후보 1d4a05f4, Host 881b3e05):
  - `prelogin_ui` **15/15**: local 선택 → 표시, 프레임 hold 중 취소 → 목록 유지·늦은 프레임 무영향, 같은 연결 재선택, SH 끄기/약/강/화면 크기, window list/select/thumbnail 0, FORBIDDEN 0.
  - `prelogin_tripwire` 3/3.
  - `prelogin_ui_control`(일반 Host) 은 기대대로 FAIL(window-list 12, local 0).
- 한계: source 는 시작부터 고정된 WGC 패턴 창이다(UI 는 바탕화면). 실 worker 의 DXGI 로그인 화면·디렉터리 인증·SYSTEM/Winlogon 근거가 아니다.
