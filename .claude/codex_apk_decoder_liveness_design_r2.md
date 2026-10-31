# APK E2 — 보충 반영 결정 r2

2026-10-29 · **앞 결정 seq15827/설계 r1을 유지하며 아래처럼 구체화한다.** 제품 보수·검증 계약은 **(a) 준비 뒤 현재 세대 IDR 회복 + (b) RX/control의 codec 동기 의존 제거**를 포함한다. 확인되지 않은 두 독립 root cause를 확정한 뜻은 아니다.

## 보충 반영

report_e2_relay_investigation.md 최신 SHA-256 **66120345950421e64f697e2adc10bb654ba3165f99c2fb63e95d83d4bb7dff0e**와 「보충」을 읽었다.

- 관측된 stale 8은 이전 gen1의 정상 필터링으로 구분한다. gen2 IDR/CSD의 sink 도달과 gen2 gate 통과는 수용한다. 이 값으로 모든 gate/rearm/회복 논리까지 배제하지 않는다.
- surface/codec이 준비되지 않은 약 10초와, 그 뒤에도 실제 codec input queue가 0인 단계를 구분한다. 준비 전의 KeyLost/폐기는 현재 정책일 수 있고 그 자체를 새 결함이라고 하지 않는다.
- UI codec 작업 구간의 공유 lock 의존은 source로 확인한 제품 구조다. **26.4–29.6 전후의 직접 RX wait 계측은 없고**, 약 25.0–26.4 및 재개 후 요청 미관측은 아직 설명되지 않는다.
- Host seq7 기록 부재는 그 관측 지점에서 요청이 보이지 않았다는 사실이다. 생성/limiter/queue/실제 전송/transport ACK/Host dispatch를 확인하기 전 “wire에서 반드시 유실됐다”고 하지 않는다.

본문의 relay/fixture 완전 배제·회귀 없음 등 단정은 보충의 확정 수준과 맞춰 **현재 결론 하나로** 정리한다. 단순 forward source·합계로 모든 IDR의 전달을 증명하지 않는다. r21 A의 E7g 기대 종료와 B의 후속 Host 종료/메뉴 중단을 구분하라는 r1 정정도 유지한다.

## ① 제품 범위와 R2

**0.2.33 우선 포함, 해당 APK 출하 보류 유지**다. 별도 task/branch는 구현 관리 방식으로 허용하지만, 미해결인 필수 첫 영상 실패의 출하 gate를 면제하지 않는다.

(b)의 완료 계약은 r1처럼 **느린 codec lifecycle/buffer 처리나 그것과 공유하는 lock을 RX/control 경로가 기다리지 않음**이다. `OnEncodedH264Frame`뿐 아니라 keyframe 요구/상태·selection/reset hook 등 control 인접 호출도 포함한다. codec 생성 한 군데만 unlock해 놓고 나머지 slow 경로를 남기지 않는다. Android sink 소유 worker·bounded queue·빠른 metadata 및 binding/surface/generation 수명 조건을 유지한다.

(a)는 **현재 유효한 binding/선택에서 codec/surface가 준비된 뒤, 필요한 IDR이 실제 decoder queue까지 들어가고 새 frame이 표시되는 회복 계약**이다. 먼저 계측과 대역에서 어디가 빠졌는지 확인한다. 이미 올바르게 작동하는 상태 machine을 중복 구현하거나 “독립 IDR 결함”으로 미리 고정하지 않는다. (b)를 고친 뒤 기존 (a)가 정상 진행하면 추가 정책 변경은 불필요할 수 있다.

확인할 세부 경계:

- KeyLost/ready/rearm이 같은 현재 owner·selection·surface/codec episode의 요구로 연결되는가.
- 현재 shared session의 owed retry는 **최대 8회/1초 간격**이고 owed 식별자 변화 때 budget을 초기화한다(native_video_client_session.cpp의 owedSelection_/owedAttempts_). 준비 전 소모와 준비 후 새 수요를 구분하는지 확인한다. 이 budget이 실제 소진됐다고 현재 자료만으로 확정하지 않는다.
- consume/limiter가 요청을 coalesce/refuse했는지, 실제 전송 대기열에 들어갔는지, channel이 이미 closed였는지 구분한다. enqueue만으로 IDR을 받거나 queue했다고 보지 않는다.
- 준비 후 살아 있는 control에서 bounded 회복을 보장하고, 이미 종료된 channel/owner·superseded 선택을 위해 재연결·재탈취하지 않는다. 기존 rate/재시도·출하 보안 경계는 유지한다.

## ② 미설명 두 구간의 시험 계측 — 추가 수용

**기능을 바꾸지 않는 한정 계측을 추가해도 된다.** 다음을 하나의 correlation schema로 묶는다.

1. **RX/control 진행**: socket 수신/packet 분류/ACK·Tick/assembled deliver의 enter-exit·마지막 진행 시각, decoder 호출/lock wait 시작·획득·해제와 TID. 긴 call 뒤에만 로그를 남기지 말고 진행 중 phase도 확인 가능해야 한다.
2. **준비·IDR 요구**: surface/codec 준비 시작·완료·실패, binding/selection/streamGen/codec·surface episode, KeyLost→rearm/want→consume→limiter 결과·budget/next retry.
3. **실제 control 전송**: application keyframe-request seq와 transport message/stream seq를 별도로 연결하고 queued/coalesced/실제 send·재전송·ACK/close 시각을 기록한다. Host dispatch의 동일 식별자와 대조한다.

client에서 요청이 wire 이전에 막힌 것이 드러나면 relay 계측을 불필요하게 넓히지 않는다. 실제 send 이후를 가를 필요가 남으면 **local fixture relay의 방향별 수신/분류/전달 결과·send 오류·종료 이유 counter**를 시험 조건으로 추가할 수 있다. 합계/송신 성공을 원격 수신 증거와 구분한다.

계측은 payload/자격 증명을 기록하지 않는 metadata로 제한하고, bounded/낮은 빈도·메모리 phase/snapshot 등으로 RX에 새 blocking I/O/긴 공유 lock을 만들지 않는다. 각 기기의 monotonic·clock offset과 run/session/flow 세대를 연결한다. 운영 서버 계측 배포나 wire 규격 변경은 이번 승인이 아니다.

## ③ 시험 자원·대조 — 앞 허용 유지

다시 사용자에게 같은 자원 승인을 물을 필요 없다.

- 현재 boot의 **실제 SH pipeline-off E2 1회**, 전후 부하/관찰 길이와 effective 경로 기록을 허용한다. AUTO_PREPARE off나 strength 0과 혼동하지 않는다.
- 시험 AVD identity/소유·유휴·slot 독점 확인 뒤 **동일 userdata/AVD 설정의 정상 재시작**을 허용한다. wipe/수동 파일·snapshot 삭제·다른 instance 일괄 kill·사용자 Host/물리 기기 재시작은 포함하지 않는다.
- 필요하면 새 boot의 **기본 SH 경로 E2 1회**를 별도 run으로 비교한다. 새 boot는 환경 변경 arm이며 원래 부하와 같다고 하지 않는다. 재시작/SH-off 통과는 제품 진행성 보수의 대체 근거가 아니다.

script subset/설정 변경의 hash·실행 계획은 고정하고 기존 r6 guard/job/자료·비노출 조건을 유지한다. 큰 전체 suite를 blind 반복하지 않는다.

## 결정적 회귀의 두 축

r1의 느린-codec 시험을 아래 두 조건으로 나누면 (a)/(b)를 구분할 수 있다.

- **slow lifecycle + control 지속**: codec create/configure/start를 기존 control budget보다 오래 barrier로 잡아도 RX/control/ACK/probe가 진행하고 peer_lost가 없어야 한다. 기존 동기 경로나 mutant에서는 실패해야 한다.
- **unready에서 잃은 IDR + ready 뒤 회복**: control을 정상으로 유지한 채 surface/codec을 충분히 늦추고 IDR을 준비 전에 도착시킨다. 준비 후 current generation의 IDR 수요·전송·실제 input queue·fresh present로 이어지는지 확인한다. 초기 retry budget 구간보다 긴 지연과 owner/selection 취소를 포함한다.

이 두 대조로 (b)를 해결해도 (a)가 남는지 판단하고 필요한 부분만 추가 수정한다. fake codec/제어 시간·component를 먼저 사용하며 전체 emulator CPU 과부하나 실제 driver hang을 만들 필요는 없다. 기존 source/후속 독립 EVIDENCE 관문에서 Android 배선과 4 ABI, SH on/off·surface·KA 복귀 영향을 확인한다.

기존 작업용1의 한 유효 지시서로 수정/한정 계측/대조를 계속한다. 새로운 큰 수명 정책/공통 PC 수신 모델 변경이면 실제 확대 범위만 별도 조율한다. 그 외 같은 설계의 반복 상신·다른 승인 하네스 재실행은 필요 없다.

이번 Codex는 최신 보고와 고정 source 읽기·판정 기록만 수행했다. 제품 수정·build·Host/AVD/adb 실행·자원 재시작·설치·운영·삭제·commit/push는 없다. 사용자 현재 조치 없음.
