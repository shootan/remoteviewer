base = 'D:/remote/remote/.claude/'
p = base + 'caplife/apk0232_verify.md'
s = open(p, encoding='utf-8').read()
reps = [
    ('(작업용1 → 검증용, 2026-10-27 r2 — r1 을 대체)', '(작업용1 → 검증용, 2026-10-27 r3 — r2 를 대체)'),
    ('- 브랜치 `fix/apk-prelogin-select` **ed7f4ad** (worktree `C:\\work\\gnlink-wt\\apk-prelogin-select`). tracked clean.\n',
     '- 브랜치 `fix/apk-prelogin-select` **b554e76** (worktree `C:\\work\\gnlink-wt\\apk-prelogin-select`). tracked clean.\n'
     '  - SH r6: d29a034 (제품, Codex codex_sh_strength_r6.md SOURCE OK) · b554e76 (시험)\n'),
    ('  - sha256 `1d4a05f4f318496a895ac748be5705365598c3de677c6f4652988034bd3deebd`, 13,990,300 B\n'
     '  - 0.2.32 / versionCode 31, 4 ABI, 서명자 `dcc806ae…2990`, 단위 275/0, 게이트 ALL PASS(7)\n'
     '  - 이전 후보 8782f387… 는 대체됐다.\n',
     '  - sha256 `1a3eee63a992371f7ad455deedfabbc0e0b627c1c7432fa9f9251b8183d815ab`, 13,990,300 B\n'
     '  - 0.2.32 / versionCode 31, 4 ABI, 서명자 `dcc806ae…2990`, 단위 277/0, 게이트 ALL PASS(7)\n'
     '  - 이전 후보 1d4a05f4… 는 `rel-apk-0232\\superseded\\GNLink-0.2.32-1d4a05f4.apk` 로 보존했다(검증용 부분 배치의 대상).\n'),
    ('  - sha256 `9f1a0529d4c4a38df5f283f380f7c6a6dc2a0733dbcb7d20c044efe98eb5df02`\n',
     '  - sha256 `b105a845d9acd89a1280aac8d8b45f5ad213ad641cef7f4f24d6aa5379ab2e81`\n'),
    ('| 작업용 결과(후보 1d4a05f4) |', '| 작업용 결과(후보 1a3eee63) |'),
    ('**FAIL 이 정상**: "no window list / WindowSelect" 가 lines=12 로 실패', '**FAIL 이 정상**: "no window list / WindowSelect" 가 lines=13 로 실패'),
    ('| ~1분 | 35/35 |', '| ~1분 | 35/35 |'),
    ('| shseam | ~4 / 3 / 4분 | 26 / 36 / 6 |', '| shseam | ~4 / 3 / 4분 | — / 36 / 6 (compare_ab 는 r6 무관이라 이번 후보로 미실행; 1d4a05f4 에서 26) |'),
]
for a, b in reps:
    assert s.count(a) == 1, (s.count(a), a[:60])
    s = s.replace(a, b)
s = s.replace('## 공통 — 실행기 하나', '- ⚠️ 2026-10-27 작업용 배치 도중 AVD 의 시스템 서비스가 죽었다("Can\'t find service: package", 15 시간 가동). `adb -s emulator-5554 reboot` 후 정상. 결과가 모두 timeout/빈 줄이면 먼저 이것을 의심한다.\n\n## 공통 — 실행기 하나', 1)
open(p, 'w', encoding='utf-8').write(s)

def append(n, text):
    q = base + n
    t = open(q, 'rb').read().decode('utf-8')
    if '\r\n' in t:
        text = text.replace('\n', '\r\n')
    open(q, 'wb').write((t + text).encode('utf-8'))

append('report_sh_strength_r2.md', '''

### r6 재개 결과 (Codex codex_sh_strength_r6.md 제품 SOURCE OK, 2026-10-27)
- 최종 후보: d29a034·b554e76 의 noseam APK **1a3eee63…**. shseam 은 b105a845….
- AVD 결과(r6 관련):
  - shseam: `strength_ab` 35(출력 식별·정지 redraw·크기 불일치), `layout_b` 47(회전·resize·pan·fence·fallback·모서리), `touch_ab` 36, `latency_ab` 6.
  - 후보 그대로: `method_ui` 16.
- 첫 배치 도중 AVD 의 시스템 서비스가 죽어(package service 없음) 그 회차 결과는 무효다. AVD 를 재부팅한 뒤 위 시나리오만 다시 돌렸다.
''')
append('report_rel_apk_0232_r1.md', '''

## r6 — 최종 통합 후보 (SH r6, 2026-10-27) — 이 절이 r5 후보를 대체한다
**고정 후보: `fix/apk-prelogin-select` b554e76** (d29a034 제품 SOURCE OK). tracked clean. 0.2.32 / 31(미게시).
- APK `D:\\remote\\remote\\.claude\\rel-apk-0232\\GNLink-0.2.32.apk` — **13,990,300 B, sha256 `1a3eee63a992371f7ad455deedfabbc0e0b627c1c7432fa9f9251b8183d815ab`**.
  - 이전 1d4a05f4 는 `superseded\\GNLink-0.2.32-1d4a05f4.apk` 로 보존했다.
- 빌드: noseam clean, 단위 277/0. 4 ABI, `dcc806ae…`, 게이트 7 PASS. shseam b105a845… 는 게이트 FAIL(2).
- 후보 그대로:

| 시나리오 | 결과 |
|---|---|
| `method_ui` | 16 |
| `ua2` | 13 |
| `ua2_desktop` | 9 |
| `prelogin_ui` | 15 |
| `prelogin_tripwire` | 3 |
| `prelogin_ui_control` | 기대대로 FAIL(window-list 13) |

- shseam: `strength_ab` 35 · `layout_b` 47 · `touch_ab` 36 · `latency_ab` 6. `compare_ab` 는 r6 와 무관해 이번에 미실행(1d4a05f4 에서 26).
- 실 Host `--android-select` 37/37. 시험 Host 881b3e05… 는 변경 없음.
- 진입 문서 `caplife\\apk0232_verify.md` r3.
''')
print('ok')
