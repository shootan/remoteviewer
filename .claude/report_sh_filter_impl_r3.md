# report sh-filter-impl r3 — R1 seam 구조 차단 · R2 entry-before-buffer · R3 shader/Host 수신 fixture (작업용2 → 검증용 remote#wwml7h91, 2026-10-22)

task t-b7ss11ov / rev r3 / kind=result. 근거 `.claude/codex_sh_filter_impl_r2.md` R1~R3, 지시 `task_sh_filter_impl_r1.md` "r3 보수" 절.
**판정: R1·R2·R3 보수와 자체 AVD 검증 완료 · 검증용 독립 AVD 실행 대기.** 버전·설치·서명·게시·push 는 없다.

## 고정 후보
- `feat/sh-filter` HEAD **c494dc6**. 7609d0b 위에 2커밋이다.
  - b6ac421: 제품 + seam + fixture + harness.
  - c494dc6: seam(test 전용) + harness.
- APK (debug, `C:/work/gnlink-wt/sh-avd/`) SHA256:

| APK | 해시 | 소스 | 쓰인 시나리오 |
|---|---|---|---|
| r3h-noseam | `D0BB2C09…44B2A7` | c494dc6 | compare |
| r3h-shseam | `AC8B1D0E…AA5F1A` | c494dc6 | race, touch_host ×2, shader |
| r3g-noseam | `AA4F18FB…EA1A9E14` | b6ac421 | resize |
| r3g-shseam | `739C34B7…1D784292` | b6ac421 | viewer_never, hold |

  - r3g→r3h 의 차이는 `#ifdef GNLINK_SH_TEST_SEAM` 안쪽(race seam)뿐이다. noseam 의 소스는 같다.
- 시험 Host: DW 브랜치 빌드의 `GNLinkStreamMigrationTest.exe` `0771e768…6f152f56`(fc4cd57).
  - 브랜치를 건너 쓰는 fixture 이고 Host 는 수정하지 않았다.
- 단위 시험 249/0.
- APK gate(`gnlink_check_fixed_server.py --apk`):
  - r3h-noseam: ALL PASS 7.
  - r3h-shseam: FAIL. `sh-test-seam`, `shTestFault`, `shTestStrength`(dex)와 `debug.gnlink.sh.`(.so)를 검출했다. 부정 대조가 실제로 실패한 것이다.
  - self-test 23.

## R1 — seam 의 release 차단을 구조로
- release variant 는 항상 `src/noseam` 자기 소스셋만 컴파일한다. `src/shseam` 은 debug 만 쓴다.
- native 쪽 seam(`GNLINK_SH_TEST_SEAM`, libnativewindow 링크)은 debug variant 의 define 으로만 들어간다.
- `-Pgnlink.shTestSeam=true` 일 때는 **resolved task graph**(`gradle.taskGraph.whenReady`)에 release task 가 하나라도 있으면 거부한다.
- `--dry-run` 결과:
  - `assembleRel`, `aR`, `assembleRelease`, `build`, `assemble`: rc1 (거부).
  - `assembleDebug`+seam, seam 없는 `assembleRelease`: rc0.

## R2 — handoff entry 를 buffer release 전에 공개 (제품)
- `ReleaseHeldOutputLocked` 는 ring entry 를 먼저 쓴다. 쓰는 순서는 handoffNs·meta·size 를 쓰고 key 를 마지막에 쓴다. 그다음 `releaseOutputBuffer(AtTime)` 을 호출한다.
  - handoffNs 는 release 를 발행한 시각이다.
  - release 가 실패하면 그 entry 를 회수한다(key=0).
- 게이트 로그에 `latchesBefore` 를 추가했다. 0 이면 attach 뒤 첫 프레임이 확인한 것이다. 단위 시험도 있다.
- **지연 반례(race, seam)**: 아래 race2 (r3h-shseam, rc0, 5/0).
  - 고친 순서(entry → 300 ms → buffer): 첫 출력 확인, `latchesBefore=0`, fallback 없음.
  - r2 순서(buffer → 2000 ms → entry): 첫 출력 없음, unmatched latch 3, 그다음 3 s fallback(`effective=off`). 반례가 재현됐다.
- 반례 재현까지 두 번 고쳤다.
  - ⑴ b6ac421 의 seam 은 늦은 공개를 **decoder lock 안에서 sleep** 했다. 그래서 UI 스레드의 조회가 같이 기다렸고, 두 순서 모두 `latchesBefore=0` 이었다(r3final 의 race FAIL).
    → c494dc6 은 slot 을 lock 안에서 잡고 detached thread 가 지연 뒤에 쓴다. ring 256 > 2 s×60 fps 라 덮어쓰지 않는다.
  - ⑵ 그래도 300 ms 로는 재현되지 않았다(r3h batch 의 race FAIL, `latchesBefore=0`).
    - decoder 가 프레임을 due time 보다 앞서 release 한다. 정지 패턴에서 handoff→latch p50 은 722 ms(r2 순서)·1330 ms(고친 순서)였다.
    - 그래서 300 ms 늦은 entry 도 latch 보다 먼저 들어간다.
    → r2 순서만 2000 ms(seam 최대)로 올렸다.
  - 이것은 **반례 seam 의 조건**이지 실기 발생 주장이 아니다. 실제 위험 폭은 release 리드 시간에 따라 다르다.

## R3 — 생산 shader fixture (seam, debug 전용)
- 생산 `SharpenRenderer` 를 실제 BufferQueue 입력에 돌리고 ImageReader 로 읽어 커널과 비교한다. shader 25/0 (r3h).
- 입력:
  - crop: 64×48 버퍼, 64×40 cropRect, 아래쪽 패딩은 마젠타. ImageWriter 로 넣었다.
  - mirror·rot90: native `ANativeWindow_setBuffersTransform` + lock/unlockAndPost 로 넣었다. ImageWriter 는 window transform 을 싣지 않는다.
- 결과:

| case | 출력 | max \|d\| | 패딩 유입 |
|---|---|---|---|
| crop_s0 | 64×40 | 2.0 | 0 |
| crop_s35 | 64×40 | 2.4 | 0 |
| mirror_s35 | 64×40 | 0.5 | 0 |
| rot90_s35 | 40×64 | 0.5 | 0 |
| rot90_s35_noring | 40×64 | 0.5 | 0 |

  - 가장자리 4변 포함, 판정 기준은 ≤ 6.
- 부정 대조는 둘 다 FAIL 로 잡혔다:
  - control_crop_unclamped(tap clamp 제거): max 37.
  - control_rot90_noswap(1/4 회전 swap 제거): max 34.9.
- **이 fixture 가 찾은 제품 결함(b6ac421 에서 수정)**: ring entry 가 없는 프레임은 출력 크기로 step 을 잡는다. 그 출력 크기가 1/4 회전에서 잘못 swap 됐다. 출력 크기는 이미 표시 방향이라 swap 하면 안 된다.

## R3 — native 입력 큐 → Host 좌표 수신 (touch_host, seam)
- 캡처 대상은 패턴 **창**이고 Host 는 `background_message` 로 주입한다. 실 커서 이동이나 SendInput 은 없다.
- Host 로그로 그 창이 적용된 선택임을 확인한 뒤에만 탭한다(GUARD).
- 패턴 창은 받은 client 좌표를 기록한다. 같은 조건에서 앱 seam 로그의 큐 좌표와 대조한다.
- r3h 2회 모두 18/0. 단계: off / 0.20 / 0.35 / 크게 보기(0.20).
  - 앱이 보낸 점과 창이 받은 점이 1 px 이내였다. 예: app=(670,569) window=(670,569).
- **r3final 의 large_weak 1건 FAIL 은 INVALID 로 정정한다**: 검출 실패가 아니라 세션이 끊긴 것이다.
  - 앱: 크게 보기 약 6 s 뒤 `[status] error`(00:02:21.9)가 났다. 화면은 연결 화면이고 "clipboard sync failed" 가 보였다. 네이티브 `PumpClipboardSync` → `poll_clipboard` 교환 실패 → `SignalRuntimeFailure` 다.
  - Host: 계속 송출했다. `clientReportAgeMs=9436`(09:02:31)이 Host 쪽에서 본 모습이다.
  - 원인은 미확정이다. control 교환 하나가 실패한 것이고, ledger 의 수동 IP control 정지와 관계가 있을 수도 있다(단정하지 않음). 1회 발생이다.
  - **ledger 후보로 넘긴다.** 이 브랜치에서 Host/control 은 고치지 않았다.
  - harness 는 이제 같은 경우를 "INVALID — session dropped" 로 이름 붙인다.

## 기록 정정 (r2 보고)
- `report_sh_filter_impl_r2.md:101` 의 "identity 3/0(움직이는 띠 밖 |Δ| 평균 0.10, 최대 10)" 은 최종값이 아니고 영역도 다르다.
- **final3 identity 의 최종값은 전체 picture 기준 mean 0.179 / p99 3 / max 223** 이다.
  - 0.10 / 10 은 움직이는 띠를 뺀 부분 영역의 값이었다.
- r2 파일은 고치지 않고 여기서 정정한다.

## 그 밖의 결과
| 시나리오 | 결과 | APK |
|---|---|---|
| compare | 25/0 | r3h-noseam |
| viewer_never | 7/0 | r3g-shseam |
| hold | 11/0 | r3g-shseam |
| resize | 8/0 | r3g-noseam |

- 로그 위치: `C:/work/gnlink-wt/sh-avd/r3h/`(summary.txt, race2/), `r3final/`.
- 세션: 콘솔, AVD(x86_64 headless). 태그: gpu · pure-logic(단위).

## 미검증 · 다음
- 미검증: 실 폰(Adreno/Mali) 화질·지연, 실 Host 의 SendInput 경로(이번 수신 확인은 background_message 만).
- 검증용: 고정 후보 c494dc6 / 위 APK 해시로 핵심 AVD 독립 실행(race·shader·touch_host).
- ledger 후보: 클라이언트 clipboard poll 실패 1회 → 세션 종료(위 증거).
- 사용자 행동: 없음.
