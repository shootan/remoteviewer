# APK inbox B→C pair 판정 / prefix 정책 설계 GO

2026-10-30 · Codex remote#tt7fcvky → 검증용 remote#7fsfy22z

## 판정

**pair는 유효한 구성 단위 관측이다. C3 채택 HOLD를 유지하고, 기존 작업용1의 같은 C task를 prefix 정책 설계 단계로 잇는다.** 지금 추가 AVD 반복이나 출하 정책 변경은 하지 않는다.

보고 `RESULT_bc_pair_verify.md` SHA-256은 `bb62022958afbc87e69c29c43fa31580a3d18eaf473ab8ad4d81d8a4121c3dbc`다. Codex가 두 run의 slot/fixture/steps/result/app/Host/relay hash14개를 다시 계산해 보고의 prefix와 대조했다. 같은 구간 out과 Host interval/요청도 직접 재계산했다. 근거는 `.claude/codex_decoder_liveness_review/bc_pair_evidence/audit.json`에 보존했다.

| 값 | B32bb788f | C9f7b0bd0 |
|---|---:|---:|
| watch SEGMENT 초 | 123.851 | 123.371 |
| decoder 표본 첫/끝 간격 초 | 122.734 | 122.881 |
| out 증가 / out·s | 1979 / **16.1243** | 1798 / **14.6320** |
| Host sent interval 합 | 2735 | 2691 |
| Host unique key requests | 142 | 81 |

8/8·120초 유지·SH-off·같은 boot·peer-lost0·guard/정리 성공은 범위대로 수용한다. 요청률은 약43% 줄었지만 같은 구간 decoder 출력은 약9.3% 낮았다. 낮은 요청량/h2c만으로 채택 조건을 만족하지 않는다. out/s는 물리 화면 fps가 아니며, out/sent72/67%도 서로 조금 다른 표본 경계/Host interval 합을 쓴 근사값이다. 이를 정확한 전송 손실률로 부르지 않는다.

Host 공급은 약22/s로 비슷했다. 다만 B는 pre-arm 휴지 상태, C는 직전 arm의 여파가 가능한 높은 pre-arm load에서 시작했다. 실행 중 load는 정책이 처리·폐기한 작업량의 **결과일 수도 있다**. “C 실행 중 load가 낮았으니 C가 더 유리한 외부 조건이었고9%가 순수 정책 효과”라고 확정하지 않는다. N=1, 고정 순서, carryover와 age/latency 미계측의 한계는 유지한다.

그럼에도 같은 공급 계열에서 C의 출력 저하가 남고 아래 전체 폐기가 재현됐다. **설계 대안을 만드는 단계로 이동하기에는 충분한 근거**다. 인과를 완전히 증명할 때까지 같은 pair를 반복하지 않는다. C3 SOURCE OK는 유지하되 최종 성능/EVIDENCE OK와 APK0.2.33 출하는 아직 아니다.

## 계수와 현재 가설

C의887개는 gap187·trim5·overflow695로 대조된다. ovf 원인 증가266에는 gap187도 들어 있으므로 실제 overflow 손실 이벤트는79회다. Codex가 logcat의 해당 누적 counter 범위에서 **9개 전체 폐기77회/693AU**를 직접 확인했다. 남은2개는 tail 손실2회와 맞는다. 즉:

**887 = 693(77×9) + 2(tail) + 187(gap 거절) + 5(trim)**

B의 inboxDrops/gapDrops와는 정의가 달라 단순 합계 우열을 내지 않는다. 이 계수만으로 잃은181개 출력의 전부를 특정 경로에 귀속하지도 않는다. 현재 Case3가 inbox에 IDR이 없다는 이유로 전체를 비우는 선택은, 이미 유효한 codec 참조에서 이어갈 수 있는 prefix까지 잃게 할 수 있다는 **설계 가설**이다. 실제 prefix 유효성은 아래 조건으로 증명해야 한다.

B의 높은 out도 모든 frame의 참조·화면 정확성을 증명한 golden 값은 아니다. concealment나 끊긴 참조를 허용해 숫자만 B에 맞추면 안 된다. 유효 prefix를 보존할 수 없다는 결과가 나오면 그 가설 실패/정확성 비용을 설명하고, 목표를 몰래 완화하지 않는다.

## 위임 범위 — 설계 + 실행 가능한 소형 대역 원형

검증용이 기존 작업용1에게 **한 갱신 지시서**로 배정한다. 기존2b492ef 소스/APK/B·C 원문은 보존하고, 필요하면 전용 실험 branch/worktree를 만든다. PC 참조 작업과 같은 파일을 두 작업자가 동시에 편집하게 하지 않는다. 지금 새 제품 default·main·버전·서버·AVD 시험을 바꾸지 않는다.

다음 다섯 항목을 하나의 추천안으로 정리하고, 핵심 주장을 실제 정책 코드/제품 sink를 쓰는 stand-in으로 확인한다. 문서만 왕복하거나 구현 결과를 자기 승인하는 흐름으로 만들지 않는다.

### 1. 언제 prefix를 decode 가능한 것으로 인정하는가

초점은 **IDR 없는 inbox overflow**에서, 이미 codec이 가진 정상 참조에서 이어지는 젊은 prefix를 보존할 수 있는 조건이다. 같은 generation·seq 증가·과거 IDR 하나만으로 정상 참조라고 가정하지 않는다. 현재 codec/gate/selection/binding/reset epoch, 아직 commit되지 않은 in-flight/예약, pending slot, 이전 손실과 gap을 함께 다룬다. 증명이 없으면 현재의 안전한 IDR 대기 경로로 간다.

유효 prefix에서는 어느 쪽을 버릴지, 손실 지점·tail gap·뒤 delta 거절·다음 IDR을 어떻게 처리할지 event trace로 제시한다. 기존 usable IDR+suffix, 새 pick/새 surface 우선 처리와 N1 예약 검증은 유지한다. H264 참조가 끊긴 delta를 최신 화면이라고 그냥 보내거나 임의 seq 재정렬을 허용하지 않는다.

### 2. pending/codec backpressure와 worker 소비

prefix를 queue에 남겨도, pending slot에 buffer를 못 넣는 동안 worker가 후속 delta를 계속 Pop해 버리면 보존 효과가 없다. **buffer 없음/느린 codec 때의 dequeue·Pump·재개**를 정책에 포함한다. RX/control/UI는 codec을 기다리지 않으며, worker도 queue 비어 있지 않다는 이유로 hot loop를 돌면 안 된다. reset/새 surface/새 IDR 등 우선 사건은 유한 시간 안에 처리한다.

현재 정상 경로의 codec 소유 worker1개·짧은 state lock 구조를 유지한다. decoder lifecycle 전체 재작성·Host ABR·wire/transport·입력/보안/사용자 설정으로 범위를 넓히지 않는다.

### 3. 최신성/age 제한

보존이 높은 fps 대신 오래된 프레임을 몰아서 내보내는 최적화가 되면 안 된다. **단일 유한 age 예산의 추천값과 근거**, 시작/검사/만료 지점과 새 IDR 우선 정책을 설계에 넣는다. 전부 보존하거나 cap을 늘려 성공시키지 않는다(8 frame/4MiB와 현재 총 logical 메모리 경계 유지).

local enqueue→worker/codec admission의 residence time, remote capture 시간의 상대 span, 실제 표시 latency를 구분한다. host QPC와 Android clock을 직접 빼지 않는다. local age를 제한했다고 전체 end-to-end 표시 지연까지 보장했다고 하지 않는다. codec API가 이미 진행 중일 때 age가 만료되는 경계도 적는다. 아직 기준선 age가 없는 만큼 관측하지 않은 수치에 맞춘 성능 주장을 하지 않는다.

### 4. episode·원인·회계 유지

유효 prefix 보존은 IDR commit도 gate 지불도 아니다. 같은 recovery의 손실/실제 IDR commit/후보 재폐기·finite exhaustion을 유지한다. prefix 때문에 요청 eligibility를 잠시 유예한다면 **그 변경과 유한 기한**을 명시한다. 예산/limiter를 몰래 늘리거나, overflow/age 만료만으로 새 episode·owed 예산·codec 재생성을 반복하지 않는다.

N2 발급순서 Merge, held/accepted/취소, 부분 원인 유효성, §8(a)의8회 상한/covered-exhausted/늦은 IDR은 유지한다. 요청 억제로 영상이 멈춰 통과하는 안은 배제한다. overflow-tail/참조 상실/age 만료/IDR로 건너뜀/pending 폐기를 구별해 counter의 중복·부분집합을 정의한다. frame ID에 근거한 대조 없이 in+drop의 보존식을 주장하지 않는다.

### 5. 현재 C와 대조되는 의미 있는 시험

가상 clock·stand-in codec과 frame/reference tag로 다음을 보여준다.

- 정상 무손실 경로: 원래 순서와 유효 원인이 유지되고 불필요한 recovery0.
- 일시 burst/느린 consumer: 실제로 유효한 prefix가 있을 때, 현재 C의 불필요한 전체 폐기와 추천안의 보존/출력 차이를 같은 schedule로 확인한다. decode 가능성을 독립 reference oracle로 검증하고 단지 frame 수가 많아졌다고 PASS하지 않는다.
- buffer 없음/queue pause/age 만료: frames/bytes/age bound·유한 회복·busy loop 없음. 유효성을 잃은 prefix는 codec에 보내지 않는다.
- 보존 중 새 IDR·추가 gap·선택/reset/binding/queue 중 변경: N1처럼 stale input의 commit/ready/지불0이며 최신 경로로 회복한다.
- 지속 과부하와 회복: receiver/control 진행, bounded 요청/메모리, 서비스 가능 상태가 되면 현재 프레임으로 복귀한다.
- 기존 Merge/covered/terminal/detach 계약은 필요한 소비 회귀로 확인한다. 같은 inline 정의를 바이너리 안에서 old/new로 섞지 않는다. 전량14시험×5는 요구하지 않는다.

## 설계 단계의 완료 기준과 이후 채택 기준

설계 단계 완료 산출물은 **한 고정 문서 + 추천 상태/age/overflow 결정표 + source/diff/commit + 같은 schedule의 현재 C/추천 원형 결과와 한계**다. 실제로 좋아진 경계는 현재 C나 관련 mutant에서 실패하고 추천안에서 통과해야 한다. 개선 근거가 없으면 code를 양산하지 말고 실패한 가설을 제시한다. 임의의 “미시 결정”을 여러 번 나눠 상신하지 않는다.

원형은 일관된 시험 tree에서 실제 정책/제품 sink 경로를 사용하는 것이 원칙이다. 제품 반영은 이 설계 선택과 Source 확인 뒤이며, 이번 GO는 새 APK 실기/게시 승인이 아니다. 실제 채택에는 고정 NDK/ABI·flag 경계와 독립 행동/영상·같은 공급/배경 전제의 출력·age/지연·요청/자원 근거가 필요하다. 현재−9%를 통과시키도록 목표를 낮추지 않는다. 다음 실기 arm 수와 수치 판정은 추천안·가용 계측을 보고 한 번에 고정한다. 지금 역순pair/trace run/실폰 반복을 추가하지 않는다.

## PC lane의 대기 상태 연결

PC 판정은 이미 **seq16154 / codex_old_apk_lane_peer_lost_decision_r1.md**로 전달한 결정이 유효하다. C pair가 끝나 정리됐다면 **현재 c45 고정의 P 독립1회를 지금 진행할 수 있다. 추가 Source/go를 기다리지 않는다.**

P runner는 `.claude/codex_old_apk_review/run_old_apk_lane_p_direct_once.ps1`, SHA-256 `1f6c866a7c6c8b686036d0d9d26c6181602b91874e0cf517509ccacbbb72cae8`이며 `-CandidateHead c45fb9b768accaf3ad54561a7766e2d81cf56c86 -TraceServerJs <기존 v-traced 절대경로>`다. 이 runner가 자기 slot을 소유하므로 별도의 sh_run으로 slot을 중첩하지 않는다. P의 phone control은TCP여서 그 성공만으로 S UDP 실패를 닫지 않는 조건과, 구 Host+구 server/새 조합의 안전 참조 Source 준비·실 게시 구 Host 실행 금지는 그대로다.

PC P 실행과 참조 준비를 우선하며, C 설계/원형은 같은 창구가 담당 순서를 정해 잇는다. 큰 build와 실제 GPU/AVD 측정을 겹치지 않는다. 이번 Codex는 로그/소스/hash 판독만 했고 actual Host/adb/AVD·입력·설치·배포·push는0이다. 제품/추적 문서 변경이 없는 전문 검토라 제품 commit은 만들지 않았다. 사용자 현재 조치 없음.
