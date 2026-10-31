import json
R = 'C:/work/gnlink-wt/ka-r3'
D = 'D:/remote/remote/.claude/caplife/'

json.dump({'repo_path': R, 'message': """feat(directory, keepalive-r2 r4): standby support is said without side effects; a standby connect always gets its reason

Codex codex_keepalive_r2_spike_r3.md decision 2: a client learns that a directory knows standby connects from
GET /api/hosts (standby: {supported, relay}) -- an older directory has no such field -- not from a first mutating
/api/connect. A standby connect the relay is not for is answered standbyRelay=not-eligible (relay-disabled when no
relay runs), never left without one. relay_standby_r4_test: both (20 checks); suite 815 PASS rc0.

Product / test / docs: product (directory) / test (relay_standby_r4_test) / docs (none).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"""}, open(D + 'kar4_m_commit.json', 'w', encoding='utf-8'), ensure_ascii=False)

# ---- docs
p = 'C:/work/gnlink-wt/ka-r3/docs/keepalive_host_standby_contract.md'
s = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in s
s = s.replace('\r\n', '\n')

def rep(old, new):
    global s
    assert s.count(old) == 1, (s.count(old), old[:80])
    s = s.replace(old, new)

rep('''  - StandbyHello 의 같은 Ack 재전송은 **같은 입장 증거**일 때만이다: 같은 기기·generation + **같은 capability**(fingerprint — token 자체는 일회용이라 보관하지 않음)''',
    '''  - StandbyHello 의 같은 Ack 재전송은 **같은 입장 증거**일 때만이다: 같은 기기·generation + **같은 capability**(r4: **SHA-256 전체 비교** — 64-bit 축약 금지, 계산 실패는 replay 아님·입장 없음; token 자체는 일회용이라 보관하지 않음)''')
rep('''  - 다른 끝점으로 옮기는 것은 Hello 재전송이 아니라 **secret 을 가진 Ping** 이다.''',
    '''  - 다른 끝점으로 옮기는 것은 Hello 재전송이 아니라 **secret 을 가진 Ping** 이다.
  - (r4 N2-b) 같은 기기의 입장 중 **현재 entry 보다 새롭지 않은 것**(gen ≤ admitGen: 늦게 온 미사용 옛 capability, 같은 gen 의 다른 proof)은 `Stale` — Superseded 로 답하고 secret 0, 현재 entry·secret 불변. 더 새로운 operation 만 교체한다. **클라이언트는 입장 generation 을 재시작을 넘어 단조로 유지**한다(②의 계약). entry 가 lease 로 사라진 뒤에는 작은 번호도 새 입장이다.''')
rep('''- **activation 결과의 상관 (제안 — Codex 미동결, 결정②)**:''', '''- **activation 결과의 상관 (r4: Codex 결정 1 GO)**:''')
rep('''새 kind 는 늘리지 않았다.''', '''새 kind 는 늘리지 않았다. Pong 은 **host 의 admission/grant 수락**이지 영상·입력·로그인 성공이 아니다. 클라이언트는 현재 entry/incarnation·그 operation 의 gen·예상 경로가 맞는 Pong 만 쓰고, bare HelloAck 만으로 activation 을 완료하지 않으며, Pong·HelloAck 의 손실·역순을 견디는 유한 상태기계를 둔다(②).''')
rep('''- **NAT 경로(호스트 변경 없음)**: heartbeat 의 `pendingPunch` 에 예약마다 entry 하나를 더 싣는다 — `RELAY_HOST_FACING_IP:<예약 포트>`, **어느 클라이언트도 갖지 않는 token**. (클라이언트 token 을 두 번 싣지 않는다 — 두 번째 항목이 같은 capability 로 두 번째 admission 을 허락하게 된다.) 호스트는 기존대로 모든 entry 에 media 소켓으로 punch 하고, 예약 소켓은 **호스트 주소에서 온 첫 datagram 의 tuple 을 pin**(포트 무관 — symmetric NAT 은 목적지마다 새로 매핑)한 뒤 그 밖은 받지 않는다. heartbeat 의 전역 tuple 을 pin 된 lease 에 덮지 않는다(호스트가 이동하면 pin 만 놓고 다음 datagram 이 다시 pin). 예약 소켓도 호스트 쪽으로 punch 1회를 보낸다(주소만 거르는 NAT 용).''',
    '''- **NAT 경로·최초 pin (r4, Codex N1)**: heartbeat 의 `pendingPunch` 에 예약마다 entry 하나를 더 싣는다 — `RELAY_HOST_FACING_IP:<예약 포트>`, 아무 클라이언트도 갖지 않는 punchToken, 그리고 **그 lease 의 `hostProof`(host 에게만)**. 클라이언트 token 은 두 번 싣지 않는다. 호스트는 그 목적지로 보내는 punch 의 token 칸에 proof 를 싣는다(그 밖의 punch 는 비움 — 클라이언트 token 을 선로에 싣지 않는다; proof 항목은 capability 로 등록하지 않는다). 예약 소켓은 **호스트 주소에서 온, 현재 proof 를 실은 Punch 만** 그 tuple 로 pin 한다(포트 무관 — symmetric NAT). proof 없음·틀림·다른 lease 것·그 밖의 datagram 은 pin·ready·전달 변경 0. heartbeat 의 전역 tuple 을 pin 된 lease 에 덮지 않는다. **호스트가 이동하면** pin 을 놓고 lease 마다 **새 proof** 를 그 heartbeat 의 pendingPunch 로 준다 — 옛 proof 재생은 아무것도 pin 하지 않는다. 예약 소켓이 호스트로 보내는 punch 1회(주소만 거르는 NAT 용)는 그대로다.''')
rep('''- 상한: 호스트당 8(legacy lease 포함), 계정·client wire IP·전체 소켓 상한(환경변수).''',
    '''- 상한: 호스트당 8(legacy lease 포함) — **r4: 어느 쪽이 먼저 오든**(standby 8 뒤 새 legacy 도 거부, 아무것도 끊지 않음; legacy 교체는 8 유지). 계정·client wire IP·전체 소켓 상한은 환경변수 — 양의 정수가 아니면 기본값과 경고.
- **지원 확인 (r4, Codex 결정 2)**: `GET /api/hosts` 가 `standby: {supported: true, relay: <relay 켜짐>}` 를 싣는다(부작용 없음; 옛 directory 엔 필드 없음 = 미지원). standby 요청에는 항상 `standbyRelay` 를 답한다 — `reserved` / 상한 이유 / `bind-failed` / `not-eligible` / `relay-disabled`. 클라이언트는 첫 mutating connect 로 지원을 알아내지 않고, 없음·timeout 을 지원/ready 로 보거나 자동 legacy Hello 로 넘어가지 않는다. 서버 enum `active` 는 Host 의 현재 owner 가 아니다 — 소유 판단에 쓰지 않는다.''')
rep('''R5(standby token 의 legacy Hello)는 기존 newest-legacy-token 규칙이 이미 거부해 생존.''',
    '''R5(standby token 의 legacy Hello)는 기존 newest-legacy-token 규칙이 이미 거부해 생존.

## 6. r4 (2026-10-26, Codex `codex_keepalive_r2_spike_r3.md` N1~N3 + 결정 1~3)
- 표 56/56(N2 반례 포함), e2e `--old-host --real-directory` 89/89 ×2(실 Host 가 proof 를 실은 punch 로 실 server.js lease 를 pin), directory suite 815 PASS rc0(`relay_standby_r4_test` 20, `relay_env_bound_test` 4).
- 부정 대조: N1a(IP 만으로 최초 pin) 5 FAIL / N1b(이동 시 새 proof 없음) 2 FAIL / N3a(legacy 미계수) 1 FAIL / N3b(r3 env 해석) 3 FAIL / N2a(앞 8바이트 비교) 1 FAIL / N2b(Stale 없음) 5 FAIL / N2c(digest 실패 무시) 1 FAIL.
- 아직: 실 NAT·운영 배포·포트 범위, 서버 LAN 의 `RELAY_HOST_FACING_IP`, ②의 클라이언트 상태기계(Pong 역순·손실·cancel), held input release 직접 반례, 비용.''')
if crlf:
    s = s.replace('\n', '\r\n')
open(p, 'wb').write(s.encode('utf-8'))

h = 'C:/work/gnlink-wt/ka-r3/docs/history.md'
t = open(h, 'rb').read().decode('utf-8')
c2 = '\r\n' in t
t = t.replace('\r\n', '\n')
t += '''
## 2026-10-26 keepalive-r2 A r4 — N1~N3 보수 (Codex `codex_keepalive_r2_spike_r3.md`)

- N1: relay 예약 소켓은 Host 전용 `hostProof`(pendingPunch, Host 에게만)를 실은 Host 의 punch 로만 최초 pin — 같은 IP 의 다른 소켓·틀린/다른 lease proof 는 변경 0; Host 이동 시 lease 마다 새 proof. Host 는 그 목적지 punch 에만 proof 를 싣고 capability 로 등록하지 않는다.
- N2: replay 판정은 capability 의 SHA-256 전체 비교(실패는 fail-closed), 같은 기기의 늦은/같은 gen 다른 proof 입장은 Stale(entry·secret 불변).
- N3: standby 8 뒤 새 legacy 도 거부(비파괴), env 상한은 양의 정수만. 결정 2: `/api/hosts` 의 standby metadata, standby 요청엔 항상 standbyRelay 이유.
- 시험: 표 56/56, e2e 89/89 ×2(실 server.js+실 Host), directory suite 815 rc0, 부정 대조 7 모두 FAIL. 운영 NAS 무변경, 자동 preconnect 출하 OFF.
'''
if c2:
    t = t.replace('\n', '\r\n')
open(h, 'wb').write(t.encode('utf-8'))
json.dump({'repo_path': R, 'files': ['docs/keepalive_host_standby_contract.md', 'docs/history.md']},
          open(D + 'kar4_d_add.json', 'w', encoding='utf-8'))
json.dump({'repo_path': R, 'message': """docs(keepalive-r2 A r4): the standby contract after N1-N3 and Codex decisions 1-3; history

Product / test / docs: product (none) / test (none) / docs (standby contract, history).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"""}, open(D + 'kar4_d_commit.json', 'w', encoding='utf-8'), ensure_ascii=False)
print('ok')
