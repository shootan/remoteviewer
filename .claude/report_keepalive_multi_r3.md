# keepalive-multi r3 보고 — 클립보드 결합 (작업용2 → 검증용 remote#pvbntb3z, 2026-10-16)

지시 `task_keepalive_multi_r3.md`, 계약 `codex_keepalive_multi_92956b1.md`. **제품 delta 없음** — 제품 HEAD 는 92956b1 그대로, 시험 커밋 **a2e9686**(시험 실행파일 1개 + CMake 대상 1개, "제품/테스트/문서: 테스트").

## 시험 구성

`km_multiclip_e2e_test` (TEST ONLY, never packaged) — `apps/native_poc/src/km_multiclip_e2e_test.cpp`, exe `d8f5cb3a…3031`.

| 층 | 실제 / 대체 |
|---|---|
| host | **실제** fail-closed 시험 host `GNLinkStreamPinnedSelectTest.exe`(`7207c99e…3feb`) 2대(A, B): 각자 staging·LOCALAPPDATA·포트, 127.0.0.1 bind, 입력 주입 off, **no-capture**(`REMOTE60_PINNED_TEST_NO_CAPTURE=1`, 캡처 객체 0 — 사용자 화면 0), `REMOTE60_CLIPBOARD_SYNC=1`, **격리 window station**(parkclip 과 같은 방식, `StationLock` 획득) 위 fixture 창에 고정. host 의 클립보드 handler(`ControlClipboardRequest`/`Update` → `HostClipboardHub`) 와 그 station 클립보드는 실제. |
| 폰 세션 | **실제** 제품 `ClientSessionController` 2개(APK native 세션, UDP 터널 control) — 클립보드 poll/push/park/resume/baseline 은 제품 코드. |
| 테이블·binding | **실제** 제품 `SessionTable`(Reserve / Commit(gen, 확립 뒤) / Activate / BeginPark). 폰 쪽 모든 클립보드 동작은 **그 동작 시점의 binding token** 으로 `SessionTable::FindByBinding` → controller — `native_bridge.cpp` 의 `bound_entry` + `nativeTakeIncomingClipboardText` / `nativeQueueClipboardText` / `nativeSeedClipboardBaseline` 과 같은 해석. |
| 대체 | JNI 마샬링, Kotlin 의 drain/push 루프(각 호출 1회로), Android `ClipboardManager` → **모의 로컬 sink**(drain 이 텍스트를 돌려준 순간 `setPrimaryClip` 대신 기록). entry 래퍼는 bridge 의 SessionEntry 와 같은 인터페이스(디코더 adapter 제외 — 화면 없음). |

- **두 host 는 클립보드 하나를 공유**한다(Medium 프로세스의 private station 은 로그온 세션의 하나뿐). 그래서 누가 무엇을 받았는지는 station 내용이 아니라 **각 host 자신의 로그 `[clipboard] update applied utf16Count=N`** 와 **sink 기록**으로 판정하고, 텍스트마다 길이를 달리했다: Hctl1(5) Pold12(6) HA12345(7) PB123456(8) HB1234567(9) H3-1234567(10) P3-12345678(11), 오래된 A binding 의 시도 20.
- 시험 host 는 no-capture 라 main loop 가 Startup 에 머물러 **클라이언트 연결 ~30 s 뒤 host watchdog 이 스스로 종료**한다(첫 실행 kmclip_r1 에서 발견: 3단계 마지막 2단언이 host 종료 뒤라 FAIL — 판정 무효). 순서를 poll(0.7 s) 몇 번 단위로 줄여 **~11 s** 에 끝내고, 끝에서 **두 host 가 아직 살아 있음**을 단언한다. 제품 변경 아님.

## 단언과 결과 — 30/0 ×3 (kmclip_r3·r4·r5, exit 0), qwinsta console Active

출발: A 연결·확립·화면 → park → B 연결·확립·화면 → park → A 재개(A 화면, B parked), baseline.
- **대조**: A 화면 중 host copy `Hctl1` → A binding drain 으로 sink 도착. **held 대조**(`--held-control`, kmclip_held 15/0): 아래 1 과 같은 1.6 s 보류 뒤 A 를 park 하지 않고 다음 tick 에 drain → `HA12345` 가 A 세션에 있었음(1 의 '적용 0'이 빈 판정이 아님).
1. **A 수신 보류 → A park / B 화면 → 늦은 A 수신 적용 0**: station 에 HA → A 세션이 받음(보류, drain 안 함) + A 에 폰 copy `Pold12` pending → `BeginPark(A)`(확인) → `Activate(B)`(resume, baseline seed). A 의 **옛 binding** 으로 drain = 없음(어느 PC 로도 해석 안 됨), B binding 으로 2.2 s drain = 없음(B 는 resume 때 baseline), **sink 에 HA 0**, host B 의 update 0(HA·Pold 모두).
2. **B 의 새 복사는 B 로만**: A 의 옛 binding 으로 복사 시도 → 거절(양 host 길이 20 없음). B binding 으로 `PB123456` → host B `update applied utf16Count=8`, host A 없음. B 화면 중 host copy `HB1234567` → B binding 으로 sink 도착.
3. **A 복귀 뒤 park 중 텍스트·옛 pending 재생 0 + 새 복사 정상**: `BeginPark(B)` → `Activate(A)`(새 binding ≠ 옛 것) → 2.2 s drain = **없음**(park 때 보류된 HA, park 중 HB 모두 재생 0), host A 에 `utf16Count=6`(P_old) **resume 뒤 0**·park 중 0(pause 전에 갔거나 안 감). 새 host copy `H3-1234567` → A binding 으로 sink 도착, 새 폰 copy `P3-12345678` → **host A 만** `utf16Count=11`, host B 없음.
- sink 기록(r2 예): `Hctl1 HB1234567 H3-1234567` — HA 없음. host A applied: `11` 만, host B: `8` 만.
- 사용자 WinSta0 클립보드 sequence 불변(206 → 206), 두 host 판정 시점 생존.

## 제품 delta

없음. 실패한 단언 없음(r1 의 2건은 시험 host 수명 문제로 판정 무효, 순서 단축 뒤 재현 0).

## 남은 미검증

- 실제 Kotlin drain/push 루프와 Android `ClipboardManager` 쓰기(대체됨) — 이 경로는 r1/r2 의 AVD UI 시험에서 클립보드 꺼진 상태로만 지나감. JNI 시그니처는 r2 의 73/73 검사로 대조.
- 두 host 가 서로 다른 클립보드를 가진 실제 2-PC 환경(여기서는 공유 — 로그로 귀속 판정).
- 실폰.
