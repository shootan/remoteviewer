# keepalive-multi r1 — 설계 수정 GO

2026-10-16 · 사용자 요구 정정: A·B PC를 오가도 떠난 PC의 연결을 계속 유지한다.

**설계 GO(아래 수정 반영). 제품 구현/출하 승인은 아니다.** 0.2.30의 단일 park 정책으로는 요구를 충족하지 못한다. 이전 Codex 설계의 ‘다른 host 선택 시 종료’도 요구를 좁혀 잡았으므로 이번 다중 연결 계약으로 대체한다. 계정/서버 전환·백그라운드 종료 등 기존 보안 경계는 유지한다.

직접 확인: keepalive_multi_proposal_2026-10-16.md, 출하 기준 b4fd204의 SessionPark.kt 및 native_bridge.cpp 전역 자원, ClientEncodedFrameSink 콜백 계약·park 상태기계. 현재 D:/remote/remote 작업 브랜치는 별도 refactor/viewer-split이므로 그 체크아웃을 출하 소스로 간주하지 않았다. 수정·빌드·기기 조작은 수행하지 않았다.

## 1. 동작과 한도

기본 모델은 active 최대1 + 나머지 parked, 그리고 잠시 connecting/parking-confirming/closing인 전환 상태다. B 보기→목록→A 연결/보기 과정에서 B를 보존한다. A 또는 B 복귀 시 살아 있는 연결은 새 directory connect/observe/punch/Hello 없이 TARGETS로 돌아가 새 선택/IDR 경로로 화면을 연다. 기존 원격 선택 창을 자동 복원하는 기능까지 이번에 추가하지 않는다.

**최대3대(active 포함)·각 park5분·LRU는 제안값이며 사용자 답 전 확정하지 않는다.** 한도/기한을 정책 객체로 두어 값만 바꿀 수 있게 구현한다. 한도 미확정과 독립적인 세션 분리·전환 설계는 진행 가능하지만 최종 기본값은 릴리스 전에 사용자 답으로 고정한다. LRU 예시는 한도3이면 A/B/C 이후 **네 번째 D** 연결 때 eviction이지 세 번째가 아니다. 한도2를 선택하면 C가 eviction을 일으킨다.

TTL은 PC를 떠나 BeginPark한 단조시각부터 PC별 계산한다. 목록 갱신·heartbeat·다른 PC 사용으로 연장하지 않는다. 같은 PC를 실제로 재활성화했다가 다시 떠날 때만 새 park episode/기한을 시작한다. 실패한 재사용 탭이나 connecting만으로 갱신하지 않는다. native 제어 루프가 UI polling 없이 만료를 집행한다.

## 2. 세션 식별·JNI 경계

테이블 논리 키는 canonical directory origin + account stable ID + hostId다. 표시명/IP/token 문자열을 키로 쓰지 않는다. 같은 host의 연결 교체도 구분하도록 **프로세스 유일 entry handle/incarnation**을 별도로 발급한다. JNI 비동기 요청과 응답은 이 handle + entry connection generation + operation/전역 lifecycle epoch를 지닌다. hostId만 받는 API는 같은 host 재생성의 늦은 응답을 구별하지 못한다.

각 entry는 controller, rendezvous, socket/stop·후보/error 상태, connect/adopt/close gate, park deadline/episode, 설정·selection snapshot을 소유한다. 현재 g_chosen_candidate/error 등 작은 전역도 누락하지 말고 자원표로 정리한다. credentials는 기존 보호 경로로만 보유하고 로그/테이블 진단에 token을 출력하지 않는다.

op 번호의 프로세스 유일성은 유지하되 **취소 범위는 entry별**이다. B connect를 시작해 A의 parked gate를 무효화해서는 안 된다. 별도 global lifecycle epoch는 sign-out/account/server/background/network 종료에만 전체를 무효화한다. S1 compare-and-close, S2 pending fallback 소유, T1 Adopt/CloseExplicit/CloseIfOwn 직렬화 및 Observe 소켓 회수는 각 entry에서 그대로 성립해야 한다. 수동 IP 연결도 전역 우회로 남기지 않는다. 수동 연결은 기존처럼 다중 park 대상에서 제외하되 전환/종료 수명은 같은 관리자 경유로 둔다.

## 3. 전환 트랜잭션

A→B는 앱의 하나의 전환 소유자로 직렬화한다. A의 입력·clipboard·preview를 먼저 inhibit하고 held input release ACK→pause 송신→새 Pong fence를 확인한 뒤 A를 parked로 둔다. 그 후 B만 active로 채택·Surface 연결·resume한다. B의 네트워크 준비를 미리 병행할 수는 있으나 A가 아직 정상 스트리밍 중인데 B 영상/입력을 활성화하지 않는다.

확인 중 B 탭을 버리지 말고 현재 전환 intent로 보관한다. A/B/C 빠른 탭에서는 최신 intent 하나만 진행하고 stale connect/fallback은 자기 entry만 정리한다. 알려진 거절/만료로 재사용 불가면 그 탭에 한해 새 연결 fallback 최대1회다. 실패한 B 연결 때문에 관계없는 parked A/C를 닫거나 자동 재접속하지 않는다. pause 실패 entry는 기존 fail-closed 종료로 처리하며 성공한 parked 연결을 일괄 종료할 이유는 아니다.

새 연결의 capacity 예약을 원자적으로 처리해 두 비동기 connect가 한도를 넘지 않게 한다. LRU는 parked 중 마지막 실제 사용이 가장 오래된 entry를 고르고 active/현재 activation 대상은 제외한다. 실패한 연결 시도만으로 불필요한 LRU를 여러 개 축출하지 않는다. eviction이 필요하면 UI/로그에 그 PC 종료 사유를 명시하고 TTL/LRU 경계를 시험한다.

## 4. active decoder·Surface는 단일, 콜백은 entry별

단일 decoder/Surface 유지 방향에 동의한다. 다만 controller 여러 개에 같은 sink raw pointer를 그대로 넘기면 parked B의 Disconnect/OnVideoStreamReset/OnVideoDiscontinuity·selection ACK·통계 drain이 active A를 초기화하거나 통계를 소비할 수 있다.

**entry별 sink adapter/라우터와 active binding token**을 둔다. token은 entry handle + connection generation + activation/binding generation + Surface generation에 결합한다. OnEncodedH264Frame뿐 아니라 OnVideoStreamReset/Discontinuity, OnDecodeEpisodeStart, selection result/tag, key-request/owed-key 조회, DecodeGeneration·decode/presentation stats의 모든 방향을 점검한다. B→A→B에서 같은 entry라도 옛 binding callback은 새 화면을 건드리지 못해야 한다.

owner 검사와 decoder mutation은 같은 동기화 경계에서 적용한다. 검사 후 잠금 해제→공용 decoder 호출의 TOCTOU를 만들지 않는다. callback을 기다리는 join/codec stop을 테이블 락 또는 그 callback이 필요한 잠금 아래 수행하지 않는다. entry handle은 참조 수명으로 획득해 표에서 지워진 뒤에도 호출 중 객체가 파괴되지 않게 하고, closing entry에는 새 작업을 거절한다. CloseAll은 먼저 global epoch/각 entry를 무효화한 뒤 map에서 분리하고 락 밖에서 cancel/join/close한다.

새 active binding은 decoder reset·D1 첫 IDR·selection ACK·R1 gesture cancellation·mobile-abr bounded grace를 자기 entry에만 시작한다. A의 디코드 수/화면크기/PGM/통계가 B 보고에 들어가지 않게 한다. B를 닫는 늦은 callback이 A의 새 Surface를 해제하는 반례를 필수로 둔다.

## 5. parked 자원과 액션

paused 세션은 제어 heartbeat/UDP keepalive를 유지하며 decoder/Surface/미리보기 작업은 소유하지 않는다. pause fence 전 in-flight tail은 유한하게 소모/드롭할 수 있으므로 순간부터 네트워크 수신 바이트0을 약속하지 않는다. **안정 park 구간에서 host video 송신/encoder/capture가 기존 idle 정책에 따라 멈추고, parked AU가 active decoder로 전달/화면표시되는 것은0**을 검증한다. unsolicited parked traffic이 계속되면 숨기지 말고 진단·기존 실패 정책으로 처리한다.

park 중 입력/clipboard push·pull/preview 및 원격 privileged 액션을 차단한다. incoming clipboard는 entry별 보관하더라도 active 전환 때 오래된 내용을 PC/로컬에 적용하지 않도록 버리고 baseline을 새로 정한다. 설정과 UI 스냅샷도 대상으로 태그한다. active 조회→나중에 전역 active에 명령 전송하는 형식은 금지한다. queued UI 작업은 처음 승인된 entry/binding을 대상으로 검증하며, 전환 뒤 B 키·클립보드·화질 요청이 A로 가서는 안 된다.

구형 host가 pause를 지원하지 않으면 해당 entry는 park 대신 종료하고 업데이트 필요/유지 불가를 표시한다. 새 host 기능을 가정해 연결을 살아 있는 것으로 꾸미지 않는다. 다른 기기의 host 탈취를 기존 정책대로 받아들이고, parked peer-lost entry를 자동 재접속해 제어권을 되찾지 않는다.

## 6. 종료 사유 수정

제안 §3의 전부 종료 목록에 ‘그 PC 연결 해제’를 포함한 문장은 수정한다.

- 전부 종료: sign-out·실제 account/directory identity 변경·앱 background(onStop, 기존 configuration 예외 유지)·확인된 network path 변화·명시적 모두 끊기. 이전 entry의 pending connect/fallback/HTTP 결과도 영구 무효화한다.
- 개별 종료: 해당 PC 끊기·peer-lost·명시적 session_refused/권한 철회·TTL/LRU·연결 교체·pause 실패. 현재 PC 끊기가 parked 다른 PC를 끊어서는 안 된다.
- **디렉터리 목록의 일시적 누락/갱신 실패만으로 정상 인증 연결을 종료하지 않는다.** 목록 presence는 TTL이나 인증 철회와 다르다. 실제 host 삭제/권한 철회를 나타내는 명시적 응답이 있다면 개별 종료한다. 잘 알려진 401은 계정 전체 인증 무효인지 해당 세션 거절인지 기존 계약대로 구분한다.

## 7. UI·한도 확인

목록 각 PC에 연결 유지/확인 중/만료/종료 상태와 남은 시간을 자기 entry 기준으로 표시하고 PC별 끊기를 제공한다. ‘모두 끊기’는 별도 명시 액션으로 구현하면 좋지만 사용자가 그 버튼을 필수로 요청한 것은 아니므로 범위를 지연시키지 않는다. sign-out/background 전체 종료는 필수다. 한도를 넘겨 종료한 PC는 다음 탭에서 정상 새 연결하며 재사용 성공으로 표시하지 않는다.

최대 연결수/park TTL/LRU 정책은 사용자에게 이미 확인 중이므로 중복 질문하지 않는다. 값이 정해지면 task의 정책 단락과 테스트 기대값을 함께 고정한다. 무제한 세션 유지·백그라운드 서비스·PC 동시 화면·새 wire/server TTL 변경·PC 창 다중 뷰어 확장은 이번 범위 밖이다.

## 8. 필수 행동 검증

2-host 핵심: 격리된 서로 다른 hostId/인증 세션의 A/B, 각각 고유 화면·입력 trace·카운터로 A→목록→B→목록→A→B. 매 복귀의 새 directory connect/observe/punch/Hello0, 각 상대의 parked control 연결 생존, active decoder owner 정확, 새 선택 IDR/첫 화면을 확인한다. 타이밍은 baseline 새 연결과 재사용 각각 제시한다.

추가 반례는 한 묶음으로:

- A의 pending connect/close/reset/selection/decoder stats를 지연→B 채택→옛 callback 해제; 이후 A 재생성. B teardown/reset/입력·clipboard 오염0.
- A/B 빠른 탭, known refused·연결 실패 fallback1, sign-out/stop 뒤 지연 connect0, active A 종료가 parked B를 보존.
- PC별 독립 만료/재활성화 갱신, 사용자 한도 N에 대해 N+1번째 요청에서 parked LRU만 종료. 활성화 실패 때 불필요한 연쇄 eviction0.
- parked 안정 구간의 영상/인코딩/decoder 작업, held input0·클립보드/미리보기0, active/park 통계 분리. tail와 control bytes는 따로 측정.
- direct와 relay 각각 다른 기기 대체 뒤 자동 재탈취0, 구host pause 미지원, 선명도 crop·크게보기·6M 저장과 전환 후 좌표/설정 대상 유지.
- 실 APK UI 독립 실행 + 결정적 interleave 단위/native 시험 + 출하 시험 seam 부재. 두 host는 fail-closed 격리 창만 캡처한다.

배터리라고 부르려면 실제 측정이 필요하다. 우선 active1+parked1 및 사용자 한도에서 control bytes/s·스레드/메모리·안정 park decoder0을 제시하고 실폰 배터리는 미검증으로 남길 수 있다. 모든 네트워크 byte0·무비용 유지라고 주장하지 않는다.

검증용은 위 계약과 사용자 한도 답을 한 task/지시서로 취합하고 작업용 한 곳에 배정한다. 변경이 큰 native 수명 리팩터링이므로 여러 작업용이 같은 브리지/관리자를 병렬 수정하지 않는다. 고정 후보→검증용 독립 행동→Codex 소스/근거 확인 후 다음 APK 단독 릴리스로 진행한다. 0.2.30 실폰 검증과 이 새 요구의 완료를 혼동하지 않는다.

이번 산출물은 설계 검토 파일뿐이다. 제품·버전·설치본·commit/push 변경 없음.
