# KA r2 kept 모델 — 결정 A

2026-10-27 / Codex remote#tt7fcvky → 검증용 remote#7fsfy22z.

**A 채택: 지원 Host의 kept 대기는 StandbyLease가 parked 활성 세션을 대체한다. 같은 PC에 standby lease와 장기 parked control 세션을 둘 다 유지하는 모델은 채택하지 않는다. 사용자 명시 접속 후 PC 목록으로 나갈 때도 지원·인증 조건을 만족하면 standby 대기로 전환한다.** 구현 GO이며 SOURCE/출하 OK는 아니다. ③ N1~N3·④ M1을 같은 유효 지시서에 함께 반영한다.

입력: 인계⑤/검증용 A 의견, 기존 `codex_keepalive_r2.md` 최신10-26 갱신 및 §1/5~7, Host 계약 §2.2~2.7, KA 보고② 진행3, 1c2116d의 현 kept/session·JNI 경로. B의 별도 상세 명세는 인계에 없으므로 B의 성질을 추정해 승인하지 않는다. 결정은 아래 A의 실행 계약이다.

## 소유 모델

- 논리 PC key는 **directory origin + 안정 account ID + hostId**다. 같은 PC의 현재 owner incarnation/operation 아래 `PreparingStandby / StandbyReady / Activating / Active / ReturningToStandby / Unsupported / Retry`를 구분한다. UI의 “유지 중/준비됨”은 실제 확인된 entry와 lease 생존일 때만 표시한다. Idle·needsCapability·무응답·handedOff라는 사실만으로 준비됨을 만들지 않는다.
- StandbyReady: 자기 UDP socket·entry/secret·Ping만 있다. controller의 활성 control/media 연결·decoder binding·영상·미리보기·원격 입력/clipboard 작업0. **Host standby entry는 control channel이 아니다.** 기존 SessionTable의 parked 활성 세션과 같은 transport로 간주하지 않는다.
- Active: 사용자 tap으로 **해당 lease**를 activation하며 socket/entry를 유지한다. 세션 controller는 활성화 동안만 실행하고 decoder는 화면에 보이는 PC 하나에만 bind한다. 다른 기기가 Host를 인수해 session이 끝나면 자동 재탈취하지 않고 standby로만 돌아간다.
- max3은 active와 standby를 합한 논리 Host 수다. 동일 Host의 lease·session handle을 두 대로 세거나 각3대씩 허용하지 않는다. 동시 staging≤1·유한 시한, 4번째 실패로 기존3대 eviction0, 성공 시 standby LRU만 정리한다. 자동 준비/heartbeat는 최근 순서를 바꾸지 않는다.

## 목록으로 나가기/수동 연결

1. **이미 lease로 activation한 연결**: 입력 release·clipboard/선택 작업 차단·decoder unbind 등 기존 화면 이탈 fence를 먼저 수행하고 세션 controller를 종료한다. session receive/handshake가 확실히 끝난 뒤 같은 lease로 SessionOver/Ping을 재개한다. 중복 handle의 한쪽 close가 원 socket/relay endpoint를 잃게 하지 않는다.
2. 로컬 종료 통지가 Host의 grant 종료를 증명하지 않는다. Host가 아직 active로 보는 구간은 ReturningToStandby로 표시하고 유한 확인/회수를 한다. Ping Ok는 entry 생존만 말하며 그 자체가 active owner 해제 증거라는 주장도 하지 않는다. server `active` enum을 Host owner 진실로 쓰지 않는다. 확인 불가/Unknown/Lost면 fresh capability로 안전하게 재입장하되 옛 handshake/entry로 ready를 만들지 않는다.
3. **사용자가 directory PC를 명시적으로 접속했지만 standby lease 없이 일반 Hello로 만든 연결**도 목록 복귀 시 지원 경로로 전환한다. 그 활성 세션의 종료를 직렬화한 뒤 read-only directory 지원 확인→새 lease socket의 observe+standby connect/admission으로 준비한다. 이 **일회성 첫 전환**의 새 directory 요청/대기는 실제로 기록하고 “기존 lease 무재접속 재사용”이라고 세지 않는다. 다음 탭부터 준비된 lease를 activation한다. 필요하면 처음의 명시 접속도 standby를 거쳐 activation할 수 있으나 사용자의 유한 수동 실패/취소 경로를 보존한다.
4. **raw IP/port 수동 연결**(directory identity·capability 없음), 구 Host·prelogin worker·지원 unknown에는 인증을 꾸며 standby를 만들지 않는다. 자동 ordinary Hello fallback/다른 active 탈취는0. 이 대상의 기존 명시 연결은 유지하되 목록 복귀 시 “상시 유지 미지원”을 알리고 활성 세션은 종료하는 보수적 경로를 쓴다. 다시 tap은 명시 cold connect다. 구 Host에 장기 parked 세션이 남아도 전체3대 상시 대기 지원이라고 보고하는 이중 모델은 만들지 않는다.

전환 중 목록 UI는 사용 가능하며, 다른 PC tap은 최신 사용자 intent로 처리한다. 지연된 admission/Observe/Provide/SessionOver가 새 계정·새 동일key lease를 건드리지 않도록 **key 외 incarnation/epoch/request ID**를 검증한다. 같은 key로 Release→Start했을 때 옛 capability를 새 lease에 Provide하는 경계도 반례에 포함한다. UI thread에서 네트워크/standby runner join을 무제한 기다리지 않는다.

## 다른 계약과 연결

- 기존 “재사용 때 Hello0/observe0/punch0”은 **준비된 lease에서 새 directory rendezvous·ordinary Hello를 하지 않는다**는 뜻으로 유지한다. activation은 StandbyActivate + 현재 gen Pong/유효 HelloAck와 새 활성 session/control 설정이 필요하다. controller.Connect 횟수까지0이라고 요구하거나 보고하지 않는다. 첫 전환/재인증/실제 장애 회복을 steady reuse와 분리한다.
- **UA-2 backend는 활성 session 범위**다. 진짜 Active→Standby로 session이 끝났다면 다음 activation은 새 session의 Host 기본 상태부터 확인하고 이전 choice를 자동 재전송하지 않는다. 같은 살아 있는 connection을 resume하는 경로가 임시로 남으면 ④ M1대로 그 entry의 pending/request 상태는 보존한다. 새 모델을 선언한 것만으로 M1을 닫지 않는다.
- onStop은 전면 유지 기본에 따라 standby와 pending을 회수하고 원 activation/session을 안전하게 취소·종료한다. onStart는 최근3대만 준비하며 화면/입력을 자동으로 되살리지 않는다. 개별 “연결 해제”는 해당 PC의 자동 재시도 억제를 유지한다. 계정/권한 철회는 old owner와 모든 pending을 무효화한다.
- bare standby 준비 실패는 일반 수동 연결 실패와 구분한다. Busy/Unsupported/Lost·backoff를 명시하고, stale lease 재접속으로 Host 제어권을 자동 인수하지 않는다. phone/PC의 동일 Host 공존과 명시 takeover 정책은 유지한다.

## 한 묶음 검증/다음 담당

검증용은 이 결정과 자기 안의 비교/합의를 기존 KA 지시서 하나에 취합하고 기존 작업용1의 e 배선을 잇는다. 새 중복 writer/task를 만들 필요 없다. 자동 preconnect 출하 OFF는 독립 검증 전까지 유지한다.

고정 후보에서 필수 행동:

- 최근A/B/C 앱 복귀→standby3(Host session/video/decoder0)→A tap 활성화→목록으로 이탈→동일 lease standby→B→A. steady reuse의 fresh directory/ordinary Hello0과 실제 첫 화면을 측정한다.
- 일반 명시 directory 연결→목록→첫 standby 전환, 전환 도중 다른 tap/취소, 구 Host/raw IP의 미지원 경로. 첫 전환의 새 요청을 숨기지 않는다.
- 다른 기기가 사용 중인 Host에 자동 standby/ping/회복이 active·held input·영상/선택을 건드리지 않음, 사용자 tap만 takeover. peer-lost 후 자동 재탈취0.
- ReleaseAll/화면 이탈/계정 전환과 대기 handshake·지연 Provide·동일 key 재생성, 4번째 실패/성공 eviction, offline1이 나머지2대 보존. ③ N2 수명 보수의 실제 제품 경로를 사용한다.
- direct 및 실제 시험 server.js relay에서 session 끝 후 같은 socket/lease 보존·proof 손실 bounded 회수. 비용/실 NAT/폰·PC 교차는 기존 후속의 미검증으로 별도 유지한다.

Codex 제품 수정·빌드·시험/서버 실행·삭제·commit/push 없음. 이 운영 판정서 외 history/계획·Git MCP commit은 검증용이 같은 구현 작업에 취합한다. 사용자 현재 조치 없음.
