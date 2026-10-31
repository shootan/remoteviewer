# SH r2 — Codex SOURCE/EVIDENCE 검토

2026-10-22 / task **t-b7ss11ov** / 후보 **7609d0b015b46cb7a2be613040c21dc969920deb**.

**NEEDS_CHANGES.** H1의 surface별 BLOCKED·늦은 해제, H2의 이벤트 기반 출력 gate, H3의 timestamp/redraw·구간 전체 nearest-rank, H4의 APK seam 검사, H5.3의 frame별 source 크기와 회전 뒤 buffer 크기 재설정은 보완됐다. 다만 **R1 release task 차단 우회 / R2 handoff metadata 공개 경합 / R3 남은 자동·독립 검증**을 닫아야 한다. 아직 “폰 실기만 대기”나 통합/출하 OK가 아니다. 기존 task에 한 묶음으로 연결한다.

## 직접 확인과 근거 층

- 작업용 보고 `.claude/report_sh_filter_impl_r2.md`, r1 판정, 2e9428b→7609d0b의 변경 목록·관련 제품/시험·Gradle/gate 코드를 대조했다. worktree HEAD와 tracked clean, CR-at-EOL 허용 diff-check를 확인했다.
- 최종 APK hash를 직접 계산했다: noseam **a77520a0bd6c0e7cb0a832055836374926492da83e21d659d9a2c58e22a1ce96**, shseam **b9a1deb863b43545ab4f6c5295462d3224ce86b458b4912b12a9d0191e346c6d**. 같은 APK에 gate를 직접 실행해 noseam **rc0 ALL PASS6**, shseam **rc1**(`sh-test-seam`, `shTestFault`, `shTestStrength`)을 확인했다. r1에서 직접 재현한 APK gate 공백은 해결됐다.
- `C:/work/gnlink-wt/sh-avd/final2/{compare,lifecycle,resize}.log`, `final3/{latency,identity,faults,viewer_never,hold,touch}.log` 및 summary를 직접 읽었다. 보고된 성공 개수와 H1 BLOCKED→늦은 해제→1회 재시작, VIEWER neverDraw 복귀, 정확한 Toast 대조, source/output 크기 변경 로그를 확인했다. 무효 final2 summary2를 성공 근거에 섞지 않는다. 단위248/0과 해당 실행 종료코드는 작업용 보고 귀속이다.
- 최신 off/weak 화면 PNG를 직접 열어 외곽과 글씨 패턴을 확인했다. **Codex/검증용의 AVD UI 독립 재실행은 아직 없다.** worker 실행 로그와 이미지 열람을 독립 실행으로 부르지 않는다.
- 아래 Gradle 검사는 Codex가 **실제 task graph dry-run만** 수행했다. 실제 compile/package/sign/install은 실행하지 않았다. JDK/SDK는 기존 PC 경로를 프로세스 환경에만 지정했다. 소스·설치 제품·실서비스·AVD·폰을 조작하지 않았다.

## R1 — Gradle이 입력 task 문자열만 검사하여 release 축약 실행을 허용 (필수)

`app/build.gradle.kts:99~109`는 `gradle.startParameter.taskNames`에 `release`가 있는지 검사한다. **Gradle이 해석한 실제 task는 검사하지 않는다.** 직접 같은 후보에서 실행한 결과:

```powershell
.\gradlew.bat --offline --no-daemon '-Pgnlink.shTestSeam=true' ':app:assembleRelease' --dry-run
# rc1: refusing [:app:assembleRelease] (정상 대조)
.\gradlew.bat --offline --no-daemon '-Pgnlink.shTestSeam=true' ':app:assembleRel' --dry-run
# rc0: BUILD SUCCESSFUL
# preReleaseBuild, compileReleaseKotlin, mergeDexRelease, packageRelease, assembleRelease 모두 graph에 포함(SKIPPED)
```

두 번째 요청은 seam source 선택이 켜진 상태에서 release graph를 통과한다. APK gate가 최종 seam을 거르는 것은 확인했지만, **release build 자체를 거부한다는 H4 계약은 아직 충족하지 않는다.** 현재 noseam APK가 오염됐다는 판정이나 실제 release APK를 만들었다는 주장은 아니다.

**보수:** 해석된 release variant/task graph에서 seam 사용을 차단하거나 release source를 항상 noseam으로 고정하는 구조로 한다. 입력 문자열의 축약형을 일일이 blacklist하는 방식은 피한다. 정상 debug+seam은 허용하고, 완전 task명·축약명·release를 의존하는 실행에서도 동일하게 거부되는지 dry-run/구성 시험으로 확인한다. 수정 후 실제 APK noseam/shseam gate 대조를 유지한다. LK/DW 표식 합집합은 통합 gate에서 보존한다.

## R2 — 첫 buffer는 공개됐는데 metadata는 아직 없는 경합 (필수)

`android_video_decoder.cpp:934~959`는 먼저 `AMediaCodec_releaseOutputBufferAtTime`/`releaseOutputBuffer`로 buffer를 공개한 뒤 handoff ring의 key/ns/stream/codec/size를 쓴다. 이 ring은 이제 단순 진단이 아니라 **첫 출력 gate와 source tap 크기의 입력**이다.

가능한 순서(소스에서 확인한 경합이며 현재 AVD에서 발생을 실측했다는 뜻은 아니다):

1. native가 buffer release 성공 직후 스케줄 아웃되고 ring 공개는 지연됨.
2. renderer가 첫 buffer를 latch → `nativeHandoffInfo`가 null → 출력 크기로 step을 대신함.
3. TextureView의 첫 latch도 null → `SharpenOutputGate` 미확인. `LatchClassifier`에는 timestamp가 기록됨.
4. ring 공개가 뒤늦게 끝나도 같은 timestamp의 다음 callback은 redraw로 early return하고 gate를 다시 확인하지 않음. 정지 원본이면 다음 frame 없이3초 뒤 불필요한 fallback.

이는 H2의 “유효 첫 frame 하나만 있어도 인정, 두 번째 frame을 요구하지 않음”에 남은 반례다. 기존 pure `SharpenFramesTest`는 이미 완성된 FrameInfo를 주므로 이 publication 순서를 다루지 않는다.

**보수:** 소비 가능한 buffer보다 대응 metadata가 먼저 보이게 공개 순서를 보장하거나, 첫 latch의 식별을 보존해 metadata가 늦을 때 bounded하게 같은 latch를 재평가한다. 실패한 release를 성공 frame으로 인정하지 않는다. handoff→latch 시간 정의도 실제 공개 시점과 일관되게 유지한다. “unknown이면 현재 source/output 크기로 추측해 gate 성공”으로 바꾸지 않는다.

**반례/대조:** 실제 제품 handoff/renderer/gate 경계에서 release와 metadata 사이를 지연 → 첫 buffer만 제공 → 올바른 크기/identity로 첫 출력 확인 또는 명시적 안전 실패, 정상 frame인데 두 번째를 기다리는 timeout0. 지연 없는 정상·release 실패·옛 codec/selection/surface도 유지한다. 시험 seam을 쓰면 출하 제외/양성 검출을 확인한다. 보수 영향에 맞게 해당 gate/크기 전환 시험만 재실행한다.

## R3 — 남은 자동 검증·독립성 및 기록 정정 (필수 보완)

1. **검증용 독립 AVD 실행:** 핵심 off/0.20/0.35 선택/다시 off, 정상 화면·정확한 fallback 후 새 원본 frame, H1 release hold 중 producer0와 해제 후 복귀, H2 VIEWER neverDraw·첫 frame/새 selection 경계, resize를 고정 APK hash로 실행한다. worker의 합계/이미지와 gate 직접 실행만으로 UI 독립성은 충족하지 않는다. DW/LK 슬롯을 침범하지 않고 가능한 순서로 연결하며 무관한 모든 시나리오 전량 반복은 요구하지 않는다.
2. **생산 shader crop/flip/padding fixture:** goldfish가 실제1088 crop을 내지 않는 것은 인정한다. 하지만 r1에서 요구한 별도 생산 shader/renderer fixture 자체는 구현하지 않았다고 보고했다. 이 결손을 decoder 한계로 닫지 않는다. coded padding sentinel·nonzero crop·flip/quarter-turn transform·가장자리 tap을 실제 shader로 검증한다. 자체 수식 CPU 복제만으로 대체하지 않는다. 공급 경계 주입을 사용해도 되고 SM-S948N Codec2 실기와는 구분한다. 자동 불가능 판정은 실제 시도/구체 제약이 있을 때 해당 부분만 한다.
3. **터치 native 큐→Host 수신:** 현재 `touch`16/0은 앱이 native 큐에 넣은 좌표까지만 본다. Host 입력 주입 off는 OS 조작을 피한 시험 구성이고 **수신 관측이 불가능하다는 근거는 아니다.** 격리 Host 수신 fixture/owned target에서 같은 원격 좌표를 관측해 off/.20/.35·크게보기/이동 경로를 잇는다. 실제 사용자 바탕화면에 주입할 필요는 없다. 가능한 orientation과 기종별 실기 제한은 분리한다.
4. **측정/보고 정정:** 최종 `final3/identity.log`는 mean_abs **0.179**, p99_abs **3**, max_abs **223**이다. 보고 본문의 “움직이는 띠 밖 평균0.10·최대10”은 이 최종 측정과 다르다. 현 하네스 `scenario_identity`는 전체 picture를 비교하며 움직이는 띠를 제외하지 않는다. 최종 값/영역/움직임 영향을 정확히 기록하고, 정적 영역의 색·미세 이동·외곽을 따로 비교한다면 그 실제 자료로 적는다. 기존 mean<3 PASS를 최대 오차10/완전 동일성 보장으로 바꾸지 않는다. r1 과거 수치는 과거 측정으로 남길 수 있다.

## 보완 확인 / 남은 S4·폰·범위

| 항목 | 이번 판정 |
|---|---|
| H1 | SOURCE 방향 OK: timeout→BLOCKED, pending release별 surface, detached=false 차단. worker hold11/0 근거 확인. 독립 실제 EGL 수명 검증은 R3 |
| H2 | 이벤트 첫 출력·codec/stream/surface 검사·VIEWER timeout 보완 확인. **R2 publication 경합 잔여**, 독립 경계 반례 필요 |
| H3 | timestamp redraw 분리, 수준/path/gen/surface별 구간, nearest-rank와 cap/overflow 표기 보완 확인. ring 공개는 R2, 독립 통계 관측은 R3 |
| H4 | APK gate 직접 정상/부정 확인 **OK**, Gradle은 **R1 보수 필요** |
| H5.3 | frame visible size와 quarter-turn step, 회전 sizeChanged 캐시 무효화 보완 확인. resize8/0은 worker 근거, metadata unknown 경합 R2·transform fixture R3 |

최종 whole-segment p95: off73.3/75.0ms, .20=92.0ms, .35=91.7ms, **AVD SwiftShader +16.7~18.7ms로 +4ms 기준 미달 유지**. latency 하네스2/0은 자료 수집/표본수 검사 통과이고 S4 성능 통과가 아니다. 약21fps 입력이므로30/45/60fps 성능을 입증하지 않는다. r1의5초창 p95중앙값+18.5ms와 혼합하지 않는다. 구간 raw 표본은 앱 내 bounded 보관, 저장 JSON은 구간 요약임도 구분한다.

SM-S948N 실 GPU+4ms·발열/전력·실 Codec2 crop·회전 터치·시각적 선호는 계속 미검증이다. 이 폰 결과로 AVD 미달을 지우거나 AVD로 모든 폰 실패를 판정하지 않는다. S5(버전/keepalive-multi/A6) 미결·기존 APK0.2.30/29·서명/게시/사용자 폰 설치/push 보류 유지. 수동 IP10초 정지는 별도 ledger이며 이번 Host 개편 승인 아님; 일반 연결 행동/영향 분리도 별도로 남긴다.

검증용은 **R1/R2/R3를 한 유효 보수 지시**로 기존 t-b7ss11ov/작업용2에 연결하고 영향 있는 자동 검증을 독립 실행한다. 제품 수정이 필요하면 관련 history/계획 상태/Git MCP commit은 같은 기능 task에 취합한다. Codex 변경은 미추적 판정서/인계뿐이며 실제 제품 빌드/설치·서비스/폰/AVD 조작·commit·push 없음(task graph dry-run/gate 실행만 수행).
