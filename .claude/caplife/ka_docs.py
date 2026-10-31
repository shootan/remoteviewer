wt = 'C:\\work\\gnlink-wt\\ka-r3\\docs\\'

def load(p):
    s = open(p, 'rb').read().decode('utf-8')
    return s, '\r\n' in s

def save(p, s, crlf):
    if crlf:
        s = s.replace('\n', '\r\n')
    open(p, 'wb').write(s.encode('utf-8'))

p = wt + 'keepalive_host_standby_contract.md'
s, crlf = load(p)
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:80])
    s = s.replace(old, new)

rep('''- **재전송 멱등**:
  - StandbyHello 는 (기기, generation)이 30 s 안에 같으면 같은 Ack 를 재전송한다. capability 를 다시 요구하지 않는다(일회용이므로).
  - Activate 는 같은 generation 이고 그 entry 가 여전히 active 면 새 세션 없이 HelloAck 만 다시 보낸다.
- **늦은 activation 거부**: 다른 쪽이 세션을 가져간 뒤 도착한 옛 generation 의 Activate 는 `Superseded` 로 거부한다. 다시 activation 하려면 더 큰 generation(= 새 사용자 행동)이 필요하다. → parked 기기는 자동으로 세션을 되찾지 않는다.''',
    '''- **재전송 멱등** (r3, Codex `codex_keepalive_r2_spike_r1.md` S1·S2 로 좁힘):
  - StandbyHello 의 같은 Ack 재전송은 **같은 입장 증거**일 때만이다: 같은 기기·generation + **같은 capability**(fingerprint — token 자체는 일회용이라 보관하지 않음) + **같은 반환 끝점** + **유효 lease**(만료 entry 를 먼저 정리). 그 밖은 새 capability 가 필요한 새 admission 이다. 공개된 기기 id·generation 은 아무것도 증명하지 않는다 — 틀린 token 은 secret 0·entry 변경 0, 다른 끝점의 같은 Hello 도 답 없음.
  - 다른 끝점으로 옮기는 것은 Hello 재전송이 아니라 **secret 을 가진 Ping** 이다.
  - Activate: entry 마다 **역할과 무관한 단조 high-water**. 0 은 generation 이 아니다(거부). 낮은 값은 stale(`Superseded`, 상태 변경 0). 같은 값은 **그 grant 가 아직 세션을 가진 동안에만** 새 세션 없이 HelloAck 를 다시 보낸다. 큰 값만 새 명시 activation.
- **늦은 activation 거부**: 2→1→B→2, 0→B→0 모두 `Superseded`. 다시 activation 하려면 더 큰 generation(= 새 사용자 행동)이 필요하다. → parked 기기는 자동으로 세션을 되찾지 않는다.
- **activation 결과의 상관 (제안 — Codex 미동결, 결정②)**: 수락된 activation 은 HelloAck 직전에 `StandbyPong`(entryId + 그 activation 의 clientGen + status 0 + features `kUdpStandbyPongActivationGranted`)을 보낸다. 거부도 `StandbyPong` 에 **거부한 그 generation** 을 싣는다. 클라이언트는 entry+generation 으로 늦은 Ack 와 현재 operation 을 구별한다. 새 kind 는 늘리지 않았다.''')

rep('''### 2.3 entry 별 liveness·권한
- entry 에는 control channel 이 없다. liveness = Ping 이다(lease 20 s, Ping 에 갱신). active entry 의 liveness 는 세션 자체이고 lease 로 만료시키지 않는다.''',
    '''### 2.3 entry 별 liveness·권한
- entry 에는 control channel 이 없다. liveness = Ping 이다(lease 20 s, Ping 에 갱신). active entry 의 liveness 는 세션 자체이고 lease 로 만료시키지 않는다.
- (r3 S3) active grant 는 **그 세션의 실제 끝**(`backendSessionEnded` 의 epoch — resume·pause 는 끝이 아님)과 함께 끝난다: entry 는 다시 parked(lease 가 그때부터), 같은 generation 은 더 이상 답하지 않는다. 그보다 이른 세션의 늦은 끝은 현재 grant 를 지우지 않는다. 세션을 가진 기기의 entry 는 그 밑에서 재 admission·Release 되지 않는다(`Live`).
- (r3 S3) entry id·secret 은 CSPRNG 결과를 확인한다: 실패한 draw 로는 entry 를 만들지 않고(Busy 로 답), id 충돌은 유한 횟수만 다시 뽑는다. ingress 는 멈추지 않는다.''')

rep('''## 3. 아직 계약 밖·다음 단계에서 정할 것
- **릴레이 서버**: 현재 호스트당 lease 1개라서, 릴레이로 두 번째 기기가 붙으면 첫 기기의 릴레이 세션이 끊긴다. 여러 entry 를 실 릴레이로 유지하려면 **클라이언트 세션별 lease + 세션별 host-facing 소켓**(호스트가 주소로도 구별), 또는 entry id 로 역방향을 demux 하는 라우팅이 필요하다(서버 변경 — Codex 확정 필요). spike 의 relay stand-in 은 두 방식을 모의할 뿐 실제 서버를 바꾸지 않았다.
- **보안 수준**: entry secret 은 평문 UDP 로 오간다. 기존 capability 와 같은 수준이다(경로상 관찰자는 둘 다 볼 수 있다). 암호화 채널은 범위 밖이다.''',
    '''### 2.7 릴레이 — 클라이언트별 standby lease + 별도 host-facing 소켓 (r3, Codex 결정 ① (a); 로컬 구현, 미배포)
- `/api/connect` 에 `standby: true`(추가 필드, 기본 off = 오늘 경로). relay 대상이면 그 connect 에 **전용 host-facing UDP 소켓을 mint 시점에 예약**한다. 응답 `standbyRelay`: `reserved` 또는 거부 이유(`host-full`/`account-full`/`ip-full`/`server-full`/`bind-failed`) — 거부면 그 connect 에 relay 후보가 없다(직결 후보는 그대로).
- 상태: `reserved` → StandbyHello(104 B, 그 capability)가 client 끝점에 bind → `provisional` → 호스트 StandbyAck Ok → `ready` → 같은 lease 위 StandbyActivate 의 HelloAck(DirectoryAuth) → `active`. **activation 에 새 directory connect 가 필요 없다.**
- 공존: 한 호스트의 standby lease 여럿이 공존한다. 새 standby lease 는 다른 lease·capability 를 폐기하지 않는다. **legacy lease**(49 B Hello, observe 소켓, newest-wins)는 그대로이고 standby lease 를 끊지 않는다. standby capability 로 legacy Hello 불가, legacy capability 로 standby lease 불가.
- **NAT 경로(호스트 변경 없음)**: heartbeat 의 `pendingPunch` 에 예약마다 entry 하나를 더 싣는다 — `RELAY_HOST_FACING_IP:<예약 포트>`, **어느 클라이언트도 갖지 않는 token**. (클라이언트 token 을 두 번 싣지 않는다 — 두 번째 항목이 같은 capability 로 두 번째 admission 을 허락하게 된다.) 호스트는 기존대로 모든 entry 에 media 소켓으로 punch 하고, 예약 소켓은 **호스트 주소에서 온 첫 datagram 의 tuple 을 pin**(포트 무관 — symmetric NAT 은 목적지마다 새로 매핑)한 뒤 그 밖은 받지 않는다. heartbeat 의 전역 tuple 을 pin 된 lease 에 덮지 않는다(호스트가 이동하면 pin 만 놓고 다음 datagram 이 다시 pin). 예약 소켓도 호스트 쪽으로 punch 1회를 보낸다(주소만 거르는 NAT 용).
- 상한: 호스트당 8(legacy lease 포함), 계정·client wire IP·전체 소켓 상한(환경변수). 회수: 세션 drop 이 소켓·색인·capability 를 함께 닫는다 — 예약 TTL = capability TTL(60 s), provisional 30 s, client 무음 60 s, `closeHostsNow`(호스트 비활성·계정 철회).
- **클라이언트 계약**: client-facing 소켓은 공유라 client→host 는 client 끝점으로 가른다 → **host lease 하나에 UDP 소켓 하나**. ②APK/③Viewer 는 여러 host 를 한 소켓으로 다중화하지 않는다.
- (b) 공유 host-facing 주소 + entry demux 는 R2 에서 제외(S4): 그 뒤에서는 호스트의 control/ACK/NACK/resume/bulk gate 가 주소라 parked 클라이언트의 패킷이 통과한다(부정 대조 KS4 로 확인).

## 3. 아직 계약 밖·다음 단계에서 정할 것
- **릴레이 실 NAT·배포**: §2.7 은 로컬(loopback) 실 server.js 로만 확인했다. 실제 NAT(포트 제한·symmetric)을 넘는 경로, `RELAY_HOST_FACING_IP`(서버 LAN 의 호스트가 공인 IP 로 hairpin 해야 하는 경우 포함), 방화벽에서 예약 포트 범위가 필요한지는 미검증 — 배포 전 R2 에서 정한다. 운영 NAS 서버는 바꾸지 않았다.
- **보안 수준** (Codex 결정 ③): entry secret 은 기존 UDP 평문 bearer 수준이다. 기밀성·무결성·재전송 방지를 주장하지 않는다. entry secret 은 Ping 으로 유지되는 동안 나중의 activation 에 쓰는 재사용 자격이라 일회용 capability 보다 오래 유효하다(같은 수명이 아니다). 메모리 수명 한정·로그/argv/env/영속 저장 금지, 종료·만료·교체·호스트 재시작에 무효화.''')

rep('''- 아직 아님: 실 릴레이 서버(호스트당 lease 1 — §3), 클라이언트(APK/PC) 배선, 비용 실측, held input release 의 직접 반례(기존 Serve 종료 동작에 기댐).''',
    '''- 아직 아님: 실 릴레이 서버(호스트당 lease 1 — §3), 클라이언트(APK/PC) 배선, 비용 실측, held input release 의 직접 반례(기존 Serve 종료 동작에 기댐).

## 5. r3 (2026-10-26, spike/host-admission-r3 = rel/0.2.163 fae0e1f 위, Codex S1~S4 + 결정 ①)
- `host_standby_table_test` 47/47. 부정 대조(표): K1 replay 를 기기+gen 만으로 / K2 replay 판정 전 만료 정리 없음 / K3 high-water 를 다른 entry 에만 / K5 세션 끝이 grant 를 안 끝냄 / K6 CSPRNG 결과 무시 / K7 Live 보호 없음 → 모두 FAIL. K4(gen 0 허용)는 등가 — high-water 0 인 entry 는 같은-gen 경로에서 이미 `Superseded`.
- `host_standby_admission_e2e_test --old-host <capture-lifecycle baseline GNLinkStream> --real-directory` 89/89:
  - 직결 / relay 대역(클라이언트별 host-facing 소켓만 — 공유 모드와 승인 전 `lastActivated` 라우팅 삭제) / **실 server.js relay** / 구 Host.
  - S4: B active 동안 C 가 window-select·control ACK·control NACK·video NACK·resume·bulk 형 datagram·틀린 secret·gen 0 activation 을 보냄 → 처리 0, C 의 leg 엔 Standby* 답 말고 아무것도 안 옴, B 의 epoch·control session 불변, B 의 다음 select 응답은 B 의 leg 로. 부정 대조 KS4(C 의 세션 패킷만 B 의 host-facing 소켓으로) → 2 FAIL.
  - 개수 변천: r2 72(직결 + relay 클라이언트별 + relay 공유 + 구 Host) → r3 WIP 64(공유 모드 삭제, S3/S4 추가) → 67(S4 확장) → 89(실 server.js relay 22).
- `apps/directory` suite(`test/run.js`) 791 PASS rc0, 그중 `relay_standby_test.js` 24. 부정 대조(서버): R1 standby 를 observe 소켓으로 / R2 legacy bind 가 standby 도 끊음 / R3 heartbeat 가 pin 을 덮음 / R4 pin 뒤 아무 출처나 받음 / R6 legacy lease 미계수 → 모두 FAIL. R5(standby token 의 legacy Hello)는 기존 newest-legacy-token 규칙이 이미 거부해 생존.''')
save(p, s, crlf)

h = wt + 'history.md'
s, crlf = load(h)
s = s.replace('\r\n', '\n')
s += '''
## 2026-10-26 keepalive-r2 A r3 — 호스트 standby 보수(S1~S4) + 릴레이 (a) 로컬 구현

- Codex `codex_keepalive_r2_spike_r1.md` 의 SOURCE S1~S4 와 결정 ①(클라이언트별 lease + 별도 host-facing 소켓). 브랜치 `spike/host-admission-r3`(rel/0.2.163 fae0e1f 병합 위). 출하 경로 자동 preconnect 는 여전히 OFF, 운영 relay 무변경.
- 호스트: replay 는 같은 입장 증거(capability fingerprint·반환 끝점·유효 lease)일 때만(S1), activation high-water 단조·0 거부·같은 gen 은 살아 있는 grant 만(S2), grant 는 실제 세션 끝과 함께·`Live` 보호·CSPRNG 결과 확인(S3), 실패/거부 응답이 그 operation 을 이름(제안). 계약 `docs/keepalive_host_standby_contract.md` §2·§5.
- 서버: `/api/connect {standby:true}` → 예약 host-facing 소켓, StandbyHello bind, ready/active 상태, legacy lease 와 공존, pendingPunch 추가 entry 로 NAT 경로(호스트 변경 없음), 상한·회수. 계약 §2.7.
- 시험: 표 47/47(+부정 대조 7, 등가 1), e2e 89/89(실 server.js relay 포함, KS4 부정 대조), directory suite 791 rc0(+서버 부정 대조 6, 생존 1 — 이유 기록). loopback 이라 실 NAT·배포는 미검증.
'''
save(h, s, crlf)
print('ok')
