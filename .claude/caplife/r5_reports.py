base = 'D:/remote/remote/.claude/'

def append(n, text):
    s = open(base + n, 'rb').read().decode('utf-8')
    if '\r\n' in s:
        text = text.replace('\n', '\r\n')
    open(base + n, 'wb').write((s + text).encode('utf-8'))

append('report_sh_strength_r2.md', '''

## r5 — Codex codex_sh_strength_r4.md N1-c1 · c2 (2026-10-27)
**고정: `fix/apk-prelogin-select` 8d67710 (제품) · a6635a2 (시험)**

### c1 — 실제로 latch 된 버퍼
- 이제 renderer 의 모든 swap 은 **자기 presentation timestamp** 를 가진다(`SharpenOutputLog`).
  - 값은 2^62 이상으로, decoder 시간과 겹치지 않는다.
  - swap **전에** 종류(VIDEO / REDRAW / CLEAR), layout 세대, 보여 주는 decoder 프레임을 기록한다. swap 이 실패하면 그 기록을 지운다.
- view 의 latch 는 자기가 실제로 latch 한 버퍼의 timestamp(`SurfaceTexture.getTimestamp`)로 조회한다(`SharpenLatch`).
  - renderer 의 "최신 swap" 은 읽지 않는다. r4 의 "한 버퍼 일찍" 한계는 이로써 사라졌다.
  - CLEAR 나 미등록 버퍼(evict·다른 renderer·실패한 swap)는 아무것도 아니다: 프레임 아님, first output 아님, layout 표시 아님.
  - handoff·지연·first-output 매칭은 기록된 decoder timestamp 로 계속한다.
- 같은 layout 이 새 이름(세대)으로 오면, 그 이름으로 redraw 한 버퍼가 latch 된 뒤에만 표시된 것으로 본다(swap 없는 인정 제거).

### c2 — 미표시 layout 에서 시작한 터치 스트림 통째 차단
- `ViewerTouchRouter.route(.., layoutShown)` 이 ACTION_DOWN 시점에 판정한다.
  - 미표시 layout 에서 시작한 PC 스트림은 최종 UP/CANCEL 까지 SWALLOW 한다. 두 번째 손가락·move·부분 up 이 송신을 시작하지 않고, 도중에 표시돼도 재개하지 않는다. 다음 새 DOWN 부터 정상이다.
- PC 스트림 도중에 layout 을 보내면 `interruptViewerTouch` 로 1회 release 하고 나머지는 삼킨다. 로컬 pan 은 그대로다.

### 시험
- 단위 **275/0**:
  - `SharpenOutputLogTest` 6 — A latch 후 B swap 때 늦은 A callback 은 A 다. 같은 decoder 프레임의 CLEAR 는 아무것도 아니다. REDRAW 는 프레임 아님·layout 표시. 미등록·evict·실패·다른 renderer 는 아무것도 아니다.
  - `ViewerTouchRouterTest` +3.
- 변이:
  - lookup = 최신 swap → 2 FAIL
  - CLEAR 를 프레임으로 → FAIL
  - r4 의 DOWN-only 보류 → FAIL
- fixture `panB133_redraw`: 마지막 latch 버퍼를 자기 timestamp(2^62+5)로 **REDRAW gen 2** 로 식별하고, decoder 프레임은 같다. `strength_ab` 35/35.
- AVD(shseam 9f1a0529): `layout_b` 47/47, `touch_ab` 36, `latency_ab` 6, `compare_ab` 26.
  - layout_b 하네스 보강: 회전을 화면 방향으로 확인하고 1회 재시도, 크게 보기 선택 1회 재시도. 한 번 회전 탭이 먹지 않아 단계가 밀렸던 하네스 결함 때문이다.
- 한계:
  - 두 번째 손가락 반례는 라우터 단위 시험이다. adb 는 다중 터치를 주입하지 못한다.
  - "A latch → B swap → 늦은 callback" 순서는 순수 단위 시험이다. AVD 에서 그 경합을 재현하지 않았다.
''')

append('report_rel_apk_0232_r1.md', '''

## r5 — 최종 통합 후보 (SH r5 + evidence A, 2026-10-27) — 이 절이 r4 후보를 대체한다
**고정 후보: `fix/apk-prelogin-select` ed7f4ad** (tracked clean). 0.2.32 / 31 그대로(미게시).
- APK `D:\\remote\\remote\\.claude\\rel-apk-0232\\GNLink-0.2.32.apk` — **13,990,300 B, sha256 `1d4a05f4f318496a895ac748be5705365598c3de677c6f4652988034bd3deebd`**. 8782f387… 는 대체됐다.
- 빌드: noseam clean, 단위 275/0. 4 ABI, `dcc806ae…`, 게이트 7 PASS. shseam 9f1a0529… 게이트 FAIL(2).
- 후보 그대로:

| 시나리오 | 결과 |
|---|---|
| `method_ui` | 16 |
| `ua2` | 13 |
| `ua2_desktop` | 9 |
| `prelogin_ui` (A) | 15 |
| `prelogin_tripwire` | 3 |
| `prelogin_ui_control` | 일반 Host 부정 대조 — no-window-list 검사 FAIL(lines=12), **기대대로** |

- shseam: `strength_ab` 35 · `compare_ab` 26 · `touch_ab` 36 · `latency_ab` 6 · `layout_b` 47.
- 실 Host `--android-select` 37/37(`sh_run.ps1 -NativeExe` 로 실행), picker 단위 60/60.
- 시험 Host `GNLinkStreamMigrationTest.exe` 881b3e05… (새 값). 출하 `GNLinkStream.exe` 1c0bc4f0… 에는 seam 표식이 0 이고, 시험 Host 에는 양성이다.
- 독립 실행 진입점: `.claude\\caplife\\apk0232_verify.md` r2.
''')

append('report_apk_prelogin_select_r1.md', '''

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
''')
print('ok')
