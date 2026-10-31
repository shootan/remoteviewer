# report keepalive-r2 A — host standby admission spike — 작업용1 (2026-10-26)

**결론: 계약 초안 + 제한 spike 의 실호스트 실행 근거 확보. 고정 후보 `spike/host-admission-r2` 9f1fac1** (= fix/capture-lifecycle **569059f** 위 2 commit: 3d04ecf spike 이식 + 9f1fac1 첫 실행 보수, clean). 원래 `spike/host-admission` 328d616(82b3d14 위)은 보존. 클라이언트 배선·출하 경로 없음 — Codex 확정 전 자동 3대 재접속은 출하하지 않는다.

## 계약 (정본: worktree `docs/keepalive_host_standby_contract.md` §1~§4)
- 새 UDP kind 311~316 `StandbyHello/Ack/Ping/Pong/Activate/Release`(고정 104 B `UdpStandbyPacket`). 구 Host 는 이 kind 를 무시 → **StandbyHello 자체가 지원 probe**(답 없으면 '상시 유지 미지원', 자동 Hello 안 보냄).
- StandbyHello 는 디렉터리 capability 로만 인증(세션 token·인증 상태 무접촉), entry = entryId + CSPRNG 128-bit secret(주소 아님 — relay 공유 주소·peer 이동에도 구별). 기기당 1·Host 8, lease 20 s(Ping 갱신), Busy·Release·만료 회수, (기기, generation) 재전송 멱등.
- **세션 무접촉**: Standby* 처리가 모든 세션 처리보다 앞, adopted peer·epoch·control·stream·input·clipboard·file·LockCred 불변. **StandbyActivate 만** 인증 Hello 와 같은 AdmitHello 경로로 세션을 가진다(첫 세션: handshake 경로 / 이후: rollover). 옛 generation 의 늦은 Activate 는 Superseded 거부 → parked 기기는 자동으로 되찾지 않음, 새 사용자 행동(더 큰 generation)만.
- 제어권 전환: 기존 epoch rollover(control Close·Serve 종료의 held input release·옛 peer 지연 패킷 non-adopted 거부·새 keyframe). capture-lifecycle 의 UA-2 와도 맞물림: activation = 새 epoch → backend 요청은 호스트 기본으로(그 세션 소유).

## 시험 (이 PC 콘솔, SLOT_REQUEST CreateNew→삭제, 루프백, 설치본·서비스 무조작)
- `host_standby_table_test` 25/25 rc0.
- `host_standby_admission_e2e_test --old-host <9991b36 GNLinkStream>` **72/72 ×2 rc0**: 두 기기 대기 공존(세션 0) → A 명시 activation → B 의 ping·재 admission·control·늦은 activation 무간섭 → B 명시 takeover(새 epoch·keyframe) → A 옛 control 거부·A parked 유지 → A 옛 gen activation Superseded / 새 gen 만 되찾음 → 상한 8·Busy·Release·lease → relay 대역 2방식(클라이언트별 / 공유 host-facing 주소)에서 entry 구별·직결 A 무간섭 → 구 Host: StandbyHello 무응답·기존 세션 무간섭.
- 이번 첫 실행에서 고친 것(모두 시험 쪽, 제품 무변경): 첫 세션은 rollover 줄이 없어 기대값 -1 / 구 Host 경우 무응답 확인 8→3회(원시 클라가 control ACK 를 안 보내 구 Host 가 ~6 s 뒤 peer-lost — 시험 하네스 한계) / 실패 증거용 `REMOTE60_E2E_DUMP_HOST_LOG`. 또 capture-lifecycle UA-2 로그가 `session epoch=` 를 겹쳐 쓰던 것을 그쪽 569059f 로 고침.

## 결정이 필요한 것 (Codex)
1. **실 릴레이**: 서버는 호스트당 lease 1(`server.js:565`, `:1753` 새 connect 가 기존 drop) — 릴레이로 두 번째 기기가 붙으면 첫 기기의 릴레이가 끊긴다. 후보: ⓐ 클라이언트 세션별 lease + 세션별 host-facing 소켓(호스트가 주소로도 구별) ⓑ entry id 로 역방향 demux. spike 는 두 모양을 대역으로만 확인, 서버 무변경.
2. **wire 확정**: kind 311~316·104 B·status 값·lease 20 s·상한 8/기기 1 — 검증용 번호 대조(LK 89/0x80000·UA 90/0x100000 와 공간이 다름: UDP kind).
3. 보안 수준: entry secret 평문 UDP(기존 capability 와 같은 수준) 수용 여부.
4. 남은 반례: held input release 직접 반례(지금은 기존 Serve 종료 동작에 기댐), 비용 실측(§6, ②~④ 단계).

## 다음
Codex 확정 → ② shared control/Host 기반 + APK(작업용2 의 feat/keepalive-r2-apk 와 연결) → ③ PC managed Viewer. R1 통합 통지가 오면 이 spike 는 지금 commit 그대로 보존하고 통합으로 전환.

## r3 — S1~S4 보수 + 릴레이 (a) 로컬 구현 (2026-10-26, 작업용1)

**고정 후보: `spike/host-admission-r3` 933814f** (worktree C:/work/gnlink-wt/ka-r3, tracked clean).
- base: 9f1fac1 → 37302f1(rel 66bb5e9 병합) → WIP 4d34f44(S1~S3 표·wire·activation Pong) → **de208b9**(rel/0.2.163 최종 fae0e1f 병합; diff 에 host_standby_*/host_startup_* 없음)
- 위 4 commit: 3135724 시험(S4) · b66a229 **서버**(directory relay (a)) · 1307907 시험(실 server.js) · 933814f 문서(계약 §2·§2.7·§5, history).
- 제품 변경 범위: host 쪽은 WIP 4d34f44 그대로(host_standby_table.hpp·host_standby_wire.hpp·host_startup_connect/control.cpp·poc_protocol.hpp). 서버는 b66a229 의 apps/directory/server.js. 운영 NAS 무변경·배포 없음·자동 preconnect 출하 OFF.

### 판정별 대응 (Codex codex_keepalive_r2_spike_r1.md)
| 항목 | 보수 | 반례 시험 | 부정 대조 |
|---|---|---|---|
| S1 | replay = 같은 기기·gen + 같은 capability fingerprint + 같은 반환 끝점 + 유효 lease(Expire 먼저). 그 밖은 새 capability. 끝점 이동은 secret 있는 Ping | 표: 다른 capability / 다른 끝점 / 다른 socket+틀린 token(secret 0·entry 불변) / 같은 Hello 다른 끝점(답 없음) / lease 뒤 20~30 s replay / Ack 유실 재전송 / Ping 이동 | K1(기기+gen 만) 4 FAIL, K2(Expire 없음) 1 FAIL |
| S2 | entry 별 단조 high-water, 0 거부, 같은 gen 은 살아 있는 grant 만 | 표: 2→1(active 중)→B→2, 0→B→0, 같은 gen Ack 유실, 새 gen takeover | K3(r2 모양) 5 FAIL, K4(0 허용)=**등가**(high-water 0 은 같은-gen 경로에서 이미 Superseded) |
| S3 | grant 는 실제 세션 끝(epoch)과 함께, 늦은 이전 끝 무시, Live 보호(재 admission·Release), CSPRNG 결과 확인·유한 id 재시도 | 표 + e2e(A 세션 실제 종료 → 같은 gen 거부·parked·새 gen 새 세션) | K5 4 FAIL, K6 1 FAIL, K7 3 FAIL |
| S4 | relay 대역을 (a) 단일 모드로(공유 모드·승인 전 lastActivated 삭제) | e2e: B active 중 C 의 window-select·ACK·control NACK·video NACK·resume·bulk·틀린 secret·gen0 → 처리 0, C leg 에 비-Standby 0, C resume "peer=other" 무시, B epoch·control session 불변, B 응답은 B leg | KS4(C 의 세션 패킷만 B 의 host-facing 소켓으로) 2 FAIL. 첫 KS4(모든 패킷 공유)는 회신 라우팅이 깨져 admission 부터 실패 — 진단력 없어 좁힘 |
| 결정① (a) | 서버: 아래 | node 시험 + 실 server.js e2e | R1~R4·R6 FAIL, R5 생존(이유 아래) |

### 릴레이 (a) — apps/directory/server.js (b66a229, 로컬)
- `/api/connect {standby:true}`(추가, 기본 off) → 그 connect 전용 host-facing 소켓을 mint 때 예약, 응답 `standbyRelay: reserved | host-full | account-full | ip-full | server-full | bind-failed`(거부면 relay 후보 없음).
- 104 B StandbyHello 가 bind → provisional → (host StandbyAck Ok) ready → (같은 lease 위 activation HelloAck+DirectoryAuth) active. 새 connect 불필요.
- standby lease 끼리 공존·서로 무폐기. legacy lease(observe 소켓·newest-wins)는 그대로이고 standby 를 끊지 않음. class 교차(standby token→legacy Hello, legacy token→StandbyHello) 거부.
- **NAT 경로, 호스트 변경 없음**: heartbeat pendingPunch 에 예약마다 `RELAY_HOST_FACING_IP:<예약 포트>` + **아무 클라이언트도 갖지 않는 token** 1항목 추가 → 호스트가 기존대로 punch → 예약 소켓이 호스트 주소의 첫 datagram tuple 을 pin(포트 무관), 그 밖 거부, heartbeat tuple 로 덮지 않음(호스트 이동 시 pin 해제). 클라이언트 token 중복 게재는 일회용 capability 2회 사용을 허락하므로 피함.
- 상한: 호스트당 8(legacy 포함), 계정/wire IP/전체 소켓(env). 회수: drop 이 소켓·색인·capability 함께, 예약 TTL 60 s·provisional 30 s·무음 60 s·closeHostsNow.
- **클라이언트 계약(②/③ 에 전달 필요)**: client-facing 소켓은 공유 → host lease 하나에 UDP 소켓 하나. 여러 host 를 한 소켓으로 다중화하지 말 것.
- R5 생존 이유: standby token 은 그 호스트의 최신 legacy token 이 될 수 없어 기존 `superseded-token` 규칙이 먼저 거부한다(class 검사는 이중 방어).

### 실행 (이 PC 콘솔, qwinsta console Active / RDP 없음, SLOT_REQUEST CreateNew→삭제, loopback, 사용자 GNLinkStream pid 2456 무조작)
| 시험 | 구성 | 결과 | exe sha256 |
|---|---|---|---|
| `remote60_host_standby_table_test` | — | 47/47 rc0 | `cc796bf1362a33961c15379beef86a7c924e43ef75b5439f029bc2756a6aa583` |
| `remote60_host_standby_admission_e2e_test` | `--old-host <fix-capture-lifecycle-baseline\Release\GNLinkStream.exe 2597f7e7…> --real-directory` | **89/89 ×2** rc0 | `26112644cda0d467411e7fc0fa950f736d89920275d948aee03da2d7b29a33be` |
| (그 안의 GNLinkStream) | build/apps/native_poc/Release | — | `1ab576ebca8faeac7d08d2dcb4d9317bcaf6385d30c6d2a669a8a767fc6d166c` |
| `apps/directory/test/run.js` | node, 전 suite | 791 PASS rc0, `RESULT: ALL PASS` (relay_standby_test 24 포함) | — |
- 명령: `.claude/caplife/ka_e2e_run.ps1 -Repeat 2 -Tag r3final -ExtraArgs '--real-directory'`(빌드→표→슬롯→e2e), `node apps/directory/test/run.js`, 부정 대조 `ka_mut_run.ps1`(K1~K7)·`ka_ks4_run.ps1`(KS4)·`ka_srv_mut.py` + `run_standby.js`(R1~R6). 출력 `.claude/caplife/ka_e2e_r3final/`, `ka_mut/`, `ka_dir_suite.txt`.
- 개수 변천(구성 다름, 대조 금지): r2 72(직결+relay 클라이언트별+relay 공유+구 Host) → WIP 64(공유 삭제, S3/S4 추가; 구 Host 포함) → 67(S4 확장) → 89(실 server.js relay 22 추가).
- 같은 소스 재빌드에서 e2e exe 해시가 바뀜(DB9E…→B214…→5E6D…; 비결정 빌드) — 고정점은 위 최종 해시.
- 실 server.js e2e: node(PATH)로 repo 의 server.js, 데이터·로그는 staging, job object 로 종료. 호스트가 예약 포트 2개에 punch → 두 lease pin → B·C 각자 entry(bound+ready 2) → B activation(lease active)·control → C ping·C 세션 패킷 무처리·B epoch 불변 → C 명시 takeover → B 늦은 control 거부·B parked ping Ok.
- 시험 hook 감사: `StandbyTable` 의 `Rng` 주입은 시험만 — 제품 생성은 host_session.hpp:160 `StandbyTable standby;`(기본 CSPRNG). 이번 r3 에 제품 시험 hook 추가 없음.

### Codex 확정이 필요한 것 (제안)
1. activation 결과 상관: 수락 시 HelloAck 직전 `StandbyPong`(entry + 그 gen + `kUdpStandbyPongActivationGranted`), 거부 Pong 에 거부한 gen. 새 kind 없음. (결정② 미동결 부분)
2. 서버 wire/상태: `/api/connect` `standby` 필드·`standbyRelay` 응답, reserved/provisional/ready/active, pendingPunch 추가 entry(별도 token) 방식의 NAT 경로, 상한 값.
3. 클라이언트 계약: host lease 당 UDP 소켓 1.

### 아직 아님
실 NAT(포트 제한·symmetric) 경로·배포·방화벽 포트 범위, 서버 LAN 호스트의 `RELAY_HOST_FACING_IP`(hairpin), held input release 직접 반례(누른 키 상태 takeover), LockCred/clipboard/file 무간섭 독립 검사, APK+PC 2기기 실 relay, 서버 재시작·5분 초과 유지·비용 실측(②~④), 클라이언트 배선. 하지 않은 것: 서명·게시·push·NAS 변경.

### r3 보충 (정확도)
- **S4 증거 범위**: KS4 가 실패시킨 것은 2건(C 의 control data 처리, B 세션 교란)뿐이다 → **control data/ACK gate 는 KS4 로 load-bearing 확인**. control NACK·video NACK·resume·bulk 는 보냈고 처리 0 이지만, 이 하네스에는 양성 대조가 없다(GNLinkCapture 가 thumbnail stub 라 재전송할 영상 없음, bulk 미협상, B 주소의 resume 도 control serving 중엔 거부) → **판별력 없음, 증거로 세지 않음**.
- **병합 commit**: de208b9 는 git CLI `git merge`(mcp-server-git 에 merge 도구 없음, 37302f1 과 같은 방식). 그 밖 commit 은 Git MCP.
- **독립 실행 시 재빌드 금지 권고**: `ka_e2e_run.ps1` 은 실행 전 빌드한다(같은 소스 재빌드도 해시가 바뀜). 고정 exe 로 직접: `set REMOTE60_ALLOW_HOST_E2E=1` 후 `C:\work\gnlink-wt\ka-r3\build\apps\native_poc\Release\remote60_host_standby_admission_e2e_test.exe --old-host C:\work\gnlink-wt\fix-capture-lifecycle-baseline\Release\GNLinkStream.exe --real-directory` (sha256 26112644…, node 가 PATH 에 있어야 함). SLOT_REQUEST 규칙 동일.
- **구 Host 신원**: 2597f7e7… 는 `fix-capture-lifecycle-baseline` 의 빌드다. r2 보고의 "9991b36" 과 같은 commit 인지는 **재확인하지 않았다** — 해시만 기록.
- **클라이언트 계약 추가**: relay listener 는 legacy lease 끝점에서 온 104 B Standby* 를 이제 **버린다**(이전엔 opaque 로 전달됐을 것). → relay 경유로는 기존 수동 연결 위에서 standby 지원을 probe 할 수 없다. relay 는 자기 소켓의 standby connect 로만, 직결은 영향 없음. ②(APK) 의 probe 설계에 반영 필요 — Codex 확정 항목 3 에 포함.
- **후보 head 갱신**: 위 계약 한 줄로 **93b15f6**(933814f 위 docs 1건, 제품·시험 diff 0). 시험 결과·해시는 그대로 유효.

## r4 — Codex N1~N3 보수 + 결정 1~3 반영 (2026-10-26, 작업용1, APK 0.2.32 뒤)

**고정 후보: `spike/host-admission-r3` f4ddc9a** (93b15f6 위 5 commit, tracked clean)
- 7d0a10c **Host**: N2 표·wire, N1 Host 절반(directory_client punch proof).
- 7ff92d5 **서버**: N1 서버, N3.
- f410f7d **시험**.
- 9bbff8a **서버**: 결정 2 metadata·거부 이유 + 그 시험.
- f4ddc9a **문서**: 계약 §2.2·§2.7·§6, history.

### 보수
- **N1**:
  - 예약마다 Host 전용 `hostProof` 를 둔다. pendingPunch 항목으로 Host 에게만 전달한다.
  - 예약 소켓은 **Host 주소에서 온, 현재 proof 를 실은 Punch 만** pin 한다.
  - Host 이동 시 lease 마다 새 proof 를 그 heartbeat 로 준다. 옛 proof 재생은 무효다.
  - Host(directory_client):
    - pendingPunch `hostProof`(32 hex)를 파싱한다.
    - 그 목적지 punch 에만 proof 를 싣는다. 다른 punch 는 비우므로, 클라이언트 token 이 선로에 오르지 않는다.
    - proof 항목은 authorizedPeers 에 넣지 않는다.
  - 옛 Host 는 proof 를 못 실으므로 standby relay pin 이 안 된다 — standby 는 새 Host 전용이다(의도).
- **N2-a**: replay 증거는 capability 의 SHA-256(BCryptHash) 전체 비교다. 계산 실패는 replay 아님·입장 없음·capability 미소비.
- **N2-b**: 같은 기기의 gen ≤ 현재 admitGen 입장은 `Stale` 이다 → Superseded, secret 0, entry·secret 불변. 클라이언트는 gen 을 재시작을 넘어 단조로 유지해야 한다(② 계약으로 기록).
- **N3**:
  - 새 legacy bind 도 호스트 8(standby 포함)을 지킨다. 거부는 아무것도 끊기 전에 한다. legacy 교체는 8 을 유지한다.
  - env 상한은 양의 정수만 받는다. 아니면 기본값과 경고(`relayEnvBound`).
- **결정 2 서버 쪽**:
  - `GET /api/hosts` 에 `standby: {supported: true, relay}` 를 실었다(부작용 없음, 옛 서버엔 없음).
  - standby 요청에는 항상 `standbyRelay` 를 답한다(`not-eligible`/`relay-disabled` 추가).
- **결정 1·3**: 계약 문서에 반영했다. 클라이언트 쪽 상태기계는 ② 범위.

### 시험 (이 PC 콘솔, RDP 없음, SLOT_REQUEST CreateNew → 삭제, loopback, 사용자 GNLinkStream 무조작)
| 시험 | 결과 | exe / 비고 |
|---|---|---|
| `remote60_host_standby_table_test` | **56/56** | `98d4024b…` |
| `host_standby_admission_e2e_test --old-host <baseline 2597f7e7…> --real-directory` | **89/89 ×2** | e2e `f82c09b9…`, GNLinkStream `cde789d4…`(N1 Host 변경 포함) |
| directory `test/run.js` | **815 PASS rc0** | `relay_standby_test` 24, `relay_standby_r4_test` 20, `relay_env_bound_test` 4 |

- 실 server.js e2e 에서 두 lease 가 pin 된 것 = 실제 Host 가 proof 를 실은 punch 를 보냈다는 근거다(N1 코드에서는 다른 경로로 pin 되지 않는다).
- 9bbff8a(서버 metadata·거부 이유)는 e2e 뒤 커밋이다. 대상 standby 흐름은 바뀌지 않았고, suite 815 는 그 뒤에 실행했다.
- **N1 반례**:
  - 같은 IP stranger 가 **Host 보다 먼저** proof 없음 / 틀린 proof / 가짜 StandbyAck → 미pin. 클라이언트는 아무것도 받지 않고, stranger 는 Hello 를 받지 못한다.
  - Host proof → pin. 그 뒤 stranger 는 무효다.
  - 다른 lease proof → 미pin. 자기 proof → pin.
  - Host 이동 → 새 proof 2개. 옛 proof 재생은 미pin(ping 무응답), 새 proof 는 pin(ping 응답).
- **N3 반례**:
  - standby 8 → 새 legacy Hello 거부(Host 에 Hello 없음). 8개 모두 ping 유지.
  - legacy 1 + standby 7 → 8번째 standby 는 host-full. legacy 교체는 허용, 여전히 host-full.
- **부정 대조**(각자 FAIL):

| mutant | 바꾼 것 | FAIL |
|---|---|---|
| N1a | IP 만으로 최초 pin | 5 |
| N1b | 이동 시 새 proof 없음 | 2 |
| N3a | legacy 미계수 | 1 |
| N3b | r3 env 해석 | 3 |
| N2a | 앞 8바이트 비교 | 1 |
| N2b | Stale 없음 | 5 |
| N2c | digest 실패 무시 | 1 |

- 명령: `ka_e2e_run.ps1 -Repeat 2 -Tag r4 -ExtraArgs '--real-directory'`, `node apps/directory/test/run.js`, `ka_r4_mut.py` + `run_standby.js`(서버) / `ka_r4_mut_run.ps1`(표). 출력 `.claude/caplife/ka_e2e_r4/`, `ka_r4_mut/`, `ka_r4_dir_suite.txt`.
- 독립 실행 시 재빌드 금지: 고정 exe 로 직접 실행(위 해시).

### 아직
- 실 NAT·운영 배포·포트 범위·서버 LAN 의 `RELAY_HOST_FACING_IP`.
- ② 클라이언트 상태기계(Pong 역순·손실·cancel, gen 단조 영속, lease 당 socket).
- held input release 직접 반례, 비용.
- 운영 NAS 무변경, 자동 preconnect 출하 OFF 유지.


## r5 — Codex N4: 시험이 저장소 밖을 지우던 경로 (codex_keepalive_r2_spike_r4.md, 2026-10-27)
**고정: `spike/host-admission-r3` 357ee5e** (f4ddc9a 위 1 commit, 시험 코드만: `apps/directory/test/` 7파일, 제품 파일 변경 0 — `git diff --name-only f4ddc9a 357ee5e` 전부 test/).

### ① 이전 실행에서 실제로 지운 경로 (사실대로)
- 대상은 전부 `%TEMP%` = `C:\Users\shota\AppData\Local\Temp` 의 **그 실행 프로세스 pid 이름 경로**다: `remote60-directory-test-<pid>.json`, `remote60-directory-logs-<pid>\`, `remote60-update-<pid>\`, `remote60-envbound-<pid>.json`(run.js·relay_env_bound_test.js), 그리고 run.js 가 부르는 child 의 `remote60-observe-contract-<pid>.json`·`remote60-proxy-test-<pid>.json`·`remote60-connect-diag-<pid>.json`·`remote60-wake-resend-<pid>.json`·`remote60-wake-control-<pid>.json`. 또 내 미추적 보조 `caplife/run_standby.js` 가 `remote60-standby-test-<pid>.json` 을 같은 방식으로 지웠다.
- 실행: 저장된 suite 2회(`ka_dir_suite.txt` 10-26 20:36, `ka_r4_dir_suite.txt` 23:07) 외에 개발 중 run.js 재실행·relay_env_bound_test 단독/변이 실행·run_standby.js 실행이 여러 번 있었다(대화 기록 기준, 정확한 횟수·pid 는 기록되지 않음).
- 각 경로는 같은 프로세스가 시작 시 force 삭제 → 생성 → 종료 시 삭제한 자기 파일이다. **pid 를 남기지 않아, 시작 시 같은 이름의 남의 파일이 있었을 가능성은 기록으로 배제할 수 없다**(이름 충돌일 때만 해당). 지금 `%TEMP%` 의 `remote60-*` 항목 0개.
- 추가 바깥 cleanup 은 하지 않았다.

### ② 보수 (357ee5e)
- `test/test_tmp.js`: 루트 = `REMOTE60_TEST_TMP`, **`.claude/test-tmp/<이름>` 바로 아래 폴더만**(만들기 전 경로 모양 검사 + 생성 뒤 실경로 재검사). `file()`·`remove()` 는 실경로(링크면 거부)가 루트 안일 때만 동작. 루트 미설정이면 suite 가 아무것도 쓰기 전에 멈춘다.
- `run.js`: 데이터·로그·업데이트 경로를 루트 아래로, 모든 child 에 `REMOTE60_TEST_TMP` + `TEMP/TMP/TMPDIR`=루트 → os.tmpdir() 를 쓰는 child 도 루트 안에 머문다. scratch 폴더 시험 4개(recovery_fault·relay_wire·account_admin·device_credential)는 설정 시 그 루트를 따른다(미설정이면 예전대로 자기 worktree `.claude/test-tmp`).
- 내 보조 `caplife/run_standby.js` 도 같은 가드로 옮겼다(재실행 relay_standby_r4_test ALL PASS).

### 확인
- 실행: `REMOTE60_TEST_TMP=D:\remote\remote\.claude\test-tmp\ka-r5-n4`, `node test/run.js` → **rc0, `^PASS` 815, `^FAIL` 0, 마지막 줄 `RESULT: ALL PASS`** (`caplife/ka_r5_dir_suite.txt`; r4 와 같은 구성·같은 개수). 실행 중 `%TEMP%` 에 새로 생기거나 바뀐 항목 0.
- 부정 대조(실제로 거부됨): 루트 미설정 → run.js rc1(아무것도 안 씀) · `%TEMP%\remote60-x.json`·루트 밖 `..\other.json`·루트 자체 삭제 → 3건 모두 거부 · `C:\work\...\scratchY` 를 루트로 → 거부, 폴더 생성 안 됨.
- ⚠️ 가드 첫 판은 루트 모양 검사 전에 mkdir 해서, 부정 대조 중 **빈 폴더 `C:\work\gnlink-wt\ka-r3\scratchX` 를 만들었다**(내가 만든 빈 폴더, 저장소 밖이라 지우지 않고 둠 — 사용자 판단). 순서를 고친 뒤 같은 시험에서 폴더가 생기지 않음을 확인했다.
- 루트 안에 남은 것: 기존 scratch 시험이 정리하지 않는 `account-admin-*`·`device-credential-*`·`server-recovery-*` 폴더와 `standby-server.log`(루트 안, 그대로 둠).
- TEMP/TMP=루트를 주면 `os.tmpdir()` 가 루트를 돌려준다(child 경로 근거).
- 다음: ② 범위(monotonic counter 내구 예약·Pong/HelloAck 역순/손실/cancel·lease 별 socket·이동 proof 응답·Punch 유실 bounded 회수).


## r6 — Codex N4 a/b (codex_keepalive_r2_spike_r5.md, 2026-10-27)
**고정: `spike/host-admission-r3` cf8fb70** (357ee5e 위 1 commit, `apps/directory/test/` 5파일만, 제품 변경 0).
- a) `test_tmp.root()`: `REMOTE60_TEST_TMP` 는 **신뢰 anchor `D:\remote\remote\.claude\test-tmp` 바로 아래 한 폴더**만 허용한다.
  - anchor 는 존재하고, 링크가 아니며, 실경로가 자기 자신이어야 한다(조상에 junction 없음).
  - 생성 전에 이름을 검사하고(한 단계 mkdir, anchor 안에서만), 생성 후에 실경로를 다시 검사한다.
  - 모양만 맞는 바깥 경로나 링크는 거부한다.
- b) child 4개(recovery_fault·relay_wire·account_admin·device_credential)는 `testTmp.scratchDir()` 로만 폴더를 받는다. raw 변수와 worktree fallback 을 없앴고, 정리는 `testTmp.remove()` 로 한다.
- 제한 대조(첫 쓰기 전 거부, 바깥 생성·삭제 없음):
  - ① 모양 맞는 바깥 루트 `C:\work\gnlink-wt\ka-r3\.claude\test-tmp\ka` → 거부, 생성 안 됨.
  - ② anchor 안 junction `ka-r6-link` → `D:\...\.claude\caplife` → "the test root is a link" 로 거부. junction 은 내가 저장소 안에 만들고 `rmdir` 로 링크만 지웠으며, 대상 caplife 는 온전하다.
  - ③ child 단독 실행(변수 없음) → rc1, worktree test-tmp 항목 12 → 12.
- 양성: anchor 루트 `ka-r6-n4` 로 recovery_fault ALL PASS · account_admin 113/0 · device_credential 83 · relay_wire(fixed) PASS. 815 전량은 지시대로 다시 돌리지 않았다.
- scratchX 는 그대로 둔다(사용자 판단 대기).


## ② 범위 제안 (작업용1, 2026-10-27) — shared native 클라이언트 + activation 경로
근거: Codex r4 "② 구현 GO"(r6 재확인), 계약 §2.2 activation Pong 상관·§2.6 구 Host·§2.7 클라이언트 계약·§3, Codex r4 "운영/출하 전" 목록. 자동 preconnect 출하 OFF·NAS 무변경·legacy Hello 자동 fallback 없음은 그대로 유지한다.

### 구현할 것 (ka-r3, 단일 writer)
a. **순수 `StandbyEntryClient` 상태기계**(소켓 없음). 상태: Idle → HelloSent → Parked(Ping) → Activating → Active / Released / Unsupported / Failed. 입력은 Ack(Ok/Busy/Superseded/Unknown), Pong(entry·gen·status·Granted), HelloAck, 시간, 사용자 취소, release.
   반례로 시험할 것:
   - Pong→HelloAck 순서, HelloAck→Pong 순서 모두 완료한다.
   - HelloAck 가 유실되면 **같은 gen** 으로 Activate 를 재전송한다(grant 가 살아 있는 동안 Host 가 HelloAck 를 다시 보낸다).
   - Pong 유실.
   - bare HelloAck 만으로는 완료하지 않는다.
   - 취소 뒤 늦은 Pong/HelloAck 는 무시한다(그 gen 은 죽은 것).
   - 다른 entry/gen 의 Pong 은 무시한다.
   - Busy 면 backoff 하고 자동 Hello 는 보내지 않는다.
   - Ack 없음이 유한 재시도 뒤에도 계속되면 Unsupported 로 간주하고 legacy Hello 는 보내지 않는다.
   - Ping 이 Unknown 이면(Host 재시작·만료) 새 capability 로 재입장한다.
   - **proof 유실**(Host 이동 뒤 relay 가 pin 하지 못함): Ping 무응답이 유한 시간 이어지면 해제·재입장하고, ready 로 남지 않는다.
b. **단조 generation 저장 인터페이스**(`GenerationStore`). 재시작을 넘어 감소하지 않는다. 플랫폼(APK SharedPreferences, PC 파일)이 구현을 제공한다. 16 B 기기 id 도 같은 저장소에 둔다.
c. **소켓 소유**: host lease 하나에 UDP 소켓 하나다.
   - relay 경로: `/api/hosts` 의 `standby.supported` 를 먼저 확인한다. 없음·timeout 은 미지원으로 본다. 그 소켓의 `standby:true` connect 를 쓴다.
   - 두 host 를 한 소켓으로 다중화하지 않는다.
d. **`ClientSessionController` activation 모드**: lease 의 소켓(prepared)으로 Hello 대신 StandbyActivate 를 보내고, Pong(Granted)과 HelloAck 를 둘 다 받아야 연결된 것으로 본다. 그 뒤는 기존 control 경로 그대로다. 새 세션 유형이 아니라 connect 모드다.
   e2e: 제품 클라이언트 둘이 실 시험 Host 에 parked → A activation → B 의 ping 은 A 무간섭 → 명시 takeover. 기존 89/89 와 같은 순서를 **제품 클라이언트로** 실행한다(`sh_run.ps1 -NativeExe`).
e. 그 뒤 APK 배선. 출하 스위치 OFF 뒤에 둔다.

### 정해야 할 것 (제 제안)
1. **브랜치**: shared core·Host 는 ka-r3 에서 진행한다. APK 배선은 작업용2 준비분(`feat/keepalive-r2-apk` 6d85ce2, base 82b3d14 — main 보다 뒤)을 `fix/apk-prelogin-select` 병합 뒤의 새 0.2.33 브랜치에서 합친다. KA 를 위해 `fix/apk-prelogin-select` 는 건드리지 않는다.
2. **activation 취소**: HelloAck 전에 사용자가 취소하면 클라이언트는 늦은 세션을 채택하지 않는다. 이미 grant 가 났을 수 있으므로 **StandbyRelease 를 보내지 않고** 그 세션이 peer-lost 로 끝나게 둔다(Release 는 Live 면 Host 가 거부한다). entry 는 parked 로 돌아오고, 다음 activation 은 더 큰 gen 으로 한다.
3. **gen·기기 id 저장**: shared 는 인터페이스만 두고 구현은 플랫폼이 한다. 손상·누락이면 새 기기 id 로 다시 시작한다(옛 entry 는 lease 만료로 사라진다).
4. **폰 범위**: 10-26 기본대로 앱 화면에 보이는 동안만 유지한다(onStop 해제). APK 배선은 이 기본을 따른다.
5. ②에서 실측하는 것: 상태기계·activation e2e·소켓/generation. §6 비용, §7 교차 기기, 실 NAT 는 ③④ 범위로 미검증 표기한다.


## ② 진행 (2026-10-27) — shared native 클라이언트·activation 경로
**고정: `spike/host-admission-r3` 92a689a** (cf8fb70 위 3 commit, tracked clean).

| 커밋 | 범위 | 내용 |
|---|---|---|
| a5a70cd | 제품+시험 | a·b `StandbyEntryClient`, `GenerationAllocator` |
| 6816e4d | 제품 | c·d `StandbyLease`, `ClientSessionConnectArgs::udpHandshake` |
| 92a689a | 시험 | e2e `--product-client` |

- 앱에서는 아직 아무것도 이 코드를 부르지 않는다(자동 preconnect 출하 OFF). 기존 Hello 경로는 `udpHandshake` 가 비어 있으면 그대로다.

### a·b — 순수 상태기계, 단조 generation
- 상태 흐름: Idle → Admitting → Parked(Ping 5 s) → Activating → Active → (세션 끝) Parked.
- Waiting: Busy·Superseded·Lost 일 때 1→2→4→8→16→30 s 기다린 뒤 **새 capability** 를 요청한다.
- Unsupported: Hello 6회에 무응답이면 이 상태다. 자동 connect 와 legacy Hello 를 하지 않는다.
- activation 완료 조건: **이 activation 의 Pong(Granted) + HelloAck**, 순서 무관.
  - 답이 유실되면 **같은 gen** 으로 재전송한다.
  - bare·늦은·취소된·다른 entry/gen 의 답은 아무것도 채택하지 않는다.
  - 취소 시 Release 는 보내지 않는다(Live 면 거부된다). 다음 activation 은 더 큰 gen 이다.
- Ping 이 16 s(lease 20 s 미만) 동안 무응답이면 Lost 다. relay 가 이동 뒤 proof 를 잃은 경우도 여기에 해당하며, ready 로 남지 않는다. Unknown 도 Lost 다.
- `GenerationAllocator`: 입장·activation 이 공유하는 단조 카운터다.
  - 사용 **전에** 저장하고, 재시작 뒤에는 그 위에서 계속한다.
  - 저장 실패면 송신 0. 저장값이 없거나 손상이면 새 기기 id 를 만든다.
- `standby_entry_client_test` **46/46**. 변이 7종 모두 FAIL: bareAck, anyGen, noDead, legacy, newGenRetx, noSave, busyAtOnce.

### c·d — lease 소켓, activation connect 모드
- `StandbyLease`: lease 하나에 UDP 소켓 하나(두 host 를 한 소켓에 다중화하지 않음)와 `StandbyEntryClient` 를 묶는다.
  - 소유자가 Pump 하며, 자체 스레드는 없다.
  - `ActivationArgs()` 는 소켓을 `ClientSessionController` 에 넘기고, Hello 대신 Activate 핸드셰이크를 실행시킨다. 그 뒤는 세션의 기존 경로다.
- e2e `--product-client` (실 GNLinkStream + fake directory, `sh_run.ps1 -NativeExe`) **35/35**:
  - A·B 가 parked 이고 세션은 0 이다.
  - A 가 activation 으로 연결된다(first session by activation, 영상, window list).
  - B 가 12 s ping 하는 동안 새 세션 0, control 종료 0, A 는 연결 유지.
  - B 가 takeover 한다(새 epoch, 영상).
  - A 의 세션이 끝나고, A 는 새 소켓으로 parked 된 뒤 더 큰 gen 으로 되찾는다.
  - 호스트의 모든 세션이 activation 에서 나왔다(ordinary Hello 0).
  - 구 Host 는 Unsupported 이며 세션 0 이다.
- 기존 suite `--old-host --real-directory` **89/89**(회귀).
- 변이(세션이 handshake 를 무시하고 항상 Hello) → 4 FAIL.
- e2e exe 48c55003…, 구 Host `fix-capture-lifecycle-baseline\Release\GNLinkStream.exe`.

### 아직 아님
- **relay 경로**: `/api/hosts` 의 `standby.supported` 확인, lease 소켓의 `standby:true` connect.
- **세션 뒤 소켓 유지**: 지금은 새 소켓을 쓴다. relay 에서는 재입장이 필요하다.
- **e APK 배선**: 브랜치 결정(제안 ①) 대기.
- **§6 비용, §7 교차 기기, 실 NAT**: 미검증.


### ② 진행 2 — relay 경로·세션 뒤 소켓 유지 (2026-10-27)
**고정: `spike/host-admission-r3` 75e37d1**. tracked clean.

| 커밋 | 범위 | 내용 |
|---|---|---|
| 60a6951 | 제품 | relay 경로 |
| 625b39f | 시험 | 실 server.js relay e2e |
| 30ef913 | 제품 | 소켓 유지 |
| 75e37d1 | 시험 | 소켓 유지 확인 |

- **relay 경로 (60a6951)**:
  - `directory_standby_support()`: `/api/hosts` 의 standby{supported, relay} 를 부작용 없이 읽는다. 옛 서버·timeout·오류는 미지원으로 본다.
  - `directory_connect(..., standby)`: `standbyRelay` 와 relay 후보 주소를 받는다.
  - `StandbyLease` 는 비연결 소켓을 쓴다. CapabilityFn 이 그 소켓으로 observe·standby connect 를 하고 **겨눌 주소**(reserved 면 relay, 아니면 Host)를 돌려준다. 그 주소에서 온 datagram 만 읽는다.
- **세션 뒤 소켓 유지 (30ef913)**: 세션에는 **복제 핸들**(WSADuplicateSocket/dup)을 준다. lease 는 자기 핸들을 유지하고 세션 동안 읽지 않는다. 세션이 끝나면 같은 끝점으로 Ping 을 이어 가므로, relay lease 가 끊기지 않는다. 앞 절의 "새 소켓·relay 재입장" 한계는 이로써 닫혔다.
- **e2e `--product-client --real-directory --old-host` 54/54** (`sh_run.ps1 -NativeExe`, e2e exe 662c8eab…):
  - 직결 35.
  - 실 server.js relay:
    - 제품 directory_login/list/standby support.
    - lease 소켓마다 standby connect 와 relay lease 예약.
    - A·B 가 relay 로 parked.
    - A 가 relay lease 위에서 activation(영상, relay 가 active 로 표시).
    - B ping 은 A 무간섭.
    - B takeover.
    - A 세션이 끝난 뒤 **같은 소켓으로 parked 유지**(loss 0, 새 connect 0), 같은 relay lease 로 회수(영상).
  - 구 Host 는 Unsupported.
- 변이:
  - standby 플래그 없이 connect → relay 사례 FAIL.
  - lease 가 소켓을 넘기고 새로 연다 → "loss 0" FAIL.
- 단위 46/46 그대로.
- 아직: e APK 배선(브랜치 결정 ①), Host 이동 뒤 proof 재증명의 실 relay 반례(서버 쪽 r4 시험에 있음 — 클라이언트는 Ping 16 s 무응답이면 Lost 로 단위 시험), §6/§7, 실 NAT.


### ② 진행 3 — e 준비 (2026-10-27): APK 브랜치, runner, 비동기 capability, JNI
**브랜치 결정 ①(검증용)**: `feat/apk-0.2.33`(C:\work\gnlink-wt\feat-apk-0233) = b554e76 + merge 75e37d1 + merge 6d85ce2. main 보다 docs 1 commit(2137f3a) 뒤.
- **feat/apk-0.2.33 1c2116d**: merge 90115a2(history 충돌만) · 8731255(5 파일 해소) · d62df9a · 9f019e5 · 1338930 · c7a7f2e · 94aeee8 · 1c2116d.
  - ⚠️ 8731255 에서 6d85ce2 가 추가한 `android_decoder_binding_test.cpp` 가 빠졌다(내가 test 파일을 unstage). d62df9a 에서 그 내용 + [2b] 로 넣었다.
  - 해소 내용:
    - decoder 는 binding token(*As) 형태를 유지하고, apk-0232 로컬 선택 4함수도 owner 검사(*As)로 만들었다. entry_sink_adapter 가 token 으로 전달한다.
    - ua2 JNI 2개는 binding 인자를 받는다.
    - MainActivity: Apply 는 backend 를 재전송하지 않는다. commit·park 재사용 때 backend 선택을 reset 한다. 설정 poll 은 bound entry 에 질의한다. desktopBackendConnected 는 active entry 기준이다.
  - merge 커밋은 git CLI(두 parent), 나머지는 Git MCP 다.
- **병합 APK 회귀(배선 전)**: noseam 9dbd492d…, shseam 0853297e…
  - `method_ui` 16, `ua2` 13, `ua2_desktop` 9, `prelogin_ui` 15, `layout_b` 47, `touch_ab` 36 — 모두 PASS.
  - 단위 290/0, native(binding 17, prelogin select 60, role park 13, pending slot 10, registry/table/park/gate PASS).
- **ka-r3 (fc1fd46 → 0a5e71d)**:

| 커밋 | 내용 | 시험 |
|---|---|---|
| b851956 | Release: parked 면 StandbyRelease 1회 → Idle. active·activating 면 보내지 않는다(Live) | 51/51, 변이 2 FAIL |
| 31feb28·26b71f7 | `StandbyRunner`: PC 마다 lease + pump 스레드, StatusOf 스냅숏, TakeActivation/SessionOver, Release. GenerationAllocator 잠금 | e2e runner 단계 |
| ddc07a4 | `standby_directory.hpp`: lease 소켓으로 observe + standby connect, relay(reserved)/Host 조준. e2e relay 가 이 제품 helper 를 쓴다 | — |
| fc1fd46 | 비동기 capability: lease 가 needsCapability → 앱이 그 lease 소켓으로 Observe, Kotlin 이 connect → Provide | e2e runner 단계가 APK 방식으로 실행 |
| 0a5e71d | `FileGenerationStore`(20 B, tmp → 교체; Windows 는 MoveFileEx) | 파일 시험 56/56(REMOTE60_TEST_TMP 안에서만, 자기 파일 정리), Windows std::rename 변이 4 FAIL |

  - e2e `--product-client --real-directory --old-host` **63/63**(runner 포함). runner 단계:
    - Release 는 Host 가 세션 끝을 본 뒤에 보낸다.
    - 끝을 보기 전의 Release 는 Live 로 거부되고, entry 는 lease 로 소멸한다(계약 §2.3). 첫 실행에서 실제로 이 동작이 보였다.
  - product-client 실행은 REMOTE60_E2E_HOST_SECONDS=420 이다(90 s Host 가 실행 중 끝나 실패한 적 있음).
- **feat 1c2116d — standby JNI**:
  - nativeStandbyInit(filesDir), Start, Status[exists, state, lost, handedOff, needsCapability], Observe(lease 소켓), Provide, Release, ReleaseAll.
  - 호출처 0(`SEND_TO_REAL_HOSTS=false`).
- **결정 대기**: kept 항목 모델 A/B(seq 14381). considerPreconnect·탭 activation 배선은 그 뒤다.



### ② r3 — Codex `codex_keepalive_r2_scope2.md` N1–N3 (2026-10-28)
**커밋** (ka-r3 `spike/host-admission-r3`):
- **03716ec** — N3 + N1 공통 판정.
- **68c8272** — N2 + N1 lease.
- feat/apk-0.2.33 에 **a629942** 로 merge(git CLI, 두 parent).
- 두 커밋 모두 제품 헤더·시험·CMake 만 바꿨다. NP 소유 파일은 건드리지 않았다.

**N1 — 활성화 HelloAck**
- `udp_hello_ack_acceptable()` (`native_video_client_tcp_control.hpp`): 일반 Hello 의 ack 판정을 **그대로 뽑아낸 것**이다. 판정 대상: 크기·magic·kind·version·VideoFec, 디렉터리 인증이면 DirectoryAuth.
  - 일반 핸드셰이크는 이 함수를 호출하도록 바뀌었을 뿐 식은 같다.
- lease 의 Receive 는 이 함수(DirectoryAuth 필수)에 **선언 size == sizeof** 를 더해 검사한다. 통과하지 못한 ack 는 통째로 버린다(state 도 features 도 바뀌지 않음).
- 무태그 Ack 계약(lease 헤더 주석에 명시):
  - HelloAck 에는 generation 이 없다. 어느 grant 인지는 Pong(entry+gen)이 정한다.
  - Ack 는 진행 중인 활성화를 **그 활성화 자신의 Pong 과 함께일 때만** 완성한다.
  - 취소된 활성화의 늦은 Ack 는 Host 의 같은 세션 admission 응답이고, 혼자서는 grant 를 만들지 못한다.
- 시험 (`standby_lease_test`, loopback 가짜 Host):
  - 잘못된 Ack 5종(버전, DirectoryAuth 없음, VideoFec 없음, 선언 size 불일치, 짧은 datagram)과 granted Pong 이 와도 400 ms 동안 Activating 에 머문다.
  - 이어서 유효한 Ack 가 오면 Active 가 되고, 그 Ack 의 features 를 그대로 받는다.
  - A 취소 → B 시작 → A 의 늦은 Pong + 늦은 Ack → 아직 Activating. B 자신의 Pong 이 와서야 Active 가 되고, features 는 검증된 Ack 의 값이다.
  - entry client 단위 시험에도 같은 순서를 추가했다.

**N2 — 수명·직렬화**
- `ActivationArgs(args, keepAlive, &op)`. runner 의 `TakeActivation(key, args, &op)` 는 keepAlive 로 **slot(shared_ptr)** 을 넘긴다. handshake 클로저가 slot 을 쥐므로 Release/ReleaseAll 뒤에도 lease 가 살아 있다(raw this 는 이 소유 아래에서만 쓴다).
- op 번호는 프로세스 전역이다. 그래서 같은 key 의 새 lease 와 번호가 겹치지 않는다.
- 다른 op 의 `SessionOver(key, op)` / `Abandon(key, op, args)` 는 아무것도 하지 않는다. handshake 가 도는 동안에는 둘 다 거부된다.
- stale handshake 는 시작부터 거부된다. abandon 됐거나 다른 op 인 경우, lock 아래에서 op_·handedOff_·cancel_ 을 확인하고 handshaking_ 을 세운다.
- 핸드오프 중의 Release 는 진행 중인 handshake 를 취소한다(false 반환, 아무것도 채택하지 않음). 이때는 아무것도 보내지 않는다. grant 가 있었다면 그 세션과 함께 끝난다.
- **읽는 쪽은 한 번에 하나**: pump 는 `reading_` 을 세운 뒤 **lock 없이** 20 ms 기다린다. ActivationArgs·ObserveFrom 은 그 대기가 끝난 다음에 소켓을 넘긴다.
  - ⚠️ 첫 구현에서는 lock 을 쥔 채 기다렸다(lease mu_ 를 select 동안, slot mu 를 Pump 동안). 변이 실행 3회가 UI 호출(TakeActivation·SessionOver·Abandon)에서 굶어 멈췄다.
  - 원인은 Windows mutex 가 공정하지 않다는 것이다. 풀어 주는 쪽이 바로 다시 잡는다.
  - 지금은 어떤 lock 도 대기를 가로질러 쥐지 않는다. runner 의 pump 도 slot lock 없이 돈다.
- StatusOf 는 lease snapshot 을 한 lock 아래에서 읽는다. Publish 는 statusMu → lease 순서다.
- 이벤트 기록은 최근 32개만 남는다.
- 시험(같은 파일):
  - args 직후 ReleaseAll: slot 은 args 안에서만 살아 있고(liveSlots), 그 handshake 는 false 를 돌려주며 아무것도 보내지 않는다. Release datagram 0, args 를 놓으면 slot 도 사라진다.
  - handshake 대기 중: 조기 SessionOver·Abandon 이 거부되고 handedOff 가 유지된다. Release 하면 handshake 가 약 100 ms 안에 false 를 돌려준다.
  - 화면 이탈(활성화 중 Release) 뒤 같은 key 의 새 lease:
    - 옛 op 의 SessionOver 는 무시되고 Parked 가 유지된다.
    - 새 활성화 중 옛 op 의 SessionOver·Abandon 은 거부되고, 세션 handle 도 열린 채로 남는다.
  - 핸드오프 6 s: pump 의 Ping 0회.
  - Abandon: handle 이 닫히고 lease 가 소켓을 되찾은 뒤 Ping 이 재개된다.
  - abandon 된 handshake 사본(실소켓 사용): 즉시 false, Activate 0회, Parked 유지.
  - 핸드오프 중 두 번째 TakeActivation 은 거부된다.

**N3 — 영속 저장**
- `FileGenerationStore` 의 저장 순서: write → FlushFileBuffers/fsync → close(결과 확인) → MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH)/rename → POSIX 는 디렉터리 fsync.
  - 모든 단계가 성공해야 true 다. 하나라도 실패하면 그 generation 으로는 보내지 않는다.
- 실패 모델(헤더 주석): 앱 kill, Save 가 true 를 반환한 **뒤**의 OS 크래시·전원 손실. 단계 사이에서 죽으면 이전 파일 아니면 새 파일이 남는다. 이미 보낸 것보다 작은 generation 은 생기지 않는다.
- writer 는 하나다. `<path>.lock` 을 Windows 는 share 0, POSIX 는 flock 으로 배타 점유한다. 두 번째 store 는 모든 Load/Save 가 실패한다(공유하지 않고 닫는다).
- 경로는 UTF-8 이고 Windows 에서는 UTF-16 으로 연다.
- 손상된 파일이면 새 CSPRNG incarnation 을 쓴다.
- APK bridge 는 `nativeStandbyInit` 에서 `g_standby` guard 아래로 **한 번만** store 를 만든다(feat `native_bridge.cpp:1371-1374`). 따라서 lock 이 두 번째 생성과 충돌하는 경로는 없다.
- 시험(`standby_entry_client_test`, REMOTE60_TEST_TMP 아래 `세대` 디렉터리만 쓰고, 자기 파일과 디렉터리는 스스로 지운다):
  - 두 번째 store 거부.
  - 재시작 후 같은 id 이고 카운터가 이어진다.
  - Write/Flush/Close/Replace/DirSync 를 각각 실패시키면(주입) 아무것도 보내지 않고 Waiting 이 된다. reopen 하면 같은 id 이고, next 는 보낸 최댓값보다 크다.
  - 손상된 파일이면 새 id 를 쓰고 1부터 시작한다.
  - 이 PC 에서는 크래시를 일으키지 않았다.

**결과** — 종료 코드와 RESULT 줄을 함께 확인했다:

| 시험 | 결과 |
|---|---|
| `remote60_standby_lease_test` 43/43 (exe 446d7027…) | exit 0, **15회 반복 15/15** |
| `remote60_standby_entry_client_test` 72/72 (exe d71cdc64…) | exit 0 |
| e2e `--product-client --real-directory --old-host` (sh_run -NativeExe, 슬롯 처리, exe 4282a5cf…) | **66/66**, rc 0. 63 → 66 은 op 를 지정한 SessionOver 확인 3개가 늘어난 것 |
| feat a629942 `assembleDebug` | BUILD SUCCESSFUL. buildCMakeDebug 4 ABI 재실행, apk 2a9d5faf… |

변이 10개 — 모두 FAIL 줄로 잡혔다. 시계로 kill 된 것은 0개다:

| 변이 | 잡은 확인 |
|---|---|
| ackUnchecked | 잘못된 Ack 로 활성화됨, FAIL 5 |
| sizeUnchecked | 선언 size 를 보지 않음, FAIL 5 |
| noKeep | slot 수명 FAIL. args 를 놓을 때 UAF 로 0xC0000409 종료 |
| opIgnored | FAIL 6 |
| handshakeIgnored | 조기 SessionOver FAIL 등 4, 0xC0000409 |
| releaseNoCancel | FAIL 2 |
| staleHandshake | 4 s 동안 Activate 재전송 FAIL. 처음에는 무효 소켓으로 시험해 변이가 살아남았고, 실소켓으로 고쳤다 |
| eventsUnbounded | 200 개 남음 |
| perLeaseOp | 늦은 op 가 일치 FAIL 5 |
| pumpWhileHandedOff | 핸드오프 중 Ping 2회 |

**계약으로 남는 것(코드가 강제하지 않음)**
- handshake 가 **끝난 뒤** 세션이 Connected 인 동안의 조기 SessionOver: lease 는 받아들이고 pump 가 다시 읽기 시작한다.
  - lease 는 세션의 reader 가 멈췄는지 알 수 없다. 그래서 "Disconnect 가 **반환된 뒤** SessionOver" 는 호출자 계약이다.
  - 강제되는 것은 handshake 창(handshaking_)뿐이다. KA e 배선에서는 bridge 가 Disconnect 반환 뒤에만 부르도록 하고, 그 순서를 시험한다.
- StatusOf 의 원자성은 구조상의 성질(한 lock)이고, 경쟁을 따로 시험하지는 않았다.
- pump 와 handshake 가 함께 읽지 않는 성질(`reading_`·`idle_` 대기)도 구조상의 성질이다. 대기를 없애는 변이는 두지 않았다 — 빼앗긴 datagram 은 결정적으로 관찰되지 않는다.
- feat 병합(a629942, dd2925d)은 **컴파일만** 확인했다(gradle, buildCMakeDebug ×4). AVD 회귀는 돌리지 않았다. 바뀐 코드에 APK 실행 경로가 없기 때문이다: TakeActivation 호출 0, `SEND_TO_REAL_HOSTS=false`.
- **232431b**(시험만): "핸드오프 6 s Ping 0" 의 기준값을 200 ms 기다린 뒤 읽는다. 핸드오프 직전에 보낸 Ping 이 기준값을 읽은 뒤에 도착할 수 있는 창을 닫았다. 43/43 ×5, exe 3e2ae2ad….

**미검증**: 실 NAT. APK 에서의 TakeActivation 호출(배선 전, `SEND_TO_REAL_HOSTS=false`). Android flock 실기.
**다음**: KA e 배선. kept 항목 모델 A/B 결정(seq 14381)을 기다리는 중이다.


### ② r4 (처음 보고 때 제목 "② r3-b") — Codex `codex_keepalive_r2_src_r3.md` N2-a/b 보수 (2026-10-28)
**커밋**: ka-r3 **82679b2** (lease + 시험 + CMake). feat/apk-0.2.33 에 **5c871ed** 로 merge 했다(dcb6607 standby_session_link 포함).
- **N2-a**: Release 가 Receive 대기 중에 끝나면, 그 뒤 도착한 완료 응답은 채택하지 않는다.
  - handshake 는 받은 packet 을 **그 operation 이 아직 유효할 때만**(같은 op, handedOff, cancel 아님, stop 아님) 넣는다.
  - 이 유효성 검사는 Release 가 cancel 을 세우는 lease lock 아래에서 한다. 최종 성공 판정도 같은 lock 아래에서 다시 확인한다.
  - 완료됐지만 이미 취소된 경우: false 를 돌려주고 features 는 넘기지 않는다. Host 쪽 grant 는 그 세션이 끝날 때 함께 끝난다.
  - operation 이 무효가 되면 handshake 는 즉시 끝난다(무한 대기 없음).
- **N2-b**: ActivationArgs·ObserveFrom 은 lock 아래에서 의도(intents_)를 등록한다.
  - pump 는 등록된 의도가 있는 동안 **새 read 를 시작하지 않는다.** 그래서 기다리는 대상은 진행 중인 read 하나뿐이다.
  - ObserveFrom 의 관찰은 이제 lock 밖에서 하고, 그동안 소켓은 관찰 단독이다(observing_). pump read 도 handoff 도 하지 않는다.
- **보고 정정**: 이전 문구 "어떤 lock 도 대기를 가로질러 쥐지 않는다" 는 **Pump·Handshake 의 Receive 와 ObserveFrom 에만** 해당한다.
  - Start·NeedCapability 는 CapabilityFn 을 lock 아래에서 호출한다. APK 의 CapabilityFn 은 비어 있어 즉시 반환하지만, 데스크톱용은 directory 호출을 그 안에서 한다.
  - 헤더 주석도 이 내용으로 고쳤다.
- **시험** (`standby_lease_test` 52/52, ×3·×5 반복, 모두 exit 0):
  - 두 순서를 각 6회 돌렸고 채택 0, Active 0 이었다: Ack → Release → Pong, Pong → Release → Ack.
  - 매 회차 뒤 늦게 온 응답을 pump 로 비운다. 남겨 두면 다음 회차의 정상 완료(무태그 Ack 계약)와 섞이기 때문이다.
  - N2-b 는 시험 전용 hook(`REMOTE60_STANDBY_TEST_HOOKS`, 시험 target 에만 정의)으로 결정적으로 재현한다. pump 의 read 가 끝나는 지점에서 handoff·observation 을 등록하고, 등록이 대기 중인 동안 read 시작이 0 인지 본다(ActivationArgs·ObserveFrom 각각).
- **변이**: 아래 다섯 가지 모두 FAIL 줄로 잡혔다.
  - 유효성 검사를 step 과 결과 양쪽에서 제거 → adopted 4/6.
  - step 에서만 제거 → Active 5/6.
  - intent 우선권 제거 → 대기 중 read 100회.
  - Release 가 cancel 하지 않음.
  - stale start 거부 제거.
  - 그 밖의 변이 10종은 1차 실행에서 확인한 그대로다.
- e2e `--product-client --real-directory --old-host` **74/74**, rc 0. 그중 8개는 새로 넣은 standby_session_link 확인이다: stale → Abandon, Connect 거부 → Abandon, 채택 → Close → 같은 소켓에서 parked.
- entry 시험 72/72.
- 보고 말미의 "A/B 결정 대기" 는 끝났다: **모델 A 확정**(`codex_keepalive_r2_kept_model.md`), 스위치는 ⓐ(`codex_ka_e_switch_decision.md`). e 배선은 진행 중이다.


### ② e — kept 모델 A 배선 + M1 (2026-10-28)
근거: `codex_keepalive_r2_kept_model.md`(A), `codex_ka_e_switch_decision.md`(ⓐ), `codex_apk0233_merge_src_r1.md`(M1).

**커밋**
- ka-r3:
  - **dcb6607** standby_session_link: 세션 entry 를 lease activation 으로 연결한다.
  - **16a1817** activation 소켓을 공유 소켓으로 다룬다. 세션은 이 소켓을 shutdown 하지 않는다.
  - **3326c84** 세션이 끝난 뒤 lease 는 원래 endpoint 를 유지한다. connect 해제(dissolve)는 하지 않는다.
- feat/apk-0.2.33:
  - **4e88a0b** 모델 A 배선.
  - **62877b4** 무응답 lease 를 구 Host 로 확정하지 않는다. 카드가 lease 상태를 따라 갱신된다.
  - merge: 5c871ed · d130407 · 21f9f17.
- NP 소유 파일은 건드리지 않았다.

**제품 동작**
- **PC 목록**: 화면 fence(입력 해제·선택 중단·decoder 스트림 리셋) → 세션 종료 → closer 스레드에서 Disconnect → 그 activation 의 SessionOver. 이후는 세 갈래다.
  - activation 으로 연 세션: 같은 lease 로 복귀한다.
  - 지원 directory 의 일반 연결: **첫 전환**으로 새 lease 를 만든다. 과정: lease 소켓으로 observe → `/api/connect` standby → relay 가 reserved 면 relay 를 조준. 이 새 directory 요청은 `standby_first_transition` 으로 로그에 남긴다.
  - raw IP·prelogin·standby 없는 directory: "상시 유지 미지원" 안내와 함께 세션을 종료한다.
  - 옛 park/resume(`resumeKept`/park_reuse)은 삭제했다 → **M1 은 경로 제거로 닫힘**.
- **탭**:
  - READY 인 lease → 새 entry 를 `nativeConnectStandby` 로 연결한다(standby_adopt, entry 자신의 gate 경유). directory·observe·punch·일반 Hello 는 0 이다.
  - 소켓을 아직 돌려받는 중인 lease → 탭을 보류했다가 실행한다.
  - 준비되지 않은 lease → 그 lease 를 놓고 일반 연결로 간다.
- **lease 관리**: 순수 `StandbyKeep` 이 담당한다.
  - 표시 상태: 확인된 entry 가 살아 있는 lease 위에 있을 때만 READY.
  - incarnation 으로 늦은 Provide 와 같은 key 재생성을 막는다(fence).
  - staging ≤ 1, PC 별 back-off.
  - 최근 3대에서 빠진 PC 의 lease 는 놓는다. 다만 화면에 떠 있는 PC 는 예외다.
- **해제 시점**: 앱 이탈·로그아웃·세션 거부·모두 끊기·네트워크 변경 → 전체 해제. 카드 끊기 → 그 PC 만 해제하고 suppress.
- **스위치**:
  - `Preconnect.AUTO_PREPARE = BuildConfig.STANDBY_AUTO_PREPARE` 이다. 앱이 **스스로** 최근 PC 를 준비할 때만 이 값을 본다.
  - debug 시험 빌드 `-Pgnlink.testStandbyAutoPrepare=true` 에서만 켜진다. release 빌드에 이 속성을 주면 빌드가 거부된다.
  - 이미 있는 lease 의 Ping 은 스위치와 상관없이 계속된다.
- **DirectoryClient**:
  - `/api/hosts` 의 standby{supported, relay} 를 읽는다(read-only).
  - `connect(standby=true)` 와 standbyRelay 를 지원한다.
  - `standbyAim` 의 조준 순서: relay(reserved) → 사설 후보 → 첫 비-relay 후보.

**AVD 가 찾아낸 실결함**(fixture 실행마다, 모두 고쳤다)
1. **ke_r2 — fixture 문제였다.**
   - Host 가 relay 를 10.0.2.2 로 dial 했다. 이 주소는 에뮬레이터 전용이다.
   - 그래서 host-proof 가 relay 에 닿지 않았다.
   - fixture 에 `REMOTE60_RELAY_HOST_FACING_IP=127.0.0.1` 을 넣어 고쳤다.
   - 다만 이 실행으로 **앱의 결함도 드러났다**: 무응답 lease 를 로그인 동안 계속 "미지원" 으로 굳혔다. 이것은 Codex 의 "미확인/일시 실패를 구버전으로 단정하지 않는다" 와 어긋난다 → 62877b4. 지금은 목록 복귀 때마다 다시 시도한다. 앱의 자동 준비만 10분 쉰다. 안내 문구는 "응답 없음(미지원 PC 이거나 경로 문제)" 이다.
2. **ke_r3 — 카드와 두 번째 activation.**
   - 카드가 "대기 준비" 에 멈췄다. lease 상태가 바뀌어도 목록을 다시 그리지 않았기 때문이다 → 62877b4.
   - **두 번째 activation 이 무응답이었고, 이후 observe 도 계속 실패했다**:
     - 세션이 받은 소켓은 lease 소켓의 **dup** 이다.
     - 세션은 이 소켓을 connect() 하고, 끝날 때 shutdown() 했다.
     - POSIX 에서는 두 호출 모두 공유 소켓 자체에 작용한다. 그래서 lease 소켓이 죽었다.
     - Windows 데스크톱 e2e 는 통과했었다. WSADuplicateSocket 의 shutdown 이 원래 소켓에 닿지 않아 결함이 가려진 것이다.
   - 수정(16a1817): `ClientSessionConnectArgs::preparedSocketShared` 를 두었다. 공유 소켓이면 세션이 shutdown 하지 않는다. reader 는 수신 tick(≤100 ms)에 스스로 끝나고, join 뒤에 close 만 한다. 이 값을 설정하지 않으면 기존 동작 그대로다.
3. **ke_r4 — 두 번째 activation 이 다시 무응답.**
   - Linux 에서 `connect(AF_UNSPEC)` 는 **자동 bind 된 포트까지 놓는다**.
   - 그래서 lease 가 다른 endpoint 로 돌아왔다. relay 는 모르는 endpoint 에서 온 StandbyActivate 를 버린다(relay 로그에서 클라이언트 포트가 바뀐 것을 확인).
   - Windows 는 UDP 소켓의 connect 해제가 아예 불가능하다. 실측: INADDR_ANY:0 → 10049, AF_UNSPEC → 10047.
   - 수정(3326c84): 해제하지 않는다.
     - lease 소켓은 자기 target(=세션의 peer)에 묶인 채 **같은 endpoint** 를 유지한다.
     - 다른 곳(새 capability 를 위한 observe)으로 보낼 때만 새 소켓을 연다. 새 capability 는 어차피 relay 에서 새 lease 다.
4. ke_r5·ke_r6 — 하네스 문제(설정 탭이 유지됨, E12 기대값) → 하네스를 고쳤다.

**시험 결과** (모두 exit 와 RESULT 를 확인)

| 시험 | 결과 |
|---|---|
| `standby_lease_test` | **57/57**, ×3 |
| 변이(신규) | 2종 모두 FAIL 로 잡힘: 묶임 미표시, shared 미설정 |
| e2e `--product-client --real-directory --old-host` | **74/74**, rc 0. standby_session_link 8개 포함: stale → Abandon, Connect 거부 → Abandon, 채택 → bridge 방식으로 Close → 같은 소켓에서 parked, 두 번째 Over 없음 |
| Kotlin 단위 | **305/0**. StandbyKeepTest 15 개 포함 |

**AVD 실 UI**
- 하네스: `feat-apk-0233/.claude/e2e/ke_avd.py` + `ke_fixture.ps1` + `ke_slot.ps1`.
  - sh_run 슬롯을 쓰고, 공용 emulator-5554 에서 돈다.
  - 실 server.js(relay on, 시험 계정) 위에서 pinned 시험 Host 4대를 쓴다: a/b/c 는 이 브랜치, old 는 baseline(standby 이전 빌드).
- **ke_r7 = 30/30 PASS, avd exit 0, scenario rc 0.**
  - APK: off c0e01212…, on 72b2aefd…
  - 시험 Host eef2f0d1…, old b6ed8984…, harness 38ddf6cd…
- 확인한 사용자 행동:

| 시나리오 | 확인한 것 |
|---|---|
| E1 | OFF 에서 앱이 스스로 준비하는 것 0 |
| E2 | A 일반 연결 → PC 목록 → 첫 전환(relay reserved) → 카드 "대기 중" |
| E3 | A 를 activation 으로 연결: connects/observes/punches 0, 첫 프레임 4.3 s |
| E4 | 같은 lease 로 복귀(새 lease·capability 0) |
| E5 (M1) | A 에서 GDI 요청 → B → A 를 다시 activation → 새 세션은 asked=none, sentSeq=0, 자동 재전송 0 |
| E6 | B → A → B: activation 3회, directory 0 |
| E7 | 다른 기기가 A 를 사용(relay 일반 Hello) → 자동 재탈취 0. 사용자가 탭하면 activation |
| E8 | 구 Host: 무응답 → 해제·안내. 다시 PC 목록 → 재시도(구 Host 로 단정하지 않음) |
| E9 | 카드 끊기 → 해제, 재생성 0 |
| E10 | 앱 이탈 → 전체 해제, OFF 재진입 시 준비 0 |
| E11 | 로그아웃 → 해제 |
| E12 (ON 빌드) | 최근 3대(C·old·A)를 자동 준비: A·C 는 READY, old 는 무응답으로 해제, B 는 최근 3대에서 빠져 준비 안 함. 탭하면 그 lease 로 activation |

- 그 전 실행: ke_r2·ke_r3·ke_r4·ke_r5·ke_r6 은 각각 위에 적은 결함·하네스 문제로 실패했다. 보존돼 있다.
- **출하 빌드 gate**:
  - plain 빌드(cae36136…): `STANDBY_AUTO_PREPARE=false`, DIRECTORY_URL 은 고정 주소.
  - `assembleRelease -Pgnlink.testStandbyAutoPrepare=true` → 거부된다.
  - APK native CMake 에 STANDBY/CONNECT_GATE test hook 정의가 없다.

**호출자 계약**
- "SessionOver 는 Disconnect 반환 뒤에만" 을 실제 경로로 확인했다:
  - bridge 의 `SessionEntry::Close` 는 CloseExplicit → Disconnect(StopWorker 가 worker 와 video reader 를 join) → `standby.Over()` 순서다.
  - `nativeCloseIfOwn` 도 같은 순서다.
  - AVD E4/E5/E6 에서 반복 activation 이 성공했다.

**남은 것·미검증**
- **ReturningToStandby 창**: PC 목록 직후 Host 가 세션 끝을 알아차리는 데 control timeout(~13 s)이 걸린다. 그동안 Host 는 영상을 relay 로 계속 보내고, lease 는 이를 받아 버린다. 그 사이 다시 탭하면 Host 표가 새 generation 으로 rollover 한다(E6 의 연속 탭에서 성공). 세션 끝을 Host 에 즉시 알리는 신호는 프로토콜 변경이라 이번 범위에서 다루지 않았다(Codex 확정 사항).
- **기타 미검증**: 실 NAT·실기기 폰, PC 클라이언트(작업용2 범위), 실 server 비용.
- **prelogin 미지원 경로**는 단위 시험(onListReturn)만 했다. AVD 에서 prelogin 역할 Host 로 돌리지는 않았다.
- **release 서명 APK 는 만들지 않았다**(검증용 담당).

**고정 대상**
- feat **d130407**, ka-r3 **3326c84**. 둘 다 tracked clean 이다.
- ke_r7 APK 는 d130407 에서 빌드했다.
- 하네스는 미추적 파일이다(`feat-apk-0233/.claude/e2e`). sha256 앞 16자리:
  - ke_avd.py c972eeb3c2a08a46
  - ke_fixture.ps1 1cd59dcb413b9323
  - ke_slot.ps1 0f908fd3728a6545
  - ka_avd.py a6f787fc2e540f41
- **공용 세션 파일 변경(16a1817)**:
  - 바뀐 곳은 `native_video_client_session.{hpp,cpp}` 다. 일반 연결 경로와 같은 파일이다.
  - 동작이 바뀌는 것은 `preparedSocketShared` 가 켜졌을 때뿐이다. 이 값은 lease 만 설정한다. 설정하지 않은 경로는 그대로다.
  - Windows e2e 74/74 로 회귀가 없음을 확인했다. → **SOURCE 검토 요청.**

**M1 의 "새 Host 상태 표시"**
- ke_r7 E5 의 줄은 `state=CHECKING lit=none` 이다. Host 상태 응답이 오기 전이다.
- ke_r5 E5 는 같은 판정을 `state=IDLE lit=DXGI asked=none seq=1 sentSeq=0` 로 통과했다(그 실행은 탭 문제로만 무효였다).
- 같은 실행의 `ke_r5/avd/E5_viewer_A_again.png` 에서 새 세션은 Host 기본값 DXGI 가 켜져 있고, GDI 에는 요청 표시가 없다.

**계약에서 벗어난 점 1건(명시)**
- Codex 계약: "Parked && !handedOff 를 grant 종료로 읽지 않는다".
- 현재 카드: PC 목록 직후 ~13 s 동안 "대기 중" 으로 보인다. 이 시간에 Host 는 아직 grant 를 쥐고 있다(Host 가 세션 끝을 control timeout 으로 안다).
- 이 구간에 탭해도 동작한다. 새 generation 으로 rollover 가 일어난다(E6 의 약 10 s 간격 연속 탭이 성공).
- 정확히 하려면 세션 끝을 즉시 알리는 신호가 필요한데, 이는 프로토콜 변경이라 보류했다. Codex 판단이 필요하다.

**Codex 필수 행동별 범위**

| 범위 | 항목 |
|---|---|
| AVD 실 UI | steady reuse, 첫 전환, 같은 lease 복귀, M1, standby 중 다른 기기 사용 → 탭만 takeover, 구 Host, 끊기·이탈·로그아웃 해제, ON 최근 3대 자동 준비 |
| 단위·e2e 만 | 늦은 Provide·같은 key 재생성(StandbyKeepTest fence, lease N2), handshake 대기 중 ReleaseAll(lease), 4번째 PC eviction(`evictions` 단위), raw-IP 목록 복귀(`onListReturn` 단위), direct 경로(데스크톱 e2e), prelogin(단위) |
| 어디서도 안 함 | 첫 전환 도중 다른 탭·취소, **화면에 떠 있는(active) 동안** 다른 기기가 가져가는 경우의 자동 재탈취 0(E7 은 standby 중의 takeover 만 확인) |


### FINDING(같은 key 재입장) — 원인·보수 + punch thread r2 (2026-10-28)
**원인(Host)**: Host 의 directory agent 가 heartbeat 직후 punch train(25×200 ms = 5 s)을 같은 스레드에서 동기로 보냈다.
- 그 5 s 동안 다음 wake 를 처리하지 못했다. 그래서 그 사이 발급된 capability 는 다음 cycle 에서야 수거됐다(5.06~5.14 s).
- 그동안 Host 는 Hello 를 "refused (capability)" 로 거절했다(throttle 로 로그에는 드물게 보임).
- 클라이언트는 Hello 를 6회(~3 s) 보낸 뒤 Unsupported 로 판정했다.
- 근거: e2e kae_relapse5 의 HOSTLOG 와 relay `[wake]` 줄. wake 는 도착했다(refreshWasPending=1). relay 와 replay 창은 원인이 아니다.
- 일반 connect 에도 같은 창이 있다.

**보수 1 — ka-r3 a7773e6**(feat merge c3bf472):
- punch train 을 별도 스레드로 옮겼다. 패킷과 간격은 그대로이고, 같은 target 은 카운트를 다시 시작한다.
- 작업이 heartbeat 주기를 넘긴 동안 온 wake 는 바로 다음 cycle 의 원인이 된다.
- e2e 78/78: crash 직후 같은 기기 재입장 599 ms, release 직후 1596 ms, 각각 새 relay lease. 보수 전에는 같은 항목이 FAIL 이었다.
- punch_reply 55/55, http contract PASS.

**보수 2 — ka-r3 6c3cb5a** (Codex `codex_ka_punch_thread_src_r1.md` N1, 큐 수명):
- Stop 은 두 스레드(cycle = producer, punch 스레드)가 **모두 끝난 뒤** 큐를 비운다.
- Punch() 는 stopping 중이면 큐에 넣지 않는다.
- Start 는 빈 큐로 시작한다. Stop 의 비우기를 받치는 이중 장치(belt)다.
- `remote60_host_agent_punch_test` 9/9 ×3. 실 HostAgent 의 Start/Punch/Stop 을 응답 없는 directory 와 송신 기록기로 돌렸다:
  - train 중간에 Stop 해도 job·송신이 남지 않는다.
  - stop 상태에서 Punch 하면 큐에 들어가지 않는다.
  - 재시작한 뒤 옛 target 과 옛 hostProof 로 송신하지 않는다. 새 train 은 정상으로 나간다.
- 변이 결과:

| 변이 | 결과 |
|---|---|
| Stop 이 큐를 유지 | 3 FAIL |
| Punch 가 stopping 중에도 큐잉 | 1 FAIL |
| Start 가 큐를 유지 | 생존 — Stop 의 비우기가 같은 경로를 덮는다(belt) |

- 시험 전용 hook 은 `REMOTE60_HOST_AGENT_TEST_HOOKS` 로만 열린다. 이 시험 target 에만 정의돼 있다.
- **통합 주의**: dirretry 의 HostAgent 변경은 34f22de(시험 fixture 포트 변경)가 아니라 **fix/directory-retry-r2 d11ef62/d6bb951** 이다. 그 커밋은 `cycleCv_` 를 추가했다(Stop 의 notify, WaitForCycleEnd). 통합할 때 Stop 은 `cycleCv_.notify_all()` 과 punch worker 의 stop/join/clear 를 **둘 다** 유지해야 한다.

**punch thread r3 — 시험 T2 보수** (Codex `codex_ka_punch_thread_src_r2.md`, 제품 6c3cb5a SOURCE OK): ka-r3 **20bc108**
- `host_agent_punch_test` 를 dirretry r2(d2f1243)와 같은 규칙으로 고쳤다.
  - `REMOTE60_TEST_TMP` 는 앵커 `D:/remote/remote/.claude/test-tmp` 바로 아래의 평범한 폴더여야 한다. 앵커와 그 폴더 모두 링크가 아니어야 한다.
  - 조건이 맞지 않으면 아무것도 만들거나 지우기 전에 거부한다(exit 2).
  - cache 파일 이름은 프로세스마다 고유하다. 삭제는 guard 를 거친 자기 파일 하나뿐이다.
- 결과: 9/9 ×3(루트 `.claude/test-tmp/ka-punch`, 실행 뒤 비어 있음). unset → 거부, 앵커 밖 루트 → 거부.
- **과거 실행 위치와 삭제 사실** (r4 정정, Codex `codex_ka_punch_thread_src_r3.md`):
  - r2 시험의 9×3 실행과 변이 실행(변이 3 + 복원 1)은 모두 `C:/work/gnlink-wt/ka-r3/build/apps/native_poc/Release/remote60_host_agent_punch_test.exe` 에서 돌렸다. 7회다.
  - 시험은 매 실행의 시작과 끝에 `C:/work/gnlink-wt/ka-r3/build/apps/native_poc/Release/host_agent_punch_test.cache` 를 DeleteFileA 했다. 저장소 밖 경로다.
  - **확인된 것은 "시험(HostAgent)이 그 파일을 만든 적이 없다" 까지다.** SaveCache 는 등록 성공이나 migration heartbeat 수락 뒤에만 불리는데, 시험의 directory 는 응답하는 곳이 없는 127.0.0.1:9 다.
  - 실행 전에 같은 이름의 파일이 없었다는 기록도, DeleteFileA 의 반환값 기록도 없다. 그래서 **"실제 삭제 0" 은 독립적으로 입증되지 않았다.**
  - 그 경로는 지금 존재하지 않는다(현재 상태일 뿐, 과거 부재의 증거는 아니다). r3 보고의 "실제로 지워진 파일은 없다" 문구는 이 내용으로 정정한다.
- **r4 — ka-r3 534242b** (Codex T2-a·T2-b, 시험만):
  - T2-a: root 가 실제로 폴더인지 확인한다. 조회 성공 + DIRECTORY + 비reparse 가 모두 맞아야 한다. 일반 파일을 root 로 주면 거부되고(exit 2) 그 파일은 보존된다.
  - T2-b: 실행마다 root 안에 **새 폴더**를 CreateDirectory 로 만든다. 같은 이름이 이미 있으면 실패하므로 다른 이름으로 재시도한다. cache 는 그 폴더 안에 둔다.
    - 실행 전에는 아무것도 지우지 않는다. 실행 뒤에는 그 파일(HostAgent 가 썼다면)과 그 실행의 폴더(비재귀)만 지운다.
    - root 에 미리 둔 남의 `host-agent.cache` 는 내용 그대로 보존됐다.
  - 결과: 9/9 ×3, 실행 폴더는 실행 뒤 제거됨. 제품 무변경.

### ② e r2 — Codex `codex_ka_e_src_r1.md` D1·N1–N5 보수 (2026-10-28)

**커밋**
- ka-r3:
  - **da2280e** N2 + D1(관측). 새 capability 로 가는 모든 진입(ObserveFrom·NeedCapability)이 묶인 소켓을 푼다(`UntieForNewCapability`). 이전 세션의 tail 을 센다.
  - **ecf1004** punch 시험 상단 주석을 보고 정정과 맞췄다(검증용 534242b 메모). 시험 동작은 그대로다.
- feat/apk-0.2.33:
  - **c908f5a** N1·N3·N4·N5·D1 표시·held input fence. JNI status 에 `tailActive`, `strays` 를 추가했다(7개).
  - **ac2a157** `standby_back host=.. strays=N tail=..` 로그(D1 비용 계수).
  - **4194957**·**7a0d577** AVD ke_r15·ke_r18 이 찾은 실결함 보수(아래 「AVD 가 찾은 결함」).
  - merge: a0f722b(da2280e), c3bf472(a7773e6 punch thread).
  - ⚠️ feat 에는 ka-r3 의 6c3cb5a·20bc108·534242b·ecf1004 가 **아직 merge 되지 않았다**. 모두 Host·시험 전용이라 APK 에는 영향이 없다.
- NP 소유 파일은 건드리지 않았다.

**D1 — 새 종료 신호 없이, 세 사실을 구분해 표시**
- 새 wire 는 추가하지 않았다. 13 s 타이머를 grant 종료 근거로 쓰지 않는다.
- ⑴ local handoff(소켓 돌려받는 중): 탭을 보류한다(아래 N1 의 최신 의도 규칙 적용).
- ⑵ entry/Ping 생존: 카드 "대기 중 · 대기 연결 있음 · 누르면 엽니다". "바로/준비됨" 같은 정리 완료 의미는 없앴다.
- ⑶ Host 의 이전 grant·영상 정리: 기존 wire 로 확인할 수 없다. lease 가 target 에서 오는 비-standby datagram(이전 세션의 영상·제어)을 센다. 1.5 s 안에 들어오면 tailActive 다. 그동안 카드는 "이전 연결 정리 중일 수 있음 · 누르면 다시 엽니다" 다. 탭은 막지 않는다(사용자의 명시적 rollover).
  - 이 값은 **tail 의 관측**이지 grant 종료의 증명이 아니다.
- tail 비용은 따로 센다(`standby_back strays=`, `standby_tail_quiet`). AVD 실측(이 fixture, loopback relay):
  - ke_r10 E7b: 돌려받을 때 strays 27 → 조용해질 때 49. 그동안 카드는 "정리 중일 수 있음" 이었다.
  - ke_r17: 돌려받을 때 strays 0·2·24·25 (tail=true 1회).
  - ke_r19: 13회 중 12회 strays=0, 1회 strays=3 tail=true. 실행마다 크게 다르다(ke_r16·r17 은 23–25). 원인은 조사하지 않았다.
  - "정리 중일 수 있음" 표시를 화면에서 본 것은 ke_r10 뿐이다. ke_r16·ke_r19 의 E7b 캡처 시점에는 tail 이 이미 끝나 있었다(settling seen=None).
  - 이 숫자는 이 환경의 값이다. 안정 standby 영상 0 과 합산하지 않는다.
- **held input**: "PC 목록" 은 세션을 먼저 fence 한다(native park fence: 새 입력·clipboard 차단, 눌린 키의 release 송신, Ping 으로 정지 확인). 정착하거나 budget(2.5 s)에 이르면 끝낸다. fence 할 수 없는 세션(구 Host·raw IP)은 즉시 끝낸다.
  - AVD E7b: Ctrl 을 누른 채 PC 목록 → **Host 자신의 입력 로그에서 key up 이 세션 종료보다 먼저**였다. `list_return_end how=fenced`.

**N1 — 최신 탭 우선**
- 사용자 탭마다 번호(tapSeq)를 매긴다. 새 탭은 보류된 탭을 버린다.
- 보류·재시도 탭은 만들어질 때의 번호를 지닌다. 그 뒤 탭이 없었을 때만 실행된다. 사용자 끊기가 건 suppression 을 풀지 않는다.
- 카드 끊기·disconnect 는 그 PC 의 보류 탭을 버린다.

**N2 — 동기 capability 경로도 소켓을 푼다** (da2280e)
- 묶인 소켓을 새 소켓으로 바꾸는 일을 ObserveFrom 과 NeedCapability 양쪽에서 한다(공통 함수).
- pump 가 읽는 중에 owner 스레드에서 NeedCapability 가 오면 pump 의 다음 차례(새 read 전)로 미룬다. read 중인 descriptor 를 닫지 않는다.
- 정상 재사용은 같은 endpoint 를 유지한다. shared=false 경로는 그대로다.
- `standby_lease_test` **66/66 ×6**: 세션 뒤 동기 capability 가 새 소켓에서 observe 한다(pump 가 읽는 중에도). Codex probe 의 `after_session_observe=0` 경로다.
- 변이 2종(동기 경로가 묶인 소켓 유지, tail 미계수) FAIL. 이전 변이도 FAIL 유지.

**N3 — clipboard r3 계약 복구**
- standby activation 으로 만든 세션: 폰의 현재 text 를 **그 새 binding 의 baseline** 으로만 seed 한다(보내지 않음). 읽을 수 없으면 실제 변경 전까지 보류한다.
- cold 접속은 이전과 같다. 옛 park 세션 모델은 되살리지 않았다.
- 단위 시험만 했다(`clipboardOnStart` Seed/Hold/Fresh). AVD fixture 의 Host 는 clipboard sync 를 끈 상태라 실 clipboard 는 보지 않았다.

**N4 — active + standby 합계 ≤ 3**
- 확정된 일반·raw-IP 세션을 lease 와 함께 센다. 넘치면 가장 오래된 lease 를 놓는다(화면에 떠 있는 PC 는 제외). 한도를 넘으면 lease 를 시작하지 않는다. 4번째가 실패하면 아무것도 놓지 않는다.
- 단위 시험(`overLimit`/`mayStart`)만 했다. raw-IP 활성 + 최근 3대 AUTO 준비 경로는 AVD 에서 돌리지 않았다.

**N5 — Activity 종료 경계**
- onDestroy: owner 를 먼저 무효화하고 이 Activity 의 lease 를 놓는다. standby 스레드는 shutdown(shutdownNow 아님: queue 의 ReleaseAll 이 실행됨)으로 끝낸다.
- 끝난 Activity 의 늦은 callback 은 아무것도 바꾸지 않는다.
- endStaging 은 key·incarnation 이 현재 것일 때만 staging·back-off 를 고친다.
- 단위 시험(늦은 job)만 했다. Activity 반복 재생성의 thread 누적은 측정하지 않았다.

**AVD 가 찾은 결함**
- **ke_r15 — 실결함(4194957 로 보수)**:
  - PC 목록의 fence 동안(≤ 2.5 s) 다른 PC 를 탭하면 native table 이 activation 을 거부했다(`keep_activate_refused`). 그 사이 만든 세션은 버려졌다.
  - 이제 이 탭도 보류한다(`tap_held_fence`). 최신 의도 규칙을 따른다: 새 탭이 대체하고, 끊기가 버리고, fence 가 끝나면 keepTick 이 실행한다.
  - ke_r16·ke_r17·ke_r19 에서 E7c·E7d PASS, `keep_activate_refused` 0.
- **ke_r18 — 실결함(7a0d577 로 보수)**:
  - E7f: capability job 이 "lease 가 현재 것" 을 확인한 뒤, 사용자 끊기가 UI 스레드에서 그 lease 를 끝냈다(21:55:32.863). 그런데 job 의 Provide 는 그대로 native lease 에 닿았다(32.943 `ok=true`). native Release 가 같은 executor 에서 그 job 뒤에 줄 서 있었기 때문이다. 결과: 끊기 뒤 capability 1회 제공, 약 80 ms 뒤 해제. 재생성은 없었다.
  - ke_r17 에서는 같은 시나리오가 `stale` 로 버려졌다. 순서에 따라 갈리는 경쟁이다.
  - 수정: `Book.ifCurrent` 가 확인과 Provide 를 book 의 lock 안에서 한다. `end()`·`endAll()` 도 같은 lock 을 쓴다. 그래서 끊기는 확인 전(stale, 제공 0)이거나 Provide 뒤(그 뒤 해제)다. 제공 시점은 lock 안에서 `standby_provided` 로 남긴다.
  - lock 안의 작업은 native Provide 하나다(lease mutex, 네트워크 없음). UI 스레드가 기다리는 시간은 그 길이다.
  - 단위 310/0. 새 시험은 `ifCurrent` 의 동기화를 빼면 FAIL 한다(변이 확인).
- ke_r16 — 하네스: 구 Host 의 lease 는 약 4 s 만에 무응답으로 놓인다. UI dump 1회가 약 3 s 라 E7f 가 끊기 버튼을 찾지 못하고 하네스가 멈췄다. → 로그를 보고 고정 위치를 즉시 탭하게 고쳤다.
- ke_r17 — 하네스: E7c–E7f 가 추가되면서 E10 시점에 lease 가 남아 있지 않았다(C 는 E7f 에서 최근 3대에서 빠져 놓였다, `reason=lru`). 앱은 놓을 것이 없어 release 를 하지 않았다. 이것은 맞는 동작이다. → E10 이 먼저 C 를 대기시키게 고쳤다.

**시험 결과** (모두 exit 와 RESULT 를 확인)

| 시험 | 결과 |
|---|---|
| `standby_lease_test` (da2280e) | **66/66**, ×6 |
| e2e `--product-client --real-directory --old-host` (a7773e6) | **78/78**, rc 0 |
| Kotlin 단위 (7a0d577) | **310/0**, rc 0 |
| AVD ke_r19 (7a0d577 APK) | **48/48 PASS**, avd exit 0, scenario rc 0. `keep_activate_refused` 0. 콘솔 세션(qwinsta), SLOT CreateNew→삭제 |
| AVD ke_r16 / ke_r17 / ke_r18 | 33/33 후 하네스 중단 / 45/46(E10 하네스) / 47/48(E7f 실결함 → 7a0d577) |

**AVD 실 UI — 이번에 추가한 시나리오** (ke_r16–ke_r18 에서 확인, ke_r19 에서 고정 APK 로 전체 재확인)

| 시나리오 | 확인한 것 |
|---|---|
| E7g | **화면에 떠 있는(active)** A 를 다른 기기가 가져감 → 세션 종료(`keep_end reason=peer_lost`), **자동 재탈취 0**, A 는 대기 |
| E7b | Ctrl 누른 채 PC 목록 → key up 이 세션 종료보다 먼저 Host 에 도착(Host 입력 로그), fence 경유 종료. 카드는 정리 완료를 주장하지 않음 |
| E7c (N1) | A 를 fence 동안 탭(보류) → B 를 탭 → A 보류는 `tap_held_superseded`, B 가 열리고 A 는 이후 activation 0 |
| E7d (N1) | A 를 보류 → A 끊기 → 보류 탭 `tap_held_dropped why=disconnect`, A 해제(`user_disconnect`), 재생성 0 |
| E7e | **C 의 첫 전환 도중** B 를 탭 → B 는 activation 으로 열리고 C 의 lease 는 방해받지 않음 → C 의 첫 전환 완료(대기 중) |
| E7f | **첫 전환 도중 끊기** → `user_disconnect` 해제, 재생성 0. ke_r17: capability 를 기다리는 중(begin 21:28:12.207)에 끊기(13.355), 늦은 capability 는 0.24 s 뒤 `ok=false … stale` 로 버려짐. ke_r19(7a0d577): 이번에는 Provide 가 먼저였고(`standby_provided`) 끊기가 그 뒤에 와서 해제. 끊기 뒤 제공 0 |

**고정 대상**
- feat **7a0d577**, ka-r3 **ecf1004**. 둘 다 tracked clean.
- APK(7a0d577 에서 빌드, `-Pgnlink.testDirectoryUrl=http://10.0.2.2:18410`, on 은 `+ -Pgnlink.testStandbyAutoPrepare=true`): off be0464994d718471…, on 4e0366b4b7504e51….
- 하네스(미추적, `feat-apk-0233/.claude/e2e`) sha256 앞 16자리: ke_avd.py 644b388cb32087d9, ke_fixture.ps1 1cd59dcb413b9323, ke_slot.ps1 0f908fd3728a6545, ka_avd.py a6f787fc2e540f41. 시험 Host 44a43610…, old b6ed8984…

**남은 것·미검증**
- N3 실 clipboard(AVD fixture 는 sync off), N4 raw-IP+AUTO 실 UI, N5 Activity 반복 재생성 누적: 단위 시험만.
- prelogin 역할의 실 UI 경로: 이번에도 하지 않았다.
- 첫 프레임 시간은 선택 → 첫 프레임 실측(약 1.0–4.1 s)이다. 목표 p95 ≤ 1 s 의 증명이 아니다.
- 실 NAT·실기기 폰·PC 교차·실 server 비용: 기존 후속 그대로.
- release 서명 APK 는 만들지 않았다(검증용 담당).

### 안전 injection
정본은 고정 패킷 `.claude/caplife/report_inj_draft.md` 이다(test-injection-finding r3, ka-r3 92f33ca). 이 절의 이전 서술은 그 패킷으로 대체됐다.
