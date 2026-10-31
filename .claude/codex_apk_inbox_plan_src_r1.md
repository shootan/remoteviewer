# APK inbox r1 — 설계 결정 / 계측·A/B plan SOURCE

2026-10-30. **대칭 trace-off A/B 방안과 plan-aware a-only fixture 방향은 수용한다. C 정책은 아래 조건으로 구현 가능하다. 계측8868fb5와 r10 측정은 보정 후 고정 후보로 연결하며, 현재 그대로 새 기기 A/B를 시작하지 않는다.** APK0.2.33 출하 보류와 rel-0164 별도 진행을 유지한다.

## 고정

- 계측 HEAD `8868fb50bcd1489fa276b357e4393b05c1251f66`, base ac587dd. tracked clean(`apk_out/` 미추적), diff --check 통과.
- 설계 SHA-256 `b5eb2b6886637d67e96ab02b592493944bc7ba856fc2beff81859694bb69eaec`.
- A/B plan `ba9ce4d1dba51500fa2ac7b6faeb6f21829ae3251081505712b988ba8c6c93e2`.
- r10 snapshot SUMS `7544f4b80d7bbc9cefcad3f6f8686d16dadb20654db5046eefc00da2be81d9fe`, diff `a6706b71c7d2c16a518d7b9d821bce95271516526f4377d81af6400983b58c4b`.
- 보고 `report_decoder_liveness_r1.md` r1 `41b4e366bcd1accb20feb7eec1fb4ec6546bf341e342fecbc45854b590195dfd`, summary `7c9880095f7ccdd6954f68ac90492cf843799c0876fdcade4f01bd54efc0753d`.

## 결정① — trace 없는 A/B를 먼저

**검증용 의견에 동의한다.** A=`9f9a91c / 8c6422fe…`, B=`ac587dd plain / 32bb788f…`를 앱 trace 없이 비교한다. A에 진단을 backport하지 않고 B에도8868의 계측 APK를 넣지 않는다. 두 APK의 build/property/실제 bytes·ABI·SH-off/effective 경로를 고정한다. ApkOn은 미사용 artifact임을 기록하며 실수로 설치하지 않는다.

앱 내부 원인 진단은 **별도 B+trace run**으로 한다. 그 경우 제품 code가 ac587dd와 계측만 다른지, 항상 계수되는 추가 진단 비용도 있는지 명시한다. C prototype을 사용했다면 B+trace라고 부르지 않는다. trace run의 CPU/latency 수치를 trace-off B의 수치에 섞지 않는다.

같은 a-only fixture·server/Host·target·Settings OFF·120초 관찰·기존 슬롯/adb 조건은 유지한다. 순서/외부 부하가 해석에 영향을 주는 경우에만 역순을 추가한다. 동일한 normal run마다 승인 왕복을 만들지는 않되, 아래 measurement source 보정을 먼저 고정한다.

## 결정② — earliest usable IDR와 retry

**상한 안의 suffix를 가진 가장 이른 usable IDR 선택은 최초 C prototype으로 수용한다.** 가장 적게 버리는 선택이지만 원격 화면 지연까지 최적인지는 별개다. 실제 queue/content age·복귀 지연을 측정하고 오래된 chain을 계속 유지해 latency를 숨기지 않는다. 필요하면 같은 예산 안의 더 최신 IDR 선택을 비교할 수 있다. cap만 확대하지 않는다.

usable은 binding/gate epoch/stream이 맞고, **AU 순서에서 손실 뒤의 참조 시작점**이며 suffix도 유효해야 한다. 단순 “gap 뒤에 enqueue됨”은 늦게 도착한 옛 AU까지 허용할 수 있다. upstream이 순서를 보장하는 범위와 중복/재정렬·seq wrap 정책을 source로 확인하고, 전달 시각만으로 판단하지 않는다.

**tail gap과 recovery episode를 분리한다.** IDR enqueue는 뒤따르는 delta를 받을 수 있게 ingress gap을 닫는 상태 전환이다. 실제 codec queue/CommitQueue 전에는 회복 episode가 끝난 것이 아니다. 회복 후보 IDR이 queue/in-flight에서 다시 폐기되면 **같은 미해결 episode를 재개**하고 기존 retry budget/backoff를 유지한다. IDR 도착/재폐기마다 episode ID·최초 요청·예산을 새로 만들면 기존 증폭 고리가 재생된다. 새 pick/Bind/reset은 별도의 명시적 무효화다.

retry 제안은 다음처럼 고정해 수용한다.

- 최초1회 + 추가 retry3회 = **최대4개의 새 논리 key request**(UDP 재전송 packet 수가 아님).
- 추가 지연은 순서대로1/2/4초, 즉 전달 교환이 시작된 이전 시도 기준의 backoff다. 이상적인 송신 시각은 t0, t0+1, t0+3, t0+7이며 “절대시각1/2/4초”와 혼용하지 않는다.
- limiter 거부·이미 pending인 동일 요청의 coalesce를 실제 새 송신으로 세지 않는다. 거부 때 tight retry loop를 만들지 않고, 같은 IDR 도착/폐기로 retry 횟수를 리셋하지 않는다.
- usable IDR을 실제로 보존 중이면 중복 요구를 보류한다. candidate가 사라졌을 때 이전 예산/기한으로 재개한다. codec queue 성공에만 회복을 확정한다.
- 기존 selection owed8회와 같은 현재 IDR 의무를 중복 처리해 **4+8개의 별도 요구를 무조건 얹지 않는다.** 기존 owed 보장은 유지하면서 같은 유효 의무를 같은 논리 교환으로 합치고, 서로 다른 원인은 지우지 않는다.4회는 recovery 자체의 예산이며 모든 source의 합계를 증명하는 숫자가 아니다.
- 마지막 시도 뒤 응답/queue 대기와 exhausted/영상 실패·명시 재시도 경로를 C source에 유한 상태로 명시한다. budget 소진 뒤 무한 black을 정상 준비로 표시하거나 자동 reconnect/takeover로 숨기지 않는다. 실제 codec를 강제로 중단/해제하지 않는다.

설계에는 episode ID의 생성/유지/무효화, 후보 IDR 소유, retry/마지막 대기, 회복/실패의 표를 한 번에 반영한다. N1 reservation·KeyDelivered·생성21회 예산·2초 detach는 유지한다. 기준선 inbox_test의 예상 FAIL은 진단 기준선이지 새 전체 회귀 PASS가 아니며, C는 바뀐 계약에 맞는 행동 대조와 이전 source 음성으로 검증한다.

## 결정③ / D8 — 취소 hook 필요, 단일 current ID 조회만으로는 부족

**hook을 사용한다. TTL만으로 대체하지 않는다.** 생성된 요청의 정확한 binding/selection·recovery 원인/token을 held에 같이 보관하고 current 여부를 확인해야 한다. “그 순간 열려 있는 episode id만 조회”해서 옛 요청을 새 episode에 붙이거나,0이면 다른 원인의 유효 요청까지 버리는 구현은 안 된다.

검사는 held→limiter에 다시 넣는 곳뿐 아니라 **이미 accepted된 pending/action을 실제 control 송신 교환으로 시작하는 경계**까지 포함한다. 회복/선택 취소 뒤에도 일반 keyframe pending/action이 남을 수 있다. 이미 시작한 교환은 completion 규칙으로 분리하고, 무효 old token이 새 시도/재시도를 만들지 않게 한다. sink state lock을 network I/O동안 잡지는 않는다.

inbox recovery 외의 codec_await_key/codec_up/selection_abort와 기존 owed·pipeline 요구를 구분한다. inbox episode가 없다는 이유만으로 기존 decoderRekeyPending 전체를 false로 만들지 않는다. 최소 API여도 descriptor/token과 유효성 조회의 의미는 모두 충족해야 한다. default 구현/다른 sink 소비 target의 호환 compile 및 park/owner/session 무효화 대조를 포함한다. wire 변경은 필요 없다.

## 계측 SOURCE — age의 실제 오귀속 보수 필요

8868의 다섯 Request 경로 origin 계수, timeout headResend·fragRetx·rxPend 분리는 방향을 수용한다. 원인별 keyWant는 off에서도 항상 증가하므로 “모든 계측 비용0/전부 compile-out”은 아니다. 일반 flag 설정 위치와 전송 정책은 그대로이며 이 진단을 PC0.2.164에 합치지 않는다.

**N1 / enqueue→queue age:** `ltCurEnqueueUs_`가 frame이 아니라 최근 ProcessFrame의 임시 상태다. 이전 IDR이 pending slot에 남은 동안 새 delta가 들어오면 timestamp가 덮인다. 나중에 옛 held IDR이 queue될 때 새 delta의 시각으로 age를 계산한다.

Codex가 고정8868 sink/fake NDK를 trace on으로 직접 대조했다. input buffer를 막아 IDR을 보류하고250ms 뒤 delta를 보내고30ms 뒤 buffer를 열었다.

```text
held=1 queued=1 actual IDR age at least 280ms, observed elapsed=286ms
ageQueue max=33ms n=1
```

실제 queue된 IDR의 약286ms 대기를33ms로 과소 기록했다. `.claude/codex_decoder_liveness_review/inbox_age_probe.py`, `inbox_8868_age/{candidate_probe.cpp,build.log,candidate_probe.log}`. build/probe rc0은 대역 진행 완료이며 metric 정확성 PASS가 아니다. 로그 SHA-256 `6ddb31d69be22d32d3c0d190aeeaee77176109bfc904a9369eef07edd22ee255`.

age는 실제 AU의 identity/timestamp를 pending/replay까지 보존하거나, direct-only라고 정확히 제한해 누락/표본 수를 별도 보고한다. 다른 frame의 timestamp를 대신 쓰면 안 된다. 이 진단을 prototype 성능의 근거로 사용하기 전에 보수한다.

추가 label 보정: inbox high-water는 trim 전 기록이라9 frame/4MiB 초과가 보일 수 있다. post-admission 상한 위반과 일시 입력 포함 peak를 구분한다. rxLockHold는 LockedState 해제 뒤의 LtHold destructor 시각까지라 unlock 뒤 스케줄 지연을 포함할 수 있다. 실제 hold를 재려면 경계를 맞추고, 현재 구조를 유지하면 scope elapsed 상한으로 이름 붙인다. create 태그는 create/configure/start 복합 구간이지 개별 API 세 구간의 분리 측정이 아니다. 부정확한 이름만으로 병목 원인을 확정하지 않는다.

## r10 plan/측정 SOURCE 보정

a-only hostSet/readiness와 full3 분리, final OFF/end/EOF/job 유지 및 제시한22건 orchestration 방향은 수용한다. **다음 A/B에는 아래 측정 보정이 필요하다.** 원인 trace의 N1과 trace-off A/B의 보정을 분리해 병렬 준비할 수 있다.

1. **qemu identity:** `Get-Process ... | Select-Object -First 1`은 approved emulator-5554를 보장하지 않는다. serial/AVD/port와 process pid/start time을 연결해 고정한다. 일치/유일성을 확인 못 하면 qemu CPU는 unknown으로 남기고 비교 지표에서 제외한다. 다른 emulator를 자기 arm의 부하로 보고하지 않는다.
2. **watch 경계/표본 비용:** 현재 sample은 여러 동기 adb/PowerShell 호출을 수행한다. 각 호출/한 sample의 deadline과 경과·rc/누락을 기록해120초 watch가 긴 sample에 끌려가지 않게 한다. duration은 monotonic, 상호 로그 상관은 epoch/offset으로 구분한다. 현재 watch begin은 첫 영상 관측 뒤 paths/env 조회 후이므로, 실제 first-present→측정 시작 지연을 기록하거나 경계를 고친다. early end가 보이면 데이터 보존/cleanup으로 끝내며 이미 HOSTS인 앱에서 메뉴 탐색을 반복하지 않는다. 필요한 로그를 못 얻었으면 무종료 PASS로 추정하지 않는다.
3. **traffic metrics:** 현재 relay trace bucket은 `...kind`별 packet 개수만 더하며 byte 합은 없다. close의 전체 flow bytes는 startup/watch/list를 나눈 byte rate가 아니다. local trace에 방향/종류별 msg.length 누적 또는 bucket byte 계수를 더하고 알려진 길이의 순수 대조를 두거나, 지원 못 하는 구간 bytes/s를 미측정으로 명시한다. rx/sent를 중복 합산하지 않고 UDP payload bytes와 encoded AU bytes를 구분한다. sampled wire frame만으로 전체 video bytes/IDR 비중을 정확히 산출하지 않는다.
4. **CPU parser 확인:** 리뷰 중 top 고정열이 %MEM을 읽을 우려를 제기했지만, 실제 r10 sample 함수를 fake 입력으로 실행하니 대표 header에서 **%CPU37.5/%MEM2.4 중37.5를 올바르게 읽었다.** 이 오인 지적은 철회하고 반려 사유로 쓰지 않는다. 해당 형식/실제 header를 남기고 알 수 없는 형식은 unknown으로 처리하면 된다. qemu cumulative CPU는 ΔCPU/Δwall·CPU수의 정의를 함께 쓴다.
5. **집계 정의:** Host unique key seq와 IDR accepted AU를 구간별로 대조한다. 창별 gap p95를 평균내 전체 구간 p95라고 부르지 않는다. 사용할 원자료가 없는 값은 sample/추정/미측정으로 정확히 표시한다. 없던 계측값을 수치0으로 채우지 않는다.

이 측정 보정은 local test 계측/plan 범위다. 운영 서버 배포·wire/timeout 정책·사용자 설정 변경은 승인하지 않는다. 수정된 작은 source/snapshot과 숫자 파싱·시간/identity·byte 계수의 기기 없는 대조를 한 패킷으로 연결하면 A/B 실행을 잇는다. 현재 r10 그대로 기기 run부터 시작하지 않는다. unsupported 지표를 명시적으로 제외하는 좁은 plan도 가능하며 필요 없는 대형 계측을 강제하지 않는다.

## 다음 상태

검증용은 **① trace-off A/B 수용,② earliest IDR/동일 episode·retry 정의,③ token 취소 hook**을 기존 작업용1 정본에 합친다. 정책 구현 C를 준비할 수 있지만 fixed SOURCE/EVIDENCE를 통과하기 전 게시하지 않는다.8868 age 보정과 r10 측정 보정, 기대 FAIL 기준선은 각각의 상태로 기록한다. matched A/B 전후의 새 source/fixture 조건과 과거 r26을 혼용하지 않는다.

Codex는 source·보존 자료 읽기, 실제 제품 sink/fake NDK의 age 대조와 sample 함수의 순수 입력 대조만 실행했다. 실제 Host/AVD/adb·GPU/OS 입력·설치·운영·삭제·제품 수정·commit/push는 없다. 미추적 검토 artifact만 작성했으므로 제품 commit은 만들지 않았다. 사용자 현재 조치 없음.
