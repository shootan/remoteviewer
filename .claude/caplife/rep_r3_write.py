import io
base = 'D:/remote/remote/.claude/'

def load(n):
    s = open(base + n, 'rb').read().decode('utf-8')
    return s, ('\r\n' in s)

def save(n, s, crlf):
    if crlf:
        s = s.replace('\r\n', '\n').replace('\n', '\r\n')
    open(base + n, 'wb').write(s.encode('utf-8'))

# --- UA-2: Codex-requested wording corrections + r2 section
n = 'report_ua2_apk_r1.md'
s, crlf = load(n)
s = s.replace('\r\n', '\n')
old1 = '설정 화면이 보이는 동안 2 s 이하 간격으로 조회한다(question=backend 0).'
new1 = '설정 화면이 보이는 동안 **약 2초 주기(상태 poll 지연 포함)**로 조회한다(question=backend 0; `elapsed >= 2000ms` 를 status poll 에서 검사, 저장 Host 로그 간격 약 2.2~2.3초). *(r2 정정: 이전 "2 s 이하" 는 구현보다 강한 표현이었다.)*'
old2 = '**GDI 버튼(Host 의 격리 GDI = 3)은 그 뒤 한 번도 Host 에 닿지 않았다**(옛 UI 에선 Apply 실패 문구로만 보였다)'
new2 = '**확인한 이전 경로(공용 세션의 1..2 범위 검사)가 GDI = 3 요청을 거부했다**(옛 UI 에선 Apply 실패 문구로만 보였다). *(r2 정정: "그 뒤 한 번도 닿지 않았다" 는 전 배포 이력 단정이라 소스로 확인한 범위로 한정한다.)*'
assert s.count(old1) == 1 and s.count(old2) == 1
s = s.replace(old1, new1).replace(old2, new2)
s += '''

## r2 — Codex codex_ua2_apk_src_r1.md N1~N3 보수 (2026-10-26)
**고정: `fix/apk-prelogin-select` cf8c021 (제품) · 41de4cf (시험)**
- 제품: `DesktopBackendControl` 에 intent {asked, sent, sentSeq} 를 두고 JNI 상태 배열을 12칸으로 넓혔다(… roleKnown, intentAsked, intentSent, intentSentSeq). `DesktopBackendPresentation` 을 다시 썼다 — 상태 NOT_CONNECTED·CHECKING·NO_ANSWER_HOST·IDLE·APPLYING·APPLIED·STORED_FOR_DESKTOP·REFUSED. 점등 = **처리된** requested. "이 연결만" 문구는 FLAG_SESSION_SCOPED 일 때만. 저장 키 `desktop_backend` 제거(연결 한정).
  - N1: 처리됐지만 거부된 요청(prelogin WGC/GDI → DXGI 전용)은 APPLYING 에 머물지 않고 REFUSED + DXGI 안내, 다시 선택 가능.
  - N2: 구 Host(첫 pong 지연·조회 무응답)는 CHECKING → NO_ANSWER_HOST; 변경은 기존 wire 로 보내되 선택 완료 점등·연결 한정 단정 없음. 재연결 시 이전 상태 전파 없음.
  - N3: 문구 정정(위 26·28행).
- 시험: `DesktopBackendPresentationTest` 13, APK 단위 262/0 (통합 후보에선 263/0). Kotlin 변이(r1 규칙 복원) → 2 FAIL. ⚠️ A/B/A 지연 응답 순서는 이 변이로 판별되지 않는다(한계로 기록).
- AVD: `ua2` 13/13 (현 Host), `ua2_old` 7/7 (기준 Host `C:\\work\\gnlink-wt\\fix-capture-lifecycle-baseline\\Release\\GNLinkStreamMigrationTest.exe` 821535a7…).
- 미검증: 실기 Host 의 로그인 전 화면에서 실제 backend 전환 화면, A/B/A 지연 응답의 실 Host 재현.
'''
save(n, s, crlf)

# --- SH r3 section
n = 'report_sh_strength_r2.md'
s, crlf = load(n)
s = s.replace('\r\n', '\n')
s += '''

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
- `D:\\remote\\remote\\.claude\\caplife\\sh_run.ps1 -Scenario <s> -Tag <t> -Seam false|true [-NoBuild]` — 제품 UI A↔B↔끄기·fallback·재시작: `method_ui`(noseam). 회전/resize·pan 재그리기·Host 모서리/레터박스·B crop/rot·스크롤 shimmer·비용/소유: `strength_ab`·`compare_ab`·`touch_ab`·`latency_ab`(shseam, `-Pgnlink.shTestSeam=true`).
- 출하 seam 확인: `automation/gnlink_check_fixed_server.py --apk <apk>` — noseam ALL PASS(7), shseam FAIL(2).

### 미검증
- 실기(Adreno/Mali) 화질·지연·발열, B 의 실기 회전/분할 화면.
'''
save(n, s, crlf)

# --- rel 0.2.32 section
n = 'report_rel_apk_0232_r1.md'
s, crlf = load(n)
s = s.replace('\r\n', '\n')
s += '''

## r2 — 최종 통합 후보 (2026-10-27) — 이 절이 위 r1 후보를 대체한다
**고정 후보: `fix/apk-prelogin-select` 05fe4b9** (tracked clean). 버전은 그대로 0.2.32 / versionCode 31(미게시). 서명 manifest·게시·push 없음.
- 담은 것(r1 이후): ① picker r2 86df4cd·1de9dc7 · ③ UA-2 r2 cf8c021·41de4cf · ② SH r3 2f6905d·05fe4b9.
- APK: `D:\\remote\\remote\\.claude\\rel-apk-0232\\GNLink-0.2.32.apk` (덮어씀; r1 의 d5506f67… 는 더 이상 후보 아님)
  - **13,927,836 B, sha256 `b43a94ab22fa9346e4e36d3043a03a1818620b9bb74307b1663de0595a16374f`**
- 빌드: noseam `clean assembleDebug testDebugUnitTest` rc0, 단위 **263/0** (로그 `caplife\\apk\\gradle_0232r2.txt`).
- aapt: `com.remote60.androiddirect` versionCode 31 / versionName 0.2.32, native-code arm64-v8a·armeabi-v7a·x86·x86_64.
- apksigner V2 인증서 SHA-256 `dcc806ae…2990` (기존 서명자와 같음).
- 게이트 `gnlink_check_fixed_server.py --apk`: 이 APK **ALL PASS (7)**; 대조 shseam APK **FAIL (2)** — 부정 대조가 실제로 실패한다.
- AVD smoke: 이 APK 그대로(해시 b43a94ab…) `method_ui` **16/16** (Host ecb2ff2e…, 콘솔 세션). B: edge 10.97 → 16.31, 기본 복귀 10.97.
- 구성요소별 결과는 각 보고: `report_apk_prelogin_select_r1.md`(r2 절), `report_ua2_apk_r1.md`(r2 절), `report_sh_strength_r2.md`(r3 절).
- 미검증: 실기 화질·지연·발열, 실기 로그인 전 화면 흐름, KA 는 이 APK 에 없음(자동 preconnect OFF).
'''
save(n, s, crlf)
print('ok')
