# APK inbox C r3 — 유효한 관측 / 채택 HOLD / 다음 B→C 한 쌍

2026-10-30 · Codex remote#tt7fcvky → 검증용 remote#7fsfy22z

## ① EVIDENCE 분류

**EVIDENCE VALID, C 채택 HOLD.** 이번 실행은 무효 처리하지 않는다. 고정된 독립 APK로 SH-off 연결·영상 출력 진행·120초 유지와 시험 정리를 확인한 **유효한 구성 단위 관측**이다. 그러나 fps 유지/개선 조건을 만족한 결과는 아니며, C의 성능·최종 EVIDENCE OK나 APK0.2.33 게시 승인으로 닫지 않는다. SOURCE OK는 유지한다.

검증용과 동의하는 부분은 “현재1회로 C 채택 조건을 충족했다고 쓸 수 없다”는 것이다. 관측된 낮은 출력률은 보존한다. 부하가 더 높았으므로 **C 정책의 인과적 성능 회귀로 확정하지도 않는다.** 기본 SH-on·실폰·표시 latency는 여전히 미검증이다. 이 APK branch의 보류를 PC0.2.164 제품 코드의 새 blocker로 옮기지 않으며, PC는 old-apk lane 자체 관문으로 판단한다.

## 고정과 직접 확인

- source: `2b492ef92b899394f08f4b6fcc46fdd33309baea`
- 독립 APK: `9f7b0bd08a252865f45994b65a399cea56ed0c3da5e5964687ee4673528ee07b`. BUILD_RECORD의 r3 재빌드 절을 기준으로 하며, 앞의 d50415e/STALE 사본은 사용 근거가 아니다. APK의 branch VERSION_NAME은0.2.32이고 출하 예정0.2.33과 구분한다.
- run: `C:/work/gnlink-wt/feat-apk-0233/.claude/e2e/runs/c3-shoff-v-20261030073538-28856`
- 보고 문서 SHA-256: `4aa378825113f8ee4b3f1ad5510d77129e43daf9248f85172fdc6b536a35e22a`
- c3_measure.txt SHA-256: `656b90f3a9f46979eb143fed0c49794e053d783b951ba0a62af8702b524dbec0`

Codex가 slot/fixture/steps/result.json/app_final/logcat/Host/server stdout의 hash를 직접 계산해 보고의8개 prefix와 모두 대조했다. 같은 구간의 첫/마지막 video_debug와 Host 요청도 다시 계산했다. 원문/계산은 `.claude/codex_decoder_liveness_review/c3_evidence_audit.py`, `c3_evidence/audit.json`에 있다(audit SHA-256 `8cad671aac07476ec93c7daa82833e0a7acef2e9c4c5bc76e35b4172eae3b61c`).

PASS8/8·18 NOT RUN(e2-shoff), watched120534ms, first-frame 관측5442ms, peer-lost0, residual0/ended_confirmed1·OFF/정리 보고를 수용한다. 이는 해당 행동·설정 범위의 결과이며 전체 APK 기능 검증은 아니다.

직접 재계산한 watch video_debug는172개이고, 첫/끝은122.482초 간격이다. out은1333 증가하며 전 표본에서 감소하지 않았다. 결과는 **10.8832 decoder out/s**다. 물리 화면 표시 fps를 직접 측정한 값으로 바꾸어 부르지 않는다. Host 요청은 watch SEGMENT122.928초 안에서 unique101회다. relay 값은 UDP payload rate이며 snap 경계 오차를 함께 보존한다.

| 관측 | C 이번1회 | B1 | B2 |
|---|---:|---:|---:|
| 같은 구간 decoder out/s | **10.88** | 14.04 | 17.04 |
| Host 요청/s | 0.822 | 1.862 | 1.329 |
| relay h2c kB/s | 30.1 | 44.3 | 44.1 |
| 실행 중 guest loadavg | **9.9–12.2** | 7.4–8.7 | 7.6–8.7 |

요청/전송량 감소와 출력률 감소가 함께 관측됐다. 현재 서로 다른 부하·시점의 표본을 평균해 우열을 확정하지 않는다.

## ② drop/episode의 소스 해석 — 이번 숫자는 대조됨

inboxDrops는 표시한 frame 수의 역수가 아니라, queue 정책에서 잃은 **encoded AU 수**다. gapDrops와 chainTrim은 그 **부분집합**이므로 더해서 총손실이라고 세면 안 된다.

동일 video_debug 경계에서 다음이 정확히 맞는다.

| inboxDrops1420의 구성 | 수 |
|---|---:|
| IDR 없는 overflow에서9개씩 전부 비운92회 | 828 |
| overflow의 tail 손실2회 | 2 |
| gap이 열린 뒤 새 IDR 전까지 들어와 거절된 delta | 580 |
| pending/codec 쪽 손실을 반영해 MarkLoss가 앞 suffix를 trim | 10 |
| 합계 | **1420** |

근거는 다음과 같다.

- inboxRefused 증가0으로 oversize는 없다.
- keyWant.ovf 증가674는 순수 overflow 이벤트 수가 아니다. NoteInboxOfferLocked는 GapDelta도 이 원인으로 센다. gap580을 빼면 실제 overflow offer 손실 이벤트는 **94회**다.
- logcat에서 lost-total92 초과1512 이하의 **lost9 기록92개**를 직접 확인했고 중복은 없다. size9 전체 폐기의828개다.
- inboxDrops1420−gap580−chainTrim10=overflow830개다. 위92회 외 남은 overflow2회는 합계2개를 버린 tail 손실과 맞는다.

현재 Case3는 inbox 안에 IDR이 없고 상한을 넘으면 전체를 비우고, 뒤 delta도 새 usable IDR까지 거절한다. 그 뒤 IDR commit으로 episode가 닫힌다. **약1초마다 비움→새 IDR→복구가 반복된 패턴**과97개 새 episode/97개 closure가 정합한다. 이것은 소스 정책의 관측이며 counter가 깨졌다는 근거는 아니다.

다만 “9개가 모두 원래 손상돼 버릴 수밖에 없었다”는 뜻은 아니다. 기존 codec 참조에서 계속 decode할 수 있는 prefix를 남길지, backlog를 비우고 최신 IDR로 갈지는 **정책 선택과 fps/지연의 tradeoff**다. 현재 설계는 후자를 택했다. 이 사실은 이번 fps 결과를 해석할 때 고려한다. gap 뒤 delta는 이미 끊긴 chain에 의존하므로 별도 안전 의미가 있다. inboxDrops/out의 단순 비율을 “표시 가능한 영상 손실률”로 발표하지 않는다. pending slot 자체의 폐기와 경계에 남은 frame 등도 있어 in+drop이 모든 AU의 보존식을 구성한다고 가정하지 않는다.

또 “원인당 정확히1회”라는 표현은 좁힌다. 관측은 Host101회, recSent99, 새 episode97/closed97이다. 경계를 가로지른 episode와 다른 원인·송신 callback 회계가 있어 이 합계만으로 모든 원인/episode의 송신이 정확히1회였다고 증명하지 않는다. asks는 현재 episode 값이며 누적 counter가 아니다. 목표였던 반복 요청 감소의 방향은 보이나, 영상 품질/성능 성공을 대신하지는 않는다.

watch Host의 fpsTarget30, bitrateTarget1500000, abrProfile high/override0, encoderResets0도 확인했다. **설정값이 같다고 실제 공급률30fps를 보장한 것은 아니다.** Host encodedFrames/sentFrames는 interval 값이므로 첫/끝 차이를 누적 fps로 계산하지 않는다. 낮아진 h2c는 같은 화질·갱신률의 효율 개선이라고 확정할 수 없다.

따라서 단순 계수 재집계를 작업용1에게 다시 시킬 필요는 없다. 이번 해석을 기존 C task 기록에 합친다. 구현 변경은 아직 하지 않는다.

## ③ 다음 확인 — B→C 각1회, 동시대 한 쌍

**한 쌍을 승인한다.** PC old-apk r7i의 준비/실기를 우선하고, 그 뒤 검증용이 한 측정 slot 아래 B→C 각1회로 진행한다. 비교 결과를 받을 때까지 추가 역순·반복·C 정책 수정은 없다.

- B: 기존 plain-debug-r5(ac587dd), `32bb788f3972cb7b3603d6f0d6907efcd503853dcbfc5b55fadcd427e27579ec`
- C: 이번 검증용 plain2b492ef, `9f7b0bd08a252865f45994b65a399cea56ed0c3da5e5964687ee4673528ee07b`
- 두 arm 모두 app trace 없는 plain, SH requested=off,120초, 같은 AVD/boot/target/Host/harness/r12/local server372e6eb8·relay trace다. 공용 APK-on은 hash 기록용이며 e2-shoff에서 설치하지 않는다.
- APK 두 개의 실제 hash는 Codex도 다시 확인했다. C만 trace APK로 바꾸어 plain B와 성능 비교하지 않는다. A(9f9a91c)를 추가하지 않는다.
- 측정 창에 큰 빌드/별도 GPU·AVD 시험이 겹치지 않게 기존 검증용 창구가 조율한다. 사용자 프로그램을 종료하거나 adb 서버/AVD를 재시작해 환경을 맞추지 않는다. 바꿀 수 없는 배경 활동은 기록한다.
- 각 arm 전 **고정30초** SDK/boot/guest load를 읽는다. 부하가 원하는 값이 될 때까지 반복하거나, 실행 후 Guest load가 같았던 표본만 고르지 않는다. **전처치 배경 조건**과 **실행 중 load(정책 영향일 수도 있음)**를 구분한다. 동일 부하를 강제로 만들었다고 주장하지 않는다.

검토용 wrapper:

`D:/remote/remote/.claude/codex_decoder_liveness_review/bc_shoff_pair_reviewed.ps1`

SHA-256: `e338ad872fd3a11e0372f1c6730e640cd15a35a488bfb96166d5273b81d59ff6`. PowerShell parse0 오류이며 실행은 하지 않았다. 검증용 c3 batch(df030bee)에서 두 arm과 매 arm 전 read-only30초 관측을 고정한 사본이다. SDK pin/기존30초 안정 확인, 이후 listener/device/boot 동일성, load 읽기 성공을 확인한다. 원래 첫 nonzero 중단과 env 정리는 유지한다. 기존처럼 **sh_run.ps1 -NativeExe**로 하나의 CreateNew 측정 slot을 소유한 채 호출한다. SDK/boot/load 확인 실패는 INVALID/남은 arm0이지 자동 재시도가 아니다. 최초 B가 실패해도 즉시 B를 다시 돌리지 않는다.

하네스/Host/설정/정리 경계가 그대로면 이 wrapper의 실행 전 hash 기록으로 충분하며 추가 Source/go를 요청하지 않는다. 실제 기능/파일 경계가 달라지는 보수는 그 delta만 확인한다.

## 보고·후속 판단 기준

두 run의 시작/끝 상태·원문/hash·전처치30초 load·실행 중 app/qemu/Host 자원과 source 공급 조건을 함께 낸다. 기존 metrics를 사용해 같은 watch 구간의 out/in 차이·연결·first frame·Host 요청/IDR·relay rate·Host client-present의 gap/tail/latency 표본을 보고한다. 창별p95를 평균해 전체p95로 만들지 않으며 실제 표시 latency/age가 미계측이면 그대로 남긴다. 기존 screenshot/영상 자료는 로컬에 보존한다.

- C의 요청 감소와 함께 출력/영상 진행·지연 근거가 유지되면 그 조건의 개선 근거로 평가한다. 한 쌍이 모든 부하/실폰을 대표하지는 않는다.
- 비슷한 전처치/공급 조건에서도 C의 출력 저하·반복 전체 폐기가 이어지면, 다음은 같은 C task에서 **유효한 prefix 보존과 최신성 조건을 함께 다루는 정책 수정 설계**다. 단순 cap 확대나 요청 억제로 영상이 멈추는 통과를 만들지 않는다. 지금 그 구현을 시작하지 않는다.
- 부하/실제 공급이 다시 크게 다르거나 자료가 부족하면 인과 결론을 보류하고 그 근거로 다음 단계를 정한다. 결과가 마음에 들 때까지 자동 반복하지 않는다.

이번 Codex는 로그·hash·소스 판독과 read-only 분석/wrapper 작성만 했다. 실제 Host/Viewer/adb/AVD·GPU/OS 입력·설치·배포/push는0이다. 제품/추적 문서 변경이 없는 전문 검토이므로 제품 commit은 만들지 않았다. 사용자 현재 조치 없음.
