base = 'D:/remote/remote/.claude/'

def append(n, text):
    s = open(base + n, 'rb').read().decode('utf-8')
    if '\r\n' in s:
        text = text.replace('\n', '\r\n')
    open(base + n, 'wb').write((s + text).encode('utf-8'))

append('report_sh_strength_r2.md', '''

## r4 — Codex codex_sh_strength_r3.md N1 a~c + Activity 자동 행동 (2026-10-27)
**고정: `fix/apk-prelogin-select` 9886c22 (제품) · 0c48c6f (시험)**

### 정정 (r3 절)
- r3 절 "검증용 독립 확인용 자동화" 의 "회전/resize·pan·모서리/레터박스·스크롤 shimmer" 를 그 명령들이 검증한다는 문장은 **틀렸다**. 당시 그 시나리오들은 그 행동을 실행하지 않았다. 이번 r4 의 `layout_b` 가 shimmer 를 뺀 나머지를 실행한다.
- r3 비용 줄 "창 p95 85.1/98.9/110.6" 은 **12개 창 p95 의 중앙값**이었다. 전체 segment p95 와 다르고, 성능 기준을 통과했다는 뜻도 아니다.

### 제품 (9886c22)
- a) 크기가 맞지 않으면 2회 뒤에 영상을 그리던 경로를 없앴다. 영상은 view 크기 출력에만 그린다.
  - 맞지 않으면 clear swap 을 하고 16 ms 뒤 재시도한다. clear swap 의 반환값도 검사한다.
  - 한 layout 에피소드는 최대 1 s 이고, 넘으면 renderer 실패로 처리해 정책상 direct 로 돌아간다.
  - clear swap 은 마지막 프레임 timestamp 를 싣는다(프레임 전이면 latch 가 무시하는 표지). 그래서 새 프레임이나 지연 표본으로 세지 않는다.
- b) 두 대기를 분리했다: 새 프레임 latch 대기와, 이미 latch 된 프레임을 새 layout 으로 다시 그리는 대기. 재시도는 새 decoder 프레임 없이 다시 그린다(updateTexImage 없음, timestamp 유지).
- c) placement 를 세대 번호와 함께 보낸다. renderer 는 마지막 **영상** swap 의 세대(`swappedPlacementGen`)를 게시하고, Activity 는 TextureView latch 에서 이를 읽는다.
  - B 가 아직 화면에 보이지 않은 layout 을 그리는 동안 시작된 원격 터치는 보내지 않고 `input_held_layout` 로그만 남긴다. 표시된 뒤의 다음 터치는 보낸다. pan 은 그대로 로컬이다.
  - 한계: latch 시점의 읽기가 swap 을 한 버퍼 일찍 인정할 수 있다.

### 시험 (0c48c6f)
- seam fault: `shTestFault layout:<n>[@k]`, `holdplace:<ms>` (시험 빌드 전용, 기존 게이트 키 `shTestFault`).
- fixture:
  - `layoutB133_never`: 영상 0 프레임, clear swap 유한, `failed:layout`.
  - `panB133_redraw`: 새 프레임 0, 크기 검사가 3번 거짓말한 뒤 redraw 정확히 1회, 새 위치(21 px 이동 max 0.0), timestamp 동일.
  - fixture 요약은 앱 파일에서 읽는다(logcat 이 4 KB 넘는 줄을 자른다).
- AVD `layout_b` (실제 Activity, 패턴 창 캡처, Host 메시지 주입) **47/47**:
  - 가로·세로 letterbox 탭 송신 0. 모서리 바로 안쪽 탭 → 창 (3,3)/(1435,805), 세로 (5,5)/(1432,801).
  - 뷰어 회전 버튼으로 세로 전환(placement view 1080x1970) 후 되돌림: fallback 0, 그림이 placement 위치, lime 탭 정확.
  - 크게보기 + 화면 이동 pan: 그림 이동, 송신 0.
  - `wm size` 85% (이 AVD 만, finally 에서 reset): placement 1630x918 을 따라감, 정확.
  - fence `holdplace:8000`: 틈의 탭 송신 0(`input_held_layout sent=3 shown=2`), 표시 뒤 탭 정확.
  - fallback `swap:40`: direct, 테두리 위치 유지, 탭 정확.
- 변이(각각 해당 검사가 FAIL, 복원 해시 일치):
  - r3 fall-through → `layoutB133_never` FAIL(drawn=1).
  - r3 redraw 대기 → `panB133_redraw` FAIL ×2(redraws=0).
  - fence 제거 → fence 검사 FAIL(탭 (720,405) 송신).
- **스크롤 shimmer: 이 하네스에 판정 기준(oracle)이 없어 자동 판정하지 않았다.** fixture crop/rot 결과를 이것으로 확대 해석하지 않는다.
- AVD 정지 pan 은 패턴 pulse(200 ms)로 프레임이 계속 오는 상태였다. "decoder 새 프레임 0" 조건은 fixture `panB133_redraw` 가 증명한다.

### 통합 배치 (최종 HEAD, 아래 rel 절의 후보)
- `strength_ab` 34/34, `compare_ab` 26/26, `touch_ab` 36/36, `latency_ab` 6/6, `layout_b` 47/47 — shseam 1f63839e…
- `method_ui` 16/16 — 후보 APK 그대로.
- 비용(같은 입력, SwiftShader; 실기 성능 아님):

| | 12창 p95 중앙값 | 마지막 segment p95 | draw wall 중앙값 | glRedraws |
|---|---|---|---|---|
| off | 89.5 | 89.5 | — | 0 |
| A 0.60 | 99.8 | 100.4 | 13.0 | 0 |
| B 0.60 | 110.3 | 110.8 | 19.4 | 0 |
''')

append('report_apk_prelogin_select_r1.md', '''

### r3 시험 보수 (Codex codex_apk_prelogin_select_src_r3.md — 제품 9afcec7 SOURCE OK)
- 7795289: 실 Host e2e `viewer_prelogin_login_e2e_test` 의 GateSink 에 `CommitLocalSelectionFor` 를 추가했다. 자체 mu 안에서 Android sink 와 같은 callback 이다. 이것이 없으면 기본 Abandoned 로 tracker 가 지워져 과거 37 PASS 를 재사용할 수 없었다.
- 그 target 만 재빌드(exe ed69ea1f…)하고 실제 로그인 화면 Host 로 `--android-select` 를 돌려 **37/37**(rc0, 콘솔 세션, 슬롯 삭제 확인)을 얻었다. 기본 Commit 은 Abandoned 그대로다(옛 read-후-callback fallback 없음).
''')

append('report_ua2_apk_r1.md', '''

### r3 통합 확인
최종 후보 APK(8782f387…)에서 `ua2` 13/13 이다. 아래 rel 절을 참조한다.
''')

append('report_rel_apk_0232_r1.md', '''

## r4 — 최종 통합 후보 (picker r3 + UA-2 r3 + SH r4, 2026-10-27) — 이 절이 r3 후보를 대체한다
**고정 후보: `fix/apk-prelogin-select` 7795289**
- APK 를 빌드한 커밋은 0c48c6f 이다. 그 뒤의 7795289 는 데스크톱 e2e 시험 파일 하나만 바꿔 APK 에 들어가지 않는다. tracked clean. 버전은 0.2.32 / 31 그대로(미게시).
- 포함 커밋:
  - picker: 9afcec7 · 3a17444 · 7795289
  - UA-2: 482a901 · 3e4d567
  - SH: 9886c22 · 0c48c6f
- APK: `D:\\remote\\remote\\.claude\\rel-apk-0232\\GNLink-0.2.32.apk` (덮어씀; r3 의 ece254f0… 는 더 이상 후보가 아니다)
  - **13,973,916 B, sha256 `8782f38701e5b6ba154a068a7d15949574a8c9a56166e248871caa4807f1769f`**
- 빌드·패키지:
  - noseam `clean assembleDebug testDebugUnitTest` rc0, 단위 **266/0**.
  - aapt versionCode 31 / 0.2.32, 4 ABI. apksigner V2 `dcc806ae…2990`.
  - 게이트: 후보 ALL PASS(7), shseam 대조 FAIL(2).
- 후보 그대로: `method_ui` 16/16, `ua2` 13/13.
- 같은 HEAD 의 shseam(1f63839e…, product-equivalent test build; 차이는 shseam 소스셋만): `strength_ab` 34, `compare_ab` 26, `touch_ab` 36, `latency_ab` 6, `layout_b` 47 — 모두 ALL PASS.
- 실 Host: `viewer_prelogin_login_e2e_test --android-select` 37/37.
- 미검증:
  - 실기 화질·지연·발열.
  - 스크롤 shimmer 판정(oracle 없음).
  - 바탕화면 대상 backend IDLE 줄의 AVD 실행.
  - shseam 빌드는 noseam 과 해시가 다르다(허용된 차이). 시험 빌드는 권한 항목을 바꾸지 않는다.
''')
print('ok')
