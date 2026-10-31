# keepalive-multi r1 보고 (작업용2 → 검증용 remote#pvbntb3z, 2026-10-16)

계약 `codex_keepalive_multi_r1.md`, 지시서 `task_keepalive_multi_r1.md`. worktree `C:/work/gnlink-wt/feat-keepalive-multi`, branch `feat/keepalive-multi` ← main `eedd207`. main 머지·push·게시·버전 변경 없음.

## 고정 후보

| commit | 내용 | 범위 |
|---|---|---|
| 0b1632e | 자원표 `docs/keepalive_multi_resource_table.md` | 문서 |
| 05d7e86 | `session_registry.hpp`(키·handle·예약·LRU·TTL·epoch) + gate per-entry 취소/프로세스 유일 번호 | native + 시험 |
| 47f0108 | 디코더 binding token(모든 sink 방향) + `EntrySinkAdapter` | APK native + 시험 |
| c5cafb4 | `session_table.hpp`(registry+map+decoder bind 접착, 전환 잠금, 락 밖 teardown) + park 한도 정책값화 | native + 시험 |
| 0bb679a | 브리지 entry 테이블화(JNI handle/binding) + Kotlin 다중 유지·전환·UI | APK + 시험 + 문서 |
| 567ba1d | AVD 에서 찾은 결함 2건 수정 + 시험 host machineId seam·harness 색 표시 | APK + 시험 |
| **2c22313 (HEAD)** | table 이 자기 park 확인 중인 PC 활성화도 거절 + 화면 PC peer 종료 시 오류 문구 유지 | APK + 시험 |

- 출하형 APK(plain `assembleDebug`, 2c22313): `341bd095d244ff65f69681355e04f2362c9d09bf5b221beda3c6fa0e4302b549`
- fixture APK(`-Pgnlink.testDirectoryUrl=http://10.0.2.2:18320`, 2c22313 소스): `647fa32fa7af468962d760d382e78853f5b152438bcdd4bf128bbfc121dd909d` (`.claude/e2e/km-fixture.apk`, km_r5)
- 시험 host `GNLinkStreamPinnedSelectTest.exe` `7207c99e…3feb`, harness `2963bbe9…bb36`, 출하형 `GNLinkStream.exe` `07835139…`(build-local)

## 설계 요약

- **자원표**(0b1632e, token 대응 갱신 0bb679a): 0.2.30 전역 `g_session_controller`·`g_rendezvous`·`g_rendezvous_error`·`g_chosen_candidate`·`g_observe/punch_count`·`g_connect_gate` → `SessionEntry`(entry 소유). 디코더/Surface 는 전역 1개, `g_macro` 는 시작 binding 에만.
- **키/handle**: 논리 키 `origin|account|hostId`, 프로세스 유일 handle(같은 PC 재연결 = 새 handle). 수동 IP 연결도 같은 테이블(manual, park 불가).
- **소유권**: op 번호 프로세스 유일(`ProcessOpCounter`), 취소는 entry gate 별. S1 CloseIfOwn·T1 CloseExplicit·Observe socket 회수가 entry 마다. 전역 lifecycle epoch 는 `nativeEndAll`(sign-out·세션 거절·exit·모두 끊기·화면 없는 상태의 네트워크 변화)에서만, 예약은 epoch 대조로 거절.
- **전환(A→B)**: `nativeBeginPark(A)` = A adapter token 0 → decoder Unbind → A 의 inhibit/release/pause/Pong fence → registry Park(TTL 시작). `nativeActivate(B)` 는 다른 entry 가 Active 이거나 confirming(1~3)이면 0(거절) → 새 binding(프로세스 카운터) → decoder Bind(코덱·D1·selection 초기화) → token → parked 면 Resume(실패 시 그 entry 만 종료). 확인 중 탭은 최신 1개 보관(`heldTap`, epoch·계정·디렉터리·앱 수명 대조) 후 실행.
- **용량**: `Reserve` 가 계수·LRU 표시를 한 잠금에서(원자), `Commit` 때 축출, `Fail` 은 표시만 취소(축출 0). LRU 대상은 Parked 만(Active·연결 중 제외).
- **콜백 울타리**: 모든 sink 방향(`...As(token)`)이 decoder 잠금 안에서 owner 검사. 화면 JNI 는 호출 시점에 잡은 binding 으로만(지연 실행인 unlock 순서도 시작 시 binding 고정). stale binding 은 아무것도 안 함.
- **잠금/수명**: table `mu_`(map) + `transitionMu_`(전환 직렬화), teardown 은 closer(분리 스레드)에서 shared_ptr 보유 채로, registry 제거는 close 후. `SessionEntry` 는 sink 를 controller 앞에 선언(파괴 순서).
- **만료**: 각 세션의 control loop(`ClientParkFence`, 한도는 정책값 `SetParkLimitMs`)가 UI polling 없이 집행, 테이블은 스스로 끝난 세션을 reap(ttl/pause_failed/peer_lost)만. 연결됐으나 표시 안 된 entry 는 10 s 뒤 `never_shown`.
- **종료 사유**: 개별 = 그 PC 끊기(카드)·연결 해제(화면 PC)·peer_lost·ttl·lru·pause_failed/unsupported·resume_refused. 전부 = 위 epoch 목록. **디렉터리 목록 누락으로 끝내던 0.2.30 `host_gone` 제거.**
- **UI**: 카드별 상태(연결 유지 중 / 준비 중)·남은 시간(분, 마지막 1분 초)·자기 `끊기`, 배너 "연결 유지 N대 · 최대 3대(화면 포함)" + `모두 끊기`.
- **정책 객체**: `SessionPark.Policy(maxHosts=3, parkTtlMs=5분)` / native `KeepalivePolicy` (`nativeSetKeepalivePolicy` 로 값만 교체). **제안값, 사용자 답 대기.**

## 결정적 시험 (전부 Release, 종료 코드 0)

| 시험 | 결과 | 반례(음성 대조) |
|---|---|---|
| session_registry_test | 31/0 | 0.2.30 단일 park 는 A↔B 왕복에서 A 상실 |
| connect_op_gate_test | 43/0 | [13] 단일 gate 면 B 시작이 A op 무효화 |
| session_table_test | 47/0 (경쟁 케이스 10^5 조회) | registry 단독은 A 미확인 pause 중 B 활성화 허용; 자기 park 확인 중 재활성화 가드 끄면 45/2 exit 1 |
| android_decoder_binding_test | 10/0 | 0.2.30(token 없음): B 늦은 reset 이 A 화면 정지 |
| client_park_state_test | 34/0 | (기존) |
| APK unit (plain, 2c22313, testDebugUnitTest 실행됨) | 213/0 | SessionParkTest 16: 0.2.30 단일 owner·r3 fallback 음성 대조 |
| JNI 시그니처 검사 `.claude/jni_sigcheck.py` | 73/73 일치 | 변조 사본 1건 불일치 → exit 1 |

## 2-host AVD (실 APK UI, fail-closed 시험 host 2대) — km_r4 **20/20**, km_r5(2c22313 fixture) **20/20 + 경로 PASS**

fixture `.claude/e2e/km_fixture.ps1`(directory :18310 + proxy :18320, km-pc-a 자홍 / km-pc-b 청록 표시, machineId `km-fixture-machine-a/b` → hostId `cdad…`/`be27…` 별개), `km_avd.py`, `km_slot.ps1`. qwinsta console Active. 사용자 host(pid 15736)·사용자 화면·폰·실 마우스 미접촉. 결과 `.claude/e2e/km_r4/`.

| 단계 | 결과 |
|---|---|
| 1 로그인, 두 PC 목록 | PASS |
| 2 A 새 연결 → 선택 → 첫 화면(자홍 스크린샷) | targets 4113 ms, 첫 화면 1695 ms |
| 3 PC 목록: A park 확인, 카드 "5분 남음"+끊기 | PASS |
| 4 B 새 연결(A 유지, handle 1≠2, A keep_end 없음) | targets 3604 ms, 첫 화면 687 ms (청록) |
| 5 PC 목록: A·B 둘 다 유지, 각자 끊기 | PASS (스크린샷 `5_hosts_both_kept.png`) |
| 6 A 재사용: A entry 카운터 connects/hellos/observes/punches 1/1/1/1 불변 | targets 318 ms, 첫 화면 632 ms (자홍) |
| 7 B 재사용: 카운터 불변 | targets 680 ms, 첫 화면 578 ms (청록) |
| 8 A 의 카드 끊기 → A 만 종료(user_disconnect), B 유지 | PASS |
| 9 A 종료 뒤 B 재사용 | PASS (카운터 불변) |
| 10 연결 해제(B) → HOSTS, sign-out → 로그인 | PASS |
| 디코더 | binding 1→2→3→4→5 매 활성화 새 bind/unbind, 새 코덱 5개 첫 입력 전부 IDR(key=1) |

**입력·설정 경로(km_r5, `.claude/e2e/km_host_route.py`, host 자기 로그 `[input-trace]`·`runtime-config-applied`)**: A 화면·B parked 상태에서 화면 탭 + 화질 BEST → host A 입력 2·config 6000000, host B **없음**; B 화면·A parked 에서 탭 → host B 입력 2, host A **없음**. (B 재사용 시 사용자 현재 설정(BEST)이 B 에 적용되는 것은 의도 — 창 밖에서 일어남.)

host 로그(`.claude/e2e/km_host_park.py`, km_r4·km_r5 동일 경향): 각 host **연결 1회**(왕복 동안 재연결 0), A 세션 종료는 8단계 시각(16:25:30)뿐. 안정 park(fence 후 2 s 제외) 구간 4개 모두 **encoded 0 / video datagram 0**, control 송신 **0.5–1.3 KB/s, 2.9–4.6 datagram/s**(host→폰, parked PC 1대당).

⚠️ km_r5 는 작업용1 측정 회차 c11on_6m60_r2 와 16:34:25–16:38:43 겹침(제가 그 회차 end 를 확인하지 않음) — 작업용1 에 알렸고 그 회차는 _ovl 처리·재측정 예정. km_r5 의 **기능 판정(20/20·경로·park 0)은 유효, 시간 수치는 부하 영향 가능** → 시간은 km_r4 값을 인용.

진행 중 실패 이력(숨기지 않음): km_r1 fixture 가 `--host-exe` 누락으로 출하 GNLinkStream 을 띄움 → machineId seam 미적용, hostId 1개(=출하 host 에 seam 없음의 부수 확인), 중단. km_r2 4단계 FAIL: B 활성화 거절 — reap 이 Connecting 세션을 peer_lost 로 끝냄 → `Alive()` 수정. km_r3: 카드 매 tick 재바인딩으로 uiautomator dump 실패 → 분 단위 표시·변화 시만 재바인딩. 둘 다 567ba1d.

## build gate (실제 산출물 + 기전)

- 출하형 APK(b9957d11) dex 의 fixture 주소 `10.0.2.2:18320` **0** / fixture APK **1**(양성). plain 빌드에서 `FixedDirectoryTest` 통과, fixture 속성 빌드에서는 같은 시험이 2건 FAIL(가드 작동 확인).
- ⚠️ connect gate 의 hook 이름 문자열(`step_lock_taken` 등)은 남아 있으나 **0.2.30 fixture APK 에도 동일하게 1** — hook 본체(`REMOTE60_CONNECT_GATE_TEST_HOOKS`)는 미컴파일, 이름만 죽은 인자. 이번 변경과 무관한 기존 상태(문서 `connect_op_gate.hpp:184` 의 "string 없음" 주장은 사실과 다름 → 별건 기록 대상).
- 출하형 `GNLinkStream.exe` `REMOTE60_PINNED_TEST_MACHINE_ID` **0** / 시험 host **1**.

## 비용

- 측정: parked PC 1대당 host→폰 control 0.5–1.3 KB/s(위). 안정 park 의 인코딩·영상 송신 0, 폰 decoder 는 binding 1개(parked 는 unbound).
- **미측정**: 폰 쪽 스레드·메모리(entry 당 controller 스레드 수), 폰→host 방향 bytes, 한도 3대 동시, 실폰 배터리.

## 남은 미검증 (자동화 전부 수행하지 못한 것)

- 실기기에서: 확인 중 빠른 A/B/C 탭(최신 intent 1개, AVD 에서 fence 가 ~30 ms 라 재현 못 함 — 단위 시험만), LRU 4번째 PC(fixture host 2대뿐 — native 시험만), PC 별 TTL 만료(5분 — native 시험만), sign-out/stop 뒤 지연 connect(native 시험만), known refused fallback, 구 host pause 미지원 표시, direct 경로(이번 AVD 는 relay), 다른 기기 탈취 뒤 재탈취 0, 선명도 crop·크게보기·6M 저장의 전환 뒤 대상 유지, 실폰.
- **Codex 결정 필요 1건**: 0.2.30 `onStop` 은 park 만 끝내고 **화면의 세션은 백그라운드에서도 유지**했다(사실, eedd207 MainActivity onStop). 계약 §6 은 onStop 을 '전부 종료'에 넣었고 서문은 '기존 보안 경계 유지'라 둘이 충돌한다. 현재 코드는 **0.2.30 유지**(parked 만 종료 + 진행 중 connect 중단 + 보관 탭 무효). 전부 종료로 바꾸려면 `onStop` 의 `endParkedKept("app_stopped")` → `endAllKept("app_stopped")` 한 곳(네트워크 변화 분기도 같은 선택).
- 연결 중(directoryBusy) 목록 탭은 0.2.30 처럼 막힘 — "최신 intent" 는 park 확인 중 탭에만 적용.
- 정책값(3대·5분) 사용자 답 미정.
