# report sh-filter-impl r2 — Codex H1~H5 보수 (작업용2 → 검증용 remote#wwml7h91, 2026-10-22)

task t-b7ss11ov / rev r2 / kind=result. 근거 `.claude/codex_sh_filter_impl_r1.md`, 지시 `task_sh_filter_impl_r1.md` r2 절.

**판정: H1~H4·H5.3 보수 + AVD 자동 검증 완료(최종 APK 전 시나리오 ALL PASS) · S4 지연은 AVD 에서 기준 미달 그대로 · 폰 실기 대기.**
- 버전 0.2.30/29 그대로다.
- 설치·게시·push 없음.
- host 수정 없음. 수동 IP 10 s 정지는 검증용 ledger 에 두고 이 브랜치에 섞지 않았다.

## 고정 후보
- worktree `C:/work/gnlink-wt/feat-sh-filter`, 브랜치 `feat/sh-filter`, HEAD **7609d0b**.

| commit | 내용 | 구분 |
|---|---|---|
| aa0a3dc | H4 gate·Gradle·폰 절차 표기 | 테스트도구+빌드+문서 |
| 7c04456 | H1 surface/세대별 소유 | 제품+단위시험 |
| 8f3341f | H2/H3/H5.3 frame identity·gate·통계·tap 간격 | 제품(Kotlin+native)+단위시험 |
| f238f70 | **실행 중 발견한 결함 보수**(아래) + 입력 seam 로그 + renderer 진단 | 제품+seam |
| 0e1adb8 | AVD 시나리오(하네스·패턴 창) | 테스트 |
| 4b595e2 | **실행 중 발견한 결함 보수**(BLOCKED 동안 tick 마다 bind 로그) + 하네스 | 제품+테스트 |
| 7609d0b | 폰 절차(r2 지연 정의)·history | 문서 |

- 최종 APK(4b595e2, clean build; 위치 `C:/work/gnlink-wt/sh-avd/`):

| APK | SHA256 | gate | 비고 |
|---|---|---|---|
| `final2-noseam.apk` | `A77520A0BD6C0E7CB0A832055836374926492DA83E21D659D9A2C58E22A1CE96` | ALL PASS 6, rc0 | seam 없는 **debug 시험물**이지 출하·서명품이 아니다 |
| `final2-shseam.apk` | `B9A1DEB863B43545AB4F6C5295462D3224CE86B458B4912B12A9D0191E346C6D` | **FAIL rc1**(3 표식 검출) | AVD 실패 주입 전용 |

- 단위 시험: 22 클래스 248/0. SharpenPathPolicyTest 16, SharpenFramesTest 13.
- 환경: AVD gnlink_kbd_test(API34, SwiftShader), 시험 Host MigrationTest 0.2.161 loopback(입력 주입 off), DESKTOP(DXGI) 캡처 + 패턴 창, 수동 IP 접속.
  - ⑵ 상당(실 APK·실 UI·에뮬레이터)이다. ⑶ 실기는 아니다.
  - 각 실행은 `RESULT:` 줄로 판정했다. 원본은 `C:/work/gnlink-wt/sh-avd/final2/*`(compare·lifecycle·resize)와 `final3/*`(latency·seam 5종)에 있다.
  - final3 전에 AVD system_server 가 고장 났다(power/input 서비스 소실, uiautomator 시간초과). cold boot 뒤 같은 APK 로 다시 돌렸다. 고장 난 회차(final2 summary2)는 무효로 둔다.

## H 별 보수와 근거

### H4 출하 차단 (aa0a3dc)
- `gnlink_check_fixed_server.py --apk` 가 모든 classes*.dex 에서 `sh-test-seam`·`shTestFault`·`shTestStrength` 를 거부한다.
  - self-test 20/20(dex 유·무).
  - 실 APK 대조: noseam PASS rc0 / shseam FAIL rc1. r1 APK 쌍과 최종 쌍 모두 같다.
- Gradle 은 `-Pgnlink.shTestSeam=true` 와 release(또는 build/assemble/bundle) 조합을 거부한다. `assembleRelease --dry-run` 이 `refusing [assembleRelease]` 로 실패한다. debug + seam 은 허용한다.

### H1 surface 소유 (7c04456)
- 정책:
  - surface id 와 세대별 대기 release 를 기록한다.
  - timeout 이면 **BLOCKED** 다(NONE 아님). 그동안 bind/select/sync 는 원하는 상태만 기억하고, 늦은 release 가 오면 1회 실행한다.
  - `detached=false`(eglDestroySurface 실패·예외)면 그 surface 를 교체될 때까지 막는다(ProducerStuck).
  - 옛 surface 의 늦은 release 는 그 SurfaceTexture 만 1회 해제한다(id 별).
- renderer: 단계별 teardown, notes 기록, release 요청 이후 draw 0. seam `hold:<ms>` 를 추가했다.
- 단위 시험:
  - r1 의 "timeout → NONE" 기대를 반례로 뒤집었다. 보류 중 5 round bind/select/unbind 에서 producer 0.
  - A 파괴 → B → C, 늦은 release 가 각각 1회 해제된다.
  - 변이 "timeout→NONE" 은 2 FAIL.
- AVD `hold` 11/0:
  - 20 s 보류: 2 s timeout → BLOCKED 동안 0.20 선택은 `path=BLOCKED` 로 기억만 하고, 그 사이 `sharpen_start`·`path=direct` 0.
  - 늦은 `sharpen_released detached=1` 뒤 새 renderer 1회. 그림이 돌아오고 움직인다.
  - 다크모드 재생성 중 보류된 surface 는 `sharpen_surface_released` 1회.
  - stuck 0, tick 마다 bind 로그 없음.

### H2 표시 확인 (8f3341f)
- native ring 이 frame 의 handoff·stream 세대·codec epoch·visible 크기를 기록한다(`nativeHandoffInfo`).
- `SharpenOutputGate` 는 decoder attach 때 arm 한다. epoch 은 attach **전에** 읽는다.
- 확인 조건: 그 renderer 세대·같은 surface·attach 뒤 handoff·더 새 codec 의 **새 frame latch**. 그 latch 가 일어나는 순간 확인한다(사후 baseline 없음).
- 선택 확인은 그 선택의 stream 세대 latch 로만 한다.
- 자체 3 s timeout 이 모든 scene 에 적용된다.
- 단위 시험: 조기 latch, 다른 세대·surface·codec, 오래된 handoff, redraw, 세대 불일치, cancel.
- AVD `viewer_never` 7/0: VIEWER 에서 끄기→0.20, renderer 가 그리지 않는다 → `no_output_after_ready` fallback·정확한 안내 문구·원본이 살아 있음.

### H3 계수·분포 (8f3341f)
- 같은 surface 의 같은 timestamp latch 는 redraw 이고 표시 frame 이 아니다. r1 의 "swap 마다 latch 1개 빼기"를 대신한다. 단위 시험에 r1 규칙이 실 frame 을 잃는 대조를 넣었다.
- 구간 = (실효 수준·경로·renderer 세대·surface). 각각 따로 센다: warm-up 1 s, stale, unmatched, redraws.
- 표본은 정확히 36 000 까지 보관하고, 넘으면 overflow 로 세어 보고한다.
- **구간 전체** nearest-rank p50/p95/p99(순위 ceil(q·n); 1..100 에서 p50=50, p95=95).
- 로그: `sh_latency_segment`(구간 종료 시 전체 분포), `sh_latency`(5 s 진행, seg 번호 포함).
- renderer 카운터는 세대마다 0 부터 센다(clamp 없음). `drawWallUs` 는 벽시계 시간이지 GPU 시간이 아니다.

### H5.3 tap 간격 (8f3341f)
- step = 그리는 frame 자신의 visible 크기다(ring). 1/4 회전이면 축을 바꾼다. ring 에 없으면 출력 크기를 쓰고 그 횟수를 센다.
- AVD `resize` 8/0: 1920x1080 desktop → **1280x720 창 전환**(뷰어 창 시트)에서 renderer `src=1280x720`, 출력 크기 불일치 frame 1개도 자기 크기로 step, fallback 0.
  - 화질 변경만으로는 desktop 이 1920x1080 그대로라 이 경우가 생기지 않는다. 그래서 두 번째 패턴 창으로 확인했다.

### 실행 중 발견·보수 (f238f70, 4b595e2)
1. **회전 뒤 renderer 가 1970x1080 으로 그림 → 2중 resample**
   - TextureView 는 view 크기가 바뀌면 default buffer 크기를 view 크기로 되돌린다.
   - bind 가 가로 회전 **전**에 일어나면 앱 캐시가 맞아 보여 다시 설정하지 않았다. 직결은 decoder buffer 가 자기 크기를 가져 무관했다.
   - 증상: 0.20 의 가는 글씨 edge 가 끄기보다 낮았고, compare 4회 중 2회 간헐 실패했다. **r1 부터 잠재**한 결함이다.
   - 보수: `onSurfaceTextureSizeChanged` 에서 캐시를 무효화한다.
   - 확인: 같은 순서(bind at 785x2264 → 회전)를 포함해 6/6 PASS, 모든 진행 줄 `src == out`(compare 검사로 넣음).
2. BLOCKED/RELEASING 동안 0.5 s sync 마다 bind 로그 2줄과 buffer 재설정이 반복됐다. 영구 stuck 이면 NAS 로그가 넘친다. 크기가 바뀔 때만 하도록 고쳤다.

### H5 자동 검증
- **터치** `touch` 16/0:
  - 끄기·0.20·0.35 에서 lime 블록 탭 → 앱이 native 에 넣은 원격 좌표(seam 로그)가 블록 중심(670,570)이고 `원격 = 화면점 − 그림 원점` ±1 px.
  - 크게 보기에서도 블록 안이다.
  - 호스트 주입 off 라 **호스트 수신은 관측하지 않았다**(앱→native 큐까지).
  - 회전은 이 AVD 뷰어가 가로 고정이라 하지 않았다.
- **안내 문구** `faults` 28/0:
  - `uiautomator events` 의 TYPE_NOTIFICATION_STATE_CHANGED 텍스트가 **정확히** "선명도 적용 불가, 원본 표시" 인지 본다. context·shader·**swap:30**·never 4/4 다.
  - 다른 Toast("Right click…")는 기록만 하고 세지 않는다.
- **compare** 25/0 · **identity** 3/0(움직이는 띠 밖 |Δ| 평균 0.10, 최대 10) · **lifecycle** 14/0.
- **crop/flip/padding fixture: 하지 않았다.**
  - goldfish 디코더는 1088 crop 을 내지 않는다(전 실행 `coded=1920x1080`).
  - 공개 API 로는 crop 을 주입할 수 없다.
  - transform 주입(NDK `ANativeWindow_setBuffersTransform`)은 가능하지만 이번에 만들지 않았다.
  - 생산 shader 의 crop/flip 동작은 **AVD 미검증**이다. SM-S948N Codec2 실기와 같다고 부르지 않는다.
- **입력 속도**: 원본이 약 21 fps(WinForms 타이머)라 30/45/60 fps 성능 PASS 를 주장하지 않는다.

## S4 지연 (AVD, SwiftShader, 최종 APK, 구간 전체, 각 70 s)

| 수준 | n | p50 ms | p95 ms | p99 ms |
|---|---|---|---|---|
| 끄기 | 1442 | 58.9 | **73.3** | 81.0 |
| 0.20 | 1455 | 75.1 | **92.0** | 96.1 |
| 0.35 | 1423 | 75.4 | **91.7** | 94.8 |
| 끄기 | 1444 | 60.1 | **75.0** | 78.4 |

- unmatched 0, warm-up·stale 은 따로 셌다.
- **+16.7~18.7 ms → 4 ms 기준 미달(AVD)**. r1 의 +18.5 ms(5 s 창 p95 중앙값, r1 측정법)는 그대로 보존하고 위 값과 섞지 않는다.
- 폰 실측(같은 정의, 폰 절차 r2 갱신)으로만 판정한다.

## 미검증 · 사용자 행동
- 폰(SM-S948N) 전부: 4 ms·발열, 체감 선명도, 회전 뒤 터치, Codec2 1088 crop, 실 GPU GL 실패.
- 호스트 쪽 터치 수신.
- crop/flip fixture.
- 설치 전에 appId·서명자·기존 설치 영향과 S5(버전·keepalive-multi·A6)를 확정해야 한다.
- 검증용 독립 실행: 위 APK 해시로 핵심 AVD UI·실패 복귀·H1/H2 반례를 돌린다(제 실행이 대신하지 않음).
- 사용자가 지금 할 일: 없음.
