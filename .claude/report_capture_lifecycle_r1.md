# report capture-lifecycle r1 — 작업용1 (2026-10-26) — task t-l5o6hmjt

**결론: PS-A·PS-B·UA-1·UA-2(host + shared client 상태 API) 구현·자동 검증 완료. 고정 후보 `fix/capture-lifecycle` **1679bde**** = fix/lk-retry **8d8655e** 위 1 commit(LK e2e·CMake 를 공유해 그 위에 쌓음, worktree clean). 버전·서명·게시·push 없음.

## 원인 (10-22 워커 로그 직접 확인)
- pid 4164: 첫 내용 프레임(16:33:39)이 stream-state 0 동안 와서 DXGI handler 에서 버려짐(`host_capture_session.cpp` 의 inactive drop) → 활성화 때 캐시 재전송은 `KickTryFill` 의 "Default 데스크톱만" 에 막힘 → 정지 화면이라 새 프레임 0. (Codex §2 의 "캐시 하나 더" 로는 안 풀리고, 원본 없음 → 전체 프레임 재획득이 필요했던 경로.)
- pid 5368: idle detach 뒤 재부착이 `host_stage_stream_active.cpp` "보안 데스크톱이면 WGC" → `PreloginStopServing("wgc requested")` rc 15.

## 구현 (Codex §1~§4)
- **PS-A**: 캐시에 입력 데스크톱 이름·세대(provenance). 보여도 되는 데스크톱(로그인 전 = 서비스 중인 logon 데스크톱, 로그인 뒤 = Default)에서만 재전송. 확인된 변화·detach·보지 않는 동안의 변화 → 캐시 삭제, 변화 감지 전 callback 의 프레임은 캐시 안 함(같은 mutex 로 직렬). 캐시 없으면 전체 프레임 요청: DXGI `RequestFullFrame()`(다음 acquire 를 내용으로, 없으면 duplication 재생성 ≤3 회) / WGC 세션 재시작 — 에피소드당 3 회·1 s 간격·재부착 후 0.5 s 유예, IDR 빚이 남으면 kick 재무장(같은 상한). 정지 화면 갱신 500 ms(기존 1000 ms 기본 정정), 첫 AU 없이도 갱신 대상. 옛 AU 재생 없음(새 IDR 인코딩).
- **PS-B**: 로그인 전 워커 재부착은 보안 데스크톱이어도 DXGI(서비스 데스크톱 attach) — 로컬 mode(`preloginServingDesktop`)로만 판단.
- **UA-1**: main loop 한 곳의 입력 데스크톱 추적(100 ms, 두 번 일치로 확인, 판정 불가는 변화도 잠금도 아님). WGC 데스크톱 캡처면 확인된 변화마다 WGC 재구성 + IDR(변화당 1 회, 재시도 3 회·backoff). DXGI 는 기존 강등/climb-back(시험에서 함께 확인). PS-A 캐시와 같은 desktop generation 계약.
- **UA-2**: backend 요청 = 그 논리 세션(epoch) 소유. 새 세션은 호스트 시작 기본(env 기본 DXGI, quarantine 유지)으로 리셋, 같은 세션 resume 은 유지. 세션마다 host 로그 `session epoch=N backend requested=… active=…`. 상태 응답(검증용 확정): pong **0x100000** `kCaptureFlagDesktopBackendStatusV1`, 메시지 **90** `ControlDesktopBackendStatus`(32 B: seq·requested·effective·active·flags·changeCount·hostSendQpcUs) — 34 의 flags bit0 `kDesktopBackendRequestFlagStatusV1` 요청에만, main loop 적용 뒤 답(최대 **250 ms**, 넘으면 flags bit4 NotYetApplied), backend 0 = 조회. 자발 통지 없음(검증용 수용: client control reader 1:1). flags bit0 세션 한정·bit1 격리·bit2 window→WGC·bit3 로그인 전·bit4 NotYetApplied. shared native client(작업용2 의 APK 가 JNI 로 쓸 것): `ClientSessionController::QueryDesktopCaptureBackend()` · `DesktopCaptureBackendStatus()` · `HostAnswersDesktopBackendStatus()`; 재연결 때 상태 초기화.

## 시험 (종료 코드 + 마지막 요약 줄; 이 PC 콘솔 `qwinsta` console Active, RDP 없음; SLOT_REQUEST CreateNew→삭제 매회; 설치본·실서비스·실세션 무조작)
| 시험 | 태그 | 결과 |
|---|---|---|
| **PS** `viewer_prelogin_login_e2e_test --still-screen` (신규 모드: 실제 SYSTEM 워커 시작 경로로 띄운 시험 빌드 워커 + 제품 수신 사슬 VideoReceiver→FrameGate→MF 디코더→FrameBuffer) | display-capture·gpu | **30/30 ×3 rc0** (base 598339a ×2, 9ff6c15 ×1) |
| **UA** `host_capture_lifecycle_ua_e2e_test` (신규: shared session=APK 엔진 + PC 뷰어 제품 수신 사슬) | display-capture·gpu | **46/46 ×3 rc0** (base 598339a ×2, 9ff6c15 ×1) |
| LK e2e `--lock-screen` / prelogin / `--not-prelogin` | 회귀 | 68 / 79 / 28 rc0 (lock 은 최종 base 8d8655e 에서도 68 rc0) |
| `viewer_stats_chain_e2e_test --scenario gap`(pause→resume) | 회귀 | PASS rc0 |
| `host_monitor_fallback_e2e_test` | 회귀(선택) | 173/173 rc0 |
| kick·backend_policy·capture_quarantine·capture_attachment·dxgi_output_selection·monitor_selection·cadence_gate·lockcred·relay·unlock_protocol·viewer_lock_cred_retry | 단위 | 전부 rc0 (relay 는 8d8655e 에서 84/84) |
- 8d8655e(LK r4: relay 만 변경) 위로 옮긴 뒤 다시 돌린 것: 전체 빌드 rc0, relay 84/84, LK e2e lock 68/68. PS·UA 는 그 전 base 결과(r4 가 건드리지 않은 경로).
- **PS 수치**: picker(stream off) → 데스크톱 선택 → 첫 **디코드** 프레임 A(부착 중, 첫 프레임이 off 중에 와서 버려진 10-22 순서) 327~346 ms · B/B2(idle detach 뒤) 140~720 ms(유예 도입 전후 포함; 최종 154~170 ms) · 이후 3 s 에 5~6 회 갱신 · 워커 생존(rc 15/14 없음) · luma spread ≥150(빈 화면 아님).
- **UA 수치**: WGC·DXGI 각 2 회 왕복(정상→secure→정상), 변화 뒤 **그 뒤 캡처의 새 IDR 디코드** 220~420 ms, reconnect 0(세션 epoch 1+1 만), 폰 WGC 세션 종료 뒤 PC 세션 `requested=wgc -> host-default=dxgi` + 실제 DXGI, 상태 응답 `requested=2 effective=2 active=2 flags=1`(250 ms 안 applied), 조회 응답·적용 0.
- **부정 대조**(시험 빌드 + 해당 수정만 되돌림): M1 PS-A → A 에서 4 s 동안 디코드 0·갱신 0(10-22 그대로) / M2 PS-B → B 에서 `capture cannot continue … (wgc requested)` 워커 종료(10-22 로그 문구 그대로) / M3 UA-1 → 재구성 0·변화 뒤 IDR 없음 3/4 / M4 UA-2 → PC 세션이 폰의 WGC 상속. 복원 후 PASS.
  - **수정 전 바이너리(9991b36 baseline 보관본)로 같은 시험을 돌리지 못한 이유**: 이 PC 화면은 정지하지 않고(실측 DXGI 60 content/s, 사용자 사용 중) 보안 데스크톱도 만들 수 없어, 시험 빌드의 대역(`GNLINK_STREAM_TEST_DESKTOP_ALIAS`·`_STILL_SCREEN`·`_SECURE_DESKTOP_EVENT`)이 필요한데 baseline 에는 그 대역이 없다. 그래서 "수정 전" = 같은 시험 빌드에서 수정만 되돌린 mutant 로 대신했다.

## 제품과 다른 점 (시험의 한계)
- PS: 워커는 시험 사용자 Default 를 'Winlogon' 이라는 이름으로 서비스(SYSTEM·Winlogon 아님), 정지 화면은 시험 빌드가 DXGI 보고 중 "새 duplication 의 첫 장 / 요청한 장" 만 통과시켜 흉내 → **duplication 재생성 분기(진짜 무변화 화면)는 실행되지 않음**(이 PC 는 무변화가 없어 다음 acquire 로 늘 채워짐). 픽셀은 이 PC 화면.
- UA: 보안 데스크톱은 이름만(시험 빌드) — **수명 주기(감지→재구성→새 IDR→디코드, 같은 연결)를 증명, 실제 UAC 를 WGC 가 찍는지는 아님**. DXGI 의 ACCESS_LOST 도 시험 빌드가 같은 fallback 요청으로 대신.
- 시험 중 한 번(커버 창 실험, 12:43·12:45 각 1 분) 이 PC 주 모니터에 정지용 창을 덮었다 — 효과 없어 제거(사용자 화면 방해).
- 출하물: 새 seam 은 모두 `REMOTE60_STREAM_TEST_SEAM`(GNLinkStreamMigrationTest 전용) 안. 출하 gate(통합 때) 대상: `GNLINK_STREAM_TEST_DESKTOP_ALIAS`·`GNLINK_STREAM_TEST_STILL_SCREEN`·`GNLINK_STREAM_TEST_SECURE_DESKTOP_EVENT`.

## 남은 실기·후속
- 사용자 실기(R1 게시 뒤 1 회 묶음): 실제 로그인 전 재부팅 화면 picker→바탕화면, 실 UAC 예/아니오 2 회(같은 연결), 폰 WGC 뒤 PC 접속 = DXGI.
- 작업용2: APK 의 UA-2 표시(위 shared API, '이 PC 의 이번 연결에만 적용', 요청·실제 적용 구분, 0x100000 없는 Host 안내).
- ledger 후보: 비동기 control reader(자발 통지) — R1 범위 밖(검증용 결정).

## r2 — Codex N1~N4 보수 (2026-10-26) — 고정 후보 **6fa709a** (569059f 위 1 commit, worktree clean)
근거 `codex_capture_lifecycle_src_r1.md`. 버전·서명·게시·push 없음. 이 PC 콘솔(`qwinsta` console Active, RDP 없음), SLOT_REQUEST CreateNew→삭제 매회, 설치본·실서비스 무조작.

**구현**
- **N1 출처**: 프레임 = attachment cookie(프레임에 실림) + 그 attachment 의 `attachmentDesktopGen`(DetachCaptureSession 에서 찍음). PublishFrame 은 캐시(bootstrapCacheMu 안)·raw queue(res.frame.mu 안) **각 채택 시점에** 둘 다 현재일 때만 채택(callbackUs 규칙 폐기, `publish refused reason=provenance`). 캐시에 cookie 저장, KickTryFill 은 현재 attachment·gen·비 suspect 만. 확인된 edge → **먼저** fence(inputEpoch 이동·raw 비움·key 요청, `edge fence desktopGen=G atUs=`) → 실행 중 attachment 가 stale 이면 backend 무관(WGC/DXGI/GDI) 재구성, gen 별 3회·backoff, 실패 시 닫힌 채(`rebuild gave up`). stream 없음 = 재개 때, idle detach = 재부착이 현재 gen. Unknown·첫 다른 이름 1회 = edge 아님, 캐시 삭제·캐시 금지(suspect). 첫 look 은 gen 미증가.
- **N2**: `host_backend_owner.hpp`(순수) — tick 당 epoch 1회 → owner → `TakeBackendRequestForOwner`(옛 epoch 버림, **새 epoch 은 남김**) → 표식은 요청 자신의 epoch. 실제 Serve 종료 `backendSessionEndedEpoch` → 그 owner 선택 1회 복귀, 이전 owner 의 늦은 종료 무시. inactive/idleDetached 는 저장만(재시작 0).
- **N3**: 아래 API 절. host: `backendChangePostedKey`·`backendStatusEpoch` — 답 applied = 이 세션 상태가 게시됨 && 변경 처리됨(250 ms, 아니면 NotYetApplied).
- **N4**: episode key = (desktop gen, stream gen, session epoch, 외부 (재)부착 경계 — full-frame WGC 재시작만 제외). 다른 source → 새 budget, 자기 재획득은 유지, 채택된 프레임만 닫음. 로그 `full-frame ask=n/3 episode= desktopGen= streamGen= epoch= source=`; library 재생성은 ask 당 3(별도 상한).
- **추가 결함(시험에서 발견·수정)**: r1 lib 가 full-frame 요청을 **포인터 전용 acquire**(LastPresentTime 0)로도 닫아 검은 프레임을 전달 — r2_d PS ×2 에서 A/B/B2 6/6 luma spread 0~1, host `delivered … content=0`. 이제 이미지 있는 acquire 만 응답.

**N3 shared client API (작업용2용) — 형태 변경 없음, 의미만 변경**
- 그대로: `ClientSessionController::RequestDesktopCaptureBackend(uint16)` · `QueryDesktopCaptureBackend()` · `DesktopCaptureBackendStatus()`(DesktopBackendStatusSnapshot) · `HostAnswersDesktopBackendStatus()`; `DesktopBackendControl::Request/Query/ConsumePending/SetHostAnswers/HostAnswers`.
- 바뀐 의미: Query 는 대기 중 변경을 **덮지 않는다**(별도 queryPending). 변경+조회가 함께 있으면 **변경 1개**만 나가고(wantsStatus) 그 답이 상태. 조회를 먼저 꺼낸 뒤의 Request 는 다음 메시지로 나간다. 구 host(답 안 함)에는 조회 0. UI 는 NotYetApplied(bit4) 동안 requested/effective/active 를 '적용됨' 으로 표시하지 말고, 그 동안 Query 로 다시 묻는다(새 세션 첫 조회는 이전 세션 선택을 받지 않음).

**시험 (6fa709a 소스와 같은 최종 빌드; 종료 코드 + 마지막 요약 줄)**
| 시험 | 태그 | 결과 |
|---|---|---|
| PS `--still-screen` ×2 | display-capture·gpu | **32/32 ×2 rc0** — 실제 duplication 재생성 `made anew attempt=1` → `delivered after 1 … content=1`, luma 203~216 |
| PS `--still-screen --still-nofirst` (신규 대조) | 〃 | **26/26 rc0** — 표시 0, 재생성 9(=ask 3 × lib 3), 포기 3, ask 3/3 뒤 4 s 정지, **새 source(idle detach→재부착) 새 budget 3**, 워커 생존 |
| UA ×2 | 〃 | **54/54 ×2 rc0** — 경계마다 KeyCapturedAfter 는 synthetic 제외, 새 attachment 첫 채택 프레임(`adopted first frame … captureUs=X`)이 새 epoch 첫 key(`capture-switch first-frame … captureQpcUs=X`)로 나가고 X 이후 실제 그림 디코드 |
| UA `--rebuild-fails` (신규) | 〃 | **18/18 rc0** — fence 가 재구성보다 먼저, 3회 실패 뒤 포기(4회 없음), provenance 거부, fence+500 ms 부터 그림 **0**, 같은 연결로 복귀 edge 에서 그림 복귀 |
| LK `--lock-screen` / prelogin / `--not-prelogin` | 회귀 | 68 / 79 / 28 rc0 |
| host_backend_owner_test(신규)·shared_core(조회/변경 4 경우)·kick·policy·attachment·quarantine·dxgi_output·monitor_selection·cadence·relay 84 | 단위 | 전부 rc0 |
- **부정 대조**(같은 시험 빌드에서 해당 수정만 되돌림, 복원 뒤 PASS·MUTANT 표식 0): M5 raw-queue 출처 거부 제거 → rebuild-fails 에서 그림 24장·refused 0 FAIL / M6 새 epoch 요청 버림(r1 규칙) → owner 시험 3 FAIL / M7 조회가 변경 덮음(r1) → shared_core FAIL / M8 새 source 새 budget 제거(r1) → nofirst '0 new asks' FAIL / **M9 포인터 전용으로 응답(r1) → 이번 실행 PASS(재현 안 됨)**: 첫 acquire 가 포인터 전용일 때만 생기는 조건(사용자 마우스 움직임 의존)이라 결정적 대조가 아님 — 근거는 수정 전 r2_d 6/6 blank.
- fence 자체의 mutant 는 두지 않았다: 출처 거부(M5)가 같은 증상을 막아 e2e 로는 결정적으로 분리되지 않는다. fence 는 로그 순서(`edge fence` < `rebuild … attempt=1`)와 epoch gate 동작으로만 확인.

**통합 EVIDENCE 준비(검증용)**
- PC 뷰어 FrameBuffer 의 `key` 는 디코더 **입력** 패킷 표시, `captureUs` 는 **출력** 그림 — MF 가 한 호출 늦게 내보내 r2 첫 시도에서 'key 의 captureUs' 결합이 거짓 실패(r2_c). 시험은 host 첫 key 줄 + 디코드 그림 시각을 따로 대조. FrameBuffer 계수 = 디코드/도착이지 실제 presented 아님(PS present thread 는 창 없음).
- synthetic(kEncodedFrameFlagSynthetic) 은 새 원본이 아니다 — UA 판정에서 제외.
- 관찰(별건, 수정 안 함): 첫 datagram 이 fence 전에 나간 AU 는 끝까지 전송된다(sender P11 계약) — 경계 직전 캡처 1장이 경계 뒤 ~100 ms 에 도착할 수 있음(r2_a UA DXGI #2 seq 244).
- 증거 파일: `.claude/caplife/r2_final/`(최종), `r2_e/`(nofirst·rebuild-fails), `r2_mut/`(M5~M9), `r2_d/`(수정 전 blank), `r2_b_host_ua1.log`(host 전체 로그). 시험 전용 env: `GNLINK_STREAM_TEST_STILL_SCREEN=1|nofirst`, `GNLINK_STREAM_TEST_DESKTOP_REBUILD_FAIL=1`, `REMOTE60_E2E_HOST_LOG_COPY` — 출하 gate 대상(REMOTE60_STREAM_TEST_SEAM 빌드 전용).

**남은 실기**: 실 UAC 승인/거부·실 정지 화면 하드웨어·실 SYSTEM 재부팅(사용자), APK 상태 UI(작업용2). KA spike 9f1fac1 은 그대로(S1~S4 는 R1 통합 뒤).

## r3 — Codex 재확인 잔여 보수 (2026-10-26) — 고정 후보 **6ffd94b** (6fa709a 위 1 commit, worktree clean)
근거 `codex_capture_lifecycle_src_r2.md` (N4·client Query·N2 대부분·N1 fence 는 닫힘 — 다시 열지 않음). 버전·서명·게시·push 없음. 이 PC 콘솔, SLOT_REQUEST CreateNew→삭제 매회, 설치본·실서비스 무조작.

**구현**
- **N1-a 재시작 실패의 소유**: `restart_capture_session(hx, owner)` — owner(General / DesktopRebuild / FullFrame)가 실패와 함께 남는다(`CaptureState::restartOwner`). desktop 재구성은 "stale 이거나, 이 gen 의 재구성이 실패한 채 아무것도 붙지 않음"인 동안 열려 있다(Detach 가 gen 을 현재로 찍어도 끝난 것으로 보지 않음). 재시도는 (gen, 세션)당 3회·backoff 이고, wrapper 의 자체 backoff 보다 먼저 시도하지 않는다(보이지 않게 거절되는 시도는 횟수로 세지 않음). 일반 retry(`host_stage_time_limit.cpp`)는 DesktopRebuild 실패를 넘겨받지 않는다. FullFrame 실패의 retry 는 그 episode 의 것(새 source 경계 아님)이다. 재시작 실패 중에는 full-frame ask 와 full-frame WGC 재시작을 하지 않는다. 일반 geometry/선택 회복은 그대로다(전역 상한 없음).
- **N1-b**: GDI 가 시작 때의 attachment cookie 를 프레임에 싣고, PublishFrame 은 cookie 0 을 거부한다. 캐시 연속성 토큰 `cacheContinuity` 는 모든 캐시 무효화가 lock 안에서 clear 전에 올리고, 프레임은 찍힐 때의 값을 싣는다. Unknown·후보 이름 전에 제출되어 늦게 끝난 readback 은 인코더로는 가지만(edge 가 아니므로 raw 는 막지 않음) 캐시에는 안 들어간다.
- **N2 종료**: Serve 마다 `backendServeSeq` 가 있고, 실제 종료는 `backendSessionEnded = PackBackendSessionEnd(epoch, seq)` 로 남는다. 요청은 자기 serve seq 를 싣는다. 끝난 Serve 의 요청(남아 있던 것·늦게 온 것)은 버린다. 같은 epoch 이라도 이후 Serve(종료 뒤 resume)는 live 다(epoch 번호만으로 판정하지 않음).
- **N3-host**: `host_backend_status_board.hpp` — owner epoch·packed·변경 수·처리 key 를 한 mutex snapshot 으로 게시(`Publish`/`PublishHandled`)하고 읽는다(`Read`). 판정은 순수 함수 `DecideBackendStatusAnswer` 다. 다른 owner 의 snapshot 이면 fields 0 + NotYetApplied 로, 이전 세션 선택을 이 세션 값으로 보내지 않는다. wire 32 B·250 ms·요청당 1답·자발 통지 없음은 그대로다. **작업용2 참고**: NotYetApplied 이면서 requested/effective/active 가 0 인 답 = "이 세션 상태 아직 없음"(표시하지 말고 다시 Query).
- **M9 결정적 대조**: 시험 빌드 `GNLINK_STREAM_TEST_STILL_SCREEN=pointerfirst` 를 추가했다(lib `TestStill::StillPointerFirst`). 열린 요청마다 1회, 새 duplication 의 첫 acquire 를 포인터 전용으로 넘긴다. 처음에는 재생성마다 넘겨서 정지 화면에서 영원히 전달되지 않았고(첫 실행 PS A 실패), 요청당 1회로 고쳤다.

**시험 (6ffd94b 와 같은 소스의 최종 빌드 — 커밋 전 `host_backend_owner.hpp` 주석 1줄만 고침(동작 무관); 종료 코드 + RESULT 줄)**
| 시험 | 결과 |
|---|---|
| UA `--rebuild-fails` (재작성: `GNLINK_STREAM_TEST_FAIL_RESTART_FILE`, Detach **뒤** 실패) | **21/21 rc0** — (1) 1회차 실패(injected 1) → 파일 제거 → **같은 gen 2회차 ok=1** → 그림 복귀 → 복귀 edge 재구성. (2) 3회 실패·포기(injected 3), **10 s 동안 4회차 0·일반 retry 0(재시작 3회뿐)**, fence+500 ms 부터 그림 0, 연결 유지, 파일 제거 뒤 복귀 edge 재구성·그림 복귀 |
| PS `--still-pointerfirst` (신규) | **34/34 rc0** — `a pointer-only acquire does not answer it (request kept)` → 그 뒤 `delivered after` |
| PS ×2 / nofirst / UA ×2 | 32 ×2 / 26 / 54 ×2 rc0 |
| LK lock / prelogin / not-prelogin | 68 / 79 / 28 rc0 |
| 단위: owner(종료 4 경우 추가)·status_board(신규)·shared_core·mailbox·runtime_failure·attachment 19(**[D] 계약 반전**: cookie 0 거부, **[E]** 무효화 전 제출·후 완료 → 인코더 O·캐시 X, **[F]** detach 중 보유 completion → raw/캐시/episode 종료 0)·kick·policy·quarantine·monitor_selection·relay 84 | 전부 rc0 |
- **부정 대조**(되돌린 빌드, 복원 뒤 PASS·표식 0):
  - M9 포인터 전용이 요청을 닫음 → pointer-first 2 FAIL
  - M10 r2 의 두 번 읽기 → status board "갱신 전 look 이 applied" FAIL
  - M11 끝난 Serve 요청 처리 → owner 4 FAIL
  - M12 일반 retry 가 넘겨받음 → rebuild-fails 재시작 6회·포기 없음 FAIL
  - M13 detach 뒤 실패 = 완료(r2 규칙) → 2회차 없음·그림 미복귀 FAIL
  - M14 연속성 토큰 미검사 → attachment [E] FAIL
  - M15 cookie 0 허용 → attachment [D] FAIL
- 시험 첫 시도의 거짓 실패(시험 결함, 수정함): rebuild-fails (2) 가 (1) 의 edge 줄을 읽어 옛 gen 으로 판정했다. 지금은 edge 수가 늘 때까지 기다린다. 그때도 host 로그상 동작은 맞았다(`r3_b_host_uafail.log`: 3회·포기·이후 0).
- 증거: `.claude/caplife/r3_final/`(최종), `r3_mut/`(M9~M15), `r3_c/`(rebuild-fails), `r3_b/`.
- 제거된 시험 seam: `GNLINK_STREAM_TEST_DESKTOP_REBUILD_FAIL`(함수 호출 전 실패라 Detach 경로를 못 탔음). 남은 시험 전용: `GNLINK_STREAM_TEST_STILL_SCREEN=1|nofirst|pointerfirst`, 기존 `GNLINK_STREAM_TEST_FAIL_RESTART_FILE`, `REMOTE60_E2E_HOST_LOG_COPY`(시험 프로그램 쪽).

**한계(그대로)**: M5 는 출처 거부의 대조이고 fence 자체의 독립 대조가 아니다. FrameBuffer·DecodingSink 는 렌더 표시가 아니며 MF 지연 출력 때 header 와 그림의 대응이 어긋날 수 있다. 실 UAC·SYSTEM/Winlogon·실 정지 하드웨어·실폰은 미검증이다.

## r4 — FullFrame 실패 retry 상한 (2026-10-26) — 고정 후보 **adc9482** (6ffd94b 위 1 commit, worktree clean)
근거 `codex_capture_lifecycle_src_r3.md` (N1-b·N2-end·N3-host·N1-a DesktopRebuild 는 SOURCE OK — 다시 열지 않음). 이 범위만 보수했다. 버전·서명·게시·push 없음.
- **원인**: r3 의 일반 retry(`host_stage_time_limit.cpp`)는 DesktopRebuild 만 제외했다. 그래서 full-frame WGC 재시작이 Detach 뒤 실패하면 8 s backoff 로 무제한 재시작했다(ask 는 1회만 차감, restartPending 동안 ask 조기 return).
- **수정**:
  - 일반 retry 는 `restartOwner == General` 만 맡는다.
  - FullFrame 자신의 실패는 `ask_full_frame` 이 그 episode 의 다음 ask 로 재시도한다. 같은 3회·1 s·source key 이고, wrapper backoff 가 지난 뒤에만 ask 로 센다.
  - backend 단계의 full-frame 재시작은 pending 이 FullFrame 자신의 것일 때만 허용한다.
  - 소진 뒤에는 일반 tick·kick·posted flag 로 추가 시작이 0 이다. 새 유효 source 는 기존 N4 대로 새 budget 을 받는다. General geometry/선택 회복은 그대로다.
- **시험 대역(시험 빌드만)**: `GNLINK_STREAM_TEST_WGC_STILL=1` — WGC attachment 마다 첫 프레임만 넘긴다(비활성 검사 뒤에 걸러서, 꺼진 동안 온 프레임은 캐시를 지운다). 실패는 기존 `GNLINK_STREAM_TEST_FAIL_RESTART_FILE`(Detach 뒤)을 쓴다.
- **반례 시험 UA `--fullframe-fails`(신규) 18/18 rc0**:
  - (C) ask 1 재시작 실패(injected) → 파일 제거 → **같은 episode ask 2 가 ok=1**, 일반 retry 0, 그림 복귀, 재시작 2회.
  - (A) 계속 실패: **재시작 3회(ask 1/3~3/3)**. 그 뒤 12 s 동안 추가 시작 0(injected 3), `capture retry recovered` 0, `ask=4/` 0, 그림 0, 연결 유지.
  - (B) 새 세션 = 새 source(epoch) → episode 3 의 ask 1 로 재시작 ok·그림 복귀.
  - host 로그(`caplife/r4_host_uaff.log`)의 순서가 위와 같다.
- **부정 대조 M16**(r3 규칙 복원: 일반 retry 가 FullFrame 도 재시도) → (C) ask 2 재시작 없음(일반 retry 가 가로챔)·(A) **재시작 6회 시도** FAIL. 복원 뒤 표식 0.
- **회귀**(복원 빌드): fullframe-fails 18 · rebuild-fails 21 · PS 32 · nofirst 26 · UA 54, 전부 rc0. 증거 `.claude/caplife/r4_final/`, `r4_mut/M16.txt`.
- 한계는 r3 와 같다(렌더 표시 아님, 실 UAC·실기기 미검증). 같은 episode 를 다 쓴 뒤 화면이 돌아오려면 새 source(재접속·idle 재부착·desktop edge·선택)가 필요하다 — Codex 가 정한 fail-closed 방향이다.
