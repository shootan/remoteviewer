# keepalive-multi r2 보고 (작업용2 → 검증용 remote#pvbntb3z, 2026-10-16)

지시 `task_keepalive_multi_r2.md`, 계약 `codex_keepalive_multi_2c22313.md` + r1. worktree `C:/work/gnlink-wt/feat-keepalive-multi`, branch `feat/keepalive-multi`. main 머지·push·게시·버전 변경 없음.

## 고정 후보

| commit | 내용 | 범위 |
|---|---|---|
| cf82498 | M1 확립 뒤에만 commit·축출 / M2 연결 중 최신 탭 / L1 종료 정책 / 구 host 안내 / 시험 전용 TTL / run_detached / 문서(자원표·history) | 제품 + 시험 + 문서 |
| **92956b1 (HEAD)** | 카드 남은 시간 마지막 1분 "1분 미만"(초 단위 갱신이 목록을 매초 다시 그림) | 제품 표시 + 시험 |

- 출하형 APK(plain `assembleDebug`, 92956b1): `e05cb5ccb6387233aa05fdc839a2efd461e0c33347a0534523cf9916231fb888`
- fixture APK(`-Pgnlink.testDirectoryUrl=http://10.0.2.2:18320`, **cf82498**): `528570ab16b3e7aa8afcf76c5b7dba346b1dbd9cdfaf73e9b7d44395a0c16414` — km2_r4 에 사용. 92956b1 과의 차이는 `SessionPark.formatRemainingCoarse` 1함수(카드 문구)뿐.
- 시험 정책 fixture APK(+ `-Pgnlink.testKeepParkTtlMs=45000`, 92956b1): `7b689f23322b8ff07cd3794f632a45c71f82e7f8b879e88d5ec47c0e76412101` — km2_ttl_r2.
- 시험 host `GNLinkStreamPinnedSelectTest.exe` / harness / `ka_relay_client_test.exe`: build-local (해시는 각 run 의 `fixture.txt`).

## M1 — 확립 전 LRU 금지 (cf82498)

- `SessionTable::Commit(handle, gen, now)` → **Committed / Pending / Failed**. Entry 의 `Established()` = controller `Connected`(WorkerMain 이 인증 Hello + control link 성립 뒤에만 설정), `Generation()` 이 connect 가 만든 세대와 같아야 함. Pending = 아무것도 안 바뀜(축출 표시 유지, 기다림 없음, 상태 tick 마다 다시 물음). Failed = 끝났거나 다른 연결 → 그 entry 반납(Fail), **축출 0**.
- JNI `nativeCommit(handle, gen)` → `[status, closed...]`. 디렉터리 연결·수동 연결 모두 `tryCommit()`(상태 tick, 최대 20 s 뒤 반납) 경유. 표시·활성화도 commit 뒤.
- 한도 기록: `nativeEntriesJson` 에 `live`(한도 안, 예약 포함)·`reserved`(미확립)·`marked`(축출 예약된 parked)·`closing`(teardown 중).
- **반례 FAIL→PASS**: `session_table_test` [9] — 한도 3(A,B,C parked) → D 예약(A 표시) → 미확립 Commit = Pending·A 유지 → D 종료 → Failed·**축출 0** → 다른 세대 Commit 거절 → 확립된 E Commit = **정확히 A 1건**. 음성 대조: r1 순서(registry 즉시 commit)는 실패할 D 때문에 A 를 이미 잃음. 55/0, exit 0.
- **실제 앱(km2_r4 P8/P9)**: 한도 3(A,B,C) → X(host 정지: Hello 무응답) 탭 → `directory_connect_started` → `directory_connect_failed step=establish udp hello ack failed evicted=0 kept=3`, A·B·C 카드 유지, host_x 연결 0 → D(direct) 탭 → `directory_connect_committed evicted=1` + `keep_end handle=1 host=km-pc-a reason=lru` 1건, host_a 세션 종료 17:50:14.

## M2 — 연결 중 최신 탭 (cf82498)

- `connectToDirectoryHost` 의 `if (directoryBusy) return` 제거. 연결 중(`connectingHandle`) 다른 PC 탭 → `cancelConnecting("superseded")`: 그 entry 의 op 무효화 + `nativeFail`(native close 가 네트워크 step 을 멈춤, timeout 대기 없음) → 새 탭 즉시 실행. 같은 PC 재탭은 같은 의도. 목록은 연결 중에도 탭 가능.
- **실제 앱, 응답 지연 fixture**(`/api/connect` 6 s 보류, ka_delay_proxy):
  - P3: B 연결 중 → A(parked) 탭 → `directory_connect_superseded handle=2`, **A 즉시 재사용**(park_reuse), B 의 늦은 응답은 `directory_connect_stale … step=before_native_connect` 로 punch/연결 0. host_b 그 구간 연결 0.
  - P4: C 연결 중 → B 탭 → C superseded, B 채택(committed), A 무관. host_c 그 구간 연결 0(첫 연결은 P7).
- 정리: 진행 중 연결의 entry 는 자기 것만 닫힘. 보관 탭(park 확인 중)·fallback 은 epoch/수명 대조 그대로.

## L1 — 종료 정책 (cf82498, 문서 `docs/keepalive_multi_resource_table.md` "종료 정책", `docs/history.md` 항목)

- **onStop**(configuration 변경 중 제외) = 화면 PC + 유지 PC + 연결 중 **전부 종료 + epoch 증가**(`endEverything` → `endAllKept`, 뷰어 입력 release·gesture·키보드 정리, scene 처음 화면). **0.2.30 의 '화면 세션 백그라운드 유지'와 달라짐을 history 에 명시**: "앱을 벗어나면 모든 PC 연결 종료".
- 확인된 네트워크 변화 = 전부 종료(화면 PC 포함). 감시는 parked PC 가 하나라도 있을 때만(화면 PC 하나뿐이면 0.2.30 처럼 감시 안 함) — 문서와 일치.
- '모두 끊기' = 이름대로 전부. 카드 끊기·화면 연결 해제 = 그 PC 만.
- 회전: Activity 가 configChanges 로 멈추지 않음 + `isChangingConfigurations` 면 종료 안 함(실기 회전 시험은 이번 묶음에 없음 — AVD 뷰어 진입 시 `configuration_changed orientation=landscape` 로그만, 종료 없음).
- **실제 앱**: P11 연결 보류 중 HOME → `keep_end_all reason=app_stopped`, 늦은 응답 채택 0, host_b·c·d 세션 17:51:09 동시 종료, 복귀 시 유지 0. P13 화면 D + parked C 에서 비행기 모드 → `keep_end_all reason=network_lost epoch=4 kept=2`(화면 PC 포함 종료).

## V1 실행 결과

### km2_r4 (6 host fail-closed, 제품 정책 3대·5분, fixture 528570ab) — **35/35, avd exit 0**

| 단계 | 결과 |
|---|---|
| P1 | 로그인, 6 PC(서로 다른 hostId·machineId) |
| P2 | A 새 연결 첫 화면 1396 ms, park 확인 |
| P3·P4 | M2 위(최신 탭 1회·옛 연결 채택 0·관계없는 A 보존) |
| P5 구 host | km-pc-old 연결 → `keep_unsupported`(토스트 "연결 유지를 지원하지 않습니다 — PC 업데이트 필요"), PC 목록 버튼 없음 → 연결 해제 → 그 PC 만 종료, A·B 유지 |
| P6 relay 대체 | 다른 기기(`ka_relay_client_test`, 같은 계정, relay) 가 B 접속(RESULT PASS) → host_b `session-rollover` → 앱 `keep_end … reason=peer_lost`, **자동 재연결 0**(`directory_connect` 없음), A 유지 |
| P7 | B 새로 → 크게보기 + 화질 BEST: host_b 만 6000000 적용(17:48:49), parked A 미적용 → C 새로: 뷰어 전체보기(B 의 크게보기는 B 에), host_c 는 활성화 때 사용자 저장값 6000000 적용 → 한도 A·B·C |
| P8·P9 | M1 위(X 실패 축출 0 → D 확립 시 A 만 LRU). **D 는 direct**: `directory_chosen 192.168.0.76:62724|private`(시험 forwarder `km_direct_fwd.js`, LAN 주소의 host 자기 private 후보를 127.0.0.1 host 로 전달, 출발지 allowlist), 첫 화면 433 ms |
| P10 비용 | 아래 |
| P11·P12 | 연결 보류 + HOME / sign-out → 채택 0, 전부 종료 |
| P13 | 재로그인 유지 0 → C park·D 화면 → 비행기 모드 → 전부 종료 |

host 로그: 연결은 앱이 의도한 때만(host_x 0, P3/P4 의 취소된 시도 0), 안정 park 구간 전부 **encoded 0 / video datagram 0**, control 0.33–0.73 KB/s.

### km2_ttl_r2 (시험 정책 park 45 s, fixture 7b689f23) — **15/15**

- `keep_policy maxHosts=3 parkTtlMs=45000`(시험 빌드에서만; 출하형 BuildConfig `TEST_KEEP_PARK_TTL_MS = 0L` → 정책 객체 기본 5분, release 빌드는 이 속성 거부).
- A park → B park(09:04:48.457) → A 재사용(화면) → **B 만** `keep_end reason=ttl` 09:05:33.908(=45.45 s, UI 조작 없이 화면은 A 뷰어), A 무관 → B 다시 = **새 연결**(handle 2→3) → A 의 첫 park 142 s 경과(첫 기한 지남)·두 번째 park 37 s → A **재사용 성공**(재활성화가 새 기한).
- 만료 집행: 각 세션 control loop 의 `ClientParkFence`(정책값 `SetParkLimitMs`) + 테이블 reap. UI 는 결과를 표시만 함. 폴링이 0 인 상태(앱 백그라운드)는 L1 상 전부 종료라 해당 없음.

### 실패·재실행 이력(숨기지 않음)
- km2_r1·r2: 스크립트 `to_hosts`/menu 가 TARGETS 화면·사라진 컨트롤을 처리 못해 중단 → 수정.
- km2_r3 32/35: P9 FAIL 3 = 앱 로그 **회전**으로 스크립트의 길이 기반 구간이 비어 판정 실패(로그상 D direct·commit evicted=1·A lru·첫 화면 131 ms 정상) → 마지막 줄 기준 구간으로 수정 → km2_r4 35/35.
- km2_ttl_r1 12/15: 제품 동작은 정상(각 45 s ttl)이나 마지막 1분 초 단위 카드가 목록을 매초 다시 그려 uiautomator 가 단계당 ~70 s → 순서가 기한을 넘김 → **제품 표시 변경**(92956b1) → km2_ttl_r2 15/15.

## 비용 (km2_r4, AVD, 기기 전체 `/proc/net/dev` 양방향 + 앱 프로세스)

| 상태 | rx B/s | tx B/s | 스레드 | RSS kB | PSS kB |
|---|---|---|---|---|---|
| 로그인 직후 목록, 유지 0 (에뮬레이터 안정 전) | 40057 | 9053 | 30 | 74656 | 33250 |
| **한도: 3대 유지(목록, 화면 PC 없음)** | **2644** | **1893** | **45** | 114284 | 54404 |
| 전부 종료 뒤 재로그인 목록, 유지 0 | 0 | 0 | 39 | 110056 | 56002 |

- 3대 유지 = PC 당 대략 수신 0.9 KB/s·송신 0.6 KB/s(host 측 control 송신 0.65–0.72 KB/s 와 맞음). 첫 기준선은 에뮬레이터 배경 통신이 섞여 비교 불가(두 번째 기준선 0/0).
- 스레드: 3대 유지 45 → 종료 뒤 39(유지 3대분 ≈6 개 해제). 첫 30 과의 차이 9 는 실행 중 늘어난 Java/Binder 풀일 수 있으나 **누수 여부는 미판정**(스레드 이름 비교 안 함). PSS 는 영상 디코더·Surface 사용 뒤라 첫 값보다 큼.
- 실폰 배터리 **미검증**.

## build gate (실제 게시 방식 assembleDebug + 기전)

- 출하형 APK(e05cb5cc): dex fixture 주소 0, BuildConfig `TEST_KEEP_PARK_TTL_MS = 0L`·`DIRECTORY_URL = https://gnlink.shotan.net`(생성 소스 확인), `.so` 에 `REMOTE60_CONNECT_GATE_TEST_HOOKS`/`testHook` 0·machineId seam 0. fixture 두 종은 fixture 주소 1(양성), TTL 빌드는 `45000L`.
- hook **이름 리터럴**(`step_lock_taken` 등)은 0.2.30 정정 기록대로 assembleDebug(−O0)의 쓰이지 않는 인자 문자열 — 기전 없음(구분 유지).
- 출하 host 측(`GNLinkStream.exe`) seam 0 은 r1 그대로(이번 host 소스 변경 없음).
- `gnlink.testKeepParkTtlMs` 는 release 태스크에서 거부(gradle `taskGraph.whenReady` 목록에 추가).

## 회귀 (exit 0)

connect_op_gate 43/0 · session_registry 31/0 · session_table **55/0** · android_decoder_binding 10/0 · client_park_state 34/0 · owed_key 32/0 · geometry 10/0. Android `assembleDebug testDebugUnitTest` 213/0(testDebugUnitTest 실행됨). JNI 시그니처 검사 73/73.

## 비차단 정리 (r2 §5)

- `run_detached`: task 를 `shared_ptr` 로 보관 → 스레드 생성 실패 시 fallback 이 빈 함수가 될 수 없음. 동기 fallback 은 table `transitionMu_` 를 쥔 문맥에서 실행됨 — 세션 Close 가 join 하는 스레드는 table 잠금을 쓰지 않으므로(decoder 잠금만, adapter 경유) 교착은 없고 다른 전환을 그동안 막을 뿐. 코드 주석에 명시.
- 장기 작업 승인 시점: 자원표 "장기 작업의 승인 시점" 표 — 매크로(시작 binding, 다르면 정지)·잠금 해제 순서(요청 시 binding 고정)·clipboard push/pull·미리보기·화질/캡처 설정. binding 조회 뒤 전환되면 그 entry 는 park inhibit 이라 controller 가 거절.

## 남은 미검증

- **stale clipboard / 미리보기의 실기 결합**: fixture host 의 clipboard 는 사용자 실제 클립보드를 건드리므로 꺼져 있음(`REMOTE60_CLIPBOARD_SYNC=0`) — host→폰·폰→host 적용 경계는 native/단위(ResumeFromPark 의 수신 폐기·baseline, park inhibit)뿐. 격리 station(parkclip) 연동은 이번 범위에서 미실행.
- 1088 crop 보수의 전환 결합: 시험 영상 480×270 이라 crop 경로가 타지 않음(실폰 필요). 크게보기·6M 대상은 위 P7 로 확인.
- 실폰·제조사 MediaCodec·실 Wi-Fi↔LTE 전환(AVD 는 비행기 모드로 onLost), 회전 실기, 스레드 누수 판정, 배터리.
- 정책값 3대·5분 **사용자 답 대기**(출하 전 필수).
