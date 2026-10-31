# keepalive-r2 A r3 — SOURCE·설계 결정

2026-10-26 / 검증용 remote#yj8mrmrx / 기존 A task t-y1lnbgnd / 고정 **933814f** / 검토 범위9f1fac1..933814f 중 KA Host·wire·directory 변경.

**admission 분리·별도 relay socket 방향은 유지한다. 현재 SOURCE는 NEEDS_CHANGES(N1~N3), 운영 배포·자동 preconnect 출하는 OFF 유지.** 아래 세 결정은 이 조건으로 확정한다. 작업용1의 다음 순서가 R1 APK0.2.32(picker→SH r2→UA-2)인 것을 수용하며 KA 보수는 그 뒤에 기존 task로 잇는다. 이 검토 때문에 APK를 세우거나 별도 native writer를 만들지 않는다.

보고 r3 전문·Host table/wire/입장·활성화 호출부·server.js 변경과 관련 auth/punch/회수·시험을 읽었다. de208b9에 병합된0.2.163 변경은 이미 승인한 제품과 KA 차이를 구분했고 다시 소스 관문을 열지 않았다. whitespace rc0 확인. 현재 worktree HEAD는93b15f6이며933814f 대비 계약 문구1줄뿐, apps/libs diff0이다. 판정 대상은 요청한 immutable933814f다. Codex 새 빌드·시험·서버 실행 없음.

## 결정 1 — activation Pong 상관: GO, 클라이언트 완료 의미 고정

StandbyPong의 entryId+clientGen+status+ActivationGranted flag로 성공/거부를 상관하는 방식을 채택한다. UDP kind311~316/104B와 Pong features bit0 배정을 유지한다. 새 kind가 필요하지 않다.

- 클라이언트는 현재 host/entry incarnation·로컬 activation operation의 generation 및 예상 peer/경로가 일치하는 Pong만 사용한다. 취소된 op/옛 generation/다른 entry는 상태 변경0이다. bare HelloAck만으로 activation을 완료하지 않는다.
- Pong은 **host admission/grant 수락**이지 영상 표시/입력·로그인 성공이 아니다. HelloAck는 기존 기능 협상 자료이고 control/role·첫 정상 영상은 각각 후속 단계로 확인한다. UDP는 Pong→HelloAck의 송신 순서가 수신 순서를 보장하지 않으므로 새 클라이언트는 손실/역순을 허용하는 유한 state machine을 둔다. 같은 activation gen 재전송은 새 grant/epoch를 만들지 않는 현재 Host 규칙을 유지한다.
- 현재 e2e helper는 granted를 본 뒤 HelloAck를 기다리는 순서여서 역순이면 재전송으로 회복한다. 이를 한 번의 왕복 성공/역순 검증이라고 확대하지 않는다. ②의 실제 클라이언트에서 late/refusal/역순/하나 유실/cancel을 제한 대조한다. Pong 성공 후 다른 기기가 인수하면 옛 쪽은 parked/명시된 소유 상실로 내려가며 자동 재탈취하지 않는다.

## 결정 2 — 서버 standby wire: 조건부 GO, 최초 pin 방식은 미승인

standby:true는 opt-in이고 기존 필드/legacy 경로를 보존한다. standbyRelay의 reserved/구체 거부 이유, reserved→provisional→ready→active는 relay의 운용 상태로 채택한다. 이 active가 Host의 현재 active owner를 항상 뜻하지는 않는다(다른 lease takeover 뒤 구 lease의 enum이 자동 parked로 바뀌는 구현은 없음). 클라이언트 소유 판단에 그 서버 enum을 쓰지 않는다.

**pendingPunch에 별도 host-facing 목적지를 주는 방향은 수용하지만, 현재의 같은 IP 첫 datagram pin은 N1 보수 전 GO가 아니다.** 호스트 변경 없음은 승인 조건이 아니며, 필요한 최소 Host proof를 추가할 수 있다. 클라이언트 capability를 두 번 나눠주는 방식으로 되돌리지 않는다.

구 directory는 standby 필드를 무시하고 legacy /api/connect의 latest-token 부작용을 낼 수 있다. 자동 준비를 켜기 전 **서버의 standby 지원을 부작용 없는 metadata/capability로 먼저 확인**한다. 지원을 모르는 서버에 첫 mutating connect를 보내 알아내는 방식은 피한다. 새 서버는 relay 비대상/disabled일 때도 standby 지원 여부와 relay 가용성/거부를 구분해 표현한다. 없음/timeout을 이미 지원/ready로 표시하거나 자동 legacy Hello로 fallback하지 않는다. 운영상 서버 먼저 배포만으로 영구 호환을 보장한다고 가정하지 않는다.

N1~N3 뒤 고정 서버 후보·로컬 실제 server.js 독립 확인→R2 NAT/포트·접근 범위/rollback 검토→운영 배포 순서다. 현재 NAS/방화벽/공유기 변경은 없다. NP STUN/race/grace 변경을 KA 서버 보수에 묵시적으로 섞지 않는다.

## 결정 3 — host lease당 클라이언트 UDP socket1: GO

계정/origin+host+entry incarnation마다 relay client-facing tuple을 독립시킨다. 각 lease의 observe/punch/standby/control/media에 쓰는 해당 UDP socket 수명을 유지하고 여러 host lease가 같은 client endpoint를 공유하지 않는다. TCP 등 다른 용도의 socket까지 총1개로 제한한다는 뜻은 아니다.

로그아웃/개별 해제/park 종료/명시 재접속 때 해당 소유자만 회수한다. live socket을 다른 host/incarnation에 재사용하면서 옛 lease의 datagram/응답을 섞지 않는다. count에는 ready3 이하와 별도 staging/closing을 구분한다. RelayMock의 공유 모드 삭제와 실제 별도 host-facing socket 사용은 이 계약과 맞는다.

## N1 (P1) host-facing 최초 pin이 IP만으로 인증됨

server.js:2030의 relayStandbyFromHost는 아직 pin되지 않았으면 known host IP 중 하나인지 확인한 뒤 **패킷 종류/입장 증거와 관계없이 첫 datagram의 port를 pin**한다. 같은 NAT/같은 IP의 다른 socket은 최초 상태에서 구별되지 않는다. 이후 pin된 tuple은 StandbyAck·Pong·HelloAck와 media의 신뢰 원점이 된다.

host-only token을 pendingPunch에 추가하지만 서버 reservation에 증거로 보관·대조하지 않는다. 기존 HostAgent::Punch가 보내는49B Punch는 authToken이0이므로 그 token은 현재 pin 인증에 쓰이지 않는다. 별도 목적지 token을 만들었다는 것만으로 호스트 증명이 되지 않는다. 현재 시험은 **정상 host를 먼저 pin한 뒤** 다른 port를 거부할 뿐 이 최초 경쟁을 검사하지 않는다.

보완: reservation/host incarnation에 묶인 유효한 host 전용 proof로 최초 tuple을 확정한다. 예를 들어 host 전용 fresh challenge를 저장하고 Host가 그 목적지에 보내는 응답에서 증명하게 하는 최소 확장이 가능하다. 같은 IP의 tokenless/틀린 proof/다른 reservation proof는 pin·ready·forward 변경0이어야 한다. 클라이언트용 token이나 공개 entryId를 호스트 증명으로 재사용하지 않는다. 일반 Punch 호환을 유지하되 증거 없는 Punch를 인증된 pin으로 올리지 않는다.

호스트 이동 시 무조건 pin만 풀어 다시 IP 첫 packet을 믿는 것도 같은 경계다. 인증된 fresh reprobe로 재결합하거나 해당 lease를 안전하게 종료·유한 재입장시키는 정책을 정한다. parked host에서 옛 pendingPunch가 이미 소비된 뒤에도 재준비가 가능한지 대조한다. 전체 active/다른 lease를 함께 끊지 않는다.

반례: 동일IP의 stranger가 **정상 host보다 먼저** 예약 port에 송신, fake StandbyAck/HelloAck 및 wrong/cross-lease proof, 올바른 proof 이후 stranger, host 이동 뒤 old proof 재생. 인증된 정상 tuple만 pin/상태 전이를 만들고 secret·control·영상이 다른 socket으로 가지 않아야 한다. 실 NAT 통과는 별도이며 loopback에서 이 인증 반례를 먼저 고정할 수 있다.

## N2 (P2) S1 입장 증거·generation의 두 잔여

**N2-a:** StandbyProofFingerprint는 FNV-1a64 값이며 그 일치가 AuthorizePeer 생략을 결정한다. 주석처럼 단순 진단 fingerprint가 아니라 **재전송의 인증 판단**에 사용된다. 원래128-bit random capability의 동일성을64-bit 비암호학적 hash 하나로 대신했다. plaintext bearer 수준을 수용한 결정이 이 입장 증거의 축약/충돌까지 허용한 것은 아니다.

최소 보완은 원문 token을 로그/영속 저장하지 않고, 인증된 replay proof를 SHA-256 등 암호학적 digest로 묶어 전체 digest를 비교하는 것이다. hash 계산 실패는 replay가 아니며 fail-closed로 처리한다. endpoint/유효 lease/동일 operation의 현재 조건은 유지한다. 새 wire/범용 암호화 채널을 만들 요청은 아니다.

**N2-b:** Admit는 같은 기기의 live entry만 보호하고, **parked entry의 admitGen보다 오래된 요청**을 거부하지 않는다. gen10의 미소비 capability A가 늦고 gen11/capability B가 먼저 정상 입장한 뒤, 늦은 gen10+A가 오면 AuthorizePeer가 성공하여 gen11 entry/secret을 교체한다. S1 판정의 오래된 입장 요청이 현재 entry를 교체하지 않는 조건이 남았다.

입장 generation/incarnation의 순서를 명시해 같은 기기의 늦은 operation은 현재 ready entry를 바꾸지 않게 한다. 클라이언트 재시작/번호 reset은 별도 인증된 incarnation 계약으로 처리하며 무조건 작은 번호를 새 입장으로 받아 해결하지 않는다. ②에서 필요한 지속 monotonic counter 또는 incarnation 정책도 함께 전달한다. 단위·wire 반례는 gen11 ready 뒤 unused cap의 gen10/같은gen 다른proof→기존entry/secret 불변, 진짜 새 operation·정상 replay는 허용이다. 현재 activationHigh의 단조 보호(S2)는 별도로 이미 수용한다.

## N3 (P2) 호스트8개 상한은 반대 입장 순서에서9개가 됨

relayStandbyRefusal은 standby 예약 시 legacy1개를 세지만, **relayBindSession(legacy)은 기존 standby 개수를 검사하지 않는다.** standby8개가 먼저 존재하고 legacy가 없을 때 새 legacy Hello가 오면 9번째 lease가 만들어진다. 현재 시험과 R6 변이는 legacy가 먼저인 방향만 검사한다.

호스트 전체8(legacy 포함) 계약은 양쪽 클래스의 신규 할당에서 지킨다. 기존 legacy의 같은 slot 교체는 보존하되, 용량 거부가 다른 standby/legacy를 먼저 폐기해서는 안 된다. 반례는 standby8→새 legacy 거부/아무 lease 변경0, legacy1+standby7의 legacy 교체는 총8 유지, 만료/Release 후 정상 할당이다. 계정/IP/전체 socket 상한의 env도 유효한 유한 정수로 파싱해 잘못된 숫자로 검사가 사라지지 않게 한다.

## 수용한 근거·다음 담당

- S1의 endpoint·원증거·Expire 선행 조건은 개선됐고, S2의 역할 무관 high-water/0 거부, S3의 실제 epoch 종료·Live 재입장/Release 보호와 CSPRNG 실패·유한 충돌 재시도는 SOURCE 방향을 수용한다. N2만 좁게 보완한다.
- S4의 별도 host-facing socket과 parked session 패킷 분리는 맞다. 실제 server.js+실 Host loopback 두 lease, ACK/NACK/resume/bulk 무간섭은 유용한 기능 근거다. 전송 source·host facing pin의 인증은 N1과 별개다.
- 저장 원문의 table47/0, e2e89/0 두 회, directory suite ALL PASS를 확인했다. 791 및 K/KS/R 변이 수치는 작업용 실행 귀속이다. K4 등가/R5 기존 방어로 생존이라고 구분한 처리는 타당하다. 생존한 변이를 실패 검출 성공으로 세지 않는다. Codex/검증용 독립 PASS·실 NAT 성공으로 확대하지 않는다.
- closeHostsNow는 relaySessions 전체를 순회해 standby reservation/lease도 닫고, drop은 자신 소유 index/socket/capability를 회수하는 방향임을 확인했다. 운영 권한/포트 범위·서버 LAN 경로·실 NAT·장시간/비용·held input release·실 APK+PC는 아직 필수 후속이다. 자동 가능한 최초 pin·입장 순서 반례를 실 NAT 대기만으로 넘기지 않는다.

검증용은 기존 KA A 정본에 **이번 세 결정과 N1~N3 한 묶음**을 반영한다. 다음 순서는 기존 합의대로 **작업용1 R1 APK0.2.32 우선**, KA 보수는 그 뒤다. ②의 분리된 API/클라이언트 상태·시험 준비는 가능하나 SOURCE 보완·핵심 독립 검사 전 운영 relay 변경/자동 preconnect 출하 OFF는 유지한다. 새 task가 필요한 실행 단위만 최초 ID·담당을 연결하고 기존 A/B를 중복 생성하지 않는다.

현재 PC0.2.163 게시본에는 영향이 없다. Codex는 미추적 판정서/인계만 작성했고 제품·공개 문서 편집/빌드·서버 실행/배포·삭제·commit/push는 하지 않았다. 기록·계획/Git MCP commit은 같은 구현 task에 취합한다. 지금 사용자 조치는 없다.
