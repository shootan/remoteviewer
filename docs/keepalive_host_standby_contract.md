# keepalive-r2 A — 호스트 대기(standby) entry 계약 초안 + spike (2026-10-22, 작업용1)

근거 계약: `codex_keepalive_r2.md` §5. 정본 task: `task_keepalive_r2.md` A. 기준: 처음 main `82b3d14`(328d616), 10-26 에 R1 후보 `fix/capture-lifecycle 569059f` 위로 옮김(브랜치 `spike/host-admission-r2`).
**상태: 계약 초안 + 제한 spike(브랜치 `spike/host-admission`).** 출하 경로·클라이언트(APK/PC) 배선은 아직 없다. 자동 3대 재접속은 이 계약이 Codex 확정되기 전까지 출하하지 않는다.

## 1. 현 소스가 말하는 것 (82b3d14)
- 호스트 세션은 하나다. `host_session.hpp:87` `streamControlActive`, `:100` `udpControlChannel`, `:104` `epoch`, `:149` `injectedInput` 은 모두 단일 값이다.
- 인증된 새 Hello 는 세션을 가져간다.
  - `host_startup_control.cpp:512` `AdmitHello(directoryAuthenticated, startsSession)` 가 epoch 를 올리고(`host_session.hpp:162~`) control channel 을 Close 한다.
  - 그 직전에 송신 peer 가 바뀐다(`:500~510`).
- `:534` 이후 control·bulk·NACK 는 adopted peer 에서 온 것만 받는다(`fromAdoptedPeer`).
- `host_session.hpp:229` 같은 token + 같은 IP 의 Hello 는 `Retransmit` → peer 를 다시 채택한다(같은 세션으로 취급).
- pause(`streamControlActive=false`)는 같은 세션의 상태다. 두 번째 기기를 받는 수단이 아니다.
- **알 수 없는 kind 는 무시된다**: non-adopted peer 의 데이터그램은 `:840` `directoryAgent.ConsumeUdpPacket` 으로 간다. 그 함수(`directory_client.cpp:517~649`)는 Punch 와 디렉터리 observe 응답만 받고 나머지는 `false` 를 돌려준다(무처리). → 새 kind 는 **구 Host 에서 무해하다**(세션을 빼앗지 않고 답도 없다).
- 릴레이는 호스트당 lease 가 하나다.
  - `server.js:565` `relayLeaseByHost`, `:1753` 새 connect 가 기존 lease 를 drop 한다(`'newer connect'`).
  - 호스트는 릴레이 경유 클라이언트를 모두 릴레이 주소 하나로 본다(`host_startup_control.cpp:209` 주석).

## 2. 계약 (spike 로 구현한 형태)
### 2.1 대기 admission vs activation
- 새 패킷 6종(`poc_protocol.hpp` `UdpPacketKind` 311~316)과 고정 104 B `UdpStandbyPacket`:
  - `StandbyHello` / `StandbyAck` / `StandbyPing` / `StandbyPong` / `StandbyActivate` / `StandbyRelease`
- **StandbyHello**: Hello 와 같은 일회용 디렉터리 capability + 기기 id(16 B) + client generation.
  - 호스트는 `directoryAgent.AuthorizePeer` 로만 인증한다. 세션의 `directoryToken`·인증 상태(`ClassifyDirectoryHello`)는 건드리지 않는다.
  - 그 뒤 entry 를 만들어 `StandbyAck`(entryId + 128-bit entry secret + lease)로 답한다.
- **세션 무접촉 규칙**: StandbyHello/Ping/Release 는 adopted peer·epoch·control channel·stream 상태·input·clipboard·file copy·LockCred 어느 것도 바꾸지 않는다(분기가 모든 세션 처리보다 앞에 있고 `continue`).
- **activation**: `StandbyActivate`(entryId + secret + generation)만 세션을 가져간다.
  - 경로는 **인증된 Hello 와 같은 단계**다: 옵션 저장 → peer 채택 → keyframe → `AdmitHello(true,true)` → `AwaitControlReady` → `HelloAck`(DirectoryAuth).
  - 클라이언트는 이후 기존 control 경로를 그대로 쓴다. 새 directory connect/observe/punch/Hello 는 필요 없다.
- 이전 Hello 세션의 token 은 activation 때 지운다(`directoryToken.clear()`). 그 Hello 의 재전송이 `Retransmit` 로 peer 를 되찾지 못하게 하기 위해서다.

### 2.2 식별·멱등·주소
- entry 의 정체는 **entryId + secret** 이다(주소 아님). secret 은 시스템 CSPRNG(`BCryptGenRandom`)로 만든다.
  - Ping 은 entry 의 끝점을 갱신한다(peer 이동을 따라감).
  - 릴레이처럼 여러 클라이언트가 같은 주소로 보여도 entry 는 구별된다.
- **재전송 멱등** (r3, Codex `codex_keepalive_r2_spike_r1.md` S1·S2 로 좁힘):
  - StandbyHello 의 같은 Ack 재전송은 **같은 입장 증거**일 때만이다: 같은 기기·generation + **같은 capability**(fingerprint — token 자체는 일회용이라 보관하지 않음) + **같은 반환 끝점** + **유효 lease**(만료 entry 를 먼저 정리). 그 밖은 새 capability 가 필요한 새 admission 이다. 공개된 기기 id·generation 은 아무것도 증명하지 않는다 — 틀린 token 은 secret 0·entry 변경 0, 다른 끝점의 같은 Hello 도 답 없음.
  - 다른 끝점으로 옮기는 것은 Hello 재전송이 아니라 **secret 을 가진 Ping** 이다.
  - Activate: entry 마다 **역할과 무관한 단조 high-water**. 0 은 generation 이 아니다(거부). 낮은 값은 stale(`Superseded`, 상태 변경 0). 같은 값은 **그 grant 가 아직 세션을 가진 동안에만** 새 세션 없이 HelloAck 를 다시 보낸다. 큰 값만 새 명시 activation.
- **늦은 activation 거부**: 2→1→B→2, 0→B→0 모두 `Superseded`. 다시 activation 하려면 더 큰 generation(= 새 사용자 행동)이 필요하다. → parked 기기는 자동으로 세션을 되찾지 않는다.
- **activation 결과의 상관 (제안 — Codex 미동결, 결정②)**: 수락된 activation 은 HelloAck 직전에 `StandbyPong`(entryId + 그 activation 의 clientGen + status 0 + features `kUdpStandbyPongActivationGranted`)을 보낸다. 거부도 `StandbyPong` 에 **거부한 그 generation** 을 싣는다. 클라이언트는 entry+generation 으로 늦은 Ack 와 현재 operation 을 구별한다. 새 kind 는 늘리지 않았다.

### 2.3 entry 별 liveness·권한
- entry 에는 control channel 이 없다. liveness = Ping 이다(lease 20 s, Ping 에 갱신). active entry 의 liveness 는 세션 자체이고 lease 로 만료시키지 않는다.
- (r3 S3) active grant 는 **그 세션의 실제 끝**(`backendSessionEnded` 의 epoch — resume·pause 는 끝이 아님)과 함께 끝난다: entry 는 다시 parked(lease 가 그때부터), 같은 generation 은 더 이상 답하지 않는다. 그보다 이른 세션의 늦은 끝은 현재 grant 를 지우지 않는다. 세션을 가진 기기의 entry 는 그 밑에서 재 admission·Release 되지 않는다(`Live`).
- (r3 S3) entry id·secret 은 CSPRNG 결과를 확인한다: 실패한 draw 로는 entry 를 만들지 않고(Busy 로 답), id 충돌은 유한 횟수만 다시 뽑는다. ingress 는 멈추지 않는다.
- 대기 entry 가 할 수 있는 것은 Ping / Release / Activate 뿐이다. 대기 entry 의 ControlData 는 기존 `fromAdoptedPeer` gate 로 버려진다.

### 2.4 제어권 전환
- 새 epoch 를 시작하면 다음이 일어난다:
  - 기존 control channel Close(`SessionRollover`).
  - 이전 Serve 종료 → held input release(`injectedInput`, 기존 동작).
  - 이전 peer 의 지연 패킷은 non-adopted 로 거부.
  - `udpPeerChanged` → 새 keyframe.
- 선택(창/모니터)은 세션 시작 상태를 따른다(기존 Hello 와 같다).

### 2.5 상한·Busy·회수
- 호스트 전체 entry 8, 기기당 1이다. 같은 기기의 새 admission 은 옛 entry 를 교체한다.
- 꽉 차면 `Busy` 로 답하고 아무것도 바꾸지 않는다. 만료 entry 는 먼저 정리된다. Release 로 즉시 회수한다.

### 2.6 구 Host
- StandbyHello 자체가 지원 probe 다. 구 Host 는 답하지 않는다(§1).
- 답이 없으면 클라이언트는 그 Host 를 "상시 유지 미지원"으로 표시하고 **자동 Hello 를 보내지 않는다**. 명시 수동 접속은 기존 Hello 경로를 쓴다.
- 새 Host 는 기존 Hello 를 그대로 받는다(구 APK/PC 호환). Hello 세션은 entry 와 무관하고 `SessionTakenByHello` 로 표시된다.
- pre-login 모드 호스트도 답하지 않는다(디렉터리 세션 하나뿐).

### 2.7 릴레이 — 클라이언트별 standby lease + 별도 host-facing 소켓 (r3, Codex 결정 ① (a); 로컬 구현, 미배포)
- `/api/connect` 에 `standby: true`(추가 필드, 기본 off = 오늘 경로). relay 대상이면 그 connect 에 **전용 host-facing UDP 소켓을 mint 시점에 예약**한다. 응답 `standbyRelay`: `reserved` 또는 거부 이유(`host-full`/`account-full`/`ip-full`/`server-full`/`bind-failed`) — 거부면 그 connect 에 relay 후보가 없다(직결 후보는 그대로).
- 상태: `reserved` → StandbyHello(104 B, 그 capability)가 client 끝점에 bind → `provisional` → 호스트 StandbyAck Ok → `ready` → 같은 lease 위 StandbyActivate 의 HelloAck(DirectoryAuth) → `active`. **activation 에 새 directory connect 가 필요 없다.**
- 공존: 한 호스트의 standby lease 여럿이 공존한다. 새 standby lease 는 다른 lease·capability 를 폐기하지 않는다. **legacy lease**(49 B Hello, observe 소켓, newest-wins)는 그대로이고 standby lease 를 끊지 않는다. standby capability 로 legacy Hello 불가, legacy capability 로 standby lease 불가.
- **NAT 경로(호스트 변경 없음)**: heartbeat 의 `pendingPunch` 에 예약마다 entry 하나를 더 싣는다 — `RELAY_HOST_FACING_IP:<예약 포트>`, **어느 클라이언트도 갖지 않는 token**. (클라이언트 token 을 두 번 싣지 않는다 — 두 번째 항목이 같은 capability 로 두 번째 admission 을 허락하게 된다.) 호스트는 기존대로 모든 entry 에 media 소켓으로 punch 하고, 예약 소켓은 **호스트 주소에서 온 첫 datagram 의 tuple 을 pin**(포트 무관 — symmetric NAT 은 목적지마다 새로 매핑)한 뒤 그 밖은 받지 않는다. heartbeat 의 전역 tuple 을 pin 된 lease 에 덮지 않는다(호스트가 이동하면 pin 만 놓고 다음 datagram 이 다시 pin). 예약 소켓도 호스트 쪽으로 punch 1회를 보낸다(주소만 거르는 NAT 용).
- 상한: 호스트당 8(legacy lease 포함), 계정·client wire IP·전체 소켓 상한(환경변수). 회수: 세션 drop 이 소켓·색인·capability 를 함께 닫는다 — 예약 TTL = capability TTL(60 s), provisional 30 s, client 무음 60 s, `closeHostsNow`(호스트 비활성·계정 철회).
- **클라이언트 계약**: client-facing 소켓은 공유라 client→host 는 client 끝점으로 가른다 → **host lease 하나에 UDP 소켓 하나**. ②APK/③Viewer 는 여러 host 를 한 소켓으로 다중화하지 않는다. 또 legacy lease 끝점에서 온 Standby* 는 relay 가 버린다 — relay 경유의 standby probe 는 기존 수동 연결 위가 아니라 자기 소켓의 standby connect 로 한다(직결은 무관).
- (b) 공유 host-facing 주소 + entry demux 는 R2 에서 제외(S4): 그 뒤에서는 호스트의 control/ACK/NACK/resume/bulk gate 가 주소라 parked 클라이언트의 패킷이 통과한다(부정 대조 KS4 로 확인).

## 3. 아직 계약 밖·다음 단계에서 정할 것
- **릴레이 실 NAT·배포**: §2.7 은 로컬(loopback) 실 server.js 로만 확인했다. 실제 NAT(포트 제한·symmetric)을 넘는 경로, `RELAY_HOST_FACING_IP`(서버 LAN 의 호스트가 공인 IP 로 hairpin 해야 하는 경우 포함), 방화벽에서 예약 포트 범위가 필요한지는 미검증 — 배포 전 R2 에서 정한다. 운영 NAS 서버는 바꾸지 않았다.
- **보안 수준** (Codex 결정 ③): entry secret 은 기존 UDP 평문 bearer 수준이다. 기밀성·무결성·재전송 방지를 주장하지 않는다. entry secret 은 Ping 으로 유지되는 동안 나중의 activation 에 쓰는 재사용 자격이라 일회용 capability 보다 오래 유효하다(같은 수명이 아니다). 메모리 수명 한정·로그/argv/env/영속 저장 금지, 종료·만료·교체·호스트 재시작에 무효화.
- lease 갱신 정책과 서버 capability 수명, 클라이언트 쪽 backoff·Busy 처리·표시, entry 재인증(호스트 재시작 시 entry 소멸 → 클라이언트는 새 capability 로 StandbyHello).
- 비용 측정(§6): 대기 entry 는 영상·control channel 이 없고 Ping 데이터그램(20 s lease 안에서 몇 초 간격)만 있다. 실측은 ②~④ 단계에서 한다.

## 4. 실행 결과 (2026-10-26, spike/host-admission-r2 = 569059f 위)
- `host_standby_table_test` 25/25 rc0.
- `host_standby_admission_e2e_test --old-host <9991b36 의 GNLinkStream>` **72/72 ×2 rc0**(이 PC 콘솔, SLOT_REQUEST CreateNew→삭제, 루프백, 설치본 무조작):
  - 두 기기 대기 공존(세션 0) → A 명시 activation(첫 세션) → B 의 ping·재 admission·control·늦은 activation 이 A 를 못 건드림 → B 명시 takeover(새 epoch·새 keyframe) → A 의 옛 control 거부·A 는 parked 로 남음 → A 의 옛 generation activation 은 superseded 거부, 새 generation 만 되찾음.
  - 상한 8·Busy·Release 회수, lease 만료.
  - relay 대역(클라이언트별 / 공유 host-facing 주소) 두 방식에서 entry 로 구별, 직결 A 무간섭.
  - 구 Host: StandbyHello 에 답 없음(= 미지원), 그 Host 의 기존 세션 무간섭.
- 처음 실행에서 드러난 시험 쪽 오류 2건(제품 아님)을 고쳤다: 첫 세션은 handshake 경로라 `session epoch=… peer=new` 줄이 없다(기대값이 하나씩 컸음) / 구 Host 경우 B 의 무응답 확인을 8회(3.2 s)→3회로 — 원시 시험 클라이언트가 control ACK 를 보내지 않아 구 Host 가 ~6 s 뒤 peer-lost 로 끝내는 것이 겹쳤다. 그리고 capture-lifecycle 의 UA-2 로그가 같은 `session epoch=` 를 써 이중 집계되던 것은 그쪽에서 `backend-owner epoch=` 로 고쳤다(569059f).
- 시험 실패 증거용으로 `REMOTE60_E2E_DUMP_HOST_LOG`(시험 프로그램 쪽 환경변수, 호스트 로그를 파일로 덧붙임)를 넣었다.
- 아직 아님: 실 릴레이 서버(호스트당 lease 1 — §3), 클라이언트(APK/PC) 배선, 비용 실측, held input release 의 직접 반례(기존 Serve 종료 동작에 기댐).

## 5. r3 (2026-10-26, spike/host-admission-r3 = rel/0.2.163 fae0e1f 위, Codex S1~S4 + 결정 ①)
- `host_standby_table_test` 47/47. 부정 대조(표): K1 replay 를 기기+gen 만으로 / K2 replay 판정 전 만료 정리 없음 / K3 high-water 를 다른 entry 에만 / K5 세션 끝이 grant 를 안 끝냄 / K6 CSPRNG 결과 무시 / K7 Live 보호 없음 → 모두 FAIL. K4(gen 0 허용)는 등가 — high-water 0 인 entry 는 같은-gen 경로에서 이미 `Superseded`.
- `host_standby_admission_e2e_test --old-host <capture-lifecycle baseline GNLinkStream> --real-directory` **89/89 ×2** (e2e exe 26112644…, GNLinkStream 1ab576eb…):
  - 직결 / relay 대역(클라이언트별 host-facing 소켓만 — 공유 모드와 승인 전 `lastActivated` 라우팅 삭제) / **실 server.js relay** / 구 Host.
  - S4: B active 동안 C 가 window-select·control ACK·control NACK·video NACK·resume·bulk 형 datagram·틀린 secret·gen 0 activation 을 보냄 → 처리 0, C 의 leg 엔 Standby* 답 말고 아무것도 안 옴, B 의 epoch·control session 불변, B 의 다음 select 응답은 B 의 leg 로. 부정 대조 KS4(C 의 세션 패킷만 B 의 host-facing 소켓으로) → 2 FAIL.
  - 개수 변천: r2 72(직결 + relay 클라이언트별 + relay 공유 + 구 Host) → r3 WIP 64(공유 모드 삭제, S3/S4 추가) → 67(S4 확장) → 89(실 server.js relay 22).
- `apps/directory` suite(`test/run.js`) 791 PASS rc0, 그중 `relay_standby_test.js` 24. 부정 대조(서버): R1 standby 를 observe 소켓으로 / R2 legacy bind 가 standby 도 끊음 / R3 heartbeat 가 pin 을 덮음 / R4 pin 뒤 아무 출처나 받음 / R6 legacy lease 미계수 → 모두 FAIL. R5(standby token 의 legacy Hello)는 기존 newest-legacy-token 규칙이 이미 거부해 생존.
