# keepalive-r2 A — 호스트 대기(standby) entry 계약 초안 + spike (2026-10-22, 작업용1)

근거 계약: `codex_keepalive_r2.md` §5. 정본 task: `task_keepalive_r2.md` A. 기준 main `82b3d14`.
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
- **재전송 멱등**:
  - StandbyHello 는 (기기, generation)이 30 s 안에 같으면 같은 Ack 를 재전송한다. capability 를 다시 요구하지 않는다(일회용이므로).
  - Activate 는 같은 generation 이고 그 entry 가 여전히 active 면 새 세션 없이 HelloAck 만 다시 보낸다.
- **늦은 activation 거부**: 다른 쪽이 세션을 가져간 뒤 도착한 옛 generation 의 Activate 는 `Superseded` 로 거부한다. 다시 activation 하려면 더 큰 generation(= 새 사용자 행동)이 필요하다. → parked 기기는 자동으로 세션을 되찾지 않는다.

### 2.3 entry 별 liveness·권한
- entry 에는 control channel 이 없다. liveness = Ping 이다(lease 20 s, Ping 에 갱신). active entry 의 liveness 는 세션 자체이고 lease 로 만료시키지 않는다.
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

## 3. 아직 계약 밖·다음 단계에서 정할 것
- **릴레이 서버**: 현재 호스트당 lease 1개라서, 릴레이로 두 번째 기기가 붙으면 첫 기기의 릴레이 세션이 끊긴다. 여러 entry 를 실 릴레이로 유지하려면 **클라이언트 세션별 lease + 세션별 host-facing 소켓**(호스트가 주소로도 구별), 또는 entry id 로 역방향을 demux 하는 라우팅이 필요하다(서버 변경 — Codex 확정 필요). spike 의 relay stand-in 은 두 방식을 모의할 뿐 실제 서버를 바꾸지 않았다.
- **보안 수준**: entry secret 은 평문 UDP 로 오간다. 기존 capability 와 같은 수준이다(경로상 관찰자는 둘 다 볼 수 있다). 암호화 채널은 범위 밖이다.
- lease 갱신 정책과 서버 capability 수명, 클라이언트 쪽 backoff·Busy 처리·표시, entry 재인증(호스트 재시작 시 entry 소멸 → 클라이언트는 새 capability 로 StandbyHello).
- 비용 측정(§6): 대기 entry 는 영상·control channel 이 없고 Ping 데이터그램(20 s lease 안에서 몇 초 간격)만 있다. 실측은 ②~④ 단계에서 한다.
