# report sh-strength r2 — 선명도 강 0.60 + 표시 크기 1패스 A/B (작업용1, 인계 t-6y8i0w1t → 검증용 remote#yj8mrmrx, 2026-10-26)

**고정 후보: `fix/apk-prelogin-select` 6d5c032** (APK 0.2.32 단일 브랜치 — ① picker 보수 e8123e4 위 3 commit, tracked clean)
- 3dc15c7 **제품 A**: `SharpnessLevel` STRONG 0.35 → **0.60**, 문구 "강하게 · 0.60", 단위 시험 기대값.
- 2b5a718 **제품 B(실험)**: `SharpenRenderer` 표시 크기 1패스 모드, MainActivity 의 버퍼 크기·변환 연결. **켜는 스위치는 시험 seam 뿐**(`--ez shTestDisplayPass true`). 출하 seam(noseam)은 항상 A.
- 6d5c032 **시험**: shader fixture 강도 행렬·A/B, AVD 하네스 `strength_ab`/`compare_ab`/`latency_ab`/`touch_ab`, 화면 밖 layered 패턴, APK 게이트 표식 추가.
- 저장값 정책: 코드 "strong" 은 유지한다. 강하게를 골랐던 폰은 새 0.60 이 되고, 끄기·약 0.20·미설정은 그대로다(SharpnessLevel 주석에 기록).

## 1. 먼저 — 0.35 가 실제 다른 최종 픽셀이었나 (결정적 same-frame)
생산 `SharpenRenderer` 에 같은 입력 프레임("text": 2px 막대·1px 선·저대비 체커, 64×40)을 넣고 ImageReader 로 읽었다. 각 사례는 shader 를 CPU 로 그대로 옮긴 모델과 비교했다(AVD x86_64 SwiftShader).
- 판정: **26/26**, 모델과 최대 |d| ≤ 0.9.
- A 끼리 픽셀 차: 0.20 ↔ 0.35 는 mean 4.2·max 37, 0.35 ↔ 0.60 은 mean 6.3·max 39 → **0.35 는 실제로 다른 픽셀이었다(적용 버그 아님).**
- 저대비 가장자리 에너지 (A): off 14.2 / 0.20 36.7 / 0.35 52.8 / 0.60 80.4.
- 부정 대조:
  - B 에서 tap 을 출력 1px 로 → max 26 (포착).
  - B 에서 원본 크기를 모르면 **그리지 않음**(held 1, drawn 0) — 출력 크기 대체 없음.

## 2. 위치 효과 vs 강도 효과 (같은 프레임)
**fixture**(B ×1.33 vs "A 출력을 같은 bilinear 로 확대"):
- s=0 에서 차 mean 0.12 → B 기하 = A + 확대(기하 일치).
- s=0.35 에서 mean 4.2·p99 51·max 92, s=0.60 에서 mean 7.3·p99 67.
- 차이는 고대비 가장자리에 몰린다(중간 clamp 순서 효과). 저대비 쪽은 거의 같다.

**실제 APK 화면 캡처**(AVD 2400×1080 — 1440×810 원격 창이 **×1.333** 으로 1920×1080 표시. `compare_ab` 14/14 오프라인 재판정):

| 지표 (화면 영역) | A off | A .20 | A .35 | A .60 | B .20 | B .35 | B .60 |
|---|---|---|---|---|---|---|---|
| 얇은 글씨 edge | 8.40 | 9.95 | 10.47 | 10.94 | 12.48 | 14.39 | 16.32 |
| 저대비 글씨 edge | 1.49 | 2.67 | 3.52 | 4.96 | 2.67 | 3.61 | 5.16 |
| 1px 선 edge | 54.7 | 55.0 | 55.0 | 55.0 | 70.6 | 74.1 | 75.4 |
| 체커 edge / 클립 비율 | 59.0/.75 | 59.3/.76 | 59.3/.76 | 59.4/.76 | 72.3/.88 | 75.4/.88 | 80.6/.88 |

- **A 경로에서는 확대 뒤 강도 간 차이가 작다**(글씨 9.95 → 10.94). 사용자의 "약하게랑 강하게랑 차이가 없어" 와 맞는다. 0.60 으로 올려도 A 의 개선 폭은 작다.
- **B 는 같은 강도에서 고대비 가장자리가 훨씬 강하다**(글씨 +37% @0.35, 1px 선 +35%). B 0.20 이 A 0.60 보다 강하다 → **위치 효과 > 강도 효과**. 저대비 글씨는 A≈B(강도만의 효과).
- 눈으로 본 결과: `.claude/caplife/sh_r2/ab_text.png`, `ab_lines.png`, `ab_checker.png` (위에서부터 A off, .20, .35, .60, B .20, .35, .60).
  - A 의 1px 선은 확대 보간으로 회색 2줄처럼 번진다. B 는 검은 선이 또렷하다.
  - B 0.60 글씨는 굵고 진하다(OSLink 관찰과 같은 방향).
  - 이 배율의 캡처에서 할로·링잉은 눈에 띄지 않는다. 다만 클립 비율이 B 쪽이 높아(체커 .88 vs .76) 포화가 늘어난다 — 실폰 체감 확인 필요.
- 색·배치는 A/B 가 같다: 평면 색 같음(234,0,3), 그림 x 위치 같음(276), 네 테두리 정상.
- **부정 대조**: B 에 TextureView 배치 변환을 한 번 더 걸면(이중 변환) 그림 검출이 깨진다 → 포착됨.

## 3. 터치·기하 (`touch_ab` 36/36)
×1.33 패턴 창의 라임 블록을 화면에서 찾아 탭하고, Host(background_message, 그 창에만)를 거쳐 창이 받은 client 좌표를 앱이 보낸 좌표와 대조했다.
- A·B 각각 off/0.20/0.60/크게보기 모두 **1px 이내 일치·블록 안**이다. 예: B 크게보기 app=(670,556) window=(670,556).
- B 는 원격 좌표 계산을 바꾸지 않았다(ViewerViewport 그대로) — 화면 배치가 그와 같다는 것을 위 캡처와 이 탭이 함께 보인다.

## 4. 비용 (`latency_ab`, AVD SwiftShader, 같은 변화 입력 ~30 fps, 각 60 s, 5 s 줄 12개)

| | handoff→latch window p95 (중앙) | GL draw wall (중앙) | 건너뜀 |
|---|---|---|---|
| A off (direct) | ~87 ms | — | — |
| A 0.60 | ~96 ms | ~12.7 ms | 0 |
| B 0.60 (출력 1970×1080 = 원본의 1.85배 픽셀) | ~108 ms | ~17.8 ms | 0 |

**B 는 A 보다 p95 가 약 +12 ms 늦다** — "1패스라 추가 지연 없음" 이 아니다. 소프트웨어 GL 값이라 실폰 GPU 비용·발열은 미측정이다. 이전 S4 AVD 미달 기록은 그대로 둔다.

## 5. 이 PC 화면 무조작
- 기존 하네스는 화면을 덮는 최상위 패턴 창을 썼다. **이번 실행은 모두 `-Layered`** 다: UpdateLayeredWindow 창을 가상 화면 오른쪽 밖에 두고, 최상위로 올리지 않았다. Host 는 그 창을 WGC 로 캡처했다.
- 첫 시도는 정지 창이라 재접속 뒤 프레임이 없었다(encodedFrames=0). 그래서 측정 영역 밖 4×4 블록을 200 ms(비용 측정은 33 ms)마다 바꾼다.
- 터치 주입은 그 패턴 창에만 했다(GUARD 확인 뒤).
- 세션: console Active, RDP 없음. SLOT_REQUEST CreateNew → 삭제, 잔여 시험 프로세스 0.

## 6. 빌드·게이트
- APK `assembleDebug testDebugUnitTest`: noseam·shseam 모두 BUILD SUCCESSFUL, 단위 249/0.
- APK 게이트(`gnlink_check_fixed_server.py --apk`):
  - noseam `9c67f8d0…`: ALL PASS 7.
  - shseam `95ae4996…`: FAIL. `shTestDisplayPass` 를 포함한 dex 표식 4개와 native 1개를 검출했다 — 부정 대조가 실제로 실패한 것이다.
  - self-test 52.
- 시험 Host: `GNLinkStreamMigrationTest` `ecb2ff2e…`(이 브랜치 빌드).

## 권고 (Codex 판정 요청)
- **A(0.60)**: 출하 가능 후보다. 같은 프레임에서 0.35 와 실제로 다르고 기하·터치도 불변이다. 다만 ×1.33 확대 뒤의 체감 개선은 작다(글씨 +4%) — 사용자 불만을 해결한다고 주장하지 않는다.
- **B**: 사용자 관찰(OSLink 의 굵은 선)과 같은 방향의 큰 효과다. 기하·터치·색은 A 와 같다. 비용은 +~12 ms p95(AVD)이고 포화(클립)가 늘어난다.
- **권고: A+B 를 Codex 검토에 올린다.** B 의 제품 채택은 다음 증거 뒤에 판단해야 한다: 실폰 비용·발열, 스크롤 shimmer, 회전·pan·크기 변경, fallback 복귀.
- **B 에서 아직 안 본 것**:
  - 회전(rot90)·실기기 1088 crop(AVD goldfish 는 crop 없음 — ImageWriter crop 은 fixture 의 A 사례로만).
  - pan 이동 중 redraw, view 크기 변경(resize), B → fallback/direct 복귀, keepalive binding 수명.
  - 움직이는 장면 shimmer.
  - 실폰.

## 하지 않은 것
- 버전·서명·게시·push 는 하지 않았다. 폰 자동 설치·조작도 하지 않았다.
- 사용자 실기 안내 묶음(off/.20/.60 같은 화면 비교 방법)은 0.32 통합 보고에서 낸다.
- B 를 출하 기본으로 켜지 않았다.


## r3 — B 를 사용자 선택 "화면 크기 (실험)" 로, 기본 A (Codex codex_sh_strength_r2.md §2·N1·N2, 2026-10-26)
**고정: `fix/apk-prelogin-select` 2f6905d (제품) · 05fe4b9 (시험)** — 통합 후보 HEAD.
- 2f6905d 제품:
  - `SharpenMethod` {DEFAULT "default", DISPLAY "display"} 신설, 저장 키 `sharpen_method`(미설정·알 수 없음 = DEFAULT). 설정 탭에 "선명화 방식: 기본 / 화면 크기 (실험)" + 안내 문구(지연·발열 증가 가능, 선명도 끄기면 쓰지 않음). 'OSLink' 문구 없음.
  - 방식 변경은 `SharpenPathPolicy.restartRenderer()` — 현재 renderer 해제 확인 뒤 새 renderer 시작.
  - N1: B 가 **의도된** 순간부터(필터 강도·fallback 없음) 버퍼를 view 크기로 → renderer 가 처음부터 그릴 레이아웃으로 시작. EGL 크기 불일치 시 최대 2회 clear swap. `DisplayPlacement` 에 view 크기를 담아 view 비율로 그린다.
  - N2: placement 를 renderer 별로 dedupe·병합 → 변화 없으면 다시 그리지 않음(새 renderer 에선 초기화).
  - 시험 seam(`--ez shTestDisplayPass`)은 그대로 seam 빌드에만 있다; 출하 seam 은 0 → 사용자 설정만이 B 를 켠다.
- 05fe4b9 시험: `SharpenPathPolicyTest` restart, shader fixture `CASES_AB` 에 cropB133·rot90B133, AVD `method_ui` 시나리오, 게이트 마커 `shTestDisplayPass`.

### r3 결과 (AVD x86_64 SwiftShader, 콘솔 세션, 슬롯 CreateNew→삭제, 화면 밖 layered 1440×810 패턴)
| 시나리오 | APK | 결과 |
|---|---|---|
| `method_ui` (제품 설정 UI, seam 없음) | noseam a9fc4d11… | **16/16** — 기본=A, 실제 탭으로 화면 크기 → B(display pass `mode=1 method=display`), 끄기→direct·위치 유지, 강하게→B 재시작, 기본 복귀=A(edge ±10%), 재시작 뒤 저장 선택 유지 |
| `strength_ab` | shseam 69c4bfb4… | **30/30** — crop B max 2.2, rot90 B max 0.6 |
| `compare_ab` | 〃 | **26/26** — 글씨 edge A.35 10.48 vs B.35 14.39 |
| `touch_ab` | 〃 | **36/36** |
| `latency_ab` | 〃 | **6/6**, 변화 없는 구간 glRedraws A 0 · B 0 |

비용(`latency_ab`, 같은 입력, median): 창 p95 끄기 85.1 ms · A 0.60 98.9 ms · B 0.60 110.6 ms; draw wall A 12.1 ms · B 21.4 ms. ⚠️ SwiftShader 수치라 실기 GPU 비용이 아니다.

### 검증용 독립 확인용 자동화 (통합 APK 기준)
- `D:\remote\remote\.claude\caplife\sh_run.ps1 -Scenario <s> -Tag <t> -Seam false|true [-NoBuild]` — 제품 UI A↔B↔끄기·fallback·재시작: `method_ui`(noseam). 회전/resize·pan 재그리기·Host 모서리/레터박스·B crop/rot·스크롤 shimmer·비용/소유: `strength_ab`·`compare_ab`·`touch_ab`·`latency_ab`(shseam, `-Pgnlink.shTestSeam=true`).
- 출하 seam 확인: `automation/gnlink_check_fixed_server.py --apk <apk>` — noseam ALL PASS(7), shseam FAIL(2).

### 미검증
- 실기(Adreno/Mali) 화질·지연·발열, B 의 실기 회전/분할 화면.


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


## r6 — Codex codex_sh_strength_r5.md N1-a · N1-b (2026-10-27) — ⏸ 사용자 "일단 대기" 로 멈춤
**커밋: `fix/apk-prelogin-select` d29a034 (제품) · b554e76 (시험)** — tracked clean, WIP 없음.
- a) `SharpenOutputLog` 의 출력 token 을 프로세스 전역 카운터 하나에서 받는다. renderer A/B 가 같은 token 을 가질 수 없다.
- b) `SharpenLatch.qualify`(Activity 가 쓰는 판정):
  - 디코드 프레임의 **첫 화면 표시** = 그 프레임의 버퍼(VIDEO, 또는 VIDEO 가 latch 안 됐을 때의 REDRAW) 중 처음 latch 된 것. 1회만 센다.
  - 이후 같은 프레임은 redraw 로 센다.
  - CLEAR·unknown 은 그림이 아니며 `NONE_SHOWN` 이다(이전 proof 를 재사용하지 않아 fence 가 유지된다).
- 단위 277/0. 변이(각각 FAIL):
  - log 별 counter → 2
  - REDRAW 는 첫 표시 불가 → 1
  - clear 가 이전 proof 유지 → 4
- **하지 않은 것(대기 지시)**: 새 후보 APK 빌드, AVD 배치, apk0232_verify.md 갱신.
  - 검증용 배치 뒤 자동 시작하도록 예약했던 작업은 아무것도 시작하기 전에 취소했다.
  - 후보 파일은 여전히 1d4a05f4… 이고, superseded 사본도 만들지 않았다.
  - 따라서 1d4a05f4 후보에는 r6 가 **들어 있지 않다**.
- 재개 시: `caplife\\rel0232_r6_after_verifier.ps1`(또는 `rel0232_r6_batch.ps1` 바로) → 새 해시 → verify 문서·rel 보고 갱신 → pin.


### r6 재개 결과 (Codex codex_sh_strength_r6.md 제품 SOURCE OK, 2026-10-27)
- 최종 후보: d29a034·b554e76 의 noseam APK **1a3eee63…**. shseam 은 b105a845….
- AVD 결과(r6 관련):
  - shseam: `strength_ab` 35(출력 식별·정지 redraw·크기 불일치), `layout_b` 47(회전·resize·pan·fence·fallback·모서리), `touch_ab` 36, `latency_ab` 6.
  - 후보 그대로: `method_ui` 16.
- 첫 배치 도중 AVD 의 시스템 서비스가 죽어(package service 없음) 그 회차 결과는 무효다. AVD 를 재부팅한 뒤 위 시나리오만 다시 돌렸다.
