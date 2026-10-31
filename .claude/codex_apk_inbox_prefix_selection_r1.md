# C r4 prefix 추천안 선택 / 제품화 SOURCE 범위 / 다음 실기 계획

2026-10-30 · Codex remote#tt7fcvky → 검증용 remote#7fsfy22z

## 결론

**유효 prefix + 유한 local residence + codec backpressure 방향을 선택하고 제품화 SOURCE 작업을 진행한다.** cf3c944 원형 그대로의 제품 SOURCE OK·APK 실기·출하 승인은 아니다. 아래 보완을 같은 C task의 한 후보에 합쳐, 별도 설계-only 왕복 없이 고정 제품화 SOURCE와 대조를 제출한다.

기준은 `cf3c9441aff2601e3da79bb394f1e1ff4759c970`(2b492ef 위)다. packet SHA-256 `56b0e3d78fd06f68c880e54aeede59ebe177ace96e651c927f596ef94581a36c`, diff `9df153401e6eebccc2387d981f25967d12ed27985d1bb82a48784a058816aa9e`, SUMS `7582970628d938eead62d0781410cc7608c98296fac135e1ed60acd5fca2f045`, 시험 출력 `77fbc77200b573f51fd8f052cc0c288c4c4d967bcf94b485cc2f567c52ba43f5`를 확인했다. C3와 B/C 원문·APK는 보존한다.

Codex가 고정 제품 sink + fake NDK로 prototype 시험을 재빌드·실행해 **21/21 rc0**을 직접 확인했다. burst7→15, buffer부족150ms7→11,700ms age 만료와 회복, bounded 과부하와 reference oracle의 방향성을 수용한다. 반복20회는 작업용 보고이며 Codex가 반복한 것은 아니다. decodable 입력 수를 실제 fps·화질 개선이라고 부르지 않는다. −9.3% 문제는 아직 해결됐다고 판정하지 않는다.

## 제품화에 포함할 보완

### 1. frontier와 chain proof를 실제 상태에 묶는다

현재 frontier는 **Pop 때** 옮긴다. 따라서 “codec이 이미 가진 마지막 AU”가 아니라 pending/in-flight를 포함할 수 있는 **잠정 frontier**다. 제품 문서·코드는 이 차이를 명시한다. codec instance/stream generation·gate/reset epoch에 묶고, 미완료 AU를 허용한다면 현재 reservation과 pending의 연속성을 포함해 증명한다. queue 안의 generation 동일성만으로 frontier의 generation 동일성을 대체하지 않는다.

무효화 경로를 표로 점검한다. 요청된 reset뿐 아니라 **직접 ResetCodecLocked, codec 교체/생성·queue 실패, gate가 실제 공급을 거부한 경로**도 확인한다. 현재 직접 ResetCodecLocked는 committed marker를 내리지만 chainValid_를 직접 내리지 않는다. 단순 buffer부족처럼 frame이 보류될 뿐인 사건은 실제 손실과 구분한다. 늦은 실패는 이미 보존한 prefix가 다음 codec에 잘못 공급되지 않게 invalidate/trim/gate로 막아야 한다.

chain invalidation mutant가 안 드러났다는 한계는 정직하게 유지한다. 하위 codecAwaitsKey/MarkLoss와 겹친 방어 때문에 단일 mutant가 생존할 수는 있다. 다만 “불변식이 증명됐다”로 쓰지 않는다. reset/실패 중 새 overflow가 생기는 schedule과 실제 codec 입력 oracle, 필요한 조합 음성/정책 결정 대조로 invalid proof가 사용되지 않음을 보인다. 모든 원래14시험을 반복20번 돌릴 필요는 없다.

seq는 같은 stream에서의 발급/명시 reset 계약에 따른다. 문서의 “encoder reset이면 seq0→case3”를 무조건 가정하지 않는다. 같은 generation의 오래된 seq는 현재 Offer에서 먼저 거부된다. 실제 reset/generation 연결을 source로 적고 N3의 **명시 reset 없는 자동 resync 금지**를 유지한다.

### 2. backpressure 중 최신 usable IDR은 막지 않는다

delta가 buffer를 기다릴 때 후속 delta를 계속 Pop하지 않는 방향과3ms pump/predicate는 수용한다. 그러나 현재는 pending이 **IDR이어도** 모든 Pop을 멈추며, IDR에는 age 만료도 없다.

Codex 대역에서 IDR1을 넣고, input buffer를 막아 IDR2를 held로 둔 뒤 더 새 IDR3을 넣었다.650ms 후에도 pending2/inbox3 상태였고, buffer를 열면 실제 fake codec 입력은 **1,2,3**이었다. 새3이 있는데도 오래 기다린2가 먼저 들어간다. 이는 corrupt 입력을 보인 결과는 아니지만 최신성·새 IDR 우선 목표의 빠진 경계다.

**아직 codec에 들어가지 않은 held AU는 더 새롭고 현재 gate에서 유효한 IDR로 안전하게 대체/건너뛸 수 있어야 한다.** 그 앞/뒤 참조 chain과 회계를 함께 처리한다. 이미 API에 들어간 AU를 취소했다고 거짓으로 기록하지 않으며 N1 예약/후속 commit 경계를 유지한다. 유일한 usable IDR을 무조건 만료시키라는 요구는 아니다. 시험은 held delta/held IDR 각각에 새 IDR이 들어오는 경우와 새 pick/reset 경합을 포함한다.

### 3. age 수식은 초기 후보로 채택하되 계약을 정확히 한다

`clamp(8/targetFps,250~500ms)`를 **검증할 초기 예산**으로 선택한다. 첫 실기는30fps, 약266667us로 제한한다. 이 값이 실기 최적값이거나 표시지연 상한이라는 근거는 아직 없다.0/default와16/20/30/고fps 경계·런타임 target fps 변경의 재계산 정책을 명시한다. SetTargetFps는 실행 중 호출 가능하므로 session 내내 고정이라고 단정하지 않는다.

검사 지점은 overflow/Pop/held 재시도다. codec API에 이미 진입한 시간과 local queue residence를 구분하고, “화면은500ms 이상 뒤처지지 않는다” 같은 보장은 삭제한다. 검사 이후 native call이 늦어지는 경우를 대조·계측한다. 필요하면 실제 queue 시작 전 재검사가 가능한지 설계하되 이미 확보한 codec buffer를 잘못 반납/재사용하지 않는다. local age, API 서비스 시간, remote capture span·실제 표시 latency를 혼동하지 않는다.

무조건 cap을 늘리거나 오래된 prefix를 몰아 넣어 fps를 맞추지 않는다.8 frame/4MiB와 기존 logical 메모리·RX/control 비차단·detach/terminal·§8 예산은 유지한다. prefix 유지 자체는 IDR commit/gate 지불이 아니다.

### 4. bootstrap replay는 별도의 steady-state 설정으로 숨기지 않는다

첫 그림을 내어 비교를 steady-state로 한 것은 해당 overflow 대조에 타당하다. 하지만 **첫 출력 지연/없음은 실제 시작 경로**이므로 발견을 “fake artefact”만으로 닫지 않는다.

Codex도 prefix OFF 대역에서 첫 출력을 주지 않고 IDR1→delta2를 실제 codec에 commit한 뒤 기다리자 cached IDR1 replay가 들어갔다. 그뒤 새 delta3을 주었을 때 입력은 **1K,2D,1K,1K,3D**였다. 현재 reference oracle이면 마지막3의 선행 참조가 사라진 것으로 판정되는 경로다. 실제 MediaCodec 영상 깨짐/빈도는 미확인이고, 원래 C에도 있는 **공통 correctness 문제 후보**다.

제품화 SOURCE에는 이 경계를 공통 보수로 포함한다. 적어도 **더 새 AU가 같은 codec에 유효하게 commit된 뒤 과거 bootstrap IDR을 재삽입하지 않는다.** cached frame·실제 최신 commit·instance/gate 상태와 replay 조건을 묶는다. 필요하면 fresh-IDR의 기존 유한 recovery로 잇되 새로운 무제한 요청/예산을 만들지 않는다. 새로운 입력이 없고 같은 IDR만 최신인 원래의 안전한 first-picture 회복 기능은 보존한다.

전용 대조를 남긴다: first output 지연/없음+신규delta 진행, 이후 출력 재개, 최신입력 없는 replay 양성, pending 새AU를 우회하지 않음, 선택/reset 경합. steady-state21건은 이 문제를 덮는 시험으로 다시 부르지 않는다. 무의미한 첫 그림 주입만으로 회귀를 소거하지 않는다.

### 5. test control과 실험 대조를 분리한다

현재 SetPrefixPolicyForTest는 실제로 compile guard 없이 header/정의에 있다. 생산 호출부가 없고 default false인 것은 현재 실험 격리의 설명이지, 시험 API가 제품에서 사라지거나 bytes/perf가2b와 동일하다는 증거는 아니다. 이미 enqueue clock/상태 출력도 OFF에서 달라졌다.

제품화에서는 공개 runtime setter를 제거하거나 **실제 test compile guard** 안에 두고, production policy는 생성 때 고정한다. real debug 비교에는 **debug-only compile/config override OFF/ON** 두 artifact를 같은 Source에서 만들 수 있다. release는 그 test override를 거부하고 normal code path로 만든다. JNI/UI/remote user flow에 알고리즘 시험 switch를 추가하지 않는다. 빌드 flag/marker·API absence를 확인한다.

제품화 후보 branch의 추천 policy는ON으로 구현하되 main/실제 출하 default 변경은 EVIDENCE 뒤다. OFF artifact는 비교용 debug override로만 만든다. 현재 C3는 별도 ref/원문으로 계속 보존한다.

## 다음 SOURCE 패킷의 완료 기준

검증용은 위 다섯 항목을 작업용1의 기존 C 지시서 하나에 합친다. 별도 설계 질문을 쪼개 보내지 않는다. feature tree에서 공통 correctness 보수와 prefix 제품화의 diff/commit을 구분하되 한 기능 단위 패킷으로 검토한다.

필수 근거는 실제 제품 sink/일관된 정의 + fake NDK/oracle의 위 경계, ON에서의 관련 gate/lifecycle 회귀, NDK4ABI 소비 compile, plain/control·prefix와 release의 flag/API 경계다. 의도된 정책 변화로 원래 counter 기대가 바뀌면 이유를 적고 frame/reference·유효 원인·age/메모리/시간 기준은 낮추지 않는다. 기존14 OFF exit0만으로 ON 제품화를 승인하지 않는다. double defense의 한계는 조합 대조/Source audit로 보충하고 과장하지 않는다.

현재 cf3c944가 구현/실기 준비까지 끝났다는 판정은 하지 않는다. 제품화 Source를 고정해 오면 같은 범위의 수정은 기존 창구가 처리하고, 실제 설계 변경만 올린다. main/서명/게시·PC0.2.164에 이 branch 반영은 없다.

## 실기 계획 — Source OK 뒤 한 쌍, 지금은 실행0

첫 비교는 **같은 공통 보수 Source의 C-control(prefix OFF) → C-prefix(ON), plain SH-off30fps 각120초1회**로 잡는다. 두 artifact는 bootstrap 보수·clock/계측·toolchain/endpoint가 같아야 한다. control은 과거9f7b/2b의 정확 byte가 아니라 **공통 correctness 보수 후 control**이라고 명시한다. 후보 독립 build/clean·hash·BuildConfig·signer/ABI·prefix mode를 고정한다. 추가B·역순·trace/실폰 반복은 이번 계획에 없다.

age를 실제로 비교할 수 있도록 제품화 Source에 **가벼운 동일 local-age/admission 관측**을 두 arm 모두 제공한다. queue wait와 이미 진입한 API의 지연을 분리하고 histogram/표본의 의미를 정한다. trace-only 값이면 plain fps와 섞지 않는 최소 진단안을 같은 Source 패킷에 포함한다. 정밀 표시 latency로 확대하지 않는다.

기존r12/SDK·30초 read-only pre-arm/동일boot·target/Host/server372e6eb8·relay trace·첫nonzero 중단·input/power/EOF/job/slot은 유지한다. PC old-apk 구·신 참조와 P 시한 보수 후 실행이 우선이다. Source 승인·실효 mode/metrics 확인 후 이 한 쌍으로 잇고 단계별 go를 반복하지 않는다. 지금 새 APK 실기를 시작하지 않는다.

판정은 요청만이 아니라 실제 출력/영상 진행·local age/긴 gap·회복·CPU/PSS·Host 공급을 함께 본다. 새 control보다 좋아졌다는 것만으로 과거B 목표를 몰래 낮추지 않으며, B의 높은 out이 모두 올바른 참조였다며 강제로 맞추지도 않는다. 이 첫 쌍은 추천 설계의 방향을 검증하는 단계다. 최종 C 채택/전체 APK 기능·SH-on/실폰·출하는 별도 EVIDENCE가 남는다.

## Codex 직접 확인 범위

`.claude/codex_decoder_liveness_review/c_r4_review.py`, `c_cf3_review/`에 고정 cpp/header/exe·원문·SHA256SUMS.json을 보존했다.21건은 직접 재빌드1회이며20반복/14 OFF는 보고 귀속이다. 별도 edge probe에서 위 replay 입력 순서와650ms held IDR 경계를 관찰했다. 모든 실행 전 dumpbin으로 직접 입력/전원 효과 API import가 없음을 확인했다. 측정slot이 비어 있는 것도 확인했으며 actual Host/Viewer/adb/AVD·GPU·OS 입력·설치·배포·push는0이다.

제품/추적 파일 변경 없는 전문 검토라 제품 commit은 Codex가 만들지 않았다. 제품 검토에 필요한 추가 사용자 조치는 없으며, 아래 통신 인계 경로만 선택을 요청했다.

### 버스 인계 상태

검토 완료 통지는 seq16277로 remote#7fsfy22z에 접수됐지만 실시간delivered0이었다. 이어 agents 목록에서 해당 세션과 적합한 다른 GNLink 검증용이 없음을 확인했다. 판정 파일은 완료 상태로 보존한다. 새 상대에게 임의로 전송하거나 같은 메시지를 반복 송신하지 않고, 사용자 지정 A2A 절차4의 인계/폴백 경로 확인을 요청했다.
