# keepalive-r2 A spike r1 — 설계·SOURCE 판정

2026-10-26 / 검증용 remote#yj8mrmrx / 기존 A task **t-y1lnbgnd** / 후보 **9f1fac15fd812f4b3efe4cdec64ec385354f62f3** = 569059f 위3d04ecf+9f1fac1.

**admission과 activation 분리 방향은 GO. 현재 spike SOURCE는 NEEDS_CHANGES(S1~S4), 실 릴레이·전체 동작 EVIDENCE는 보류다.** 다음②의 격리된 shared/API·APK 준비는 진행 가능하나, 이 구현을 승인된 host 기반으로 간주하여 자동 preconnect/실서버 배선을 켜는 것은 아직 불가하다. capture-lifecycle N1~N4/R1 보수가 작업용1 우선이라는 순서는 유지한다. 아래 결정과 보완을 기존 KA 정본 한 곳에 취합한다.

입력 보고 전문·계약 문서·table/wire·두 admission 호출부·protocol/CMake·시험의 핵심 시나리오/relay mock 및 실제 directory relay/AuthorizePeer 소스를 읽었다. HEAD9f1fac1·tracked clean·diff whitespace rc0를 직접 확인했다. 새 빌드/실행·실서버 작업은 하지 않았다. table25/25와 e2e72/72×2는 **작업용 인용**이며 독립 검사로 바꾸지 않는다. KA 의존 base의 capture-lifecycle SOURCE 보완도 추후 최신 후보에 통합한다.

## 결정 ① 실 릴레이 — 클라이언트별 lease + 별도 host-facing 소켓 채택

**후보 ⓐ를 채택한다.** 단순히 relayLeaseByHost를 여러 개 담게 바꾸는 것으로 끝나지 않는다. 현 `apps/directory/server.js`는 다음 제약을 함께 가진다.

- relayBindSession의 호스트당 lease1 / newer connect drop.
- capability 발급 시 relayLatestTokenByHost가 직전 token을 삭제하고 bind도 최신 token만 허용.
- host endpoint→session의 단일 역방향 index, 공용 observeSock에서 host로 송신.
- 49B 일반 Hello만 bind하고 DirectoryAuth HelloAck만 provisional→active로 인정.104B StandbyHello는 현재 서버에서 새 경로를 만들지 못한다.

R2 안의 서버 변경 목표는 **host→유한 lease 집합, connect/entry별 인증과 두 leg 라우팅, standby admission 자체로 non-preempt bind/ready**다. 새 자동 connect/admission은 이미 존재하는 다른 lease·capability·active를 폐기하지 않는다. legacy 명시 Hello의 기존 takeover 의미는 별도 분기해 보존하며 단순 'newest wins 전면 삭제'로 옛 패킷의 재탈취를 열지 않는다. 준비된 entry activation에는 새 directory connect가 필요 없어야 한다.

각 lease의 host-facing UDP 소켓을 분리해 기존 호스트의 adopted IP:port gate도 entry를 구분하게 한다. **공용 host-facing endpoint + entry-id로 standby 응답만 demux하는 ⓑ는 이번 R2에서 제외한다.** 기존 control/bulk/NACK/resume은 매 패킷 entry 인증이 없어 그 형태로는 parked 무간섭이 보장되지 않는다(S4).

소켓 분리에는 NAT 경로 준비도 포함한다. 현재 observe 경로로 얻은 host tuple이 새 relay 소켓 목적지에도 유효하다고 가정하지 않는다. 인증된 디렉터리 안내로 각 host-facing endpoint에 경로를 준비하고, 그 lease에서 실제 관찰한 host wire tuple/재검증·오래된 endpoint fence를 사용한다. 전역 heartbeat tuple을 모든 lease에 무조건 덮지 않는다. host 이동/같은 client endpoint 재사용/서버 재시작/host 비활성·계정 철회 때 정확한 소유 lease만 닫는다. 유한 socket/lease/미인증 reservation 상한·TTL·할당 실패 Busy와 rollback을 둔다. 호스트8 entry 상한과 서버 lease 상한/legacy active 포함 정책을 맞추고 계정/IP 전체 상한도 둔다.

이는 **서버 구현 방향 승인**이다. 검증용이 기존 KA 실행 정본에 인증·상태전이·정확한 bind endpoint/포트·NAT 경로·범위/rollback을 취합해 서버 담당을 연결한다. R1 native writer와 중복 쓰지 않는다. 제한된 로컬 실제 server.js 다중 lease 시험→고정 후보 검토→승인된 R2 배포 순서이며, 이번 판정만으로 운영 NAS 서버나 방화벽/라우터 설정을 바꾸지 않는다. 권한·포트 개방이 실제 필요하면 구체 증거로 그 부분만 조율한다. NP의 STUN/race·grace 변경을 이 작업에 조용히 합치지 않는다.

## 결정 ② wire 번호·정책

**UDP kind311~316을 이 기능에 배정한다.** 검증용의 main/capture/APK 번호 대조를 수용하고, 이번 후보의 enum은 기존308~310 뒤 별도 kind이며 LK89·UA90 control 메시지 공간과 다름을 확인했다. 최종 R2 통합에서 전체 합집합 충돌을 한 번 더 검사한다.

host 전체8/설치기기당1, standby lease20초, status0 OK/1 Busy/2 Unknown/3 Superseded는 초기 정책 GO다. 클라이언트의 최근 host3대 한도와 별개다. Ping은20초보다 충분히 짧게 bounded jitter로 보내고, 수신/갱신 실패를 'ready'로 계속 표시하지 않는다. 제품5분 만료 제거는 인증·transport lease 제거가 아니다. 서버/클라이언트 별 비용 수치는 후속 실측이며 지금 성공 수치를 만들지 않는다.

**104B 레이아웃과 현재 activation 응답 의미는 아직 최종 동결하지 않는다.** 성공 응답이 일반 HelloAck라 entryId/clientGen/호스트 incarnation·operation을 직접 식별하지 않는다. 클라이언트 취소·이전 activation의 늦은 Ack와 새 operation을 구별하는 결과 계약을② 전에 정한다. 새 번호를 마구 늘릴 필요 없이 예약한 Ack/Pong의 식별·phase/flags 의미를 명시할 수 있다. 구 호스트로 미지 요청을 무한 전송하지 않고 유한 probe 무응답은 '미지원 또는 경로 미확인', 자동 일반 Hello0이다. timeout만으로 구 버전을 확정하지 않는다. 수동 접속 fallback은 사용자 명시 행동에만 허용한다.

## 결정 ③ entry secret 평문 수준

**기존 UDP의 평문 bearer 전송 수준을 유지하는 안은 아래 조건으로 수용한다. R2에 새 범용 암호화 전송계층을 필수 추가하지 않는다.** 다만 '일회용 directory capability와 같은 수명/권한'이라는 표현은 틀리다. entry secret은 Ping으로 유지되는 동안 나중에 activation할 수 있는 재사용 자격이므로 더 오래 유효하다. 관찰 가능한 경로의 공격자에게 기밀성/메시지 무결성·재전송 방지를 제공한다고 주장하지 않는다. 기존 원격 화면/control 전체를 암호화했다고도 쓰지 않는다. 공개 UI의 deviceId/entryId는 비밀이 아니다.

필수 조건은 시스템 난수 실패 시 fail-closed, 입장 때 검증한 증거에만 결합된 제한 재전송(S1), 비밀의 메모리 수명 한정·로그/argv/env/영속저장/일반 진단 덤프 금지, 종료·만료·교체·host 재시작/계정 변경의 무효화다. 권한 철회/host 로그아웃을 이미 알고도 Ping만으로 자격을 영구 연장하지 않는다. 명시 로그아웃/개별 해제는 client entry와 pending 작업까지 폐기한다. 인증 만료/철회 갱신은 새 연결 탈취와 구별해 설계하고, 새 자격이 필요할 때만 bounded 재인증한다. 추가 MAC/암호화 강화는 별도 후속 제안으로 가능하나 아래 실제 인증 우회를 단순히 '평문을 허용했다'며 남길 수는 없다.

## SOURCE 보완 S1 — 공개 식별자만으로 secret 재발급, 만료 검사 우회 (P1)

`host_standby_wire.hpp::standby_handle`는 tokenLen이0이 아니면 table.IsReplay(deviceId,clientGen)가 true인 경우 AuthorizePeer를 생략한다. IsReplay는 기기ID·gen·30초만 확인한다. 따라서 **알려진 deviceId/gen과 아무 비어 있지 않은 잘못된 token**으로 다른 socket에서 요청해도 기존 entry secret을 그 요청자의 주소로 받는다. 원래 capability 일치조차 요구하지 않는다. 이는 평문 observer 위협과 별개로 공개 ID를 인증 수단으로 올려버린 우회다.

또한 Admit는 IsReplay를 Expire보다 먼저 수행한다. lease20초가 지났어도30초 replay 창 안의 entry가 아직 sweep되지 않았으면 성공 Ack로 되살아난 것처럼 보인다(그 직후 Ping/Activate가 Expire해 Unknown). 원시 비밀을 반복해서 내보내는 replay가 endpoint·입장 증거·유효 lease와 묶이지 않는다.

보완: 유효 lease 먼저 판정, 같은 입장 operation의 **검증했던 원래 capability 증거**와 제한된 return path에만 동일 Ack를 재송신한다. 원문 비밀을 새로 영속 보관하지 말고 필요하면 메모리 내 fingerprint/입장 nonce 등으로 식별한다. 다른 주소로 비밀을 옮기는 것은 deviceId/gen 재전송으로 허용하지 않고 이미 가진 proof 또는 새 capability로 수행한다. admission generation의 오래된 요청이 현재 entry를 교체하지 않는 규칙도 정한다. 반례: 다른 socket+맞는device/gen+틀린token, 만료20~30초 replay, 진짜 Ack 유실 재전송, 인증된 endpoint 이동. wrong-token 응답은 secret0/entry 변경0이어야 한다.

## SOURCE 보완 S2 — activation generation이 감소/0 sentinel 재사용 (P1)

`host_standby_table.hpp::Activate`는 **activeEntryId != entryId일 때만** gen<=activateGen을 거부한다. 현재 active A에 오래된 gen이 오면 fresh=true로 새 세션을 만들고 activateGen까지 낮춘다. 구체적으로 A gen2 활성→늦은A gen1(허용·1로감소)→B 활성→옛A gen2(2>1이라 재탈취). gen0도 처음 허용하고 activateGen==0은 '활성 이력 없음'의 sentinel이므로 B takeover 뒤 A gen0 재전송이 다시 통과한다.

보완: entry별 단조 high-water를 역할과 무관하게 보존,0 거부 또는 별도 initialized 상태, 같은gen은 **같은 entry가 같은 활성 grant를 소유할 때만** 멱등 재응답, 작거나 superseded된 값은 상태 변경0. wrap/restart는 새 entry/incarnation으로 구분한다. 반복 activation의 endpoint 이동·이미 끝난 session의 Ack 재사용도 실제 owner epoch와 함께 판단한다. 반례는 위2→1→B→2,0→B→0, 같은gen 정상 Ack 유실·새gen 명시 takeover. 성공 Ack도 entry/op와 결합한다(결정②).

## SOURCE 보완 S3 — 실제 session 종료·entry 회수·난수 실패 (P1/P2)

activeEntryId는 Activate가 바꾸고 일반 새 Hello가0으로 만들 뿐 실제 Serve 종료와 연결되지 않는다. Expire는 active entry를 무기한 제외하므로, 제어 세션이 끝나고 Ping도 멈춰도 옛 active entry가 남고 같은gen Activate는 fresh=false로 끝난 세션의 HelloAck만 되돌릴 수 있다. 자동 대기 재접속이 이 상태를 ready/active라고 채택해서는 안 된다. 반대로 같은기기 새 StandbyHello가 active entry를 교체하면 table owner만0이 되고 실제 세션은 그대로 살아 있어 두 소유권이 갈라진다.

보완: entryId+grant/owner epoch에 귀속된 실제 session 종료 통지로 active 표식을 정리한다. 늦은 이전 종료는 새 grant를 지우지 않고 resume/pause는 종료로 읽지 않는다. standby lease 갱신은 session liveness와 구분한다. 같은기기 재admission/Release가 기존 active에 미치는 의미도 명시해 table과 실제 owner가 불일치하지 않게 한다. 기기ID는 클라이언트가 정하는 설치 식별자이지 directory 인증이 보증한 계정 ID가 아님을 유지한다.

`Next64`는 BCryptGenRandom의 반환값을 버린다. 실패로0만 나오면 entryId 생성 do/while이 UDP ingress에서 끝나지 않거나 secret 생성 실패가 all-zero/부분 난수로 성공 처리될 수 있다. 난수 생성 결과를 확인해 entry를 전혀 만들지 않는 실패 경로, 유한 ID충돌 재시도, 시험용 난수 실패/충돌 반례를 둔다. 제품 ingress가 난수 실패 때문에 멈추지 않아야 한다.

## SOURCE 보완 S4 — 공유 relay endpoint에서는 parked control gate가 성립하지 않음 (P1)

standby Ping의 entry 구별은 맞지만 **그 뒤 control은 entry를 보지 않는다.** `host_startup_control.cpp`의 fromAdoptedPeer는 IP:port이며 초기 control streamId도 정해진 공통 상수다. B가 shared relay를 통해 active가 된 뒤 같은 host-facing 소켓 뒤의 parked C가 raw ControlData/ACK·NACK/resume/bulk를 보내면 주소 gate를 통과한다. table의 entry별 secret은 이 기존 패킷을 보호하지 않는다.

현재 relay e2e는 B 활성 뒤 C의 Ping만 보내고 C의 control·bulk·resume 무간섭을 검사하지 않는다. RelayMock은 StandbyActivate를 **호스트 승인 전에** lastActivated로 바꾸므로 잘못된 secret/옛gen의 거부된 activation도 비-standby host 응답의 목적지를 바꿀 수 있다. 이 mock은 ⓑ의 올바른 라우팅 증거가 아니다.

이번 채택 ⓐ에서는 실제 별도 소켓으로 B/C를 구분하고, B active 뒤 C의 data/ACK/NACK/resume/bulk·거부된 activation이 B의 세션/역방향 송신을 바꾸지 않는 반례를 둔다. ⓑ는 단순 standby demux로 안전하다는 주장을 제거하고 미승인 대안으로 남긴다. 공유 endpoint를 향후 지원하려면 전체 패킷의 인증된 entry/owner envelope가 필요하며 현재 R2에 자동으로 더하지 않는다.

## 실행 근거·② 진행 범위와 다음 담당

- table25·e2e72는 분리된 direct entry의 admission/Ping 무간섭·명시 takeover·상한·일부 lease 및 구 host probe의 유용한 spike 근거다. **SOURCE 전체 OK/실 relay 성공/held input release 실증/최종 영상 IDR 표시**로 확대하지 않는다. raw window-select 처리 로그는 control 경로 근거이지 실제 원격 UI 동작의 전부가 아니다.
- S1~S4를 구분하는 source 보수·제한 반례를 기존 A spike/Host 기반 계약에 취합한다. held key/button을 실제로 누른 상태에서 takeover/종료해 release됨·옛 peer 지연 입력/LockCred/clipboard/file 작업 무간섭을 후속 독립 검사에 포함한다. direct+실 server.js relay에서 APK+PC2기기, active1+parked 공존, lease·서버 재시작·5분 초과 유지/비용은②~④의 필수 EVIDENCE다.
- **②는 조건부 GO**: 작업용2의 기존 APK 준비와 격리된 standby 상태기계·유한 probe/lease API·취소/incarnation/클라이언트 operation 연결은 작성 가능하다. S1~S4와 activation Ack 상관 계약이 닫히기 전 **출하 경로 자동 preconnect OFF**, 현재 운영 relay에 실 배선/출하·게시 없음. legacy/무응답으로 자동 일반 Hello를 보내는 fallback 금지. 보완 후 같은 고정 후보의 Host 기반 SOURCE+검증용 핵심 독립 행동 확인으로 이 배선 장벽을 닫는다. 매 커밋 전체 검사 관문은 만들지 않는다.
- 우선순위는 **작업용1 capture-lifecycle N1~N4→R1 통합/릴리스**, 이후 KA A/② shared writer다. 별도 APK/UI 준비는 분리 파일에서 진행 가능하다. R2를 작업용1의 R1 보수와 중복 native writer로 돌리지 않는다. 사용자10-26의 R1=PC0.2.163/APK0.2.32, **R2=PC0.2.164/APK0.2.33**, 폰 기본 전면 유지가 최신 계약이다. 옛 task/§7의 후보 번호·background 미답 표기는 이 결정으로 정리한다.
- 검증용이 단일 KA 실행 정본에 이번 결정·보수·relay 담당/범위를 연결한다. 기존 A **t-y1lnbgnd**, B **t-x27113l6**를 재사용하고,②/서버가 새 실행 단위면 최초 task ID·담당을 연결한다. 별도 작업용 병렬 지휘나 ACK용 추가 왕복은 불필요하다. 원 WIP328d616·현재 후보/증거는 보존한다.

Codex 산출물은 미추적 판정서/인계뿐이고 제품·공개 문서 편집/빌드/설치/운영 서버·방화벽 변경/서명/게시/commit/push는 없다. 구현 history·계획checkbox·Git MCP 커밋은 해당 기능 task에 취합한다. 지금 사용자 조치는 없다.
